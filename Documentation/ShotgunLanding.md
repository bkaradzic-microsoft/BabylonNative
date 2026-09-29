# Shotgun landing branches

## Code-only split, 2026-10-01

`shotgun-landing` is based on upstream
`79648b2ce7536b94dc7904065dd1a864f6439f2e` and contains 19 review commits.
Its code tip is `e3ce2b6807ccbb72cc1e291cd44baa4b0a66e1b7`.
Every commit modifies C++ source. Directly related headers, shaders,
source regressions, and build wiring stay with their implementation.
Previously merged PRs are inherited from upstream rather than replayed.
This includes the newer MSAA zero-initialization and Metal-import guards.

`shotgun-landing-support` is stacked on that branch with 15 additional
commits. It carries fonts and their licenses/provenance, reference images,
scene fixtures/catalog, generated JavaScript test bundles, documentation,
vendored bootstrap assets, package changes, and fork/build defaults.
The support-only range contains no C++ changes. Asset-copy CMake rules move
with their assets instead of leaving missing-file build dependencies in the
code branch. Shader headers compiled into Canvas remain code, not assets.

There are no landing-added CI/workflow changes on either branch.
`validation_native.test.cjs` is absent from both branch tips and from every
new commit. Upstream's other tests and CI configuration are unchanged.
The pre-split 30-commit stack is preserved at `dfda91c0` on
`backup/shotgun-landing-before-code-split-20261001`.

| Position | Group | Code review unit |
| --- | --- | --- |
| 1 | E1 | Attribute-less instance counts |
| 2 | E2 | Additional readback bounds and storage-size guards |
| 3 | E7 | Native Canvas instance branding |
| 4 | E7/H4 | Straight-RGBA gradient interpolation |
| 5 | N1 | Rectangle path reset |
| 6 | N2 | Per-framebuffer MSAA state |
| 7 | N3 | SVG decoding and rasterization |
| 8 | N4 | Texture coordinates and pixel uploads |
| 9 | N5 | Glyph rasterization and metrics |
| 10 | N6 | Paths and compound clipping |
| 11 | H1 | Mips, filtering, and varying layout |
| 12 | H2a | MRT clears and shared framebuffer attachments |
| 13 | H2b | Formats, texture layers, and volumes |
| 14 | H3a | Compute compilation and raw binding masks |
| 15 | H3b | Storage uploads, dispatch, and sampler restore |
| 16 | H3c | GPU instance repacking and primitive expansion |
| 17 | H4 | Canvas alpha representations and dynamic mips |
| 18 | H5a | Validation execution and browser environment source |
| 19 | H6 | Native decoder entry points and engine protocol |

Review the two ranges separately:

```text
git log --reverse --oneline 79648b2c..shotgun-landing
git diff --stat 79648b2c..shotgun-landing
git log --reverse --oneline shotgun-landing..shotgun-landing-support
git diff --stat shotgun-landing..shotgun-landing-support
```

The code-only checkout builds UnitTests and Playground without adding the
support assets to its source tree. Running the full shotgun catalog still
requires the companion assets and matching Babylon.js runtime. Likewise,
regenerating JavaScript test bundles is necessary when running source
regressions without the support branch's checked-in generated bundles.
This split is not a claim of stock Babylon.js protocol compatibility.

Both branches were built and swept on Windows V8/D3D11 with the companion
catalog/assets and matching Babylon.js protocol-10 runtime. The current
upstream bgfx.cmake pin is `8edfdc30` (bgfx `22174cf4`, bimg `c085da68`,
bx `1c986bd1`); it is not downgraded to the old landing pin.
Release UnitTests/Playground build on both branches, and the same 23 selected
native controls pass on each with the matching Babylon.js runtime.

| Executable source/configuration | PASS | FAIL | HANG/UNK |
| --- | --- | --- | --- |
| Code-only branch, upstream glTF-only Draco codec | 708 | 7 | 0 |
| Support branch, full Draco codec; identical C++/header/shader sources | 713 | 2 | 0 |

The five code-only differences are scenes 202-206, which report
`Unsupported major version` with upstream's `DRACO_GLTF_BITSTREAM=ON`.
The full-codec setting deliberately lives in support, not the code branch.
With that configuration, all 715 scene statuses match the pre-split
`dfda91c0` baseline. The two remaining failures have identical pixel counts:
167 has 17,771 differing pixels (7.404583%, gate 5.9%), and 218 has 4,492
(1.871667%, gate 0.15%). Five exclusions remain unchanged.

All 838 runtime input hashes were verified before and after each sweep.
Only the executable differs between the two new runs. Relative to the
pre-split runtime, the runner and tracked procedural-texture bundle also
changed; the catalog, references, fonts, core Babylon.js bundle, and gates
did not. Six passing scenes vary numerically in the support sweep without
crossing a gate, including the historically intermittent scene 176.
These runs reuse known dependency sources, including fork shader/runtime
dependencies; they do not establish clean-fetch closure, stock Babylon.js
compatibility, Debug coverage, or cross-platform parity.

The earlier review holds and image/font/backend/dependency follow-ups remain
open. No tolerance, reference image, or exclusion was changed to make the
split pass validation. No PR is opened by publishing these branches.

## Historical pre-split guide (`dfda91c0`)

The remainder records the original 30-commit stack and its earlier
validation; its commit count, base, and test results are not the current
post-split branch description.

This branch consolidates BabylonNative `shotgun` at
`d2727b4f61d283db0ed1d92c513065126ed3055a` onto upstream master
`601379cfd11dd66b338c6534647bcbf843b7ac77`. It represents the final net
changes, not the chronological experiments, reverts, or upstream merge
commits. Newer published PR refinements and upstream device-loss handling
are retained. The automatic `inst_attr_map.txt` and `repack_layout.txt`
diagnostic dumps are omitted.

There are 30 linear review commits: 14 reviewed inputs, followed by 16
residual groups. No PR is opened by creating this branch. Neither shotgun
nor any existing PR branch is rewritten.

## Reviewed inputs

| Position | Group | Input | Review unit |
| --- | --- | --- | --- |
| 1 | E1 | #1884 | MSAA allocation and sample flags |
| 2 | E1 | #1885 | Cube allocation and face selection |
| 3 | E1 | #1888 | Attribute-less instance counts |
| 4 | E2 | #1892 | Readback coordinates and typed-buffer bounds |
| 5 | E3 | #1893 | Canvas image reload and cancellation ownership |
| 6 | E4 | #1894 | High-precision splat matrix storage |
| 7 | E4 | #1897 | Meshopt compatibility entry point |
| 8 | E5 | #1896 | Vertex-array layout ownership |
| 9 | E6 | #1890 | Readiness, terminal errors, and cleanup |
| 10 | E6 | #1895 | Reverse-depth isolation |
| 11 | E7 | #1844 | Native Canvas instance branding; held design |
| 12 | E7/H4 | #1889 | Straight-RGBA gradients; held alpha contract |
| 13 | H6 | #1902 | Newer upstream bgfx and device-loss integration |
| 14 | N1 | `cf56bbd7` | `strokeRect` geometry and pixel regressions |

## Residual groups

| Position | Group | Review unit | Prerequisites and boundaries |
| --- | --- | --- | --- |
| 15 | N2 | Per-framebuffer raster MSAA | E1 allocation; default/window and explicit-target state, with dynamic-sample controls |
| 16 | N3 | SVG decoding and rasterization | E3 lifecycle; bounded decoding and parse failures, not a duplicate reload fix |
| 17 | N4 | Texture-coordinate conventions | E2 readback; CPU upload and shader sampling must agree; some volume/IBL controls additionally need H2 and matching Babylon.js |
| 18 | N5 | Glyph rasterization, metrics, and fonts | STB/fontstash placement and metrics, with licensed Arimo/DroidSans assets and their packaging |
| 19 | N6 | Paths and compound clip masks | E7 branding, N3 rasterizer, N5 test font; shader sources and generated backends stay together |
| 20 | H1 | Mip selection and varying layout | Upstream bgfx/bimg; LOD clamp stays separate from filtering; coordinate controls build on N4 |
| 21 | H2a | MRT clears and shared attachments | N2 raster state; floating-point clear API and all its callers, shared depth and attachment routing |
| 22 | H2b | Texture formats, layers, and volumes | H2a attachment implementation; new allocation and comparison-sampling contracts need matching Babylon.js |
| 23 | H3a | Compute compilation and raw masks | H1 compiler layout; includes full-build/WebMin configuration and CPU packaging regressions; runtime compute is D3D11-only |
| 24 | H3b | Storage, dispatch, and sampler restore | H3a compilation and H1 LOD state; byte buffers use upstream raw SRV/UAV masks, without custom bgfx flags |
| 25 | H3c | GPU instances and primitive expansion | H3b storage/dispatch and N2 draw state; GPU repacking, topology caches, ownership, and triangle-strip coverage |
| 26 | H4 | Canvas alpha representations and mips | E7 gradient contract, H1 mips, matching Babylon.js upload behavior; omitted arguments preserve legacy premultiplied sources |
| 27 | H5a | Runner and browser portability | N5 fonts; native environment, Havok/package setup, vendored bootstrap assets, engine options, headless behavior, and complete runner regressions |
| 28 | H5b | Canonical fixtures and references | Scene/reference snapshots; no numerical gate changes in rendering fixes |
| 29 | H5c | Catalog and numerical policy | Test selection, exclusions, tolerances, and policy documentation, isolated from renderer changes |
| 30 | H6 | Fork wiring and build defaults | Integration-only URLs, protocol/version wiring, Win32 V8 and codec defaults; not an independent renderer fix |

The N6 scoped-path implementation supersedes N1's simple reset while keeping
its three pixel regressions. Layer/volume extensions preserve the reviewed
cube-dimension and finite-face checks. The float-clear API also adapts the
newer device-loss recovery and cube-face tests.

## Reviewing and cherry-picking

Each commit has a `Review-Group` trailer; the first 14 also identify their
source PR or extraction. Inspect the linear stack with:

```text
git log --reverse --format="%h %s" 601379cf..shotgun-landing
git show --stat <commit>
git show <commit>
```

The existing PR inputs remain distinct cherry-pick units. Residual groups
have the prerequisites above: dependent protocol, compiler, and shared-file
patches are not promised to apply or run alone on stock master. Cherry-pick
their prerequisites first, or extract the named prerequisite surface when
preparing a narrower PR. Do not bring reference replacements, numerical
budgets, or integration defaults into an unrelated correctness PR.

The combined stack intentionally uses the matching Babylon.js shotgun
runtime and the JsRuntimeHost/SPIRV-Cross/glslang fork wiring in H6.
Those companion changes are not included in this repository. The newer
bgfx.cmake pin comes from #1902; there is no new bgfx/bimg/bx patch here.

## Review improvements

Input-safety refinements are folded into their existing review groups, not
appended as a separate fix stack:

- E2 validates actual mip/rectangle/face bounds before readback allocation
  and checks storage sizes before narrowing to bgfx's uint32 representation.
- H2b validates texture dimensions, layers/depth, and comparison enums before
  integer narrowing or mutation. Finite positive fractional dimensions still
  truncate, as required by ratio-based render targets and WebGL's size
  conversion; values that truncate to zero remain invalid. Layers and enums
  must be integers. Zero-layer/default-layer behavior is preserved.
- H3b checks storage allocation rounding and source/destination ranges before
  pointer construction or mutation. Misaligned and disposed-buffer updates
  fail without modifying the shadow. Direct JavaScript update errors now
  throw rather than only logging and appearing to succeed.
- N4 includes its complete pixel-mirror helpers, source-rectangle fields,
  scoped paths, and NanoVG path-save/restore support instead of relying on
  definitions introduced later in N6. This relocation leaves the tip's
  Canvas implementation unchanged.
- H3c no longer writes `repack_layout.txt`. H5a loads earcut and procedural
  textures once, and includes both vendored bootstrap assets instead of
  depending on H5b to supply them.

These changes do not complete the review plan. Image/font contracts, backend
linkage and metadata, dependency freezing, and the remaining historical
boundary/extraction issues remain separate work.
The MRT skip-sentinel correction belongs in bgfx; Native's current masking
helper/fallback stays unchanged until a corrected dependency is pinned.
No image budget, exclusion, or reference change is part of these fixes.

## Held work and validation limits

This is a review candidate, not an assertion that every group is ready to
merge. #1844 still needs agreement on the runtime type-tag/branding design.
#1889/H4 still needs the paired upload-alpha contract and its known
stock-stack transparent-GUI regression resolved.

An isolated Windows V8/D3D11 build compiles UnitTests and Playground in both
Debug and Release. The host-independent runner suite passes all 42 cases.
The first review-improvement batch passes 36 selected Release native tests
and 35 Debug tests. Matched JavaScript reports 193 passing and the inherited
`RequestFile` synchronous-throw failure. The original broader landing
selection also identified an inherited no-mip pixel-control failure; this
batch does not claim to fix it. Raw-buffer reflection and the actual GPU
storage round-trip pass in Release.

Independent historical builds pass at E2 with image loading disabled,
at N4 for NativeEngine/Canvas, and at H5a for Playground, before H5b's
scene/reference assets. These use the same resolved dependency sources as
the isolated tip build, not a clean-fetch dependency-provenance claim.

A full matched `validation_native.js` sweep passes 713 of 715 enabled
scenes, with only the existing failures at 167 and 218. All 715 statuses
and both failure pixel counts match the original landing and untouched
shotgun baselines. Runtime inputs were verified before and after the sweep;
only the candidate executable differs. The five exclusions, references,
and numerical gates remain unchanged.

Debug additionally hits the pre-existing bgfx assertion against CPU
updates of compute-write buffers. The same assertion exists at shotgun's
older bgfx pin. It remains a storage-path review issue, not a newly added
dependency patch. The selected Debug run excludes that known assertion;
it is not a claim that the entire Debug suite passes. These checks do not
establish stock-stack compatibility or cross-platform validation.
