#pragma once

#include <napi/env.h>
#include <Babylon/Api.h>

namespace Babylon::Polyfills::RenderTargetTexture
{
    // Patches BABYLON.NativeEngine.prototype.createRenderTargetTexture to
    // normalize a known-bad `options.type` value from upstream Babylon.js code.
    //
    // Upstream Babylon.js (`packages/dev/core/src/Misc/areaLightsTextureTools.ts`,
    // `AreaLightTextureTools._scaleImageDownAsync`) passes
    // `Constants.TEXTURE_2D` (= GL_TEXTURE_2D = 3553, a *texture target*) as
    // the `type` field of `RenderTargetCreationOptions`. That field expects a
    // `Constants.TEXTURETYPE_*` value (default `UNSIGNED_BYTE`). The WebGL2
    // engine silently tolerates the mistake; the native engine's strict
    // (format, type) -> bgfx format mapping in `nativeHelpers.ts` correctly
    // rejects it with:
    //
    //   RuntimeError: Unsupported texture format or type: format 5, type 3553.
    //
    // The result is that every Babylon Native consumer that ends up calling
    // `AreaLightTextureTools.processAsync` (e.g. any scene using
    // `RectAreaLight.emissionTexture`) throws before the texture is ever
    // produced. Until upstream Babylon.js corrects the call site, this
    // polyfill bridges the gap by normalizing `options.type === TEXTURE_2D`
    // back to `TEXTURETYPE_UNSIGNED_BYTE` inside the NativeEngine's
    // createRenderTargetTexture entry point. All other type values are
    // passed through unchanged.
    //
    // Call Initialize AFTER babylon.max.js has been evaluated; the patch
    // requires BABYLON.NativeEngine.prototype to exist. When using the
    // jsruntimehost ScriptLoader, the simplest pattern is:
    //
    //   scriptLoader.LoadScript("app:///Scripts/babylon.max.js");
    //   scriptLoader.Dispatch([](Napi::Env env) {
    //       Babylon::Polyfills::RenderTargetTexture::Initialize(env);
    //   });
    //
    // The Dispatch queues onto the same ordered task chain as LoadScript, so
    // Initialize is guaranteed to run after babylon.max.js finishes
    // evaluating.
    //
    // Safe to call multiple times; the patch is idempotent.
    void BABYLON_API Initialize(Napi::Env env);
}
