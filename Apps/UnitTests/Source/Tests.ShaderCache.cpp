#include <gtest/gtest.h>

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Plugins/ShaderCache.h>
#include <Babylon/Plugins/ShaderCacheInternal.h>
#include <Babylon/ScriptLoader.h>

#include "App.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <future>
#include <iostream>
#include <fstream>
#include <gsl/util>
#include <sstream>

using namespace std::chrono_literals;

extern Babylon::Graphics::Configuration g_deviceConfig;

TEST(ShaderCache, SaveAndLoad)
{
    Babylon::Plugins::ShaderCache::Enable();

    Babylon::Graphics::Device device{g_deviceConfig};

    device.StartRenderingCurrentFrame();

    Babylon::AppRuntime::Options options{};

    options.UnhandledExceptionHandler = [](const Napi::Error& error) {
        std::cerr << "[Uncaught Error] " << Napi::GetErrorString(error) << std::endl;
        std::quick_exit(1);
    };

    Babylon::AppRuntime runtime{options};

    std::promise<void> scriptIsDone{};
    std::promise<void> sceneIsReady{};

    runtime.Dispatch([&device, &sceneIsReady](Napi::Env env) {
        device.AddToJavaScript(env);

        Babylon::Polyfills::Console::Initialize(env, [](const char* message, auto) {
            std::cout << message << std::endl;
        });
        Babylon::Polyfills::Window::Initialize(env);
        Babylon::Plugins::NativeEngine::Initialize(env);

        env.Global().Set("setSceneReady",
            Napi::Function::New(
                env, [&sceneIsReady](const Napi::CallbackInfo&) {
                    sceneIsReady.set_value();
                },
                "setSceneReady"));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.LoadScript("app:///Assets/babylon.max.js");
    loader.LoadScript("app:///Assets/tests.shaderCache.basicScene.js");
    loader.Dispatch([&scriptIsDone](Napi::Env) {
        scriptIsDone.set_value();
    });

    scriptIsDone.get_future().get();

    auto sceneIsReadyFuture = sceneIsReady.get_future();
    while (sceneIsReadyFuture.wait_for(16ms) != std::future_status::ready)
    {
        device.FinishRenderingCurrentFrame();
        device.StartRenderingCurrentFrame();
    }

    const auto shaderCachePath = GetExecutableDirectory() / "shaderCache.bin";

    uint32_t shaderCount{};
    {
        std::ofstream stream(shaderCachePath, std::ios::binary);
        ASSERT_TRUE(stream.is_open()) << "Failed to open for write: " << shaderCachePath;
        shaderCount = Babylon::Plugins::ShaderCache::Save(stream);
        EXPECT_EQ(shaderCount, 1);
    }
    {
        std::ifstream stream(shaderCachePath, std::ios::binary);
        ASSERT_TRUE(stream.is_open()) << "Failed to open for read: " << shaderCachePath;
        auto deserializedCount = Babylon::Plugins::ShaderCache::Load(stream);
        EXPECT_EQ(deserializedCount, shaderCount);
    }
    std::error_code ec;
    const auto removed = std::filesystem::remove(shaderCachePath, ec);
    EXPECT_FALSE(ec) << "Failed to remove " << shaderCachePath << ": " << ec.message();
    EXPECT_TRUE(removed) << "Expected shader cache file to be removed: " << shaderCachePath;

    device.FinishRenderingCurrentFrame();

    Babylon::Plugins::ShaderCache::Disable();
}

TEST(ShaderCache, ComputeEntriesRoundTripSeparatelyFromGraphics)
{
    Babylon::Plugins::ShaderCache::Enable();
    Babylon::Plugins::ShaderCache::Clear();
    const auto disableCache = gsl::finally([] {
        Babylon::Plugins::ShaderCache::Clear();
        Babylon::Plugins::ShaderCache::Disable();
    });

    const std::string vertexSource = "void main() { gl_Position = vec4(0.0); }";
    const std::string fragmentSource = "void main() { gl_FragColor = vec4(1.0); }";
    const std::string computeSource = "#version 310 es\nlayout(local_size_x = 1) in;\nvoid main() {}\n";

    Babylon::Graphics::BgfxShaderInfo graphics{};
    graphics.VertexBytes = {1, 2, 3};
    graphics.FragmentBytes = {4, 5};
    graphics.VertexAttributeLocations = {{"position", 0}};
    graphics.UniformStages = {{"diffuseSampler", 2}};

    Babylon::Graphics::BgfxShaderInfo compute{};
    compute.ComputeBytes = {9, 8, 7, 6};
    compute.UniformStages = {{"depthInput", 5}};
    compute.UniformNames = {{"_12_textureSize", "textureSize"}};
    compute.MultisampledSamplers = {{"depthInput", true}};

    Babylon::Plugins::ShaderCache::AddShader(vertexSource, fragmentSource, graphics);
    Babylon::Plugins::ShaderCache::AddComputeShader(computeSource, compute);

    // Compute and graphics entries use separate key spaces.
    EXPECT_EQ(Babylon::Plugins::ShaderCache::GetComputeShader(vertexSource), nullptr);
    EXPECT_EQ(Babylon::Plugins::ShaderCache::GetShader(computeSource, ""), nullptr);

    std::stringstream stream{std::ios::in | std::ios::out | std::ios::binary};
    ASSERT_EQ(Babylon::Plugins::ShaderCache::Save(stream), 2u);
    Babylon::Plugins::ShaderCache::Clear();
    EXPECT_EQ(Babylon::Plugins::ShaderCache::GetComputeShader(computeSource), nullptr);
    ASSERT_EQ(Babylon::Plugins::ShaderCache::Load(stream), 2u);

    std::string crlfComputeSource;
    for (const char ch : computeSource)
    {
        if (ch == '\n')
        {
            crlfComputeSource.push_back('\r');
        }
        crlfComputeSource.push_back(ch);
    }
    const auto loadedCompute = Babylon::Plugins::ShaderCache::GetComputeShader(crlfComputeSource);
    ASSERT_NE(loadedCompute, nullptr);
    EXPECT_EQ(loadedCompute->ComputeBytes, compute.ComputeBytes);
    EXPECT_TRUE(loadedCompute->VertexBytes.empty());
    EXPECT_TRUE(loadedCompute->FragmentBytes.empty());
    EXPECT_EQ(loadedCompute->UniformStages, compute.UniformStages);
    EXPECT_EQ(loadedCompute->UniformNames, compute.UniformNames);
    EXPECT_EQ(loadedCompute->MultisampledSamplers, compute.MultisampledSamplers);

    const auto loadedGraphics = Babylon::Plugins::ShaderCache::GetShader(vertexSource, fragmentSource);
    ASSERT_NE(loadedGraphics, nullptr);
    EXPECT_EQ(loadedGraphics->VertexBytes, graphics.VertexBytes);
    EXPECT_EQ(loadedGraphics->FragmentBytes, graphics.FragmentBytes);
    EXPECT_TRUE(loadedGraphics->ComputeBytes.empty());
    EXPECT_EQ(loadedGraphics->VertexAttributeLocations, graphics.VertexAttributeLocations);
    EXPECT_EQ(loadedGraphics->UniformStages, graphics.UniformStages);

    std::stringstream resaved{std::ios::in | std::ios::out | std::ios::binary};
    EXPECT_EQ(Babylon::Plugins::ShaderCache::Save(resaved), 2u);
    EXPECT_EQ(resaved.str(), stream.str());
}