#include "StorageBuffer.h"
#include "Babylon/Graphics/DeviceContext.h"
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace Babylon
{
    namespace
    {
        uint32_t RoundUp(uint32_t value, uint32_t multiple)
        {
            return (value + multiple - 1) / multiple * multiple;
        }
    }

    StorageBuffer::StorageBuffer(Graphics::DeviceContext& deviceContext, uint32_t byteLength, bool asVertexBuffer, uint32_t byteStride)
        : m_deviceContext{deviceContext}
        , m_deviceId{m_deviceContext.GetDeviceId()}
        , m_byteLength{byteLength}
        , m_asVertexBuffer{asVertexBuffer}
        , m_byteStride{byteStride == 0 ? 16u : byteStride}
        , m_shadow(RoundUp(byteLength, m_byteStride), 0)
    {
    }

    StorageBuffer::~StorageBuffer()
    {
        Dispose();
    }

    void StorageBuffer::Dispose()
    {
        if (m_disposed)
        {
            return;
        }

        if (bgfx::isValid(m_handle) && m_deviceId == m_deviceContext.GetDeviceId())
        {
            bgfx::destroy(m_handle);
        }
        m_handle = BGFX_INVALID_HANDLE;

        m_shadow.clear();
        m_disposed = true;
    }

    void StorageBuffer::Update(gsl::span<const uint8_t> bytes, uint32_t byteOffset)
    {
        if (byteOffset + bytes.size() > m_byteLength)
        {
            throw std::runtime_error{"StorageBuffer update out of range"};
        }

        // Always keep the CPU shadow in sync so CPU-side consumers (debug/readback/repack
        // fallbacks) see the latest data even after the GPU buffer exists.
        std::memcpy(m_shadow.data() + byteOffset, bytes.data(), bytes.size());

        if (!bgfx::isValid(m_handle))
        {
            return;
        }

        // Already created: a compute (UAV) buffer is USAGE_DEFAULT, so route through
        // bgfx::update, which copies via a staging buffer. bgfx addresses dynamic vertex
        // buffers by element, so the offset must be stride-aligned.
        if (byteOffset % m_byteStride != 0)
        {
            throw std::runtime_error{"StorageBuffer update byteOffset must be a multiple of the buffer stride"};
        }

        const uint32_t startVertex = byteOffset / m_byteStride;
        bgfx::update(m_handle, startVertex, bgfx::copy(bytes.data(), static_cast<uint32_t>(bytes.size())));
    }

    void StorageBuffer::EnsureCreated()
    {
        if (bgfx::isValid(m_handle))
        {
            return;
        }

        bgfx::VertexLayout layout;
        layout.begin();
        layout.m_stride = static_cast<uint16_t>(m_byteStride);
        layout.end();

        const uint16_t flags = BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_COMPUTE_RAW;

        // COMPUTE_WRITE buffers cannot be initialized from CPU memory via createDynamicVertexBuffer(mem).
        // Create empty, then seed with Update (staging copy) so initial particle data is present.
        const uint32_t numVertices = static_cast<uint32_t>(m_shadow.size()) / m_byteStride;
        m_handle = bgfx::createDynamicVertexBuffer(numVertices, layout, flags);

        if (!bgfx::isValid(m_handle))
        {
            throw std::runtime_error{"Failed to create storage buffer"};
        }

        if (!m_shadow.empty())
        {
            // Seed GPU contents from the CPU shadow. Note: bgfx D3D11 asserts m_dynamic on
            // update for non-UAV buffers; UAV buffers take the staging-copy path when the
            // staging-buffer config is off. We still try so initial particle state is not empty.
            bgfx::update(m_handle, 0, bgfx::copy(m_shadow.data(), static_cast<uint32_t>(m_shadow.size())));
        }
    }

    void StorageBuffer::SetCompute(bgfx::Encoder* encoder, uint8_t stage, bgfx::Access::Enum access)
    {
        EnsureCreated();
        encoder->setBuffer(stage, m_handle, access);
    }

    void StorageBuffer::SetVertex(bgfx::Encoder* encoder, uint8_t stream, uint32_t startVertex, uint32_t numVertices, bgfx::VertexLayoutHandle layout)
    {
        EnsureCreated();
        encoder->setVertexBuffer(stream, m_handle, startVertex, numVertices, layout);
    }

    bgfx::DynamicVertexBufferHandle StorageBuffer::Handle()
    {
        EnsureCreated();
        return m_handle;
    }
}
