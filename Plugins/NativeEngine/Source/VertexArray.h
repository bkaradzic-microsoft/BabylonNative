#pragma once

#include "IndexBuffer.h"
#include "VertexBuffer.h"
#include <set>
#include <map>

namespace Babylon
{
    class VertexArray final
    {
    public:
        VertexArray() = default;
        ~VertexArray();

        VertexArray(const VertexArray&) = delete;
        VertexArray& operator=(const VertexArray&) = delete;

        void Dispose();

        void RecordIndexBuffer(IndexBuffer* indexBuffer);
        void RecordVertexBuffer(VertexBuffer* vertexBuffer, uint32_t location, uint32_t byteOffset, uint32_t byteStride, uint32_t numElements, uint32_t type, bool normalized, uint32_t divisor);

        // Records a GPU compute-written storage buffer as a per-instance (divisor==1) source. The
        // per-instance bytes are packed at byteOffset with byteStride inside the storage buffer;
        // NativeEngine repacks them into bgfx i_data slots on the GPU (see InstanceRepacker).
        void RecordStorageBuffer(StorageBuffer* storageBuffer, uint32_t location, uint32_t byteOffset, uint32_t byteStride, uint32_t numElements);

        void SetIndexBuffer(bgfx::Encoder* encoder, uint32_t firstIndex, uint32_t numIndices);
        void SetVertexBuffers(bgfx::Encoder* encoder, uint32_t startVertex, uint32_t numVertices, uint32_t instanceCount = 0);

        const std::map<bgfx::Attrib::Enum, VertexBuffer::InstanceInfo>& GetInstances() const { return m_vertexBufferInstances; }

        // True when any recorded instance source is a GPU storage buffer, in which case the CPU
        // BuildInstanceDataBuffer path is skipped and NativeEngine binds a GPU-repacked buffer.
        bool HasStorageInstances() const;

    private:
        IndexBuffer* m_indexBuffer{};

        struct VertexBufferRecord
        {
            VertexBuffer* Buffer{};
            const uint32_t Offset{};
            const bgfx::VertexLayoutHandle LayoutHandle{};

            VertexBufferRecord(VertexBuffer* buffer, const uint32_t offset, bgfx::VertexLayoutHandle layoutHandle)
                : Buffer{buffer}
                , Offset{offset}
                , LayoutHandle{layoutHandle}
            {
            }
        };

        std::map<bgfx::Attrib::Enum, VertexBufferRecord> m_vertexBufferRecords{};

        std::map<bgfx::Attrib::Enum, VertexBuffer::InstanceInfo> m_vertexBufferInstances;

        bool m_disposed{};
    };
}
