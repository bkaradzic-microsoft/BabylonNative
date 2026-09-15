# Validation Tests
Validation tests load playground, perform rendering and compare the results with a reference Image.
The reference is part of the repo so adding new tests is updating the tests list and commit a new reference image.

# Playground

Most of the tests are [playground made on the web](https://playground.babylonjs.com/)
Once it's done, you can save it, get a snippet Id and add it to the test lists.

# Tests lists

In order to add a new test scene, first thing to do is to add a few lines in `Apps\Playground\Scripts\config.json`.

```json
{
    "root": "https://cdn.babylonjs.com",
    "tests": [
        {
            "title": "setParent",
            "playgroundId": "#JD49CT#2",
            "referenceImage": "setParent.png"
        },
        ...
}
```

`title` : a string used for Window title and logging results in the console
`playgroundId` : the snippet id of the playground you want to test
`referenceImage` : the reference image name you want to compare to. You don't have a reference yet, so choose a self-explanatory name with .png extension.

`canvasBackgroundColor` : optional CSS color used behind transparent screenshot pixels, matching the Babylon.js visualization harness. The default is `greenyellow`; for example, the FrameGraph OIT geometry-renderer fixture uses `"white"`. Native parses the color with `Canvas.parseColor` and composites translucent backgrounds over the browser's white page.

`useLargeWorldRendering` : optional engine-creation flag enabling floating origin for all scenes, including utility layers, and high-precision CPU matrices. `useHighPrecisionMatrix` enables only the matrix precision setting. The runner selects matrix precision before creating its first engine and recreates the engine when a test changes large-world mode. Because matrix precision is global, a mixed run containing either flag keeps high-precision matrices throughout that run; floating origin remains scoped to each test.

The runner restores its deterministic `Math.random` implementation and resets its seed before each test. A snippet may install its own random generator, but that generator must not leak into subsequent tests in a mixed run.

Validation disables back-buffer MSAA with `TestUtils.setMSAASamples(0)`, matching the browser harness's `antialias: false`. This does not change the embedding runtime's default or a scene's explicitly multisampled render targets. Custom diagnostic scripts can select 0/1 (disabled), 2, 4, 8, or 16 samples with the same API.

FrameGraph retains requested MSAA for graphs that only use depth as an attachment. On Native, graphs with explicit depth-texture dependencies still use single-sample targets because multisampled depth cannot be resolved for ordinary shader sampling. Disabling MSAA for every graph unnecessarily changes bounding-box and post-process coverage.

Native's 3D texture sampling uses the same shader-visible row convention as WebGL, for both raw uploads and rendered volumes. The compiler normalizes sample/fetch Y coordinates without changing the depth slice, and raw uploads normalize each XY slice without modifying caller data. Voxelization, grid combination, and mip generation must therefore use their ordinary shared shaders, not Native-specific axis offsets or reflections.

Native supports GPU irradiance prefiltering, including CDF importance sampling and the dominant-light direction used by OpenPBR. CDF lookups use explicit, clamped `floor(uv * textureSize)` texel bins: normalized nearest sampling can choose the adjacent logical row at exact boundaries after a backend's texture-origin reflection. Use the shared CDF lookup helper, not fractional offsets, material-color adjustments, or a CPU irradiance substitute.

`Scene.isReady()` does not include asynchronous `GUI.Image` loads. The Native runner also checks `AdvancedDynamicTexture.guiIsReady()` during its bounded convergence warmup, and fails explicitly if the scene never converges. Tests should still use image load observables when scene logic depends on decoded dimensions; do not add unconditional sleeps.

Material convergence is checked in the active camera's render pass, restoring the previous pass afterward. Inspecting an unused pass's cached defines can falsely veto a ready scene indefinitely.

The runner loads bundled, licensed fonts from `Apps/Dependencies`, avoiding a network dependency during font initialization. Arimo supplies Arial-compatible metrics for the `Arial` family; Droid Sans remains the fallback and supplies the historical `droidsans`/`monospace` aliases. The latter preserves existing Native fixtures rather than providing a true monospaced face. Each font includes its license and immutable source provenance. Native's SDF glyph rasterization still differs from browser text rasterization even with matching layout metrics.

When migrating an animated reference to a prewarmed fixture, include the captured frame in the simulation-step budget. The Havok multi-region reference represents 180 physics steps: 179 prewarm steps plus the first rendered frame, not 180 prewarm steps plus another step during rendering.

For tests shared with Babylon.js, synchronize the canonical reference from `packages\tools\tests\test\visualization\ReferenceImages` and its configuration together, including the Playground revision, capture count, and canvas background. Do not regenerate a shared reference from Native to conceal a rendering difference.

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