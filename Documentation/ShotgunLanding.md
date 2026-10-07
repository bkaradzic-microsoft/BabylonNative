# Remaining shotgun rendering stack

The stack is rebased onto BabylonNative `3428526f` after #1913 merged.
It retains the newer upstream Canvas identity/lifetime, shader-coordinate,
texture-validation, GUI-readiness, and Draco-isolation fixes. Landed work is
not replayed as duplicate patches.

## Code and support split

`shotgun-landing` contains the remaining native changes and source regressions:

| Order | Change | Dependency |
|-------|--------|------------|
| 1 | Straight-RGBA Canvas gradient interpolation | Matching Canvas upload alpha contract; standalone #1889 remains draft |
| 2 | Compute shader compilation, packaging, and raw SRV/UAV masks | Full glslang/SPIRV-Cross builds; dynamic compute compilation currently D3D11-only |
| 3 | Ordered storage uploads, dispatches, and sampler restoration | Compute compiler and matching JavaScript command stream |
| 4 | Embedded GPU instance repacking and primitive topology expansion | Storage buffers and matching native instance bindings; fixed repack shader needs no runtime compiler |
| 5 | Additive clear/stencil commands, floating readback, FXC workaround, and integration regressions | Protocol 9; optional Babylon.js capability selection and the SPIRV-Cross fixes below |

`shotgun-landing-support` is stacked on that code. It carries the accepted
catalog/references, full-codec/build defaults, generated JavaScript tests, and
this documentation. It does not change C++/headers/shaders or CI scripts.

The catalog remains **720 total / 715 enabled / 5 excluded**. Reference images,
fixtures, render counts, pixel thresholds, and error allowances are preserved
from the accepted pre-merge support stack; a rebase is not a reason to weaken
them.

## Dependency and runtime pairing

| Dependency | Revision / policy |
|------------|-------------------|
| bgfx.cmake | Upstream `68daa46f30ad7ad36a2543e219d5c92f2dfdeb4a` (#1918) |
| bgfx | `cb0c6d0c6133d123989aaae10cbd35fa139cb648` |
| bimg | `101b5b5fd4670f82cfdec8e98aa1ab9ee93bb2a1` |
| bx | `09cf98d55c555ddd672e08d847e2f9ded0673424` |
| JsRuntimeHost | `bkaradzic-microsoft/JsRuntimeHost` at `0ad1c2dcaab54d19fa9ad8c2562b7c29364774b7`, based on upstream #262 |
| glslang | Upstream `284e4301e5a6b44b279635276588db7cdd942624` |
| SPIRV-Cross | `bkaradzic-microsoft/SPIRV-Cross` at `26c40e831c35e2794d5d3a72384f4c27628ee056` |
| Draco | Upstream glTF subset in code; full bitstream in support |

The old glslang fork override is no longer needed. SPIRV-Cross still requires
`enable_fxc_nested_loop_workaround`. JsRuntimeHost retains the unmerged XHR
load/error event behavior and unhandled-promise reporting over upstream
`2f8b9be3`; #262 did not include these fixes. Code-only comparison builds need
both source overrides.

Raw array uploads and rendered arrays now share one sampling origin, including
integer fetches and explicit gradients. Compressed array layers are decoded
before row flipping, preserving partial edge blocks and caller data. Canvas
filter borders acquire transparent colors from the shared clear palette rather
than assuming palette slot zero is transparent.

The retained runtime fork reports URL-open failures asynchronously through
`error` then `loadend`, with status zero and diagnostics. Its XHR and the paired
Babylon.js `RequestFile` regressions assert that contract explicitly; the
upstream synchronous-throw expectation does not apply to this integration.

Support enables NativeDraco/NativeMeshopt and defaults desktop Windows to V8,
while respecting an explicitly selected engine. Full Draco can decode older
and non-glTF streams but can also encode features rejected by a glTF-only
decoder; it is an integration choice, not an upstream default change.

**The engine remains on protocol 9.** `CLEAR` and `SETSTENCIL` retain their
original payload lengths and default masks of `0xFF`. Optional `CLEAR2` and
`SETSTENCIL2` append the attachment mask and comparison mask, respectively.
Updated Babylon.js selects each command independently when available and emits
the exact legacy payload otherwise. Legacy engines retain their existing
limitations: no selective MRT clear mask or stencil comparison mask.

`readTexture` retains its RGBA8 byte contract, including for float sources.
Optional `readTexture2` preserves float sources as RGBA32F; updated Babylon.js
uses it when available and otherwise uses the old method. Both entry points
share bounds validation and respect supplied-buffer offsets.

Stock Babylon.js 9.29.0 can use the legacy paths. The additional shotgun
rendering features still require the matching Babylon.js fork, including
Canvas upload alpha/mipmap forwarding and GPU-particle bindings. Stage its UMD
bundles in the normal `Apps\node_modules` package locations before native asset
packaging. Keep core, GUI, loaders, materials, serializers, addons, and procedural
textures together: old ES5 extension constructors cannot subclass a modern
class-based core. Existing npm packaging supplies earcut, Havok, and procedural
textures; obsolete vendored copies are not restored. Previously frozen
protocol-10 bundles must be rebuilt, not reused with this protocol-9 engine.

## Validation record

The post-merge comparison uses Windows Release/V8/D3D11 and the frozen
protocol-10 Babylon.js 9.28 runtime from the accepted sweep: 838 hash-recorded input files,
four workers, and a 40-second process timeout per scene. The new executable
and current `validation_native.js`/`native_env.js` replace their old versions;
all other frozen inputs are checked unchanged.

These are source-override builds, not clean-fetch stock builds. The manifests
record every override's source revision/status, including the retained UrlLib
fork, and the graphics submodule revisions above.

The prior full-codec baseline is 713 passes and two failures: scene 167
(17,768 pixels, 7.403333%, allowance 5.9%) and scene 218 (4,492 pixels,
1.871667%, allowance 0.15%). Restricted Draco additionally rejects scenes
202-206. Raw sweeps and isolated rechecks remain separate:

| Sweep | PASS | FAIL | HANG |
|-------|------|------|------|
| Initial post-merge code | 698 | 15 | 2 |
| Initial post-merge support | 703 | 10 | 2 |
| Final code | 703 | 8 | 4 |
| Final support | 712 | 2 | 1 |

All eight initial shadow image regressions are corrected. The two initial
hangs, 255 and 503, require the retained XHR fixes and now pass.

The final code sweep additionally loses 160, 161, 169, 171, and 325. Scene 160
records an asset transport failure; 160/161/169/171 pass isolated repeats with
the same 838 inputs and 40-second timeout. The final support timeout is 405.
Repeated 325/405 runs have zero pixel difference but exit 1 after an
`operation canceled` error during shutdown. The previous accepted binaries
reproduce this too: 12/12 comparisons for 325 and 3/3 for 405. These are not
counted as clean process passes or silently substituted into the raw totals.
The existing sweep harness classifies validated images independently of late
process errors, so its PASS counts are not zero-exit guarantees.

The final native suite is 158/159, including 196 passing JavaScript cases.
The remaining no-mip sampling failure reproduces with the previous executable.
The runtime selection passes 11 native cases with one backend skip; its
JavaScript suite has 251 passes and four existing pending cases. TypeScript
reports the same ten diagnostics as upstream, with no new diagnostics.

Final rendering executables predate only the corrected `RequestFile` test
assertion and regenerated unit-test bundle; their native sources and staged
rendering inputs are unchanged. Immutable `218-validation-*` manifests,
comparisons, and isolated control logs retain the complete record.

## Embedded instance-repacking follow-up

The fixed repacker now loads a bgfx embedded compute shader rather than compiling
ESSL through `ShaderProvider`. Its parameters, raw-buffer bindings, instance-slot
layout, dispatch ordering, and sampler restoration are unchanged. Dynamic
JavaScript/GPU-particle compute compilation remains separate and unchanged.

`Plugins/NativeEngine/Source/Shaders/cs_instance_repack.sc` has checked-in DXBC,
DXIL, ESSL, GLSL, Metal, and SPIR-V headers generated by bgfx shaderc. Ordinary
builds need no shaderc executable. Set `SHADERC_PATH` when changing the source;
NativeEngine and Canvas reuse `Dependencies/shaderc.cmake` for regeneration.
All six variants compile; GPU execution was checked on D3D11, not all backends.

The added native regression checks strided attributes of one to four words,
reverse slot ordering, partial workgroups, destination growth, and smaller
subsequent dispatches without using the runtime shader compiler. The focused
selection passes 26/26, including 196 JavaScript cases. An earlier combined run
hit the Canvas mipmap assertion; isolated JavaScript and the subsequent combined
repeat pass. Old-binary controls also pass, so that transient failure is recorded
without claiming an established cause.

All 36 GPU-particle scenes (670-705) exit zero and retain exactly their prior
threshold-pixel counts with the same frozen inputs and unchanged gates. These
are targeted checks, not a replacement full sweep; the Batch 218 raw totals above
remain the full-sweep record.

## Additive protocol-9 compatibility follow-up

The standalone protocol-10 commit is removed. Its replacement is folded into
the integration commit, leaving five native commits and four support commits.
The embedded repacker and dynamic compute implementation are unchanged.

On Windows Release/V8/D3D11, stock 9.29.0 with the new native engine passes
6/6 selected compatibility cases. Updated Babylon.js with the actual older
protocol-9 native executable from #1910 also passes 6/6. The paired selection
passes 22/22 native cases, including 196 JavaScript cases. Ten texture/command
cases cover the raw mixed-version stream and the fixture's rendering-safe
teardown. The stream alternates legacy and extended commands with observable
sentinels, checking that neither reader consumes the next command's words.

Babylon.js core compilation and scoped lint complete without errors; the
native-engine unit selection passes 71/71. A temporary devhost app exercises
six browser-side command/readback selection checks, not native GPU decoding;
the app and server are removed afterward.

All 43 selected stencil, MRT, CDF, and GPU-particle scenes exit zero and
reproduce exactly their Batch 218 threshold-pixel counts. All 838 frozen inputs
are verified before replacing the executable and seven matched UMD bundles.
References, catalog, fixtures, thresholds, and exclusions are unchanged.
This is not a new full sweep; the raw totals above remain historical evidence.

Initial staging attempts mixed modern core with frozen ES5 materials and
failed the GradientMaterial constructor. The existing ES5 transform also
failed on this development bundle. Those attempts are retained in `220-*`
logs; the final passing V8 run uses a consistently rebuilt modern bundle set.
No Chakra or cross-backend compatibility result is claimed for those bundles.

## Independent open PRs

#1888 and #1844 were closed as superseded by #1913. #1910 was rebased and
published separately; its 371-scene stock catalog and all 35 CI checks pass. #1889 was also
rebased and remains draft: the stock GUI transparency scene differs by
81,174 / 240,000 pixels (33.8225%) against its unchanged 2.5% allowance.
