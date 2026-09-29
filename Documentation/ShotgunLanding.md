# Shotgun landing branch

This branch consolidates BabylonNative `shotgun` at
`d2727b4f61d283db0ed1d92c513065126ed3055a` onto upstream master
`601379cfd11dd66b338c6534647bcbf843b7ac77`. It represents the final net
changes, not the chronological experiments, reverts, or upstream merge
commits. Newer published PR refinements and upstream device-loss handling
are retained. An unconditional temporary instance-layout file dump is
omitted.

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
| 27 | H5a | Runner and browser portability | N5 fonts; native environment, Havok/package setup, engine options, headless behavior, and complete runner regressions |
| 28 | H5b | Canonical fixtures and references | Scene/reference snapshots and vendored runtime assets; no numerical gate changes in rendering fixes |
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

## Held work and validation limits

This is a review candidate, not an assertion that every group is ready to
merge. #1844 still needs agreement on the runtime type-tag/branding design.
#1889/H4 still needs the paired upload-alpha contract and its known
stock-stack transparent-GUI regression resolved.

An isolated Windows V8/D3D11 build compiles UnitTests and Playground in both
Debug and Release. The host-independent runner suite passes all 42 cases.
The affected Release native selection passes 98 of 100 tests. JavaScript
reports 192 passing and one inherited `RequestFile` synchronous-throw
failure; the no-mip pixel control also fails. Both failing native selectors
reproduce on the untouched shotgun executable; no gate or reference was
changed to hide them. Raw-buffer reflection and the actual GPU storage
round-trip pass in Release.

Debug additionally hits the pre-existing bgfx assertion against CPU
updates of compute-write buffers. The same assertion exists at shotgun's
older bgfx pin. It remains a storage-path review issue, not a newly added
dependency patch. These checks are not a complete rendering sweep, a
stock-stack compatibility claim, or cross-platform validation.
