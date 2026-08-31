#include "InstanceRepacker.h"

#include "Program.h"
#include "ShaderProvider.h"
#include "StorageBuffer.h"

#include <Babylon/Graphics/DeviceContext.h>
#include <Babylon/Graphics/FrameBuffer.h>

#include <bgfx/bgfx.h>

#include <cstring>
#include <vector>

namespace Babylon
{
    namespace
    {
        // Repack kernel. Under force_storage_buffer_as_uav every SSBO is a RWByteAddressBuffer, so
        // Params is a flat uint[]:
        //   params[0]=instanceCount, [1]=attrCount, [2]=dstStrideU, [3]=pad
        //   params[4+a*4..]= srcOffsetU, srcStrideU, numU, dstOffsetU
        //
        // IMPORTANT: this dispatch MUST NOT run on the draw encoder. encoder->dispatch clears
        // bind state (and SetCompute overwrites texture stages). Doing so mid-draw strips the
        // material's particle sheet sampler and makes instances invisible even when the instance
        // buffer content is correct. Always use a side encoder (bgfx::begin/end).
        constexpr const char* kRepackSource = R"(#version 310 es
precision highp float;
precision highp int;
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
layout(std430, binding = 0) buffer Params { uint params[]; };
layout(std430, binding = 1) buffer Src { uint srcData[]; };
layout(std430, binding = 2) buffer Dst { uint dstData[]; };
void main() {
    uint inst = gl_GlobalInvocationID.x;
    uint instanceCount = params[0];
    if (inst >= instanceCount) { return; }
    uint attrCount = params[1];
    uint dstStrideU = params[2];
    // TEMP: also stash src[0] of inst0 into a high slot via atomic-free write of marker
    // so INSTRB can tell repack ran (dst[0] gets 1.0 if src empty path still executes).
    for (uint a = 0u; a < attrCount; ++a) {
        uint base = 4u + a * 4u;
        uint srcOffsetU = params[base + 0u];
        uint srcStrideU = params[base + 1u];
        uint numU = params[base + 2u];
        uint dstOffsetU = params[base + 3u];
        uint so = srcOffsetU + inst * srcStrideU;
        uint dof = dstOffsetU + inst * dstStrideU;
        for (uint f = 0u; f < numU; ++f) {
            dstData[dof + f] = srcData[so + f];
        }
    }
}
)";

        constexpr uint32_t kSlotSize = 16;
        constexpr uint32_t kParamsHeaderU = 4;
    }

    InstanceRepacker::InstanceRepacker(Graphics::DeviceContext& deviceContext, ShaderProvider& shaderProvider)
        : m_deviceContext{deviceContext}
        , m_shaderProvider{shaderProvider}
    {
    }

    InstanceRepacker::~InstanceRepacker()
    {
        for (auto& pair : m_perVertexArray)
        {
            DestroyDest(pair.second);
        }
        m_perVertexArray.clear();
    }

    void InstanceRepacker::EnsureProgram()
    {
        if (m_program != nullptr)
        {
            return;
        }

        auto info = m_shaderProvider.GetCompute(kRepackSource);
        m_program = std::make_shared<Program>(m_deviceContext);
        m_program->InitializeCompute(info);
    }

    void InstanceRepacker::DestroyDest(PerVertexArray& state)
    {
        if (state.DestStorage != nullptr)
        {
            state.DestStorage->Dispose();
            state.DestStorage.reset();
        }
        state.Capacity = 0;
        state.Stride = 0;
    }

    void InstanceRepacker::Forget(VertexArray* vertexArray)
    {
        auto it = m_perVertexArray.find(vertexArray);
        if (it != m_perVertexArray.end())
        {
            DestroyDest(it->second);
            if (it->second.Params != nullptr)
            {
                it->second.Params->Dispose();
                it->second.Params.reset();
            }
            m_perVertexArray.erase(it);
        }
    }

    bgfx::DynamicVertexBufferHandle InstanceRepacker::Repack(bgfx::Encoder* /*drawEncoder*/, Graphics::FrameBuffer& frameBuffer, VertexArray* vertexArray,
            const std::map<uint32_t, VertexBuffer::InstanceInfo>& instances, uint32_t instanceCount)
    {
        if (instances.empty() || instanceCount == 0)
        {
            return BGFX_INVALID_HANDLE;
        }

        const uint32_t attributeCount = static_cast<uint32_t>(instances.size());
        if (attributeCount > kMaxAttributes)
        {
            return BGFX_INVALID_HANDLE;
        }

        StorageBuffer* source = instances.begin()->second.StorageSource;
        if (source == nullptr)
        {
            return BGFX_INVALID_HANDLE;
        }

        EnsureProgram();
        if (m_program == nullptr || !bgfx::isValid(m_program->Handle()))
        {
            return BGFX_INVALID_HANDLE;
        }

        const uint16_t instanceStride = static_cast<uint16_t>(attributeCount * kSlotSize);
        const uint32_t destBytes = static_cast<uint32_t>(instanceCount) * instanceStride;

        PerVertexArray& state = m_perVertexArray[vertexArray];

        if (state.DestStorage == nullptr || state.Capacity < instanceCount || state.Stride != instanceStride
            || state.DestStorage->ByteLength() < destBytes)
        {
            DestroyDest(state);
            state.DestStorage = std::make_shared<StorageBuffer>(
                m_deviceContext, destBytes, /*asVertexBuffer*/ true, /*byteStride*/ instanceStride);
            state.Capacity = instanceCount;
            state.Stride = instanceStride;
        }

        // Build params blob: header + one uvec4 per attribute (reverse location order -> slot order).
        const uint32_t paramsU = kParamsHeaderU + attributeCount * 4;
        std::vector<uint32_t> params(paramsU, 0);
        params[0] = instanceCount;
        params[1] = attributeCount;
        params[2] = instanceStride / sizeof(float);
        params[3] = 0;

        uint32_t slot = 0;
        for (auto iter = instances.rbegin(); iter != instances.rend(); ++iter, ++slot)
        {
            const auto& element = iter->second;
            const uint32_t base = kParamsHeaderU + slot * 4;
            params[base + 0] = element.Offset / sizeof(float);
            params[base + 1] = element.Stride / sizeof(float);
            params[base + 2] = element.ElementSize / sizeof(float);
            params[base + 3] = (slot * kSlotSize) / sizeof(float);
        }

        const uint32_t paramsBytes = paramsU * sizeof(uint32_t);
        if (state.Params == nullptr || state.Params->ByteLength() < paramsBytes)
        {
            if (state.Params != nullptr)
            {
                state.Params->Dispose();
                state.Params.reset();
            }
            state.Params = std::make_shared<StorageBuffer>(m_deviceContext, paramsBytes, /*asVertexBuffer*/ true);
        }
        state.Params->Update(gsl::make_span(reinterpret_cast<const uint8_t*>(params.data()), paramsBytes), 0);

        // Use the frame encoder. Caller (DrawInstanced) restores material texture binds after
        // dispatch clears them. begin(true) per-draw exhausts the encoder pool within a frame.
        bgfx::Encoder* computeEncoder = m_deviceContext.GetActiveEncoder();
        if (computeEncoder == nullptr)
        {
            return BGFX_INVALID_HANDLE;
        }

        // force_storage_buffer_as_uav: bind every SSBO as ReadWrite (UAV), never Access::Read (SRV).
        state.Params->SetCompute(computeEncoder, 0, bgfx::Access::ReadWrite);
        source->SetCompute(computeEncoder, 1, bgfx::Access::ReadWrite);
        state.DestStorage->SetCompute(computeEncoder, 2, bgfx::Access::ReadWrite);

        const uint32_t numGroups = (instanceCount + 63u) / 64u;
        frameBuffer.Compute(*computeEncoder, m_program->Handle(), numGroups, 1, 1);

        return state.DestStorage->Handle();
    }
}