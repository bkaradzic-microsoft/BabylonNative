#ifdef HAS_SHADER_COMPILER
#include "StorageBuffer.h"

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Graphics/DeviceContext.h>
#include <Babylon/Graphics/Texture.h>
#include <Babylon/Plugins/ShaderCache.h>
#include <Babylon/Plugins/ShaderCacheInternal.h>
#include <Babylon/Plugins/ShaderCompiler.h>
#include <gsl/util>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

extern Babylon::Graphics::Configuration g_deviceConfig;

namespace
{
    [[maybe_unused]] constexpr std::string_view WriteSource = R"(#version 310 es
precision highp float;
precision highp int;
layout(local_size_x = 1) in;
layout(std430, binding = 3) readonly buffer Source { uint sourceData[]; };
layout(std430, binding = 5) buffer Destination { uint destinationData[]; };
void main() {
    uint i = gl_GlobalInvocationID.x;
    destinationData[i] = sourceData[i] * 2u + 5u;
}
)";

    [[maybe_unused]] constexpr std::string_view ReadSource = R"(#version 310 es
precision highp float;
precision highp int;
layout(local_size_x = 1, local_size_y = 1) in;
layout(std430, binding = 6) readonly buffer Source { uint data[]; };
layout(rgba8, binding = 0) writeonly uniform highp image2D dest;
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    uint i = uint(p.y) * 4u + uint(p.x);
    imageStore(dest, p, vec4(float(data[i]) / 255.0, float(i) / 255.0, 0.0, 1.0));
}
)";

    [[maybe_unused]] std::array<uint32_t, 2> ReadRawMasks(const std::vector<uint8_t>& bytes)
    {
        if (bytes.size() < 20 || bytes[3] != 12)
        {
            throw std::runtime_error{"Expected a bgfx v12 shader header"};
        }
        std::array<uint32_t, 2> masks{};
        std::memcpy(masks.data(), bytes.data() + 12, sizeof(masks));
        return masks;
    }

    [[maybe_unused]] bgfx::ProgramHandle CreateComputeProgram(const Babylon::Graphics::BgfxShaderInfo& info)
    {
        const auto shader = bgfx::createShader(bgfx::copy(info.ComputeBytes.data(), static_cast<uint32_t>(info.ComputeBytes.size())));
        if (!bgfx::isValid(shader))
        {
            throw std::runtime_error{"Failed to create compute shader"};
        }
        const auto program = bgfx::createProgram(shader, true);
        if (!bgfx::isValid(program))
        {
            throw std::runtime_error{"Failed to create compute program"};
        }
        return program;
    }

    [[maybe_unused]] std::string ConstantComputeSource(std::string_view value)
    {
        return std::string{"#version 310 es\nprecision highp float;\nlayout(local_size_x = 1) in;\n"
                           "layout(std430, binding = 1) buffer Output { vec4 values[]; };\n"
                           "void main() { values[0] = vec4("} +
               std::string{value} + "); }\n";
    }

    [[maybe_unused]] bool RendererSupportsCompute()
    {
        Babylon::Graphics::Device device{g_deviceConfig};
        device.StartRenderingCurrentFrame();
        const bool supported = (bgfx::getCaps()->supported & BGFX_CAPS_COMPUTE) != 0;
        device.FinishRenderingCurrentFrame();
        return supported;
    }

    // Dispatches a compute shader that writes one vec4 to a storage buffer bound at binding 1 and reads it back.
    [[maybe_unused]] std::array<float, 4> DispatchComputeShader(const Babylon::Graphics::BgfxShaderInfo& shaderInfo)
    {
        Babylon::Graphics::Device device{g_deviceConfig};
        device.StartRenderingCurrentFrame();
        std::array<float, 4> values{};
        std::promise<std::string> completed;
        auto future = completed.get_future();
        {
            Babylon::AppRuntime runtime{};
            std::unique_ptr<Babylon::StorageBuffer> buffer;
            bgfx::ProgramHandle program{bgfx::kInvalidHandle};
            runtime.Dispatch([&](Napi::Env env) {
                try
                {
                    device.AddToJavaScript(env);
                    auto& context = Babylon::Graphics::DeviceContext::GetFromJavaScript(env);
                    auto scope = context.AcquireFrameCompletionScope();
                    program = CreateComputeProgram(shaderInfo);
                    buffer = std::make_unique<Babylon::StorageBuffer>(context, static_cast<uint32_t>(sizeof(values)));
                    auto* encoder = context.GetActiveEncoder();
                    buffer->SetCompute(encoder, 1, bgfx::Access::ReadWrite);
                    encoder->dispatch(context.AcquireNewViewId(), program, 1, 1, 1);
                    auto readback = std::make_shared<Babylon::Graphics::Texture>(context);
                    readback->Create2D(1, 1, false, 1, bgfx::TextureFormat::RGBA32F,
                        BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
                    bgfx::TextureRegion destination{};
                    destination.init(readback->Handle());
                    bgfx::BufferRegion sourceRegion{};
                    sourceRegion.init(buffer->Handle(), 0, static_cast<uint32_t>(sizeof(values)));
                    encoder->blit(context.AcquireNewViewId(), destination, sourceRegion);
                    context.ReadTextureAsync(readback->Handle(),
                        gsl::make_span(reinterpret_cast<uint8_t*>(values.data()), sizeof(values)))
                        .then(arcana::inline_scheduler, arcana::cancellation::none(),
                            [readback, &completed](const arcana::expected<void, std::exception_ptr>& result) {
                                std::string error;
                                if (result.has_error())
                                {
                                    try
                                    {
                                        std::rethrow_exception(result.error());
                                    }
                                    catch (const std::exception& ex)
                                    {
                                        error = ex.what();
                                    }
                                }
                                completed.set_value(std::move(error));
                            });
                }
                catch (const std::exception& ex)
                {
                    completed.set_value(ex.what());
                }
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
            while (future.wait_for(std::chrono::milliseconds{1}) != std::future_status::ready)
            {
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    std::cerr << "Timed out waiting for compute readback" << std::endl;
                    std::quick_exit(1);
                }
                device.FinishRenderingCurrentFrame();
                device.StartRenderingCurrentFrame();
            }
            EXPECT_EQ(future.get(), "");
            if (bgfx::isValid(program))
            {
                bgfx::destroy(program);
            }
        }
        device.FinishRenderingCurrentFrame();
        return values;
    }

    struct ComputeResources
    {
        std::unique_ptr<Babylon::StorageBuffer> Source;
        std::unique_ptr<Babylon::StorageBuffer> Destination;
        bgfx::ProgramHandle WriteProgram{bgfx::kInvalidHandle};
        bgfx::ProgramHandle ReadProgram{bgfx::kInvalidHandle};
        bgfx::TextureHandle Texture{bgfx::kInvalidHandle};
        bgfx::TextureHandle Readback{bgfx::kInvalidHandle};

        ~ComputeResources()
        {
            if (bgfx::isValid(WriteProgram))
            {
                bgfx::destroy(WriteProgram);
            }
            if (bgfx::isValid(ReadProgram))
            {
                bgfx::destroy(ReadProgram);
            }
            if (bgfx::isValid(Texture))
            {
                bgfx::destroy(Texture);
            }
            if (bgfx::isValid(Readback))
            {
                bgfx::destroy(Readback);
            }
        }
    };
}

#if defined(BABYLON_NATIVE_GRAPHICS_API_D3D11) || defined(BABYLON_NATIVE_GRAPHICS_API_D3D12)
TEST(NativeEngineCompute, PackagesRawBindingMasks)
{
    Babylon::Plugins::ShaderCompiler compiler;
    const auto write = compiler.CompileCompute(WriteSource);
    EXPECT_EQ(ReadRawMasks(write.ComputeBytes), (std::array<uint32_t, 2>{1u << 3, 1u << 5}));
    const auto read = compiler.CompileCompute(ReadSource);
    EXPECT_EQ(ReadRawMasks(read.ComputeBytes), (std::array<uint32_t, 2>{1u << 6, 0u}));
}

TEST(NativeEngineCompute, OmitsOptimizedOutRawBindings)
{
    Babylon::Plugins::ShaderCompiler compiler;
    const auto shader = compiler.CompileCompute(R"(#version 310 es
precision highp float;
precision highp int;
layout(local_size_x = 1) in;
layout(std430, binding = 3) readonly buffer Unused { uint unusedData[]; };
layout(std430, binding = 5) buffer Destination { uint data[]; };
void main() { data[gl_GlobalInvocationID.x] = 17u; }
)");
    EXPECT_EQ(ReadRawMasks(shader.ComputeBytes), (std::array<uint32_t, 2>{0u, 1u << 5}));
}

TEST(NativeEngineCompute, ImageBindingsAreNotRawBuffers)
{
    Babylon::Plugins::ShaderCompiler compiler;
    const auto shader = compiler.CompileCompute(R"(#version 310 es
precision highp float;
layout(local_size_x = 1) in;
layout(rgba8, binding = 0) writeonly uniform highp image2D dest;
void main() { imageStore(dest, ivec2(gl_GlobalInvocationID.xy), vec4(1.0)); }
)");
    EXPECT_EQ(ReadRawMasks(shader.ComputeBytes), (std::array<uint32_t, 2>{0u, 0u}));
}

TEST(NativeEngineCompute, RejectsRawBindingsOutsideHeaderMask)
{
    Babylon::Plugins::ShaderCompiler compiler;
    try
    {
        compiler.CompileCompute(R"(#version 310 es
precision highp float;
precision highp int;
layout(local_size_x = 1) in;
layout(std430, binding = 32) readonly buffer Source { uint sourceData[]; };
layout(std430, binding = 0) buffer Destination { uint data[]; };
void main() { data[0] = sourceData[0]; }
)");
        FAIL() << "Accepted a raw binding outside the shader header mask";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_STREQ(error.what(), "Raw buffer binding exceeds bgfx's 32-bit shader mask");
    }
}

TEST(NativeEngineCompute, GraphicsStagesKeepEmptyRawMasks)
{
    Babylon::Plugins::ShaderCompiler compiler;
    const auto shader = compiler.Compile(
        "in vec2 position; void main() { gl_Position = vec4(position, 0.0, 1.0); }",
        "precision highp float; layout(location = 0) out vec4 color; void main() { color = vec4(1.0); }");
    EXPECT_EQ(ReadRawMasks(shader.VertexBytes), (std::array<uint32_t, 2>{0u, 0u}));
    EXPECT_EQ(ReadRawMasks(shader.FragmentBytes), (std::array<uint32_t, 2>{0u, 0u}));
}
#endif

TEST(NativeEngineCompute, RawStorageBufferRoundTrip)
{
#if defined(USE_NOOP_METAL_DEVICE) || defined(SKIP_RENDER_TESTS)
    GTEST_SKIP();
#else
    if (!RendererSupportsCompute())
    {
        GTEST_SKIP() << "Renderer does not support compute";
    }
    Babylon::Graphics::Device device{g_deviceConfig};
    device.StartRenderingCurrentFrame();
    std::array<uint8_t, 4 * 4 * 4> pixels{};
    std::promise<void> completed;
    auto completion = completed.get_future();
    {
        Babylon::AppRuntime runtime;
        ComputeResources resources;
        runtime.Dispatch([&](Napi::Env env) {
            try
            {
                device.AddToJavaScript(env);
                auto& context = Babylon::Graphics::DeviceContext::GetFromJavaScript(env);
                auto scope = context.AcquireFrameCompletionScope();
                Babylon::Plugins::ShaderCompiler compiler;
                resources.WriteProgram = CreateComputeProgram(compiler.CompileCompute(WriteSource));
                resources.ReadProgram = CreateComputeProgram(compiler.CompileCompute(ReadSource));

                resources.Source = std::make_unique<Babylon::StorageBuffer>(context, 64, 16, false);
                resources.Destination = std::make_unique<Babylon::StorageBuffer>(context, 64);
                std::array<uint32_t, 16> source{};
                resources.Source->Update(gsl::make_span(reinterpret_cast<const uint8_t*>(source.data()), sizeof(source)), 0);
                for (uint32_t i = 0; i < source.size(); ++i)
                {
                    source[i] = 3u * i + 1u;
                }
                resources.Source->Update(gsl::make_span(reinterpret_cast<const uint8_t*>(source.data()), sizeof(source)), 0);

                resources.Texture = bgfx::createTexture2D(4, 4, false, 1, bgfx::TextureFormat::RGBA8,
                    BGFX_TEXTURE_COMPUTE_WRITE);
                resources.Readback = bgfx::createTexture2D(4, 4, false, 1, bgfx::TextureFormat::RGBA8,
                    BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
                if (!bgfx::isValid(resources.Texture) || !bgfx::isValid(resources.Readback))
                {
                    throw std::runtime_error{"Failed to create compute readback texture"};
                }

                auto* encoder = context.GetActiveEncoder();
                if (encoder == nullptr)
                {
                    throw std::runtime_error{"Expected an active compute encoder"};
                }
                resources.Source->SetCompute(encoder, 3, bgfx::Access::Read);
                resources.Destination->SetCompute(encoder, 5, bgfx::Access::ReadWrite);
                encoder->dispatch(context.AcquireNewViewId(), resources.WriteProgram, 16);
                resources.Destination->SetCompute(encoder, 6, bgfx::Access::Read);
                encoder->setImage(0, resources.Texture, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA8);
                encoder->dispatch(context.AcquireNewViewId(), resources.ReadProgram, 4, 4);
                bgfx::TextureRegion destinationRegion{};
                destinationRegion.init(resources.Readback);
                bgfx::TextureRegion sourceRegion{};
                sourceRegion.init(resources.Texture);
                encoder->blit(context.AcquireNewViewId(), destinationRegion, sourceRegion);

                context.ReadTextureAsync(resources.Readback, pixels)
                    .then(arcana::inline_scheduler, arcana::cancellation::none(),
                        [&completed](const arcana::expected<void, std::exception_ptr>& result) {
                            if (result.has_error())
                            {
                                completed.set_exception(result.error());
                            }
                            else
                            {
                                completed.set_value();
                            }
                        });
            }
            catch (...)
            {
                completed.set_exception(std::current_exception());
            }
        });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        while (completion.wait_for(std::chrono::milliseconds{1}) != std::future_status::ready)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                std::cerr << "Timed out waiting for raw storage-buffer GPU readback" << std::endl;
                std::quick_exit(1);
            }
            device.FinishRenderingCurrentFrame();
            device.StartRenderingCurrentFrame();
        }
    }
    device.FinishRenderingCurrentFrame();
    ASSERT_NO_THROW(completion.get());
    for (uint32_t i = 0; i < 16; ++i)
    {
        EXPECT_EQ(pixels[i * 4], 6u * i + 7u) << "pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 1], i) << "pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 2], 0u) << "pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 3], 255u) << "pixel " << i;
    }
#endif
}

TEST(NativeEngineCompute, CachedComputeShadersDispatch)
{
#if defined(USE_NOOP_METAL_DEVICE) || defined(SKIP_RENDER_TESTS)
    GTEST_SKIP();
#else
    if (!RendererSupportsCompute())
    {
        GTEST_SKIP() << "Renderer does not support compute";
    }
    Babylon::Plugins::ShaderCache::Enable();
    Babylon::Plugins::ShaderCache::Clear();
    const auto disableCache = gsl::finally([] {
        Babylon::Plugins::ShaderCache::Clear();
        Babylon::Plugins::ShaderCache::Disable();
    });

    Babylon::Plugins::ShaderCompiler compiler;
    const auto requested = ConstantComputeSource("1.0, 2.0, 3.0, 4.0");
    std::string requestedCrlf;
    for (const char ch : requested)
    {
        if (ch == '\n')
        {
            requestedCrlf.push_back('\r');
        }
        requestedCrlf.push_back(ch);
    }
    // Plant a different shader under the requested source: only a cache hit can produce its output.
    Babylon::Plugins::ShaderCache::AddComputeShader(requestedCrlf,
        compiler.CompileCompute(ConstantComputeSource("5.0, 6.0, 7.0, 8.0")));
    const auto cached = Babylon::Plugins::ShaderCache::GetComputeShader(requested);
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(DispatchComputeShader(*cached), (std::array<float, 4>{5.f, 6.f, 7.f, 8.f}));

    std::stringstream stream{std::ios::in | std::ios::out | std::ios::binary};
    ASSERT_EQ(Babylon::Plugins::ShaderCache::Save(stream), 1u);
    Babylon::Plugins::ShaderCache::Clear();
    ASSERT_EQ(Babylon::Plugins::ShaderCache::Load(stream), 1u);
    const auto loaded = Babylon::Plugins::ShaderCache::GetComputeShader(requested);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->ComputeBytes, cached->ComputeBytes);
    EXPECT_EQ(DispatchComputeShader(*loaded), (std::array<float, 4>{5.f, 6.f, 7.f, 8.f}));
#endif
}

#endif
