# Validation Tests
Validation tests load playground, perform rendering and compare the results with a reference Image.
The reference is part of the repo so adding new tests is updating the tests list and commit a new reference image.

# Playground

Most of the tests are [playground made on the web](https://playground.babylonjs.com/)
Once it's done, you can save it, get a snippet Id and add it to the test lists.

# Tests lists

In order to add a new test scene, first thing to do is to add a few lines in `Apps\Playground\Scripts\config.json`.

Keep `config.json` human-readable with two-space indentation and a final newline. Programmatic updates must preserve this layout; use `JSON.stringify(config, null, 2) + "\n"` or its equivalent instead of compact serialization.

```json
{
  "root": "https://cdn.babylonjs.com",
  "tests": [
    {
      "title": "setParent",
      "playgroundId": "#JD49CT#2",
      "referenceImage": "setParent.png"
    }
  ]
}
```

`title` : a string used for Window title and logging results in the console
`playgroundId` : the snippet id of the playground you want to test
`referenceImage` : the reference image name you want to compare to. You don't have a reference yet, so choose a self-explanatory name with .png extension.

`threshold` : optional per-channel RGB difference cutoff (default `25`). A pixel differs if any RGB channel's absolute difference reaches this cutoff; alpha is ignored.

`errorRatio` : optional allowed percentage of differing pixels (default `2.5`). The comparison fails only when the measured percentage exceeds this allowance. Use a positive value: `0` falls back to the default rather than requiring an exact match. Historical measurements in `note` annotations are not the current allowance.

For suite-wide tolerance tightening, use the worst measurement from repeated complete sweeps of the same renderer, fixtures, and references. Start with 10% relative headroom plus 0.02 percentage points, round upward to 0.05-percentage-point increments, and use a 0.05% minimum for new budgets. Remove an above-default exception when every measured run meets 2.5%, even if that cap reduces the proposed headroom. Preserve any already-stricter allowance; this policy must never raise an existing gate. Run the complete suite again under the proposed gates before publishing. Investigate intermittent missing resources or geometry rather than absorbing them into a larger budget. Keep the RGB cutoff, references, and capability exclusions unchanged during a tolerance-only pass.

`canvasBackgroundColor` : optional CSS color used behind transparent screenshot pixels, matching the Babylon.js visualization harness. The default is `greenyellow`; for example, the FrameGraph OIT geometry-renderer fixture uses `"white"`. Native parses the color with `Canvas.parseColor` and composites translucent backgrounds over the browser's white page.

`useLargeWorldRendering` : optional engine-creation flag enabling floating origin for all scenes, including utility layers, and high-precision CPU matrices. `useHighPrecisionMatrix` enables only the matrix precision setting. The runner selects matrix precision before creating its first engine and recreates the engine when a test changes large-world mode. Because matrix precision is global, a mixed run containing either flag keeps high-precision matrices throughout that run; floating origin remains scoped to each test.

The runner restores its deterministic `Math.random` implementation and resets its seed before each test. A snippet may install its own random generator, but that generator must not leak into subsequent tests in a mixed run.

Stable repeated captures on one backend do not establish cross-GPU noise reproducibility. Trigonometric GPU hashes such as `fract(cos(dot(...)) * 43758.5453)` amplify small backend math differences into different procedural patterns, even with identical inputs. Scene 255's allowance records this reference-portability problem, not a sky-position error. Before assigning another residual to the same cause, verify an active hash call and use controlled captures or a diagnostic replacement of the hash; a shared, unused `getRand` declaration is not evidence. Distinguish GPU hashing from seeded CPU randomness, capture timing, and CPU/GPU algorithm substitutions. Keep fixtures, references, and numerical gates unchanged during that investigation.

Validation disables back-buffer MSAA with `TestUtils.setMSAASamples(0)`, matching the browser harness's `antialias: false`. This does not change the embedding runtime's default or a scene's explicitly multisampled render targets. Custom diagnostic scripts can select 0/1 (disabled), 2, 4, 8, or 16 samples with the same API.

FrameGraph retains requested MSAA for graphs that only use depth as an attachment. On Native, graphs with explicit depth-texture dependencies still use single-sample targets because multisampled depth cannot be resolved for ordinary shader sampling. Disabling MSAA for every graph unnecessarily changes bounding-box and post-process coverage.

Cascaded shadows retain the requested filter on Native. `FILTER_NONE` uses the color cascade array with nearest sampling; it must not be silently replaced with PCF because of FrameGraph's single-sample depth restriction. Verify both hard-shadow silhouettes and explicitly filtered PCF/PCSS controls before tightening a shadow-scene allowance.

Native's 3D texture sampling uses the same shader-visible row convention as WebGL, for both raw uploads and rendered volumes. The compiler normalizes sample/fetch Y coordinates without changing the depth slice, and raw uploads normalize each XY slice without modifying caller data. Voxelization, grid combination, and mip generation must therefore use their ordinary shared shaders, not Native-specific axis offsets or reflections.

Native supports GPU irradiance prefiltering, including CDF importance sampling and the dominant-light direction used by OpenPBR. CDF lookups use explicit, clamped `floor(uv * textureSize)` texel bins: normalized nearest sampling can choose the adjacent logical row at exact boundaries after a backend's texture-origin reflection. Use the shared CDF lookup helper, not fractional offsets, material-color adjustments, or a CPU irradiance substitute.

Decoded 16-bit PNG images pass through half-float precision before RGBA8 upload, matching the browser image-decoding path. Direct UNORM16-to-8 rounding and full-precision floating-point mip chains both produce different normal vectors. The shared conversion covers NativeEngine texture loads and Canvas images, including grayscale and alpha, without changing CPU mip filtering. Raw textures, HDR/EXR floating-point data, and authored DDS/KTX formats retain their existing precision. Do not compensate for PNG decoding differences with material adjustments or replacement references.

RGB and grayscale PNGs can declare a transparent color key in a `tRNS` chunk without storing a per-pixel alpha channel. The decoder expands that key into alpha before upload, comparing the original samples before any 16-bit precision reduction. Packed grayscale keys also apply across odd-width rows. NativeEngine and Canvas image loads replicate grayscale into RGB and preserve its alpha; opaque grayscale uses alpha 255, not 1. Include GPU readback coverage, since a decoder-only check cannot detect alpha lost during image conversion.

Native expands line loops and triangle fans into equivalent indexed lists, including unindexed and instanced draws. Expansion honors draw ranges, fixed-index restart markers, and dynamic index updates. Babylon.js enables these draws only when the runtime advertises `supportsPrimitiveModeExpansion`; older runtimes retain the unsupported-mode warning. Primitive-mode scenes must include every shape: a generous whole-image tolerance must not substitute for topology support.

`Scene.isReady()` does not include asynchronous `GUI.Image` loads. The Native runner also checks `AdvancedDynamicTexture.guiIsReady()` during its bounded convergence warmup, and fails explicitly if the scene never converges. Tests should still use image load observables when scene logic depends on decoded dimensions; do not add unconditional sleeps.

Utility layers keep their textures and materials in virtual scenes, separate from the main scene's readiness. The runner includes virtual scenes whose active camera belongs to the scene being captured, checking their GUI controls and material convergence as well. It advances render IDs while waiting, not animation frames. The GUI Slate fixture uses the canonical prewarmed snippet; extra capture frames or a loose image tolerance must not conceal an unloaded title bar.

Material convergence is checked in the active camera's render pass, restoring the previous pass afterward. Inspecting an unused pass's cached defines can falsely veto a ready scene indefinitely.

Pixel-backed offscreen canvases, ImageBitmap copies, and `putImageData` use temporary NanoVG images. These images must remain alive until `nvgEndFrame` submits the deferred drawing commands, with pending resources also released on context disposal. A correct `getImageData` CPU mirror does not prove the GPU image was drawn; regressions must read back the dynamic texture. Cropped `drawImage` calls must map the selected source rectangle onto the destination rather than scale the entire source into it.

Rectangular Canvas clips can have negative widths or heights: normalize their origin and extents before intersecting NanoVG's scissor. Clamping negative dimensions to zero hides valid GUI content, including scene 401's top-right label. Cover both signed axes, nested parent clips, and rotated transforms with GPU comparisons against equivalent positive rectangles.

Canvas render targets use premultiplied alpha internally. NativeEngine passes the dynamic texture's `premulAlpha` argument to `getCanvasTexture`: `false` produces a separate straight-alpha GPU copy, while `true` (or an omitted argument from an older engine) preserves the premultiplied representation. Without mipmaps, premultiplied uploads use the original source; with mipmaps, they use a separate premultiplied copy. Conversion preserves texel row order, zero-alpha pixels, and Canvas drawing contents, and must be ordered before the consuming texture blit and scene draw. Cached outputs remain separate for each alpha/mipmap representation so queued commands retain valid sources. GPU regression coverage must include both alpha representations, queued mixed-mode copies, updates, clears, and resizes; opaque-only images cannot expose double premultiplication.

Gradient stops interpolate straight RGB and alpha independently, matching Canvas2D. Premultiplying the stops before interpolation accidentally compensated for the old upload bug; it must not remain after fixing uploads. Requested dynamic-texture mipmaps are generated from the converted alpha representation and copied at every level when the runtime advertises `supportsDynamicTextureMipMaps`. Older runtimes and backends without RGBA8 automatic mip generation retain the previous non-mipmapped behavior.

The runner loads bundled, licensed fonts from `Apps/Dependencies`, avoiding a network dependency during font initialization. Arimo supplies Arial-compatible metrics for the `Arial` family; Droid Sans remains the fallback and supplies the historical `droidsans`/`monospace` aliases. The latter preserves existing Native fixtures rather than providing a true monospaced face. Each font includes its license and immutable source provenance. Native's SDF glyph rasterization still differs from browser text rasterization even with matching layout metrics.

When migrating an animated reference to a prewarmed fixture, include the captured frame in the simulation-step budget. The Havok multi-region reference represents 180 physics steps: 179 prewarm steps plus the first rendered frame, not 180 prewarm steps plus another step during rendering.

For tests shared with Babylon.js, synchronize the canonical reference from `packages\tools\tests\test\visualization\ReferenceImages` and its configuration together, including the Playground revision, capture count, and canvas background. Do not regenerate a shared reference from Native to conceal a rendering difference.

Particle fixtures often prewarm before the first captured frame. Follow the canonical capture count even when the Playground revision is unchanged: extra live frames can move an already-prewarmed effect away from its reference. Prewarming each CPU/NPE system is not equivalent to interleaving live frames across systems, since they consume the seeded random sequence in a different order. Synchronize the fixture rather than adjusting particle geometry, random seeds, or reference pixels.

Browser default loading screens are HTML/CSS overlays and are not part of Native's GPU framebuffer capture. Keep such browser-only visualization tests excluded from Native with an explicit reason. Native applications can still provide an `ILoadingScreen` implementation appropriate for their host UI.

Native's GLSL compute support does not imply WGSL or storage-texture support. The excluded WGSL graphics/storage-texture fixtures exercise unsupported backend capabilities, not image-tolerance problems.

`ComputeShader.dispatchWhenReady()` rejects compilation failures, dispatch exceptions, and readiness timeouts. A successfully dispatched previous pipeline still resolves after a failed recompile. Handle the returned promise; an explicit compilation failure is not a readiness hang.

# Generate Reference Images

Your test list is updated and your playground is ready to test. it's now time to generate a reference image.
open `Apps\Playground\Scripts\validation_native.js` and change `var generateReferences = false;` to true.
Run ValidationTest program, all reference images will be generated in `Apps/Playground/Results` subfolder of your build directory.
Copy the reference image for your test from that folder to `Apps/Playground/ReferenceImages` and add it with Git.

# Test new reference

Revert your change to `Apps\ValidationTests\Scripts\validation_native.js` and run validation tests.
Your new reference will compared to the rendering of your newly added Playground.