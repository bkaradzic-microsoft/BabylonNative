#include <gtest/gtest.h>

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Plugins/ExternalTexture.h>
#include <Babylon/ScriptLoader.h>

#include "Helpers.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

extern Babylon::Graphics::Configuration g_deviceConfig;

// These tests pin down the orientation of gl_FragCoord.y.
//
// Babylon Native's shader model is "shader-visible coordinates are GL-logical,
// converted to physical at each sampler access". D3D, Metal and Vulkan rasterize
// with a top-left origin while GL uses bottom-left, and Babylon Native does not
// flip geometry, so gl_FragCoord.y arrives mirrored from the hardware and has to
// be corrected by the shader compiler (FragCoordYFlipTraverser).
//
// Both tests render a full-screen quad into a render target and read the result
// back. Helpers::ReadPixels returns rows in memory order, so row 0 is the top of
// the image on every backend.
namespace
{
    // Renders a full-screen quad into a width x height render target using the
    // supplied fragment shader, and returns the RGBA8 pixels in memory order.
    //
    // The fragment shader may declare a `uniform vec2 targetSize` (set to the
    // render target dimensions) and a `uniform sampler2D inputSampler` (bound to
    // a raw texture whose row y is filled with the RGBA value produced by
    // makeRow(y), when withInputTexture is true).
    std::vector<uint8_t> RenderFullScreenQuad(
        uint32_t width,
        uint32_t height,
        const std::string& vertexShader,
        const std::string& fragmentShader,
        bool withInputTexture,
        const std::string& setupScript = {})
    {
        // Clip-space quad, so no projection matrix is involved and the geometry
        // lines up with the render target exactly. uv follows the GL convention
        // of (0,0) at the bottom-left corner.
        Babylon::Graphics::Device device{g_deviceConfig};
        device.StartRenderingCurrentFrame();

        auto outputTexture = Helpers::CreateTexture(
            device.GetPlatformInfo().Device, width, height, 1, true);
        Babylon::Plugins::ExternalTexture outputExternalTexture{outputTexture};

        Babylon::AppRuntime::Options options{};
        options.UnhandledExceptionHandler = [](const Napi::Error& error) {
            std::cerr << "[Uncaught Error] " << Napi::GetErrorString(error) << std::endl;
            std::cerr.flush();
        };

        Babylon::AppRuntime runtime{options};
        runtime.Dispatch([&device](Napi::Env env) {
            env.Global().Set("globalThis", env.Global());
            device.AddToJavaScript(env);

            Babylon::Polyfills::Console::Initialize(env, [](const char* message, auto) {
                std::cout << message << std::endl;
            });
            Babylon::Polyfills::Window::Initialize(env);
            Babylon::Plugins::NativeEngine::Initialize(env);
        });

        Babylon::ScriptLoader loader{runtime};
        loader.LoadScript("app:///Assets/babylon.max.js");

        const std::string script = R"(
            (function () {
                var vertexShader = VERTEX_SHADER_SOURCE;

                var fragmentShader = FRAGMENT_SHADER_SOURCE;

                globalThis.startup = function (outputNativeTexture, width, height) {
                    var engine = new BABYLON.NativeEngine();
                    engine.getCaps().parallelShaderCompile = null;
                    var scene = new BABYLON.Scene(engine);
                    scene.autoClear = true;
                    scene.clearColor = new BABYLON.Color4(0, 0, 0, 1);

                    var outputTexture = new BABYLON.RenderTargetTexture(
                        "output",
                        { width: width, height: height },
                        scene,
                        {
                            colorAttachment: engine.wrapNativeTexture(outputNativeTexture),
                            generateDepthBuffer: true,
                            generateStencilBuffer: false
                        });

                    var camera = new BABYLON.FreeCamera("camera", new BABYLON.Vector3(0, 0, -1), scene);
                    camera.setTarget(BABYLON.Vector3.Zero());
                    camera.mode = BABYLON.Camera.ORTHOGRAPHIC_CAMERA;
                    camera.orthoTop = 1;
                    camera.orthoBottom = -1;
                    camera.orthoLeft = -1;
                    camera.orthoRight = 1;
                    camera.outputRenderTarget = outputTexture;

                    // Two triangles in clip space covering the whole target. The
                    // vertex shader passes position straight through, so no
                    // projection matrix is involved and the quad lines up exactly
                    // with the render target regardless of camera conventions.
                    var quad = new BABYLON.Mesh("quad", scene);
                    var vertexData = new BABYLON.VertexData();
                    vertexData.positions = [
                        -1, -1, 0,
                         1, -1, 0,
                         1,  1, 0,
                        -1,  1, 0
                    ];
                    vertexData.uvs = [
                        0, 0,
                        1, 0,
                        1, 1,
                        0, 1
                    ];
                    vertexData.indices = [0, 1, 2, 0, 2, 3];
                    vertexData.applyToMesh(quad);
                    quad.alwaysSelectAsActiveMesh = true;

                    var material = new BABYLON.ShaderMaterial(
                        "fragCoordShader",
                        scene,
                        { vertexSource: vertexShader, fragmentSource: fragmentShader },
                        {
                            attributes: vertexShader.indexOf("attribute vec2 uv") !== -1
                                ? ["position", "uv"]
                                : ["position"],
                            uniforms: ["targetSize"],
                            samplers: WITH_INPUT_TEXTURE ? ["inputSampler"] : []
                        });
                    material.onError = function (_effect, errors) {
                        console.error("ShaderMaterial compilation error: " + errors);
                    };
                    material.backFaceCulling = false;
                    material.depthFunction = BABYLON.Constants.ALWAYS;
                    material.setVector2("targetSize", new BABYLON.Vector2(width, height));

                    if (WITH_INPUT_TEXTURE) {
                        // Row y is filled with a monotonically decreasing red ramp so
                        // that a vertical mirror is unambiguous. Blue encodes the low
                        // bits of the row index to catch off-by-one errors.
                        var data = new Uint8Array(width * height * 4);
                        for (var y = 0; y < height; ++y) {
                            for (var x = 0; x < width; ++x) {
                                var i = (y * width + x) * 4;
                                data[i] = 255 - y * 4;
                                data[i + 1] = 0;
                                data[i + 2] = y * 4;
                                data[i + 3] = 255;
                            }
                        }
                        var raw = engine.createRawTexture(
                            data,
                            width,
                            height,
                            BABYLON.Constants.TEXTUREFORMAT_RGBA,
                            false /* generateMipMaps */,
                            false /* invertY */,
                            BABYLON.Constants.TEXTURE_NEAREST_SAMPLINGMODE);
                        var wrapper = new BABYLON.Texture(null, scene);
                        wrapper._texture = raw;
                        wrapper.wrapU = BABYLON.Constants.TEXTURE_CLAMP_ADDRESSMODE;
                        wrapper.wrapV = BABYLON.Constants.TEXTURE_CLAMP_ADDRESSMODE;
                        material.setTexture("inputSampler", wrapper);
                    }

                    quad.material = material;
                    SETUP_SCRIPT
                    globalThis.__scene = scene;
                };

                globalThis.render = function () {
                    var scene = globalThis.__scene;
                    var preparation = globalThis.__prepare ? globalThis.__prepare() : Promise.resolve();
                    return preparation.then(function () {
                        return scene.whenReadyAsync();
                    }).then(function () {
                        scene.render();
                    });
                };
            })();
        )";

        // Inject the caller's shaders as JS string literals.
        const auto toJsStringLiteral = [](const std::string& source) {
            std::string result = "\"";
            for (char c : source)
            {
                if (c == '\n')
                {
                    result += "\\n";
                }
                else if (c == '"')
                {
                    result += "\\\"";
                }
                else if (c == '\\')
                {
                    result += "\\\\";
                }
                else
                {
                    result += c;
                }
            }
            result += "\"";
            return result;
        };

        const auto replaceToken = [](std::string& text, const std::string& token, const std::string& value) {
            for (size_t pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos))
            {
                text.replace(pos, token.size(), value);
                pos += value.size();
            }
        };

        std::string finalScript = script;
        replaceToken(finalScript, "VERTEX_SHADER_SOURCE", toJsStringLiteral(vertexShader));
        replaceToken(finalScript, "FRAGMENT_SHADER_SOURCE", toJsStringLiteral(fragmentShader));
        replaceToken(finalScript, "WITH_INPUT_TEXTURE", withInputTexture ? "true" : "false");
        replaceToken(finalScript, "SETUP_SCRIPT", setupScript);

        loader.Eval(finalScript, "frag_coord_orientation_test.js");

        std::promise<void> startupDone;
        loader.Dispatch([&outputExternalTexture, &startupDone, width, height](Napi::Env env) {
            auto jsOutput = outputExternalTexture.CreateForJavaScript(env);
            env.Global().Get("startup").As<Napi::Function>().Call({
                jsOutput,
                Napi::Number::New(env, width),
                Napi::Number::New(env, height),
            });
            startupDone.set_value();
        });
        startupDone.get_future().wait();

        device.FinishRenderingCurrentFrame();
        device.StartRenderingCurrentFrame();

        std::promise<void> renderDone;
        loader.Dispatch([&renderDone](Napi::Env env) {
            auto jsPromise = env.Global().Get("render").As<Napi::Function>().Call({}).As<Napi::Promise>();

            auto jsOnFulfilled = Napi::Function::New(env, [&renderDone](const Napi::CallbackInfo&) {
                renderDone.set_value();
            });
            auto jsOnRejected = Napi::Function::New(env, [&renderDone](const Napi::CallbackInfo& info) {
                renderDone.set_exception(std::make_exception_ptr(
                    std::runtime_error{Napi::GetErrorString(info[0].As<Napi::Error>())}));
            });

            jsPromise.Get("then").As<Napi::Function>().Call(jsPromise, {jsOnFulfilled, jsOnRejected});
        });

        auto renderFuture = renderDone.get_future();
        EXPECT_EQ(renderFuture.wait_for(std::chrono::seconds(30)), std::future_status::ready)
            << "render timed out";
        EXPECT_NO_THROW(renderFuture.get()) << "render rejected";

        device.FinishRenderingCurrentFrame();

        auto pixels = Helpers::ReadPixels(device.GetPlatformInfo(), outputTexture, width, height);
        Helpers::DestroyTexture(outputTexture);
        return pixels;
    }
}

TEST(NativeEngineShadows, PointLightCubeOrientationMatchesAllShadowLookups)
{
#if defined(SKIP_EXTERNAL_TEXTURE_TESTS) || defined(SKIP_RENDER_TESTS)
    GTEST_SKIP();
#else
    constexpr uint32_t CELL_SIZE = 8;
    constexpr uint32_t FACE_COUNT = 6;
    constexpr uint32_t WIDTH = FACE_COUNT * 2 * CELL_SIZE;
    constexpr uint32_t HEIGHT = CELL_SIZE;

    const std::string vertexShader =
        "precision highp float;\n"
        "attribute vec3 position;\n"
        "attribute vec2 uv;\n"
        "varying vec2 vUV;\n"
        "void main(void) { vUV = uv; gl_Position = vec4(position, 1.0); }\n";

    struct ShadowCase
    {
        int Filter;
        const char* Name;
        bool BackFaceCulling;
        bool CullBackFaces;
        bool ForceBackFacesOnly;
        const char* Lookup;
    };
    const ShadowCase cases[] = {
        {0, "none/back-cull", true, true, false, "computeShadowCube(worldPos, vec3(0.0), shadowSampler, 0.0, vec2(0.1, 10.1))"},
        {0, "none/front-cull", true, false, false, "computeShadowCube(worldPos, vec3(0.0), shadowSampler, 0.0, vec2(0.1, 10.1))"},
        {0, "none/force-back-faces", true, true, true, "computeShadowCube(worldPos, vec3(0.0), shadowSampler, 0.0, vec2(0.1, 10.1))"},
        {1, "esm", true, true, false, "computeShadowWithESMCube(worldPos, vec3(0.0), shadowSampler, 0.0, 20.0, vec2(0.1, 10.1))"},
        {2, "poisson", true, true, false, "computeShadowWithPoissonSamplingCube(worldPos, vec3(0.0), shadowSampler, 1.0 / 64.0, 0.0, vec2(0.1, 10.1))"},
        {4, "close-esm", true, true, false, "computeShadowWithCloseESMCube(worldPos, vec3(0.0), shadowSampler, 0.0, 20.0, vec2(0.1, 10.1))"},
    };

    for (const auto& shadowCase : cases)
    {
        SCOPED_TRACE(shadowCase.Name);

        // Each pair addresses one cube face in PointLight.getShadowDirection order:
        // +X, -X, -Y, +Y, +Z, -Z. The first ray goes through an off-axis
        // caster; the second reflects that ray across the face's projection-Y
        // axis and must stay lit. The +/-Y faces use Z as their vertical axis.
        const std::string fragmentShader =
            "precision highp float;\n"
            "varying vec2 vUV;\n"
            "uniform samplerCube shadowSampler;\n"
            "#define SHADOWS\n"
            "#include<shadowsFragmentFunctions>\n"
            "void main(void) {\n"
            "    float cell = floor(min(vUV.x, 0.999999) * 12.0);\n"
            "    vec3 direction;\n"
            "    if (cell < 0.5) direction = vec3( 1.00,  0.31,  0.17);\n"
            "    else if (cell < 1.5) direction = vec3( 1.00, -0.31,  0.17);\n"
            "    else if (cell < 2.5) direction = vec3(-1.00, -0.27,  0.19);\n"
            "    else if (cell < 3.5) direction = vec3(-1.00,  0.27,  0.19);\n"
            "    else if (cell < 4.5) direction = vec3( 0.23, -1.00,  0.37);\n"
            "    else if (cell < 5.5) direction = vec3( 0.23, -1.00, -0.37);\n"
            "    else if (cell < 6.5) direction = vec3(-0.21,  1.00,  0.33);\n"
            "    else if (cell < 7.5) direction = vec3(-0.21,  1.00, -0.33);\n"
            "    else if (cell < 8.5) direction = vec3( 0.29,  0.35,  1.00);\n"
            "    else if (cell < 9.5) direction = vec3( 0.29, -0.35,  1.00);\n"
            "    else if (cell < 10.5) direction = vec3(-0.25, -0.33, -1.00);\n"
            "    else direction = vec3(-0.25,  0.33, -1.00);\n"
            "    vec3 worldPos = normalize(direction) * 6.0;\n"
            "    float visibility = " +
            std::string{shadowCase.Lookup} +
            ";\n"
            "    gl_FragColor = vec4(vec3(visibility), 1.0);\n"
            "}\n";

        const std::string setupScript =
            R"(
                var filter = )" +
            std::to_string(shadowCase.Filter) +
            R"(;
                var backFaceCulling = )" +
            std::string{shadowCase.BackFaceCulling ? "true" : "false"} +
            R"(;
                var cullBackFaces = )" +
            std::string{shadowCase.CullBackFaces ? "true" : "false"} +
            R"(;
                var forceBackFacesOnly = )" +
            std::string{shadowCase.ForceBackFacesOnly ? "true" : "false"} +
            R"(;

                camera.layerMask = 0x1;
                quad.layerMask = 0x1;
                quad.renderingGroupId = 1;

                var light = new BABYLON.PointLight("shadowLight", BABYLON.Vector3.Zero(), scene);
                light.shadowMinZ = 0.1;
                light.shadowMaxZ = 10.0;

                // Force the portable packed-RGBA shadow path so the lookup
                // shader has one format on every Native backend.
                var caps = engine.getCaps();
                var capNames = [
                    "textureHalfFloatRender",
                    "textureHalfFloatLinearFiltering",
                    "textureFloatRender",
                    "textureFloatLinearFiltering"
                ];
                var savedCaps = capNames.map(function (name) { return caps[name]; });
                var shadowGenerator;
                try {
                    capNames.forEach(function (name) { caps[name] = false; });
                    shadowGenerator = new BABYLON.ShadowGenerator(64, light);
                } finally {
                    capNames.forEach(function (name, index) { caps[name] = savedCaps[index]; });
                }
                shadowGenerator.filter = filter;
                shadowGenerator.bias = 0.0;
                shadowGenerator.depthScale = 20.0;
                shadowGenerator.forceBackFacesOnly = forceBackFacesOnly;

                var casterMaterial = new BABYLON.StandardMaterial("casterMaterial", scene);
                casterMaterial.disableLighting = true;
                casterMaterial.backFaceCulling = backFaceCulling;
                casterMaterial.cullBackFaces = cullBackFaces;

                var casterDirections = [
                    new BABYLON.Vector3( 1.00,  0.31,  0.17),
                    new BABYLON.Vector3(-1.00, -0.27,  0.19),
                    new BABYLON.Vector3( 0.23, -1.00,  0.37),
                    new BABYLON.Vector3(-0.21,  1.00,  0.33),
                    new BABYLON.Vector3( 0.29,  0.35,  1.00),
                    new BABYLON.Vector3(-0.25, -0.33, -1.00)
                ];
                casterDirections.forEach(function (direction, face) {
                    direction.normalize();
                    var caster = BABYLON.MeshBuilder.CreateBox(
                        "caster" + face,
                        { width: 0.8, height: 0.8, depth: 0.8 },
                        scene);
                    caster.position.copyFrom(direction.scale(3.0));
                    caster.material = casterMaterial;
                    caster.layerMask = 0x2;
                    shadowGenerator.addShadowCaster(caster, false);
                });

                material.options.samplers.push("shadowSampler");
                material.setTexture("shadowSampler", shadowGenerator.getShadowMapForRendering());
                globalThis.__prepare = function () {
                    return Promise.all([
                        shadowGenerator.forceCompilationAsync(),
                        material.forceCompilationAsync(quad)
                    ]);
                };
            )";

        const auto pixels = RenderFullScreenQuad(WIDTH, HEIGHT, vertexShader, fragmentShader, false, setupScript);
        ASSERT_EQ(pixels.size(), static_cast<size_t>(WIDTH) * HEIGHT * 4);

        const auto redAtCell = [&pixels](uint32_t cell) {
            const uint32_t x = cell * CELL_SIZE + CELL_SIZE / 2;
            const uint32_t y = HEIGHT / 2;
            return static_cast<int>(pixels[(static_cast<size_t>(y) * WIDTH + x) * 4]);
        };
        const char* faceNames[] = {"+X", "-X", "-Y", "+Y", "+Z", "-Z"};
        for (uint32_t face = 0; face < FACE_COUNT; ++face)
        {
            const int shadowed = redAtCell(face * 2);
            const int lit = redAtCell(face * 2 + 1);
            EXPECT_LT(shadowed, 64)
                << faceNames[face] << " off-axis caster ray was not shadowed (visibility=" << shadowed << ")";
            EXPECT_GT(lit, 192)
                << faceNames[face] << " projection-Y mirror ray was not lit (visibility=" << lit << ")";
        }
    }
#endif
}

TEST(NativeEngineClear, PreservesTextureBindings)
{
#if defined(SKIP_EXTERNAL_TEXTURE_TESTS) || defined(SKIP_RENDER_TESTS)
    GTEST_SKIP();
#else
    const std::string vertexShader =
        "precision highp float;\n"
        "attribute vec3 position;\n"
        "attribute vec2 uv;\n"
        "varying vec2 vUV;\n"
        "void main(void) { vUV = uv; gl_Position = vec4(position, 1.0); }\n";
    const std::string fragmentShader =
        "precision highp float;\n"
        "uniform sampler2D inputSampler;\n"
        "uniform sampler2D otherSampler;\n"
        "varying vec2 vUV;\n"
        "void main(void) { gl_FragColor = mix(texture2D(inputSampler, vUV), texture2D(otherSampler, vUV), 0.5); }\n";
    for (int clearMode = 0; clearMode < 4; ++clearMode)
    {
        SCOPED_TRACE(::testing::Message() << "clearMode=" << clearMode);
        const std::string setupScript = R"(
            var first = BABYLON.RawTexture.CreateRGBATexture(new Uint8Array([32, 64, 96, 255]), 1, 1, scene, false, false, 1);
            var second = BABYLON.RawTexture.CreateRGBATexture(new Uint8Array([128, 160, 192, 255]), 1, 1, scene, false, false, 1);
            material.setTexture("inputSampler", first);
            material.setTexture("otherSampler", second);
            material.onBindObservable.add(function () {
                var clearMode = )" + std::to_string(clearMode) + R"(;
                if (clearMode === 3) engine.enableScissor(0, 0, 1, height);
                engine.clear(new BABYLON.Color4(0, 0, 0, 1), (clearMode & 1) !== 0, (clearMode & 2) !== 0, false);
                if (clearMode === 3) engine.disableScissor();
            });
        )";
        const auto pixels = RenderFullScreenQuad(2, 2, vertexShader, fragmentShader, false, setupScript);
        ASSERT_EQ(pixels.size(), 16u);
        for (size_t offset = 0; offset < pixels.size(); offset += 4)
        {
            EXPECT_NEAR(pixels[offset], 80, 1);
            EXPECT_NEAR(pixels[offset + 1], 112, 1);
            EXPECT_NEAR(pixels[offset + 2], 144, 1);
            EXPECT_EQ(pixels[offset + 3], 255);
        }
    }
#endif
}

TEST(NativeEngineClear, ProceduralTextureRetainsBothInputs)
{
#if defined(SKIP_EXTERNAL_TEXTURE_TESTS) || defined(SKIP_RENDER_TESTS)
    GTEST_SKIP();
#else
    const std::string vertexShader =
        "precision highp float;\n"
        "attribute vec3 position;\n"
        "attribute vec2 uv;\n"
        "varying vec2 vUV;\n"
        "void main(void) { vUV = uv; gl_Position = vec4(position, 1.0); }\n";
    const std::string fragmentShader =
        "precision highp float;\n"
        "uniform sampler2D inputSampler;\n"
        "varying vec2 vUV;\n"
        "void main(void) { gl_FragColor = texture2D(inputSampler, vUV); }\n";
    const std::string setupScript = R"(
        var first = BABYLON.RawTexture.CreateRGBATexture(new Uint8Array([32, 64, 96, 255]), 1, 1, scene, false, false, 1);
        var second = BABYLON.RawTexture.CreateRGBATexture(new Uint8Array([128, 160, 192, 255]), 1, 1, scene, false, false, 1);
        var procedural = new BABYLON.ProceduralTexture("sampled", 2, {
            fragmentSource: "precision highp float; varying vec2 vUV; uniform sampler2D first; uniform sampler2D second;" +
                "void main(void) { gl_FragColor = mix(texture2D(first, vUV), texture2D(second, vUV), 0.5); }"
        }, scene, null, false);
        procedural.setTexture("first", first);
        procedural.setTexture("second", second);
        material.setTexture("inputSampler", procedural);
    )";
    const auto pixels = RenderFullScreenQuad(2, 2, vertexShader, fragmentShader, false, setupScript);
    ASSERT_EQ(pixels.size(), 16u);
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        EXPECT_NEAR(pixels[offset], 80, 1);
        EXPECT_NEAR(pixels[offset + 1], 112, 1);
        EXPECT_NEAR(pixels[offset + 2], 144, 1);
        EXPECT_EQ(pixels[offset + 3], 255);
    }
#endif
}

TEST(NativeEngineTextureSampling, NoMipSamplingPreservesFiltersAndModeChanges)
{
#if defined(SKIP_EXTERNAL_TEXTURE_TESTS) || defined(SKIP_RENDER_TESTS)
    GTEST_SKIP();
#else
    const std::string vertexShader =
        "precision highp float;\n"
        "attribute vec3 position;\n"
        "attribute vec2 uv;\n"
        "varying vec2 vUV;\n"
        "void main(void) { vUV = uv; gl_Position = vec4(position, 1.0); }\n";
    struct SamplingCase
    {
        int Mode;
        int Anisotropy;
        int Red;
        int Green;
        int MagnifiedRed;
    };
    const SamplingCase cases[] = {
        {1, 1, 0, 0, 255},   // Nearest, no mips.
        {2, 1, 128, 0, 191}, // Linear, no mips.
        {7, 1, 128, 0, 255}, // Nearest mag, linear min, no mips.
        {12, 1, 0, 0, 191},  // Linear mag, nearest min, no mips.
        {2, 4, 255, 0, 255}, // Anisotropic, constant-color base mip.
        {3, 1, 0, 255, 191}, // Linear mip filtering restored.
        {4, 1, 0, 255, 255}, // Point mip filtering restored.
    };
    for (const auto& sample : cases)
    {
        SCOPED_TRACE(::testing::Message() << "mode=" << sample.Mode << ", anisotropy=" << sample.Anisotropy);
        const std::string setupScript = R"(
            var requestedMode = )" + std::to_string(sample.Mode) + R"(;
            var anisotropy = )" + std::to_string(sample.Anisotropy) + R"(;
            // Distinct lower mips separate mip selection from spatial filtering.
            var sampled;
            for (var mip = 0, size = 8; size >= 1; mip++, size /= 2) {
                var pixels = new Uint8Array(size * size * 4);
                var offset = 0;
                for (var y = 0; y < size; y++) {
                    for (var x = 0; x < size; x++) {
                        pixels[offset++] = mip === 0 && (anisotropy > 1 || x % 2 === 0) ? 255 : 0;
                        pixels[offset++] = mip === 0 ? 0 : 255;
                        pixels[offset++] = 0;
                        pixels[offset++] = 255;
                    }
                }
                if (mip === 0) {
                    sampled = BABYLON.RawTexture.CreateRGBATexture(pixels, size, size, scene, true, false, 1);
                } else {
                    engine.updateTextureData(sampled.getInternalTexture(), pixels, 0, 0, size, size, 0, mip);
                }
            }
            sampled.anisotropicFilteringLevel = anisotropy;
            sampled.updateSamplingMode(4);
            sampled.updateSamplingMode(requestedMode);
            material.setTexture("inputSampler", sampled);
        )";
        for (const bool magnify : {false, true})
        {
            SCOPED_TRACE(::testing::Message() << "magnify=" << magnify);
            const std::string fragmentShader =
                "precision highp float;\n"
                "uniform sampler2D inputSampler;\n"
                "varying vec2 vUV;\n"
                "void main(void) { gl_FragColor = texture2D(inputSampler, vUV * " +
                std::string{magnify ? "0.125" : "0.5"} + " + 0.25); }\n";
            const auto pixels = RenderFullScreenQuad(2, 2, vertexShader, fragmentShader, true, setupScript);
            ASSERT_EQ(pixels.size(), 16u);
            for (size_t offset = 0; offset < pixels.size(); offset += 4)
            {
                EXPECT_NEAR(pixels[offset], magnify ? sample.MagnifiedRed : sample.Red, 1);
                EXPECT_NEAR(pixels[offset + 1], magnify ? 0 : sample.Green, 1);
                EXPECT_EQ(pixels[offset + 2], 0);
                EXPECT_EQ(pixels[offset + 3], 255);
            }
        }
    }
#endif
}

TEST(NativeEngineInstanceData, QueuedDrawRetainsDataBeforeUpdate)
{
#if defined(SKIP_EXTERNAL_TEXTURE_TESTS) || defined(SKIP_RENDER_TESTS)
    GTEST_SKIP();
#else
    const std::string vertexShader =
        "precision highp float;\n"
        "attribute vec3 position;\n"
        "#include<instancesDeclaration>\n"
        "varying float vValue;\n"
        "void main(void) {\n"
        "#include<instancesVertex>\n"
        "vValue = finalWorld[3].x; gl_Position = vec4(position, 1.0); }\n";
    const std::string fragmentShader =
        "precision highp float;\n"
        "varying float vValue;\n"
        "void main(void) { gl_FragColor = vec4(vValue, 0.0, 0.0, 1.0); }\n";
    const std::string setupScript = R"(
        material.options.uniforms.push("world");
        var matrices = new Float32Array(BABYLON.Matrix.Translation(0.25, 0, 0).m);
        quad.thinInstanceSetBuffer("matrix", matrices, 16, false);
        globalThis.__prepare = function () {
            return material.forceCompilationAsync(quad, { useInstances: true });
        };
        quad.onAfterRenderObservable.add(function () {
            matrices[12] = 0.75;
            quad.thinInstanceBufferUpdated("matrix");
        });
    )";
    const auto pixels = RenderFullScreenQuad(2, 1, vertexShader, fragmentShader, false, setupScript);
    ASSERT_EQ(pixels.size(), 8u);
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        EXPECT_NEAR(pixels[offset], 64, 1);
        EXPECT_EQ(pixels[offset + 1], 0);
        EXPECT_EQ(pixels[offset + 2], 0);
        EXPECT_EQ(pixels[offset + 3], 255);
    }
#endif
}

// gl_FragCoord.y must follow the GL convention of increasing towards +Y in clip
// space. The quad maps uv.y = 0 to clip y = -1 and uv.y = 1 to clip y = +1, so
// the interpolated vUV.y is a ground-truth ramp running in that same direction
// and normalized gl_FragCoord.y has to agree with it everywhere. Without the
// correction gl_FragCoord.y runs the other way on D3D/Metal/Vulkan and the two
// ramps become mirror images.
//
// The comparison is made between two channels of a single render rather than
// against absolute row indices on purpose: Helpers::ReadPixels is a plain
// glReadPixels on OpenGL, which returns the bottom scanline first, while the
// D3D11 path returns the top scanline first. An absolute check would therefore
// encode the readback convention of one backend rather than the shading
// language rule under test.
TEST(ShaderCompilation, FragCoordYMatchesInterpolatedUV)
{
#if defined(SKIP_EXTERNAL_TEXTURE_TESTS) || defined(SKIP_RENDER_TESTS)
    GTEST_SKIP();
#else
    constexpr uint32_t WIDTH = 8;
    constexpr uint32_t HEIGHT = 64;

    const std::string vertexShader =
        "precision highp float;\n"
        "attribute vec3 position;\n"
        "attribute vec2 uv;\n"
        "varying vec2 vUV;\n"
        "void main(void) { vUV = uv; gl_Position = vec4(position, 1.0); }\n";

    const std::string fragmentShader =
        "precision highp float;\n"
        "uniform vec2 targetSize;\n"
        "varying vec2 vUV;\n"
        "void main(void) {\n"
        "    gl_FragColor = vec4(gl_FragCoord.y / targetSize.y, vUV.y, 0.0, 1.0);\n"
        "}\n";

    auto pixels = RenderFullScreenQuad(WIDTH, HEIGHT, vertexShader, fragmentShader, false);
    ASSERT_EQ(pixels.size(), static_cast<size_t>(WIDTH) * HEIGHT * 4);

    const auto texel = [&pixels](uint32_t row) {
        const size_t offset = static_cast<size_t>(row) * WIDTH * 4;
        return std::make_pair(static_cast<int>(pixels[offset]), static_cast<int>(pixels[offset + 1]));
    };

    const auto first = texel(0);
    const auto middle = texel(HEIGHT / 2);
    const auto last = texel(HEIGHT - 1);
    std::cout << "row 0 fragCoord=" << first.first << " uv=" << first.second
              << ", row " << (HEIGHT / 2) << " fragCoord=" << middle.first << " uv=" << middle.second
              << ", row " << (HEIGHT - 1) << " fragCoord=" << last.first << " uv=" << last.second
              << std::endl;

    // Guard against the whole comparison passing vacuously: the reference ramp
    // has to actually sweep the range rather than sitting at a constant.
    ASSERT_GT(std::abs(first.second - last.second), 200)
        << "vUV.y reference ramp did not vary across the target";

    // Both channels are produced by the same fragment invocation, so they must
    // agree row by row no matter which end of the image the readback starts at.
    // The tolerance absorbs interpolation and 8-bit quantization only; a flipped
    // gl_FragCoord.y misses by the full range of the ramp.
    for (uint32_t row = 0; row < HEIGHT; ++row)
    {
        const auto values = texel(row);
        ASSERT_LE(std::abs(values.first - values.second), 6)
            << "gl_FragCoord.y disagrees with the interpolated vUV.y at row " << row
            << " (gl_FragCoord=" << values.first << ", vUV=" << values.second << ")";
    }
#endif
}

// Indexing a screen-sized texture with gl_FragCoord must give the same image as
// indexing it with the interpolated UVs of a full-screen quad. This is the
// pattern used by order-independent transparency, TAA and screen space
// curvature, and it only holds if the gl_FragCoord correction and
// FlipSamplerCoordinatesTraverser compose to a no-op.
//
// Comparing the two addressing modes against each other rather than against the
// source pixels keeps the test independent of how createRawTexture lays its data
// out in memory.
TEST(ShaderCompilation, FragCoordAndUVAddressATextureIdentically)
{
#if defined(SKIP_EXTERNAL_TEXTURE_TESTS) || defined(SKIP_RENDER_TESTS)
    GTEST_SKIP();
#else
    constexpr uint32_t WIDTH = 8;
    constexpr uint32_t HEIGHT = 64;

    const std::string vertexShader =
        "precision highp float;\n"
        "attribute vec3 position;\n"
        "attribute vec2 uv;\n"
        "varying vec2 vUV;\n"
        "void main(void) {\n"
        "    vUV = uv;\n"
        "    gl_Position = vec4(position, 1.0);\n"
        "}\n";

    const std::string uvShader =
        "precision highp float;\n"
        "varying vec2 vUV;\n"
        "uniform vec2 targetSize;\n"
        "uniform sampler2D inputSampler;\n"
        "void main(void) {\n"
        "    gl_FragColor = texture2D(inputSampler, vUV);\n"
        "}\n";

    const std::string fragCoordShader =
        "precision highp float;\n"
        "varying vec2 vUV;\n"
        "uniform vec2 targetSize;\n"
        "uniform sampler2D inputSampler;\n"
        "void main(void) {\n"
        "    gl_FragColor = texture2D(inputSampler, gl_FragCoord.xy / targetSize);\n"
        "}\n";

    auto uvPixels = RenderFullScreenQuad(WIDTH, HEIGHT, vertexShader, uvShader, true);
    auto fragCoordPixels = RenderFullScreenQuad(WIDTH, HEIGHT, vertexShader, fragCoordShader, true);

    ASSERT_EQ(uvPixels.size(), static_cast<size_t>(WIDTH) * HEIGHT * 4);
    ASSERT_EQ(fragCoordPixels.size(), uvPixels.size());

    const auto red = [](const std::vector<uint8_t>& pixels, uint32_t row) {
        return static_cast<int>(pixels[static_cast<size_t>(row) * WIDTH * 4]);
    };

    // Guard against a vacuous pass: the source must actually vary down the image,
    // otherwise a vertical mirror would be undetectable.
    ASSERT_GT(std::abs(red(uvPixels, 0) - red(uvPixels, HEIGHT - 1)), 200)
        << "the source texture must vary from top to bottom for this test to mean anything";

    std::cout << "uv       rows: " << red(uvPixels, 0) << " .. " << red(uvPixels, HEIGHT - 1) << std::endl;
    std::cout << "fragCoord rows: " << red(fragCoordPixels, 0) << " .. " << red(fragCoordPixels, HEIGHT - 1) << std::endl;

    for (uint32_t row = 0; row < HEIGHT; ++row)
    {
        ASSERT_EQ(red(fragCoordPixels, row), red(uvPixels, row))
            << "row " << row << " differs between gl_FragCoord and uv addressing";
    }
#endif
}

TEST(ShaderCompilation, PbrRoughnessSquareInNestedLoops)
{
#if defined(SKIP_EXTERNAL_TEXTURE_TESTS) || defined(SKIP_RENDER_TESTS)
    GTEST_SKIP();
#else
    const std::string vertexShader =
        "precision highp float;\n"
        "attribute vec3 position;\n"
        "void main(void) { gl_Position = vec4(position, 1.0); }\n";

    // Use the production include: FXC can replace roughness squared with
    // roughness when the saturated value is used inside nested dynamic loops.
    const std::string fragmentShader =
        "precision highp float;\n"
        "uniform vec2 targetSize;\n"
        "#include<helperFunctions>\n"
        "#include<pbrHelperFunctions>\n"
        "void main(void) {\n"
        "    vec3 result = vec3(0.0);\n"
        "    for (int i = 0; i < int(targetSize.x); ++i) {\n"
        "        for (int j = 0; j < int(targetSize.y); ++j) {\n"
        "            float roughness = clamp(1.0 / targetSize.x, 0.0, 1.0);\n"
        "            result = vec3(roughness, convertRoughnessToAverageSlope(roughness), sqrt(roughness));\n"
        "        }\n"
        "    }\n"
        "    gl_FragColor = vec4(result, 1.0);\n"
        "}\n";

    auto pixels = RenderFullScreenQuad(2, 1, vertexShader, fragmentShader, false);
    ASSERT_EQ(pixels.size(), 8u);
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        EXPECT_NEAR(pixels[offset], 128, 1);
        EXPECT_NEAR(pixels[offset + 1], 64, 1) << "roughness 0.5 must produce alphaG 0.2505, not 0.5005";
        EXPECT_NEAR(pixels[offset + 2], 180, 1);
        EXPECT_EQ(pixels[offset + 3], 255);
    }
#endif
}

TEST(ShaderCompilation, SaturatedArithmeticInNestedLoops)
{
#if defined(SKIP_EXTERNAL_TEXTURE_TESTS) || defined(SKIP_RENDER_TESTS)
    GTEST_SKIP();
#else
    const std::string vertexShader =
        "precision highp float;\n"
        "attribute vec3 position;\n"
        "void main(void) { gl_Position = vec4(position, 1.0); }\n";
    const std::string arithmetic =
        "float value = clamp(1.0 / targetSize.x, 0.0, 1.0);\n"
        "float squared = value * value;\n"
        "float fifth = squared * squared * value;\n"
        "float attenuation = clamp(1.0 - value, 0.0, 1.0);\n"
        "attenuation *= attenuation;\n"
        "result = vec3(squared, fifth, attenuation);\n";
    const std::vector<std::pair<std::string, std::string>> loops{
        {"for (int i = 0; i < int(targetSize.x); ++i) { for (int j = 0; j < int(targetSize.y); ++j) {\n", "}}\n"},
        {"int i = 0; while (i++ < int(targetSize.x)) { int j = 0; while (j++ < int(targetSize.y)) {\n", "}}\n"},
        {"int i = 0; do { int j = 0; do {\n", "} while (++j < int(targetSize.y)); } while (++i < int(targetSize.x));\n"},
    };
    for (const auto& loop : loops)
    {
        SCOPED_TRACE(loop.first);
        const std::string fragmentShader =
            "precision highp float;\n"
            "uniform vec2 targetSize;\n"
            "void main(void) {\n"
            "vec3 result = vec3(0.0);\n" +
            loop.first + arithmetic + loop.second +
            "gl_FragColor = vec4(result, 1.0);\n"
            "}\n";
        auto pixels = RenderFullScreenQuad(2, 1, vertexShader, fragmentShader, false);
        ASSERT_EQ(pixels.size(), 8u);
        for (size_t offset = 0; offset < pixels.size(); offset += 4)
        {
            EXPECT_NEAR(pixels[offset], 64, 1);
            EXPECT_NEAR(pixels[offset + 1], 8, 1);
            EXPECT_NEAR(pixels[offset + 2], 64, 1);
            EXPECT_EQ(pixels[offset + 3], 255);
        }
    }
#endif
}
