#include "StorageBuffer.h"
#include "Babylon/Graphics/DeviceContext.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Babylon
{
    namespace
    {
        uint32_t RoundUp(uint32_t value, uint32_t multiple)
        {
            if (value == 0 || multiple == 0 || multiple > std::numeric_limits<uint16_t>::max())
            {
                throw std::invalid_argument{"StorageBuffer size and stride must be positive and the stride must fit uint16"};
            }
            const uint32_t remainder = value % multiple;
            const uint32_t padding = remainder == 0 ? 0 : multiple - remainder;
            if (value > std::numeric_limits<uint32_t>::max() - padding)
            {
                throw std::overflow_error{"StorageBuffer rounded size exceeds uint32"};
            }
            return value + padding;
        }
    }

    StorageBuffer::StorageBuffer(Graphics::DeviceContext& deviceContext, uint32_t byteLength, bool asVertexBuffer, uint32_t byteStride, bool computeWrite)
        : m_deviceContext{deviceContext}
        , m_deviceId{m_deviceContext.GetDeviceId()}
        , m_byteLength{byteLength}
        , m_asVertexBuffer{asVertexBuffer}
            , m_computeWrite{computeWrite}
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

    void StorageBuffer::ValidateUpdate(size_t byteLength, uint32_t byteOffset) const
    {
        if (m_disposed)
        {
            throw std::runtime_error{"Cannot update a disposed StorageBuffer"};
        }
        if (byteOffset > m_byteLength || byteLength > m_byteLength - byteOffset)
        {
            throw std::runtime_error{"StorageBuffer update out of range"};
        }
        if (byteOffset % m_byteStride != 0)
        {
            throw std::runtime_error{"StorageBuffer update byteOffset must be a multiple of the buffer stride"};
        }
    }

    void StorageBuffer::Update(gsl::span<const uint8_t> bytes, uint32_t byteOffset)
        {
            ValidateUpdate(bytes.size(), byteOffset);
            if (bytes.empty())
            {
                return;
            }

            // Always keep the CPU shadow in sync so CPU-side consumers (debug/readback/repack
            // fallbacks) see the latest data even after the GPU buffer exists.
            std::memcpy(m_shadow.data() + byteOffset, bytes.data(), bytes.size());

            // Create on first write so deferred SSBO uploads (particle sim params) are not stuck
            // in the shadow until a later SetCompute. Without this, the first N dispatches can run
            // against a freshly created zeroed UAV if EnsureCreated's seed update is lost.
            EnsureCreated();

            // Compute (UAV) buffers are USAGE_DEFAULT; route through bgfx::update (staging copy).
            // bgfx addresses dynamic vertex buffers by element, so the offset must be stride-aligned.
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

        // Readonly compute buffers (params, particlesIn) omit COMPUTE_WRITE so bgfx keeps
                // them dynamic and CPU updates via bgfx::update actually stick on D3D11.
                // Read-write particle outputs keep COMPUTE_WRITE for UAV stores.
                const uint16_t flags = static_cast<uint16_t>(
                    m_computeWrite ? BGFX_BUFFER_COMPUTE_READ_WRITE : BGFX_BUFFER_COMPUTE_READ);

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

    void StorageBuffer::FlushShadowToGpu()
    {
        EnsureCreated();
            if (!bgfx::isValid(m_handle) || m_shadow.empty())
            {
                return;
            }
            bgfx::update(m_handle, 0, bgfx::copy(m_shadow.data(), static_cast<uint32_t>(m_shadow.size())));
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
