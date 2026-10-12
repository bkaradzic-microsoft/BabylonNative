[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BabylonJsRoot,
    [Parameter(Mandatory)][string]$Playground,
    [Parameter(Mandatory)][string]$Artifacts,
    [ValidateSet('shadow', 'replace')][string]$Mode = 'shadow',
    [ValidateRange(1, 4)][int]$Workers = 2
)

$ErrorActionPreference = 'Stop'
$BabylonJsRoot = (Resolve-Path -LiteralPath $BabylonJsRoot).Path
$Playground = (Resolve-Path -LiteralPath $Playground).Path
$Artifacts = [IO.Path]::GetFullPath($Artifacts)
$legacyConfig = Join-Path (Split-Path $Playground) 'Scripts\config.json'
$sharedConfig = Join-Path $BabylonJsRoot 'packages\tools\tests\test\visualization\config.json'
$cli = Join-Path $BabylonJsRoot 'node_modules\playwright\cli.js'
foreach ($file in @($cli, $legacyConfig, $sharedConfig, (Join-Path $BabylonJsRoot 'packages\tools\tests\test\playwright\nativePage.ts'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required native visualization input is missing: $file"
    }
}
foreach ($name in @('CONFIG_PATH', 'TEST_FILENAME', 'AFFECTED_TAGS', 'EXCLUDE_REGEX_ARRAY', 'SCREENSHOT_THRESHOLD', 'SCREENSHOT_MAX_PIXEL', 'TIMEOUT')) {
    if ([Environment]::GetEnvironmentVariable($name)) {
        throw "$name must be unset for a complete migration comparison."
    }
}
if (Test-Path -LiteralPath (Join-Path $BabylonJsRoot '.env')) {
    throw 'Use a Babylon.js checkout without a .env file for migration runs; it can override selection and screenshot tolerances after preflight.'
}
if (Test-Path -LiteralPath $Artifacts) {
    if (Get-ChildItem -LiteralPath $Artifacts -Force | Select-Object -First 1) {
        throw "Use an empty artifact directory to avoid mixing runs: $Artifacts"
    }
} else {
    New-Item -ItemType Directory -Path $Artifacts | Out-Null
}

$variables = @{
    BABYLON_NATIVE_PLAYGROUND = $Playground
    BABYLON_NATIVE_GRAPHICS_API = 'd3d11'
    BABYLON_NATIVE_PLATFORM = 'win32'
    BROWSER = 'Chrome'
    CDN_BASE_URL = 'https://cdn.babylonjs.com'
    LOG_TO_CONSOLE = 'true'
    PLAYWRIGHT_JSON_OUTPUT_NAME = (Join-Path $Artifacts 'results.json')
}
$saved = @{}
foreach ($name in $variables.Keys) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name)
    [Environment]::SetEnvironmentVariable($name, $variables[$name])
}
$nativePreference = $PSNativeCommandUseErrorActionPreference
$PSNativeCommandUseErrorActionPreference = $false
Push-Location $BabylonJsRoot
try {
    $suiteCommit = & git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve Babylon.js revision.' }
    $manifest = @{
        nodeVersion = (& node --version)
        runnerImage = $env:ImageVersion
        suiteCommit = $suiteCommit
        suiteChanges = @(& git status --porcelain)
        executable = $Playground
        executableSha256 = (Get-FileHash -LiteralPath $Playground -Algorithm SHA256).Hash
        catalogSha256 = (Get-FileHash -LiteralPath $sharedConfig -Algorithm SHA256).Hash
        workers = $Workers
        runtimeScripts = @(Get-ChildItem (Join-Path (Split-Path $Playground) 'Scripts') -File |
            Get-FileHash -Algorithm SHA256 | Select-Object Path, Hash)
        referenceImages = @(Get-ChildItem (Join-Path $BabylonJsRoot 'packages\tools\tests\test\visualization\ReferenceImages') -File -Filter '*.png' |
            Get-FileHash -Algorithm SHA256 | Select-Object Path, Hash)
        mode = $Mode
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $Artifacts 'inputs.json') -Encoding utf8
    $node = (Get-Command node.exe -CommandType Application | Select-Object -First 1).Source
    $start = [Diagnostics.ProcessStartInfo]::new($node)
    $start.WorkingDirectory = $BabylonJsRoot
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @($cli, 'test', '--config', '.\playwright.config.ts', '--tsconfig', '.\tsconfig.json',
        '--project', 'native', '--workers', "$Workers", '--retries', '0', '--update-snapshots=none',
        '--trace', 'off', '--reporter', 'line,json', '--output', (Join-Path $Artifacts 'test-results'))) {
        $start.ArgumentList.Add($argument)
    }
    $child = [Diagnostics.Process]::new()
    $child.StartInfo = $start
    $stdout = $null
    $stderr = $null
    $started = $false
    try {
        $stdout = [IO.File]::Open((Join-Path $Artifacts 'playwright.log'), 'Create', 'Write', 'Read')
        $stderr = [IO.File]::Open((Join-Path $Artifacts 'playwright-stderr.log'), 'Create', 'Write', 'Read')
        $started = $child.Start()
        if (-not $started) { throw 'Failed to start Playwright.' }
        # Drain native diagnostic output directly to files, without PowerShell's output pipeline.
        $outCopy = $child.StandardOutput.BaseStream.CopyToAsync($stdout)
        $errCopy = $child.StandardError.BaseStream.CopyToAsync($stderr)
        Write-Output "Running native Playwright (PID $($child.Id)); logs: $Artifacts"
        $child.WaitForExit()
        $null = $outCopy.GetAwaiter().GetResult()
        $null = $errCopy.GetAwaiter().GetResult()
        $testExit = $child.ExitCode
    } finally {
        if ($started -and -not $child.HasExited) {
            $child.Kill($true)
            $child.WaitForExit()
        }
        if ($stdout) { $stdout.Dispose() }
        if ($stderr) { $stderr.Dispose() }
        $child.Dispose()
    }
    $report = Join-Path $Artifacts 'results.json'
    if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
        throw "Playwright exited $testExit without a JSON report; this is not a successful run."
    }
    & node (Join-Path $PSScriptRoot 'playwrightReport.mjs') $report $legacyConfig $sharedConfig (Join-Path $Artifacts 'migration.json') $Mode
    $reportExit = $LASTEXITCODE
    if ($testExit -ne 0 -or $reportExit -ne 0) {
        throw "Native visualization failed: Playwright exit=$testExit, migration report exit=$reportExit. See $Artifacts"
    }
} finally {
    Pop-Location
    foreach ($name in $saved.Keys) {
        [Environment]::SetEnvironmentVariable($name, $saved[$name])
    }
    $PSNativeCommandUseErrorActionPreference = $nativePreference
}
