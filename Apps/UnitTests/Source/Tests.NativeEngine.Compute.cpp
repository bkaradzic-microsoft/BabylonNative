#ifdef HAS_SHADER_COMPILER

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Graphics/DeviceContext.h>
#include <Babylon/Plugins/ShaderCompiler.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>

extern Babylon::Graphics::Configuration g_deviceConfig;

namespace
{
    constexpr std::string_view WriteSource = R"(#version 310 es
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

    constexpr std::string_view ReadSource = R"(#version 310 es
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

    std::array<uint32_t, 2> ReadRawMasks(const std::vector<uint8_t>& bytes)
    {
        if (bytes.size() < 20 || bytes[3] != 12)
        {
            throw std::runtime_error{"Expected a bgfx v12 shader header"};
        }
        std::array<uint32_t, 2> masks{};
        std::memcpy(masks.data(), bytes.data() + 12, sizeof(masks));
        return masks;
    }

    bgfx::ProgramHandle CreateComputeProgram(const Babylon::Graphics::BgfxShaderInfo& info)
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

}

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
