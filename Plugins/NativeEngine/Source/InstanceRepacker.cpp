#include "InstanceRepacker.h"

#include "Program.h"
#include "ShaderProvider.h"
#include "StorageBuffer.h"

#include <Babylon/Graphics/DeviceContext.h>
#include <Babylon/Graphics/FrameBuffer.h>

#include <array>
#include <cstring>

namespace Babylon
{
    namespace
    {
        // Repack kernel. Reads per-instance attribute data packed float-by-float in a raw source
        // storage buffer (the particle buffer) and scatters each attribute into its own 16-byte
        // i_data slot in the destination buffer, matching VertexBuffer::BuildInstanceDataBuffer.
        //
        // All units in Params are in uints (4-byte words) since the buffers are raw ByteAddressBuffers.
        //   attrs[a] = uvec4(srcOffsetU, srcStrideU, numU, dstOffsetU)
        // force_storage_buffer_as_uav promotes every SSBO to an RWByteAddressBuffer UAV, so the
        // three buffers bind as u0/u1/u2 in binding order.
        constexpr const char* kRepackSource = R"(#version 310 es
precision highp float;
precision highp int;
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
layout(std430, binding = 0) readonly buffer Params {
    uint instanceCount;
    uint attrCount;
    uint dstStrideU;
    uint pad0;
    uvec4 attrs[32];
};
layout(std430, binding = 1) readonly buffer Src { uint srcData[]; };
layout(std430, binding = 2) writeonly buffer Dst { uint dstData[]; };
void main() {
    uint inst = gl_GlobalInvocationID.x;
    if (inst >= instanceCount) { return; }
    for (uint a = 0u; a < attrCount; ++a) {
        uvec4 d = attrs[a];
        uint so = d.x + inst * d.y;
        uint dof = d.w + inst * dstStrideU;
        for (uint f = 0u; f < d.z; ++f) {
            dstData[dof + f] = srcData[so + f];
        }
    }
}
)";

        constexpr uint32_t kSlotSize = 16;
        // Params blob: 4 header uints followed by kMaxAttributes uvec4 entries.
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
        if (bgfx::isValid(state.Dest))
        {
            bgfx::destroy(state.Dest);
            state.Dest = BGFX_INVALID_HANDLE;
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
            }
            m_perVertexArray.erase(it);
        }
    }

    bgfx::DynamicVertexBufferHandle InstanceRepacker::Repack(bgfx::Encoder* encoder, Graphics::FrameBuffer& frameBuffer, VertexArray* vertexArray,
        const std::map<bgfx::Attrib::Enum, VertexBuffer::InstanceInfo>& instances, uint32_t instanceCount)
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

        // All storage-backed instance attributes for a given draw share the same source storage
        // buffer (the particle buffer). Pick it from the first instance.
        StorageBuffer* source = instances.begin()->second.StorageSource;
        if (source == nullptr)
        {
            return BGFX_INVALID_HANDLE;
        }

        EnsureProgram();
        if (!bgfx::isValid(m_program->Handle()))
        {
            return BGFX_INVALID_HANDLE;
        }

        const uint16_t instanceStride = static_cast<uint16_t>(attributeCount * kSlotSize);

        PerVertexArray& state = m_perVertexArray[vertexArray];

        // (Re)create the destination compute-writable dynamic vertex buffer when it is too small or
        // when the attribute layout (stride) changed.
        if (!bgfx::isValid(state.Dest) || state.Capacity < instanceCount || state.Stride != instanceStride)
        {
            DestroyDest(state);

            bgfx::VertexLayout layout;
            layout.begin();
            layout.m_stride = instanceStride;
            layout.end();

            state.Dest = bgfx::createDynamicVertexBuffer(instanceCount, layout, BGFX_BUFFER_COMPUTE_WRITE | BGFX_BUFFER_COMPUTE_RAW);
            state.Capacity = instanceCount;
            state.Stride = instanceStride;
        }

        if (!bgfx::isValid(state.Dest))
        {
            return BGFX_INVALID_HANDLE;
        }

        if (state.Params == nullptr)
        {
            const uint32_t paramsBytes = (kParamsHeaderU + kMaxAttributes * 4) * sizeof(uint32_t);
            state.Params = std::make_shared<StorageBuffer>(m_deviceContext, paramsBytes, /*asVertexBuffer*/ true);
        }

        // Build the params blob. Iterate in reverse attrib-key order to mirror
        // BuildInstanceDataBuffer: the highest-key attribute is packed at slot (byte) offset 0.
        std::array<uint32_t, kParamsHeaderU + kMaxAttributes * 4> params{};
        params[0] = instanceCount;
        params[1] = attributeCount;
        params[2] = instanceStride / static_cast<uint32_t>(sizeof(float)); // dstStrideU
        params[3] = 0;

        uint32_t slotOffsetBytes = 0;
        uint32_t index = 0;
        for (auto iter = instances.rbegin(); iter != instances.rend(); ++iter)
        {
            const auto& element = iter->second;
            const uint32_t base = kParamsHeaderU + index * 4;
            params[base + 0] = element.Offset / static_cast<uint32_t>(sizeof(float));      // srcOffsetU
            params[base + 1] = element.Stride / static_cast<uint32_t>(sizeof(float));      // srcStrideU
            params[base + 2] = element.ElementSize / static_cast<uint32_t>(sizeof(float)); // numU
            params[base + 3] = slotOffsetBytes / static_cast<uint32_t>(sizeof(float));     // dstOffsetU
            slotOffsetBytes += kSlotSize;
            ++index;
        }

        state.Params->Update(gsl::make_span(reinterpret_cast<const uint8_t*>(params.data()), params.size() * sizeof(uint32_t)), 0);

        // Bind: params -> u0, src -> u1, dst -> u2 (binding order), then dispatch on the frame
        // buffer's sequential view so the repack is ordered before the draw that consumes it.
        state.Params->SetCompute(encoder, 0, bgfx::Access::Read);
        source->SetCompute(encoder, 1, bgfx::Access::Read);
        encoder->setBuffer(2, state.Dest, bgfx::Access::Write);

        const uint32_t numGroups = (instanceCount + 63u) / 64u;
        frameBuffer.Compute(*encoder, m_program->Handle(), numGroups, 1, 1);

        return state.Dest;
    }
}
