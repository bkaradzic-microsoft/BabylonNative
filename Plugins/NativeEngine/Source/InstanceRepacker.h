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

    class StorageBuffer;
    class VertexArray;

    // Repacks GPU-written per-instance data (transform feedback output, packed float-by-float in a
    // raw storage buffer) into bgfx's 16-byte-per-attribute i_data slot layout, entirely on the GPU. The produced layout is byte-identical to
    // VertexBuffer::BuildInstanceDataBuffer (reverse-attrib packing, highest attrib key at byte
    // offset 0), so the existing instanced shader-variant path renders it unchanged.
    class InstanceRepacker final
    {
    public:
        explicit InstanceRepacker(Graphics::DeviceContext& deviceContext);
        ~InstanceRepacker();

        InstanceRepacker(const InstanceRepacker&) = delete;
        InstanceRepacker& operator=(const InstanceRepacker&) = delete;

        // Dispatches the repack compute for the given vertex array's storage-backed instances on the
        // frame buffer's (sequential) view so it is ordered before the subsequent draw, and returns
        // the dynamic vertex buffer handle to bind via setInstanceDataBuffer. Returns an invalid
        // handle if there is nothing to repack.
        bgfx::DynamicVertexBufferHandle Repack(Graphics::FrameBuffer& frameBuffer, VertexArray* vertexArray,
            const std::map<uint32_t, VertexBuffer::InstanceInfo>& instances, uint32_t instanceCount);

        // Releases the GPU resources associated with a vertex array (call on vertex-array deletion).
        void Forget(VertexArray* vertexArray);

    private:
        // Upper bound on bgfx's maxInstanceData slots.
        static constexpr uint32_t kMaxAttributes = 32;

        struct PerVertexArray
        {
            // Destination is a StorageBuffer so the repack kernel can bind it as a RAW UAV
            // (same path as Params/Src). Handle() is then passed to setInstanceDataBuffer.
            std::shared_ptr<StorageBuffer> DestStorage;
            uint32_t Capacity{}; // in instances
            uint16_t Stride{};   // bytes per instance (== attributeCount * 16)
            std::shared_ptr<StorageBuffer> Params;
        };

        void EnsureProgram();
        void DestroyDest(PerVertexArray& state);

        Graphics::DeviceContext& m_deviceContext;
        const uintptr_t m_deviceId;
        bgfx::ProgramHandle m_program{bgfx::kInvalidHandle};
        std::map<VertexArray*, PerVertexArray> m_perVertexArray;
    };
}
