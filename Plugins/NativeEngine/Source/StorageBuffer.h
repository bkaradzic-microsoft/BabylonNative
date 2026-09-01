#pragma once

#include <bgfx/bgfx.h>
#include <gsl/gsl>
#include <vector>

namespace Babylon
{
    namespace Graphics
    {
        class DeviceContext;
    }

    // Wraps a bgfx compute storage buffer backed by a raw (ByteAddressBuffer) UAV/SRV
    // (BGFX_BUFFER_COMPUTE_RAW), mirroring the web StorageBuffer used by the compute path
    // (e.g. GPUParticleSystem). The buffer is created lazily on first use, seeded with the
    // CPU-side shadow bytes, so a create-then-update sequence works even though a compute
    // (UAV) buffer is USAGE_DEFAULT and cannot be Map-updated.
    class StorageBuffer final
    {
    public:
        // byteStride is the bgfx vertex-layout stride used when the buffer is also bound as a
        // vertex/instance buffer. For pure compute RAW buffers leave it at the default 16.
        // Instance-data destinations must pass (numSlots * 16) so setInstanceDataBuffer advances
        // a full multi-slot instance per step.
                // computeWrite: when false, omit BGFX_BUFFER_COMPUTE_WRITE so the buffer stays CPU-updatable
                // (required for sim-params SSBOs that the CPU refreshes every dispatch).
                StorageBuffer(Graphics::DeviceContext& deviceContext, uint32_t byteLength, bool asVertexBuffer, uint32_t byteStride = 16, bool computeWrite = true);
                ~StorageBuffer();

        // No copy or move semantics.
        StorageBuffer(const StorageBuffer&) = delete;
        StorageBuffer(StorageBuffer&&) = delete;

        void Dispose();

        // Writes bytes into the buffer at byteOffset. Before the bgfx buffer is created this
        // updates the CPU shadow (used to seed creation); afterwards it updates the GPU buffer.
        void Update(gsl::span<const uint8_t> bytes, uint32_t byteOffset);

                // Re-uploads the entire CPU shadow to the GPU (no-op if not yet created). Used to ensure
                // deferred param SSBO uploads are visible to a subsequent compute dispatch on backends
                // where create-then-update seeding can silently leave UAV memory cleared.
                void FlushShadowToGpu();

        // Binds the buffer as a compute resource on the given stage (u#/t# register).
        void SetCompute(bgfx::Encoder* encoder, uint8_t stage, bgfx::Access::Enum access);

        // Binds the buffer as a vertex stream (the storage buffer doubles as a vertex buffer
        // for rendering, e.g. particle positions).
        void SetVertex(bgfx::Encoder* encoder, uint8_t stream, uint32_t startVertex, uint32_t numVertices, bgfx::VertexLayoutHandle layout);

        uint32_t ByteLength() const { return m_byteLength; }
        gsl::span<const uint8_t> ShadowBytes() const { return gsl::make_span(m_shadow.data(), m_shadow.size()); }
        bgfx::DynamicVertexBufferHandle Handle();

    private:
        void EnsureCreated();

        Graphics::DeviceContext& m_deviceContext;
        const uintptr_t m_deviceId{};

        const uint32_t m_byteLength{};
        const bool m_asVertexBuffer{};
                const bool m_computeWrite{true};
                const uint32_t m_byteStride{};

        std::vector<uint8_t> m_shadow{};
        bgfx::DynamicVertexBufferHandle m_handle{bgfx::kInvalidHandle};

        bool m_disposed{};
    };
}
