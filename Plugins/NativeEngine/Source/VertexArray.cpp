#include "VertexArray.h"
#include <cassert>
#include "Babylon/Graphics/DeviceContext.h"

namespace Babylon
{
    VertexArray::~VertexArray()
    {
        Dispose();
    }

    void VertexArray::Dispose()
    {
        if (m_disposed)
        {
            return;
        }

        m_indexBuffer = nullptr;
        m_vertexBufferRecords.clear();
        m_vertexBufferInstances.clear();

        m_disposed = true;
    }

    void VertexArray::RecordIndexBuffer(IndexBuffer* indexBuffer)
    {
        m_indexBuffer = indexBuffer;
        m_indexBuffer->Build();
    }

    void VertexArray::RecordVertexBuffer(VertexBuffer* vertexBuffer, uint32_t location, uint32_t byteOffset, uint32_t byteStride, uint32_t numElements, uint32_t type, bool normalized, uint32_t divisor)
    {
        auto attrib = static_cast<bgfx::Attrib::Enum>(location);
        auto attribType = static_cast<bgfx::AttribType::Enum>(type);

        if (divisor == 1)
        {
            if (attribType != bgfx::AttribType::Float || normalized)
            {
                throw std::runtime_error{"Unsupported vertex buffer attribute type or normalized flag"};
            }

            // Check if instancing is supported.
            const bool instancingSupported = 0 != (BGFX_CAPS_INSTANCING & bgfx::getCaps()->supported);
            if (!instancingSupported)
            {
                throw std::runtime_error{"Instancing is not supported"};
            }

            // bgfx supports up to caps.limits.maxInstanceData per-instance vec4 slots.
            if (m_vertexBufferInstances.size() >= bgfx::getCaps()->limits.maxInstanceData)
            {
                throw std::runtime_error{"Number of vertex buffer instances exceeds the maximum supported instance-data slots"};
            }

            m_vertexBufferInstances[attrib] = {vertexBuffer, byteOffset, byteStride, static_cast<uint16_t>(sizeof(float) * numElements)};
        }
        else
        {
            vertexBuffer->Build(byteStride);

            bgfx::VertexLayout layout{};
            layout.begin();
            layout.add(attrib, static_cast<uint8_t>(numElements), attribType, normalized);
            layout.m_stride = static_cast<uint16_t>(byteStride);
            layout.m_offset[attrib] = static_cast<uint16_t>(byteOffset % byteStride);
            layout.end();

            if (!m_vertexBufferRecords.try_emplace(attrib, vertexBuffer, byteOffset / byteStride, bgfx::createVertexLayout(layout)).second)
            {
                throw std::runtime_error{"Multiple vertex buffers with the same attribute cannot be recorded"};
            }
        }
    }

    void VertexArray::RecordStorageBuffer(StorageBuffer* storageBuffer, uint32_t location, uint32_t byteOffset, uint32_t byteStride, uint32_t numElements)
    {
        auto attrib = static_cast<bgfx::Attrib::Enum>(location);

        // Check if instancing is supported.
        const bool instancingSupported = 0 != (BGFX_CAPS_INSTANCING & bgfx::getCaps()->supported);
        if (!instancingSupported)
        {
            throw std::runtime_error{"Instancing is not supported"};
        }

        if (m_vertexBufferInstances.size() >= bgfx::getCaps()->limits.maxInstanceData)
        {
            throw std::runtime_error{"Number of vertex buffer instances exceeds the maximum supported instance-data slots"};
        }

        VertexBuffer::InstanceInfo info{};
        info.Buffer = nullptr;
        info.Offset = byteOffset;
        info.Stride = byteStride;
        info.ElementSize = static_cast<uint32_t>(sizeof(float) * numElements);
        info.StorageSource = storageBuffer;
        m_vertexBufferInstances[attrib] = info;
    }

    bool VertexArray::HasStorageInstances() const
    {
        for (const auto& pair : m_vertexBufferInstances)
        {
            if (pair.second.StorageSource != nullptr)
            {
                return true;
            }
        }
        return false;
    }

    void VertexArray::SetIndexBuffer(bgfx::Encoder* encoder, uint32_t firstIndex, uint32_t numIndices)
    {
        if (m_indexBuffer != nullptr)
        {
            m_indexBuffer->Set(encoder, firstIndex, numIndices);
        }
    }

    void VertexArray::SetVertexBuffers(bgfx::Encoder* encoder, uint32_t startVertex, uint32_t numVertices, uint32_t instanceCount)
    {
        // Check if instancing is supported.
        const bool instancingSupported = 0 != (BGFX_CAPS_INSTANCING & bgfx::getCaps()->supported);
        if (!m_vertexBufferInstances.empty() && instancingSupported && !HasStorageInstances())
        {
            bgfx::InstanceDataBuffer instanceDataBuffer{};
            VertexBuffer::BuildInstanceDataBuffer(instanceDataBuffer, m_vertexBufferInstances, instanceCount);
            encoder->setInstanceDataBuffer(&instanceDataBuffer);
        }
        else if (instanceCount > 0 && !HasStorageInstances() && (0 != (BGFX_CAPS_VERTEX_ID & bgfx::getCaps()->supported)))
        {
            // Attribute-less instancing: the draw requests multiple instances but has no per-instance
            // vertex data (e.g. the clustered-light tile-mask proxies, which derive the light index,
            // vertical batch offset and bit mask purely from gl_InstanceID). Without an instance data
            // buffer bgfx would fall back to drawing a single instance, leaving the tile mask empty and
            // breaking clustered lighting. Tell bgfx the instance count explicitly so gl_InstanceID
            // iterates 0..instanceCount-1. Requires BGFX_CAPS_VERTEX_ID (gl_InstanceID support).
            encoder->setInstanceCount(instanceCount);
        }

        uint8_t stream = 0;
        for (const auto& pair : m_vertexBufferRecords)
        {
            auto& record{pair.second};
            record.Buffer->Set(encoder, stream++, record.Offset + startVertex, numVertices, record.LayoutHandle);
        }
    }
}
