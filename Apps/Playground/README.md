# Playground

## Playwright host

The Win32 Playground can expose BabylonNative to the Babylon.js Playwright
visualization harness. Build the `Playground` target and launch it indirectly
through the Babylon.js `native` Playwright project:

```powershell
$env:BABYLON_NATIVE_PLAYGROUND = "C:\path\to\Build\Apps\Playground\Release\Playground.exe"
npx playwright test --config playwright.config.ts --project native
```

`--playwright` starts a hidden, long-lived Playground process and reserves
prefixed stdout lines for the bridge protocol. It supports JavaScript
evaluation, viewport resize, console forwarding, PNG framebuffer capture, and
shutdown. Browser DOM and input APIs are intentionally not emulated.

## Windows/D3D11 CI migration

The shared catalog, web reference images, Playwright assertions and native adapter
live in Babylon.js under `packages\tools\tests`. BabylonNative owns only the
Playground host and CI integration. Do not copy the test catalog or introduce a
native reference-image set.

The `Native visual migration (Win32 D3D11)` workflow accepts a full Babylon.js
commit SHA containing the native project. Existing CI callers still default to
legacy validation; the new workflow is opt-in:

- `shadow` runs legacy validation and then Playwright. Optional suite setup and
  Playwright failures are non-blocking but explicitly reported, with JSON,
  logs, image diffs and crash dumps uploaded.
- `replace` runs Playwright instead of legacy validation. It fails unless the
  complete shared native selection passes and every legacy D3D11 runnable scene
  has an unambiguous, matching source/render configuration that was executed
  and passed. Missing cases, changed scene revisions, ambiguous mappings,
  exclusions and unexecuted cases block cutover.

Run the same CI entry point locally from the BabylonNative checkout:

```powershell
.\Apps\scripts\runPlaywright.ps1 `
    -BabylonJsRoot ..\Babylon.js `
    -Playground .\Build-Playwright\Apps\Playground\Release\Playground.exe `
    -Artifacts .\playwright-results `
    -Mode shadow -Workers 2
```

Use a new or empty artifact directory. The runner disables snapshot updates,
records source revision, executable/runtime-script/reference hashes and Node
version, and produces `migration.json` with actual execution statuses and
case-by-case legacy coverage. Filtering and tolerance overrides are rejected.
Use a Babylon.js checkout without a local `.env` file so it cannot reintroduce
overrides after preflight. BrowserStack setup is disabled for this native run.
Both modes return a failure locally if Playwright fails; only the CI shadow
step is non-blocking. Runner stdout and stderr are written to `playwright.log`
and `playwright-stderr.log`; native diagnostic banners are preserved in these
logs without routing the full suite's output through PowerShell's object pipeline.

The B.js suite revision is pinned separately from the BN build. BN's installed
UMD bundles remain the ones packaged with Playground; their hashes are recorded
and are not silently replaced with CDN `latest`. Validate the chosen pairing
before enabling replacement. Newer test catalogs can require newer engine
features; such failures are not grounds to loosen web goldens or hide coverage.

Keep `validation_native.js` and its diagnostic/RenderDoc workflow available
during migration. A successful Windows cutover does not replace other platform
jobs. Hosted verification requires publishing both integration changes and
selecting the resulting B.js commit; local changes alone cannot be checked out
by GitHub Actions.

Regression checks for the migration report and Windows CI entry point:

```powershell
node --test .\Apps\scripts\playwrightReport.test.mjs .\Apps\scripts\runPlaywright.test.mjs
```
