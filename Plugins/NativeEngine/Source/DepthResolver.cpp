#include "DepthResolver.h"

#include <Babylon/Graphics/DeviceContext.h>
#include <bgfx/embedded_shader.h>

#include <optional>
#include <stdexcept>
#include <vector>

#include "Shaders/dxbc/vs_depth_resolve.h"
#include "Shaders/dxil/vs_depth_resolve.h"
#include "Shaders/essl/vs_depth_resolve.h"
#include "Shaders/glsl/vs_depth_resolve.h"
#include "Shaders/metal/vs_depth_resolve.h"
#include "Shaders/spirv/vs_depth_resolve.h"
#include "Shaders/dxbc/fs_depth_resolve.h"
#include "Shaders/dxil/fs_depth_resolve.h"
#include "Shaders/essl/fs_depth_resolve.h"
#include "Shaders/glsl/fs_depth_resolve.h"
#include "Shaders/metal/fs_depth_resolve.h"
#include "Shaders/spirv/fs_depth_resolve.h"
#include "Shaders/dxbc/vs_depth_resolve_array.h"
#include "Shaders/dxil/vs_depth_resolve_array.h"
#include "Shaders/essl/vs_depth_resolve_array.h"
#include "Shaders/glsl/vs_depth_resolve_array.h"
#include "Shaders/metal/vs_depth_resolve_array.h"
#include "Shaders/spirv/vs_depth_resolve_array.h"
#include "Shaders/dxbc/fs_depth_resolve_array.h"
#include "Shaders/dxil/fs_depth_resolve_array.h"
#include "Shaders/essl/fs_depth_resolve_array.h"
#include "Shaders/glsl/fs_depth_resolve_array.h"
#include "Shaders/metal/fs_depth_resolve_array.h"
#include "Shaders/spirv/fs_depth_resolve_array.h"

namespace Babylon
{
    namespace
    {
        const bgfx::EmbeddedShader DepthShaders[]{
            BGFX_EMBEDDED_SHADER(vs_depth_resolve),
            BGFX_EMBEDDED_SHADER(fs_depth_resolve),
            BGFX_EMBEDDED_SHADER(vs_depth_resolve_array),
            BGFX_EMBEDDED_SHADER(fs_depth_resolve_array),
            BGFX_EMBEDDED_SHADER_END()};
    }

    struct DepthResolver::Target
    {
        explicit Target(Graphics::DeviceContext& context)
            : Context{context}, DeviceId{context.GetDeviceId()}
        {
        }

        ~Target()
        {
            if (DeviceId == Context.GetDeviceId())
            {
                for (const auto frameBuffer : FrameBuffers)
                {
                    bgfx::destroy(frameBuffer);
                }
                if (bgfx::isValid(Texture))
                {
                    bgfx::destroy(Texture);
                }
            }
        }

        Graphics::DeviceContext& Context;
        uintptr_t DeviceId;
        bgfx::TextureHandle Texture{bgfx::kInvalidHandle};
        std::vector<bgfx::FrameBufferHandle> FrameBuffers;
        std::optional<uint64_t> Revision;
    };

    DepthResolver::DepthResolver(Graphics::DeviceContext& context)
        : m_context{context}, m_deviceId{context.GetDeviceId()}
    {
    }

    DepthResolver::~DepthResolver()
    {
        m_targets.clear();
        if (m_deviceId == m_context.GetDeviceId())
        {
            for (const auto program : m_programs)
            {
                if (bgfx::isValid(program))
                {
                    bgfx::destroy(program);
                }
            }
            if (bgfx::isValid(m_sampler))
            {
                bgfx::destroy(m_sampler);
            }
            if (bgfx::isValid(m_layer))
            {
                bgfx::destroy(m_layer);
            }
        }
    }

    void DepthResolver::EnsureProgram(bool array)
    {
        if (!bgfx::isValid(m_sampler))
        {
            m_sampler = bgfx::createUniform("bnDepthResolveSource", bgfx::UniformType::Sampler);
            if (!bgfx::isValid(m_sampler))
            {
                throw std::runtime_error{"Failed to create depth resolve sampler"};
            }
        }
        if (!bgfx::isValid(m_layer))
        {
            m_layer = bgfx::createUniform("bnDepthResolveLayer", bgfx::UniformType::Vec4);
            if (!bgfx::isValid(m_layer))
            {
                throw std::runtime_error{"Failed to create depth resolve layer uniform"};
            }
        }
        auto& program = m_programs[array ? 1 : 0];
        if (bgfx::isValid(program))
        {
            return;
        }
        const auto renderer = bgfx::getRendererType();
        const auto vertex = bgfx::createEmbeddedShader(DepthShaders, renderer, array ? "vs_depth_resolve_array" : "vs_depth_resolve");
        const auto fragment = bgfx::createEmbeddedShader(DepthShaders, renderer, array ? "fs_depth_resolve_array" : "fs_depth_resolve");
        if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment))
        {
            if (bgfx::isValid(vertex))
            {
                bgfx::destroy(vertex);
            }
            if (bgfx::isValid(fragment))
            {
                bgfx::destroy(fragment);
            }
            throw std::runtime_error{"Failed to create embedded depth resolve shaders"};
        }
        program = bgfx::createProgram(vertex, fragment, true);
        if (!bgfx::isValid(program))
        {
            throw std::runtime_error{"Failed to create embedded depth resolve program"};
        }
    }

    bgfx::TextureHandle DepthResolver::Resolve(const std::shared_ptr<Graphics::MultisampledDepthState>& source)
    {
        if (!source || !bgfx::isValid(source->Handle))
        {
            throw std::runtime_error{"Depth resolve requires a valid multisampled depth texture"};
        }
        std::erase_if(m_targets, [](const auto& entry) { return entry.first.expired(); });
        auto& target = m_targets[source];
        if (!target)
        {
            auto created = std::make_unique<Target>(m_context);
            created->Texture = bgfx::createTexture2D(source->Width, source->Height, false, source->Layers, source->Format, BGFX_TEXTURE_RT);
            if (!bgfx::isValid(created->Texture))
            {
                throw std::runtime_error{"Failed to create resolved depth texture"};
            }
            for (uint16_t layer = 0; layer < source->Layers; ++layer)
            {
                bgfx::Attachment attachment{};
                attachment.init(created->Texture, bgfx::Access::Write, layer, 1, 0, BGFX_ATTACHMENT_NONE);
                const auto frameBuffer = bgfx::createFrameBuffer(1, &attachment);
                if (!bgfx::isValid(frameBuffer))
                {
                    throw std::runtime_error{"Failed to create depth resolve framebuffer"};
                }
                created->FrameBuffers.push_back(frameBuffer);
            }
            target = std::move(created);
        }
        if (source->HasNativeWriter && !source->External && target->Revision == source->Revision)
        {
            return target->Texture;
        }

        const bool array = source->Layers > 1;
        EnsureProgram(array);
        for (uint16_t layer = 0; layer < source->Layers; ++layer)
        {
            m_context.FlushViewsIfNeeded();
            const auto view = m_context.AcquireNewViewId();
            bgfx::resetView(view);
            bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
            bgfx::setViewFrameBuffer(view, target->FrameBuffers[layer]);
            bgfx::setViewRect(view, 0, 0, source->Width, source->Height);
            auto* encoder = m_context.GetActiveEncoder();
            encoder->discard(BGFX_DISCARD_ALL);
            encoder->setTexture(0, m_sampler, source->Handle);
            const float values[4]{static_cast<float>(layer), 0, 0, 0};
            encoder->setUniform(m_layer, values);
            encoder->setVertexCount(3);
            encoder->setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS);
            encoder->setStencil(BGFX_STENCIL_NONE);
            encoder->submit(view, m_programs[array ? 1 : 0]);
        }
        target->Revision = source->Revision;
        ++m_resolveCount;
        return target->Texture;
    }
}
