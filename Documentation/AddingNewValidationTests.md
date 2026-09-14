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

The runner loads bundled, licensed fonts from `Apps/Dependencies`, avoiding a network dependency during font initialization. Arimo supplies Arial-compatible metrics for the `Arial` family; Droid Sans remains the fallback and supplies the historical `droidsans`/`monospace` aliases. The latter preserves existing Native fixtures rather than providing a true monospaced face. Each font includes its license and immutable source provenance. Native's SDF glyph rasterization still differs from browser text rasterization even with matching layout metrics.

When migrating an animated reference to a prewarmed fixture, include the captured frame in the simulation-step budget. The Havok multi-region reference represents 180 physics steps: 179 prewarm steps plus the first rendered frame, not 180 prewarm steps plus another step during rendering.

For tests shared with Babylon.js, synchronize the canonical reference from `packages\tools\tests\test\visualization\ReferenceImages` and its configuration together, including the Playground revision, capture count, and canvas background. Do not regenerate a shared reference from Native to conceal a rendering difference.

# Generate Reference Images

Your test list is updated and your playground is ready to test. it's now time to generate a reference image.
open `Apps\Playground\Scripts\validation_native.js` and change `var generateReferences = false;` to true.
Run ValidationTest program, all reference images will be generated in `Apps/Playground/Results` subfolder of your build directory.
Copy the reference image for your test from that folder to `Apps/Playground/ReferenceImages` and add it with Git.

# Test new reference

Revert your change to `Apps\ValidationTests\Scripts\validation_native.js` and run validation tests.
Your new reference will compared to the rendering of your newly added Playground.