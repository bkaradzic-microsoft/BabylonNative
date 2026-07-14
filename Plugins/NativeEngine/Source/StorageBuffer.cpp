#include "StorageBuffer.h"
#include "Babylon/Graphics/DeviceContext.h"
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace Babylon
{
    namespace
    {
        // Raw byte-address buffers are created through the vertex-buffer path, which needs a
        // layout stride. Use a 16-byte (RGBA32F) stride; the raw UAV/SRV is byte-addressed
        // regardless, so the only effect is that the backing size is rounded up to a multiple
        // of 16 bytes.
        constexpr uint32_t kStride = 16;

        uint32_t RoundUp(uint32_t value, uint32_t multiple)
        {
            return (value + multiple - 1) / multiple * multiple;
        }
    }

    StorageBuffer::StorageBuffer(Graphics::DeviceContext& deviceContext, uint32_t byteLength, bool asVertexBuffer)
        : m_deviceContext{deviceContext}
        , m_deviceId{m_deviceContext.GetDeviceId()}
        , m_byteLength{byteLength}
        , m_asVertexBuffer{asVertexBuffer}
        , m_shadow(RoundUp(byteLength, kStride), 0)
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

        if (!bgfx::isValid(m_handle))
        {
            // Not created yet: update the CPU shadow that seeds creation.
            std::memcpy(m_shadow.data() + byteOffset, bytes.data(), bytes.size());
            return;
        }

        // Already created: a compute (UAV) buffer is USAGE_DEFAULT, so route through
        // bgfx::update, which copies via a staging buffer. bgfx addresses dynamic vertex
        // buffers by element, so the offset must be stride-aligned.
        if (byteOffset % kStride != 0)
        {
            throw std::runtime_error{"StorageBuffer update byteOffset must be a multiple of 16"};
        }

        const uint32_t startVertex = byteOffset / kStride;
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
        layout.m_stride = static_cast<uint16_t>(kStride);
        layout.end();

        const uint16_t flags = BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_COMPUTE_RAW;

        const bgfx::Memory* memory = bgfx::copy(m_shadow.data(), static_cast<uint32_t>(m_shadow.size()));
        m_handle = bgfx::createDynamicVertexBuffer(memory, layout, flags);

        if (!bgfx::isValid(m_handle))
        {
            throw std::runtime_error{"Failed to create storage buffer"};
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
