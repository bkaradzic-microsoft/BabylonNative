#pragma once

#include "VertexBuffer.h"

#include <bgfx/bgfx.h>

#include <map>
#include <memory>

namespace Babylon
{
    namespace Graphics
    {
        class DeviceContext;
        class FrameBuffer;
    }

    class Program;
    class ShaderProvider;
    class StorageBuffer;
    class VertexArray;

    // Repacks GPU compute-written per-instance data (packed float-by-float inside a raw storage
    // buffer, e.g. the GPUParticleSystem particle buffer) into bgfx's 16-byte-per-attribute i_data
    // slot layout, entirely on the GPU. The produced layout is byte-identical to
    // VertexBuffer::BuildInstanceDataBuffer (reverse-attrib packing, highest attrib key at byte
    // offset 0), so the existing instanced shader-variant path renders it unchanged.
    class InstanceRepacker final
    {
    public:
        InstanceRepacker(Graphics::DeviceContext& deviceContext, ShaderProvider& shaderProvider);
        ~InstanceRepacker();

        InstanceRepacker(const InstanceRepacker&) = delete;
        InstanceRepacker& operator=(const InstanceRepacker&) = delete;

        // Dispatches the repack compute for the given vertex array's storage-backed instances on the
        // frame buffer's (sequential) view so it is ordered before the subsequent draw, and returns
        // the dynamic vertex buffer handle to bind via setInstanceDataBuffer. Returns an invalid
        // handle if there is nothing to repack.
        bgfx::DynamicVertexBufferHandle Repack(bgfx::Encoder* encoder, Graphics::FrameBuffer& frameBuffer, VertexArray* vertexArray,
                    const std::map<uint32_t, VertexBuffer::InstanceInfo>& instances, uint32_t instanceCount);

        // Releases the GPU resources associated with a vertex array (call on vertex-array deletion).
        void Forget(VertexArray* vertexArray);

    private:
        // Maximum per-instance attributes; matches the attrs[] array in the repack kernel and is an
        // upper bound on bgfx's maxInstanceData slots.
        static constexpr uint32_t kMaxAttributes = 32;

        struct PerVertexArray
        {
            bgfx::DynamicVertexBufferHandle Dest{bgfx::kInvalidHandle};
            uint32_t Capacity{}; // in instances
            uint16_t Stride{};   // bytes per instance (== attributeCount * 16)
            std::shared_ptr<StorageBuffer> Params;
        };

        void EnsureProgram();
        void DestroyDest(PerVertexArray& state);

        Graphics::DeviceContext& m_deviceContext;
        ShaderProvider& m_shaderProvider;
        std::shared_ptr<Program> m_program;
        std::map<VertexArray*, PerVertexArray> m_perVertexArray;
    };
}
