#include "StorageBuffer.h"

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Graphics/DeviceContext.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <gtest/gtest.h>
#include <napi/pointer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>

extern Babylon::Graphics::Configuration g_deviceConfig;

namespace
{
    void RunStorageTest(const std::function<void(Napi::Env, Babylon::Graphics::DeviceContext&)>& test)
    {
        Babylon::Graphics::Device device{g_deviceConfig};
        device.StartRenderingCurrentFrame();
        std::promise<void> completed;
        auto completion = completed.get_future();
        {
            Babylon::AppRuntime runtime;
            runtime.Dispatch([&](Napi::Env env) {
                try
                {
                    device.AddToJavaScript(env);
                    test(env, Babylon::Graphics::DeviceContext::GetFromJavaScript(env));
                    completed.set_value();
                }
                catch (...)
                {
                    completed.set_exception(std::current_exception());
                }
            });
            if (completion.wait_for(std::chrono::seconds{30}) != std::future_status::ready)
            {
                std::cerr << "Timed out waiting for storage buffer validation" << std::endl;
                std::quick_exit(1);
            }
        }
        device.FinishRenderingCurrentFrame();
        ASSERT_NO_THROW(completion.get());
    }
}

TEST(NativeEngineStorageBuffer, RejectsInvalidSizesBeforeAllocating)
{
    RunStorageTest([](Napi::Env, Babylon::Graphics::DeviceContext& context) {
        EXPECT_THROW((Babylon::StorageBuffer{context, 0, false}), std::invalid_argument);
        EXPECT_THROW((Babylon::StorageBuffer{context, UINT32_MAX, false}), std::overflow_error);
        EXPECT_THROW((Babylon::StorageBuffer{context, UINT32_MAX - 14, false}), std::overflow_error);
        EXPECT_THROW((Babylon::StorageBuffer{context, 16, false, 65536}), std::invalid_argument);
        Babylon::StorageBuffer buffer{context, 17, false};
        EXPECT_EQ(buffer.ByteLength(), 17u);
        EXPECT_EQ(buffer.ShadowBytes().size(), 32u);
    });
}

TEST(NativeEngineStorageBuffer, RejectedUpdatesPreserveShadow)
{
    RunStorageTest([](Napi::Env, Babylon::Graphics::DeviceContext& context) {
        Babylon::StorageBuffer buffer{context, 32, false};
        std::array<uint8_t, 16> bytes{};
        bytes.fill(91);
        for (const uint32_t offset : {1u, 17u, 32u, UINT32_MAX})
        {
            SCOPED_TRACE(offset);
            EXPECT_THROW(buffer.Update(bytes, offset), std::runtime_error);
            EXPECT_TRUE(std::all_of(buffer.ShadowBytes().begin(), buffer.ShadowBytes().end(),
                [](uint8_t value) { return value == 0; }));
        }
        EXPECT_NO_THROW(buffer.Update({}, 32));
        buffer.Dispose();
        EXPECT_THROW(buffer.Update(bytes, 0), std::runtime_error);
    });
}

TEST(NativeEngineStorageBuffer, RejectsJavaScriptSourceAndDestinationRanges)
{
    RunStorageTest([](Napi::Env env, Babylon::Graphics::DeviceContext&) {
        Babylon::Plugins::NativeEngine::Initialize(env);
        auto engine = env.Global().Get("_native").As<Napi::Object>().Get("Engine").As<Napi::Function>().New({});
        auto create = engine.Get("createStorageBuffer").As<Napi::Function>();
        auto update = engine.Get("updateStorageBuffer").As<Napi::Function>();
        for (const double size : {0.0, -1.0, 0.5, 4294967296.0, static_cast<double>(UINT32_MAX),
                 std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        {
            SCOPED_TRACE(size);
            EXPECT_THROW(create.Call(engine, {Napi::Number::New(env, size)}), Napi::Error);
        }
        auto value = create.Call(engine, {Napi::Number::New(env, 32)});
        auto* buffer = value.As<Napi::Pointer<Babylon::StorageBuffer>>().Get();
        auto data = Napi::ArrayBuffer::New(env, 16);
        const std::array<std::array<double, 3>, 11> invalidRanges{{
            {17, 0, 0}, {16, 1, 0}, {8, 16, 0}, {4294967295.0, 16, 0},
            {-1, 1, 0}, {0.5, 1, 0}, {0, 1.5, 0}, {0, 1, 0.5},
            {0, 1, 1}, {0, 16, 32}, {0, 1, std::numeric_limits<double>::quiet_NaN()},
        }};
        for (const auto& range : invalidRanges)
        {
            EXPECT_THROW(update.Call(engine, {
                value, data, Napi::Number::New(env, range[0]), Napi::Number::New(env, range[1]),
                Napi::Number::New(env, range[2])}), Napi::Error);
            EXPECT_TRUE(std::all_of(buffer->ShadowBytes().begin(), buffer->ShadowBytes().end(),
                [](uint8_t byte) { return byte == 0; }));
        }
        EXPECT_NO_THROW(update.Call(engine, {
            value, Napi::ArrayBuffer::New(env, 0), Napi::Number::New(env, 0),
            Napi::Number::New(env, 0), Napi::Number::New(env, 32)}));
        engine.Get("dispose").As<Napi::Function>().Call(engine, {});
    });
}
