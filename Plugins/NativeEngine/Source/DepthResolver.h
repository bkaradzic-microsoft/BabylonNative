#pragma once

#include <Babylon/Graphics/Texture.h>

#include <array>
#include <map>
#include <memory>

namespace Babylon
{
    class DepthResolver final
    {
    public:
        explicit DepthResolver(Graphics::DeviceContext& context);
        ~DepthResolver();
        DepthResolver(const DepthResolver&) = delete;
        DepthResolver& operator=(const DepthResolver&) = delete;

        bgfx::TextureHandle Resolve(const std::shared_ptr<Graphics::MultisampledDepthState>& source);

#ifdef BABYLON_NATIVE_NATIVEENGINE_TEST_HOOKS
        uint64_t ResolveCount() const { return m_resolveCount; }
#endif

    private:
        struct Target;
        void EnsureProgram(bool array);

        Graphics::DeviceContext& m_context;
        const uintptr_t m_deviceId;
        std::array<bgfx::ProgramHandle, 2> m_programs{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
        bgfx::UniformHandle m_sampler{bgfx::kInvalidHandle};
        bgfx::UniformHandle m_layer{bgfx::kInvalidHandle};
        using Source = std::weak_ptr<Graphics::MultisampledDepthState>;
        std::map<Source, std::unique_ptr<Target>, std::owner_less<Source>> m_targets;
        uint64_t m_resolveCount{};
    };
}
