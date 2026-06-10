#include <Babylon/Polyfills/RenderTargetTexture.h>

#include <napi/env.h>

namespace Babylon::Polyfills::RenderTargetTexture
{
    namespace
    {
        // Defends the native engine against upstream Babylon.js callers that pass
        // a non-TEXTURETYPE_* value in the `type` slot of
        // `RenderTargetCreationOptions` (e.g. AreaLightTextureTools._scaleImageDownAsync
        // passes `Constants.TEXTURE_2D` = GL_TEXTURE_2D = 3553, a texture *target*).
        //
        // The native engine's getNativeTextureFormat() strictly switches on
        // (format, type) and throws on unknown pairs:
        //
        //     RuntimeError: Unsupported texture format or type:
        //     format 5, type 3553.
        //
        // WebGL2 doesn't fail in the same way: `_getWebGLTextureType` and
        // `_getRGBABufferInternalSizedFormat` in thinEngine.pure.ts both fall
        // through to UNSIGNED_BYTE / RGBA8 for any type value that isn't a
        // known TEXTURETYPE_* constant. This polyfill mirrors that fall-through
        // for the native engine: at install time it collects every
        // BABYLON.Constants.TEXTURETYPE_* value into a Set; at call time it
        // rewrites any out-of-set `options.type` to TEXTURETYPE_UNSIGNED_BYTE
        // before the native engine sees it.
        constexpr const char* JS_SOURCE = R"javascript(
(function () {
    "use strict";

    if (typeof BABYLON === "undefined") {
        return;
    }
    if (!BABYLON.NativeEngine || !BABYLON.NativeEngine.prototype) {
        return;
    }
    if (!BABYLON.Constants) {
        return;
    }

    var proto = BABYLON.NativeEngine.prototype;
    if (proto.__renderTargetTexturePolyfillInstalled) {
        return;
    }
    proto.__renderTargetTexturePolyfillInstalled = true;

    var original = proto.createRenderTargetTexture;
    if (typeof original !== "function") {
        return;
    }

    var TEXTURETYPE_UNSIGNED_BYTE = BABYLON.Constants.TEXTURETYPE_UNSIGNED_BYTE;

    // Enumerate every TEXTURETYPE_* constant once at install time. Any
    // numeric value of options.type that isn't one of these is treated as
    // "unknown" and rewritten to UNSIGNED_BYTE - same fall-through behavior
    // as WebGL2's _getWebGLTextureType().
    var validTypes = {};
    for (var key in BABYLON.Constants) {
        if (key.indexOf("TEXTURETYPE_") === 0) {
            var value = BABYLON.Constants[key];
            if (typeof value === "number") {
                validTypes[value] = true;
            }
        }
    }

    proto.createRenderTargetTexture = function (size, options) {
        if (options
            && options.type !== undefined
            && options.type !== null
            && typeof options.type === "number"
            && !validTypes[options.type]) {
            // Shallow clone so the caller's options object is not mutated.
            var fixed = {};
            for (var k in options) {
                if (Object.prototype.hasOwnProperty.call(options, k)) {
                    fixed[k] = options[k];
                }
            }
            fixed.type = TEXTURETYPE_UNSIGNED_BYTE;
            return original.call(this, size, fixed);
        }
        return original.call(this, size, options);
    };

    if (typeof console !== "undefined" && console.log) {
        console.log("[RenderTargetTexture polyfill] NativeEngine.createRenderTargetTexture patched: unknown options.type values normalized to TEXTURETYPE_UNSIGNED_BYTE (matching WebGL2 fall-through).");
    }
})();
)javascript";

        constexpr const char* JS_SOURCE_URL = "babylon-native://polyfills/RenderTargetTexture.js";
    }

    void Initialize(Napi::Env env)
    {
        // The free function Napi::Eval(env, source, url) is declared by every
        // engine-specific <napi/env.h> across both N-API trees (Chakra, V8,
        // JavaScriptCore in Core/Node-API/Include/Engine/<X>/, and JSI in
        // Core/Node-API-JSI/Include/napi/). Using it uniformly avoids the
        // Shared-only env.RunScript() which is missing from the JSI tree.
        Napi::HandleScope scope{env};
        Napi::Eval(env, JS_SOURCE, JS_SOURCE_URL);
    }
}
