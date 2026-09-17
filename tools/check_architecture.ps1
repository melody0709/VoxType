[CmdletBinding()]
param(
    [switch]$VerboseOutput,
    [ValidateSet("", "P1", "P2", "P3", "P4", "P5", "P6")]
    [string]$Stage = "",
    [string]$Root = ""
)

# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 (the interpreter
# build.bat uses) reads a BOM-less script with the ANSI code page. A non-ASCII
# byte placed immediately before a newline makes it swallow that newline, which
# merges the following code line into a comment and breaks parsing silently.
# For the same reason all project sources below are read via .NET (UTF-8), never
# via Get-Content.

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
    $ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
    $RepoRoot = (Resolve-Path (Join-Path $ScriptRoot "..")).Path
} else {
    $RepoRoot = (Resolve-Path $Root).Path
}
Set-Location $RepoRoot

$script:HasFailure = $false
$script:Checks = 0
$script:FailedChecks = 0

function Record-Result {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Details
    )
    $script:Checks++
    if ($Passed) {
        Write-Host " [PASS] ${Name}: $Details" -ForegroundColor Green
    } else {
        Write-Host " [FAIL] ${Name}: $Details" -ForegroundColor Red
        $script:HasFailure = $true
        $script:FailedChecks++
    }
}

function Read-Lines {
    param([string]$Path)
    return @([System.IO.File]::ReadAllLines($Path))
}

function Read-Text {
    param([string]$Path)
    return [System.IO.File]::ReadAllText($Path)
}

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " VoxType Architecture Invariants & Ratchet Check (v2)" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# -----------------------------------------------------------------------------
# 0. Ratchet baselines. Decrease-only. Measured on 2026-09-17.
#    Method: project sources parsed as UTF-8 via .NET; includes resolved by
#    header basename to the layer that owns the header file.
# -----------------------------------------------------------------------------
$BASELINES = @{
    GlobalsIncluders        = 0
    GlobalsExterns          = 0
    MainLines               = 150
    SettingsLines           = 3450
    LayerViolations         = 15
    GlobalsCrossLayerHeader = 0
}

# Allowed layer dependencies: key = layer of the including file,
# value = layers whose project headers may be included. $null = unrestricted.
$LAYER_ALLOW = @{
    "core"     = @()
    "audio"    = @("core")
    "asr"      = @("core", "audio")
    "ui"       = @("core")
    "platform" = @("core")
    "app"      = $null
}

$allSourceFiles = @()
foreach ($dir in @("src", "tests", "tools")) {
    $targetDir = Join-Path $RepoRoot $dir
    if (Test-Path $targetDir) {
        $allSourceFiles += @(Get-ChildItem -Path $targetDir -Recurse -File |
            Where-Object { $_.Extension -in @(".h", ".cpp") })
    }
}

# header basename -> owning layer
$headerLayer = @{}
foreach ($f in $allSourceFiles) {
    if ($f.Extension -ne ".h") { continue }
    $rel = $f.FullName.Substring($RepoRoot.Length + 1).Replace("\", "/")
    $parts = $rel.Split("/")
    if ($parts.Length -ge 3 -and $parts[0] -eq "src") {
        $headerLayer[$f.Name] = $parts[1]
    }
}

function Get-ProjectIncludes {
    param([string]$Path)
    $text = Read-Text -Path $Path
    $found = @()
    foreach ($m in [regex]::Matches($text, '#\s*include\s*[<"]([^>"]+)[>"]')) {
        $found += $m.Groups[1].Value
    }
    return $found
}

# -----------------------------------------------------------------------------
# 1. Size ratchets
# -----------------------------------------------------------------------------
$globalsIncluders = @()
foreach ($file in $allSourceFiles) {
    $raw = Read-Text -Path $file.FullName
    if ($raw -match '#\s*include\s*[<"][^>"]*globals\.h[>"]') {
        $globalsIncluders += $file
    }
}
$currIncluders = $globalsIncluders.Count
Record-Result -Name "Ratchet: globals.h includers" `
    -Passed ($currIncluders -le $BASELINES["GlobalsIncluders"]) `
    -Details "$currIncluders / $($BASELINES['GlobalsIncluders']) (current/max_allowed)"

if ($VerboseOutput) {
    foreach ($item in $globalsIncluders) {
        Write-Host "    - $($item.FullName.Substring($RepoRoot.Length + 1))" -ForegroundColor Gray
    }
}

$globalsPath = Join-Path $RepoRoot "src\app\globals.h"
$globalsExists = Test-Path $globalsPath
$currExterns = 0
if ($globalsExists) {
    $externLines = @(Read-Lines -Path $globalsPath | Where-Object { $_ -match '^\s*extern\s+' })
    $currExterns = $externLines.Count

    $crossLayer = @()
    foreach ($inc in (Get-ProjectIncludes -Path $globalsPath)) {
        $base = [System.IO.Path]::GetFileName($inc)
        if ($headerLayer.ContainsKey($base) -and $headerLayer[$base] -ne "app") {
            $crossLayer += $inc
        }
    }

    Record-Result -Name "Ratchet: globals.h extern count" `
        -Passed ($currExterns -le $BASELINES["GlobalsExterns"]) `
        -Details "$currExterns / $($BASELINES['GlobalsExterns']) (current/max_allowed)"

    Record-Result -Name "Ratchet: globals.h cross-layer include hub" `
        -Passed ($crossLayer.Count -le $BASELINES["GlobalsCrossLayerHeader"]) `
        -Details "$($crossLayer.Count) / $($BASELINES['GlobalsCrossLayerHeader']) project headers from other layers"

    if ($VerboseOutput) {
        foreach ($c in $crossLayer) { Write-Host "    - $c" -ForegroundColor Gray }
    }
} else {
    Record-Result -Name "Ratchet: globals.h extern count" -Passed $true -Details "globals.h eliminated (0)"
    Record-Result -Name "Ratchet: globals.h cross-layer include hub" -Passed $true -Details "globals.h eliminated (0)"
}

$mainPath = Join-Path $RepoRoot "src\app\main.cpp"
$currMainLines = 0
if (Test-Path $mainPath) { $currMainLines = (Read-Lines -Path $mainPath).Count }
Record-Result -Name "Ratchet: main.cpp line count" `
    -Passed ($currMainLines -le $BASELINES["MainLines"]) `
    -Details "$currMainLines / $($BASELINES['MainLines']) lines (current/max_allowed)"

$settingsPath = Join-Path $RepoRoot "src\ui\settings.cpp"
$currSettingsLines = 0
if (Test-Path $settingsPath) { $currSettingsLines = (Read-Lines -Path $settingsPath).Count }
Record-Result -Name "Ratchet: settings.cpp line count" `
    -Passed ($currSettingsLines -le $BASELINES["SettingsLines"]) `
    -Details "$currSettingsLines / $($BASELINES['SettingsLines']) lines (current/max_allowed)"

# -----------------------------------------------------------------------------
# 2. Layer direction. Includes are resolved by owning layer, so bare-filename
#    includes (the only style used in this repo) are covered.
# -----------------------------------------------------------------------------
$layerViolations = @()
foreach ($f in $allSourceFiles) {
    $rel = $f.FullName.Substring($RepoRoot.Length + 1).Replace("\", "/")
    $parts = $rel.Split("/")
    if ($parts[0] -ne "src" -or $parts.Length -lt 3) { continue }
    $layer = $parts[1]
    if (-not $LAYER_ALLOW.ContainsKey($layer)) { continue }
    $allowed = $LAYER_ALLOW[$layer]
    if ($null -eq $allowed) { continue }

    foreach ($inc in (Get-ProjectIncludes -Path $f.FullName)) {
        $base = [System.IO.Path]::GetFileName($inc)
        if (-not $headerLayer.ContainsKey($base)) { continue }
        $owner = $headerLayer[$base]
        if ($owner -eq $layer) { continue }
        if ($allowed -contains $owner) { continue }
        $layerViolations += [pscustomobject]@{ File = $rel; Include = $inc; Owner = $owner }
    }
}

Record-Result -Name "Ratchet: cross-layer include violations" `
    -Passed ($layerViolations.Count -le $BASELINES["LayerViolations"]) `
    -Details "$($layerViolations.Count) / $($BASELINES['LayerViolations']) (current/max_allowed)"

if ($VerboseOutput -or $layerViolations.Count -gt $BASELINES["LayerViolations"]) {
    foreach ($v in $layerViolations) {
        Write-Host "    ! $($v.File) -> $($v.Include) [$($v.Owner)]" -ForegroundColor Red
    }
}

$coreViolations = @($layerViolations | Where-Object { $_.File -like "src/core/*" })
Record-Result -Name "Hard: src/core has zero upper-layer dependency" `
    -Passed ($coreViolations.Count -eq 0) `
    -Details "$($coreViolations.Count) violation(s)"
foreach ($v in $coreViolations) { Write-Host "    ! $($v.File) -> $($v.Include)" -ForegroundColor Red }

# -----------------------------------------------------------------------------
# 3. CMake target structure
# -----------------------------------------------------------------------------
$cmakePath = Join-Path $RepoRoot "CMakeLists.txt"
$cmakeContent = Read-Text -Path $cmakePath
$batContent = ""
$batPath = Join-Path $RepoRoot "build.bat"
if (Test-Path $batPath) { $batContent = Read-Text -Path $batPath }

function Get-CallBlocks {
    param([string]$Text, [string]$Verb)
    $blocks = @()
    $idx = 0
    $needle = $Verb + "("
    while ($true) {
        $start = $Text.IndexOf($needle, $idx)
        if ($start -lt 0) { break }
        $depth = 0
        $i = $start + $needle.Length
        while ($i -lt $Text.Length) {
            $c = $Text[$i]
            if ($c -eq "(") { $depth++ }
            elseif ($c -eq ")") {
                if ($depth -eq 0) { break }
                $depth--
            }
            $i++
        }
        $blocks += $Text.Substring($start, $i - $start + 1)
        $idx = $i + 1
    }
    return $blocks
}

function Get-BlockTargetName {
    param([string]$Block)
    $inner = $Block.Substring($Block.IndexOf("(") + 1)
    $parts = @($inner -split '[\s\r\n]+' | Where-Object { $_ -ne "" })
    if ($parts.Count -eq 0) { return "" }
    return $parts[0]
}

# Arguments declared through target_sources() also satisfy the non-empty rule.
$targetsWithSourceCall = @{}
foreach ($block in (Get-CallBlocks -Text $cmakeContent -Verb "target_sources")) {
    $n = Get-BlockTargetName -Block $block
    if (-not [string]::IsNullOrWhiteSpace($n)) { $targetsWithSourceCall[$n] = $true }
}

# Argument text of a call block, with the trailing ")" removed.
function Get-BlockBody {
    param([string]$Block)
    $inner = $Block.Substring($Block.IndexOf("(") + 1)
    if ($inner.EndsWith(")")) { $inner = $inner.Substring(0, $inner.Length - 1) }
    return $inner
}

$targets = @{}
foreach ($block in (Get-CallBlocks -Text $cmakeContent -Verb "add_library")) {
    $name = Get-BlockTargetName -Block $block
    if ([string]::IsNullOrWhiteSpace($name)) { continue }
    $tokens = @((Get-BlockBody -Block $block) -split '[\s\r\n]+' | Where-Object { $_ -ne "" })
    $type = ""
    if ($tokens.Count -ge 2 -and $tokens[1] -match '^(STATIC|SHARED|MODULE|OBJECT)$') { $type = $tokens[1] }
    $sources = @($tokens | Where-Object { $_ -match '\.(cpp|c|rc)$' })
    $isEmpty = ($type -ne "" -and $sources.Count -eq 0 -and -not $targetsWithSourceCall.ContainsKey($name))
    $targets[$name] = [pscustomobject]@{ Kind = "library"; Block = $block; IsEmpty = $isEmpty }
}
foreach ($block in (Get-CallBlocks -Text $cmakeContent -Verb "add_executable")) {
    $name = Get-BlockTargetName -Block $block
    if ([string]::IsNullOrWhiteSpace($name)) { continue }
    $tokens = @((Get-BlockBody -Block $block) -split '[\s\r\n]+' | Where-Object { $_ -ne "" })
    $sources = @($tokens | Where-Object { $_ -match '\.(cpp|c|rc)$' })
    $isEmpty = ($sources.Count -eq 0 -and -not $targetsWithSourceCall.ContainsKey($name))
    $targets[$name] = [pscustomobject]@{ Kind = "executable"; Block = $block; IsEmpty = $isEmpty }
}

# 3.1 zero-source library/executable: CMake generate fails outright
$emptyLibs = @($targets.Keys | Where-Object { $targets[$_].IsEmpty })
Record-Result -Name "Hard: no zero-source target" `
    -Passed ($emptyLibs.Count -eq 0) `
    -Details "$($emptyLibs.Count) empty target(s)"
foreach ($e in $emptyLibs) { Write-Host "    ! $e declares no sources - CMake generate will fail with 'No SOURCES given to target'" -ForegroundColor Red }

# 3.2 /utf-8 coverage. sherpa-onnx cxx-api.h holds non-ASCII string literals;
#     compiling it without /utf-8 is a hard error (C2001), not a warning.
$missingUtf8 = @()
foreach ($name in $targets.Keys) {
    $hasFlag = $false
    foreach ($block in (Get-CallBlocks -Text $cmakeContent -Verb "target_compile_options")) {
        if ((Get-BlockTargetName -Block $block) -ne $name) { continue }
        if ($block -match '/utf-8') { $hasFlag = $true }
    }
    foreach ($block in (Get-CallBlocks -Text $cmakeContent -Verb "target_link_libraries")) {
        if ((Get-BlockTargetName -Block $block) -ne $name) { continue }
        if ($block -match 'voxtype_build_flags') { $hasFlag = $true }
    }
    if (-not $hasFlag) { $missingUtf8 += $name }
}
Record-Result -Name "Hard: every target compiles with /utf-8" `
    -Passed ($missingUtf8.Count -eq 0) `
    -Details "$($missingUtf8.Count) target(s) missing /utf-8"
foreach ($m in $missingUtf8) { Write-Host "    ! $m has no /utf-8 and no voxtype_build_flags inheritance" -ForegroundColor Red }

# 3.3 PCH vs NOMINMAX. MSVC force-includes the PCH (/FI). If <windows.h> is in
#     the PCH and NOMINMAX is undefined, the min/max macros leak into every
#     translation unit and std::min / std::max stop compiling (C2589).
$pchBlocks = Get-CallBlocks -Text $cmakeContent -Verb "target_precompile_headers"
$pchHasWindowsH = $false
foreach ($b in $pchBlocks) { if ($b -match '<windows\.h>') { $pchHasWindowsH = $true } }
$nominmaxPresent = ($cmakeContent -match 'NOMINMAX')
$pchOk = $true
if ($pchBlocks.Count -gt 0 -and $pchHasWindowsH -and -not $nominmaxPresent) { $pchOk = $false }
if ($pchBlocks.Count -eq 0) {
    Record-Result -Name "Hard: PCH <windows.h> requires NOMINMAX" -Passed $true -Details "no PCH in use"
} else {
    Record-Result -Name "Hard: PCH <windows.h> requires NOMINMAX" -Passed $pchOk `
        -Details $(if ($pchOk) { "NOMINMAX defined alongside PCH" } else { "<windows.h> in PCH without NOMINMAX - std::min/std::max will fail with C2589" })
}

# 3.4 qwen_free_proto_sign.cpp must stay out of the production link graph,
#     including through static library targets.
$targetSources = @{}
$targetLinks = @{}
foreach ($name in $targets.Keys) {
    $srcs = @()
    foreach ($m in [regex]::Matches($targets[$name].Block, '(src|tests|tools)[/\\][^\s\)]+\.(cpp|rc)')) {
        $srcs += $m.Value
    }
    $targetSources[$name] = $srcs
    $links = @()
    foreach ($block in (Get-CallBlocks -Text $cmakeContent -Verb "target_link_libraries")) {
        if ((Get-BlockTargetName -Block $block) -ne $name) { continue }
        foreach ($other in $targets.Keys) {
            if ($other -eq $name) { continue }
            if ($block -match ("(?<![A-Za-z0-9_])" + [regex]::Escape($other) + "(?![A-Za-z0-9_])")) { $links += $other }
        }
    }
    $targetLinks[$name] = $links
}

$prodSources = @()
$visited = @{}
$queue = @("VoxType")
while ($queue.Count -gt 0) {
    $cur = $queue[0]
    $queue = @($queue | Select-Object -Skip 1)
    if ($visited.ContainsKey($cur)) { continue }
    $visited[$cur] = $true
    if ($targetSources.ContainsKey($cur)) { $prodSources += $targetSources[$cur] }
    if ($targetLinks.ContainsKey($cur)) { $queue += $targetLinks[$cur] }
}
$signLeak = @($prodSources | Where-Object { $_ -match 'qwen_free_proto_sign\.cpp' })
Record-Result -Name "Security: qwen_free_proto_sign isolated from production link graph" `
    -Passed ($signLeak.Count -eq 0) `
    -Details $(if ($signLeak.Count -eq 0) { "not reachable from VoxType target" } else { "reachable via production link graph" })

# 3.5 Test/tool artifacts must land under build/artifacts, never inside the
#     build/cmake intermediate tree. build.bat --test looks them up in
#     build/artifacts/tests.
$badOutputDirs = @()
foreach ($m in [regex]::Matches($cmakeContent, 'RUNTIME_OUTPUT_DIRECTORY\s+"([^"]+)"')) {
    $value = $m.Groups[1].Value
    $resolved = $value.Replace('${CMAKE_BINARY_DIR}', 'build/cmake/x64-release')
    $resolved = $resolved.Replace('${CMAKE_CURRENT_BINARY_DIR}', 'build/cmake/x64-release')
    $normalized = $resolved.Replace("\", "/")
    while ($normalized -match '/[^/]+/\.\.') { $normalized = [regex]::Replace($normalized, '/[^/]+/\.\.', '') }
    if ($normalized -notmatch '^build/artifacts/') { $badOutputDirs += $value }
}
Record-Result -Name "Layout: RUNTIME_OUTPUT_DIRECTORY stays under build/artifacts" `
    -Passed ($badOutputDirs.Count -eq 0) `
    -Details "$($badOutputDirs.Count) off-layout output dir(s)"
foreach ($d in $badOutputDirs) { Write-Host "    ! $d does not resolve under build/artifacts - build.bat --test would not find it" -ForegroundColor Red }

# -----------------------------------------------------------------------------
# 3.6 Anti-bypass checks. An autonomous executor under pressure to "make the
#     guard green" can tamper with the guard itself, drop tests, or disable the
#     wiring. These checks close those paths.
# -----------------------------------------------------------------------------

# 3.6.1 Baseline integrity: baselines may only ratchet down. Compared against
#       what is committed at HEAD; skipped while the file is still untracked.
$gitAvailable = $false
$baselineRaised = @()
try {
    $headText = (& git show "HEAD:tools/check_architecture.ps1" 2>$null | Out-String)
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($headText)) {
        $gitAvailable = $true
        foreach ($key in $BASELINES.Keys) {
            $m = [regex]::Match($headText, '(?m)^\s*' + [regex]::Escape($key) + '\s*=\s*(\d+)')
            if ($m.Success) {
                $headValue = [int]$m.Groups[1].Value
                if ([int]$BASELINES[$key] -gt $headValue) {
                    $baselineRaised += "$key raised $headValue -> $($BASELINES[$key])"
                }
            }
        }
    }
} catch {
    $gitAvailable = $false
}
if ($gitAvailable) {
    Record-Result -Name "Anti-bypass: guard baselines were not raised vs HEAD" `
        -Passed ($baselineRaised.Count -eq 0) `
        -Details "$($baselineRaised.Count) raised baseline(s)"
    foreach ($b in $baselineRaised) { Write-Host "    ! $b - requires explicit user approval" -ForegroundColor Red }
} else {
    Record-Result -Name "Anti-bypass: guard baselines were not raised vs HEAD" -Passed $true `
        -Details "skipped - guard not committed yet or git unavailable (run 'git add tools/check_architecture.ps1' + commit to activate)"
}

# 3.6.2 The existing offline regression tests must survive the refactor.
$requiredTests = @("qwen_free_protocol_test", "qwen_audio_json_test", "llm_refine_test",
                   "audio_diagnostics_test", "asr_json_protocol_test")
$testsMissing = @()
foreach ($t in $requiredTests) {
    if ($cmakeContent -notmatch [regex]::Escape($t)) { $testsMissing += "$t (CMakeLists.txt)" }
    if ($batContent -notmatch [regex]::Escape($t)) { $testsMissing += "$t (build.bat --test)" }
}
Record-Result -Name "Anti-bypass: offline regression tests intact" `
    -Passed ($testsMissing.Count -eq 0) `
    -Details "$($testsMissing.Count) missing reference(s)"
foreach ($t in $testsMissing) { Write-Host "    ! $t" -ForegroundColor Red }

# 3.6.3 The guard must stay wired into build.bat's main flow, not only --test.
$guardCallCount = ([regex]::Matches($batContent, 'check_architecture\.ps1')).Count
$guardIdx = $batContent.IndexOf("check_architecture.ps1")
$testModeIdx = $batContent.IndexOf('VOXTYPE_TEST_MODE!"=="1"')
$wiredOk = ($guardCallCount -eq 1 -and $guardIdx -ge 0 -and $testModeIdx -ge 0 -and $guardIdx -lt $testModeIdx)
Record-Result -Name "Anti-bypass: guard still wired into every build.bat run" `
    -Passed $wiredOk `
    -Details $(if ($wiredOk) { "1 call, before the --test block" } else { "calls=$guardCallCount; must be exactly 1 and must precede the --test block" })

# 3.6.4 file(GLOB) hides source-file regressions from every structural check.
$globUsed = ($cmakeContent -match 'file\s*\(\s*GLOB')
Record-Result -Name "Anti-bypass: no file(GLOB) source collection" `
    -Passed (-not $globUsed) `
    -Details $(if ($globUsed) { "file(GLOB ...) present - source lists must be explicit" } else { "explicit source lists" })

# 3.6.5 Unknown directories under src/ would silently escape the layer matrix.
$knownLayers = @("app", "asr", "audio", "core", "ui", "platform")
$srcRoot = Join-Path $RepoRoot "src"
$unknownLayers = @()
if (Test-Path $srcRoot) {
    $unknownLayers = @(Get-ChildItem -Path $srcRoot -Directory |
        Where-Object { $knownLayers -notcontains $_.Name } |
        Select-Object -ExpandProperty Name)
}
Record-Result -Name "Anti-bypass: no unknown layer directory under src/" `
    -Passed ($unknownLayers.Count -eq 0) `
    -Details "$($unknownLayers.Count) unknown layer dir(s)"
foreach ($u in $unknownLayers) { Write-Host "    ! src/$u is not in the layer matrix - new layers must be declared in the plan first" -ForegroundColor Red }

# -----------------------------------------------------------------------------
# 4. Stage gates (-Stage P1..P6). Turns each milestone DoD into a machine check.
# -----------------------------------------------------------------------------
if (-not [string]::IsNullOrWhiteSpace($Stage)) {
    Write-Host "------------------------------------------------------------" -ForegroundColor Cyan
    Write-Host " Stage gate: $Stage" -ForegroundColor Cyan
}

function Test-StageGate {
    param(
        [string]$Name,
        [scriptblock]$Condition
    )
    $detail = ""
    try {
        $detail = [string](& $Condition)
    } catch {
        $detail = $_.Exception.Message
    }
    $passed = ($detail -eq "OK")
    if ($passed) { $detail = "satisfied" }
    Record-Result -Name $Name -Passed $passed -Details $detail
}

switch ($Stage) {
    "P1" {
        Test-StageGate -Name "P1: globals.h carries no cross-layer provider headers" -Condition {
            if (-not (Test-Path $globalsPath)) { return "OK" }
            $t = Read-Text -Path $globalsPath
            $bad = @()
            foreach ($p in @('sherpa-onnx/', 'onnxruntime', 'baidu_asr.h', 'llm_refine.h', 'firered_vad.h',
                            'audio_diagnostics.h', 'wasapi_capture.h', 'input_context.h', 'qwen_free_postprocess.h')) {
                if ($t -match [regex]::Escape($p)) { $bad += $p }
            }
            if ($bad.Count -eq 0) { "OK" } else { "still includes: " + ($bad -join ", ") }
        }
    }
    "P2" {
        Test-StageGate -Name "P2: src/audio/engine.cpp dissolved into layer-owned files" -Condition {
            $missing = @()
            if (Test-Path (Join-Path $RepoRoot "src\audio\engine.cpp")) { $missing += "src/audio/engine.cpp still present" }
            if (-not (Test-Path (Join-Path $RepoRoot "src\core\path_service.h"))) { $missing += "src/core/path_service.h missing" }
            if (-not (Test-Path (Join-Path $RepoRoot "src\core\config_store.h"))) { $missing += "src/core/config_store.h missing" }
            if (-not (Test-Path (Join-Path $RepoRoot "src\asr\engine_local.h"))) { $missing += "src/asr/engine_local.h missing" }
            if ($missing.Count -eq 0) { "OK" } else { $missing -join "; " }
        }
    }
    "P3" {
        Test-StageGate -Name "P3: main.cpp under 250 lines" -Condition {
            if ($currMainLines -lt 250) { "OK" } else { "main.cpp is $currMainLines lines" }
        }
        Test-StageGate -Name "P3: hud pagination extracted and covered by an automated test" -Condition {
            $missing = @()
            if (-not (Test-Path (Join-Path $RepoRoot "src\ui\hud_pagination.h"))) { $missing += "src/ui/hud_pagination.h missing" }
            if (-not (Test-Path (Join-Path $RepoRoot "tests\hud_pagination_test.cpp"))) { $missing += "tests/hud_pagination_test.cpp missing" }
            if ($cmakeContent -notmatch 'hud_pagination_test') { $missing += "CMakeLists.txt has no hud_pagination_test target" }
            if ($batContent -notmatch 'hud_pagination_test') { $missing += "build.bat --test does not run hud_pagination_test" }
            if ($missing.Count -eq 0) { "OK" } else { $missing -join "; " }
        }
    }
    "P4" {
        Test-StageGate -Name "P4: settings.cpp under 3500 lines" -Condition {
            if ($currSettingsLines -lt 3500) { "OK" } else { "settings.cpp is $currSettingsLines lines" }
        }
        Test-StageGate -Name "P4: text injection extracted to the platform layer" -Condition {
            if (Test-Path (Join-Path $RepoRoot "src\platform\text_injector.cpp")) { "OK" } else { "src/platform/text_injector.cpp missing" }
        }
    }
    "P5" {
        Test-StageGate -Name "P5: globals.h deleted" -Condition {
            if ($globalsExists) { "src/app/globals.h still present" } else { "OK" }
        }
        Test-StageGate -Name "P5: globals includers and externs are zero" -Condition {
            if ((-not $globalsExists) -and $currIncluders -eq 0) { "OK" } else { "globals includers=$currIncluders" }
        }
    }
    "P6" {
        Test-StageGate -Name "P6: no raw pointer audio slice signatures remain" -Condition {
            $hits = @()
            foreach ($f in $allSourceFiles) {
                $t = Read-Text -Path $f.FullName
                if ($t -match 'const float\*\s*\w+\s*,\s*(size_t|int|std::size_t)') {
                    $hits += $f.FullName.Substring($RepoRoot.Length + 1)
                }
            }
            if ($hits.Count -eq 0) { "OK" } else { "$($hits.Count) file(s): " + ($hits -join ", ") }
        }
    }
}

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
Write-Host "------------------------------------------------------------" -ForegroundColor Cyan
if ($script:HasFailure) {
    Write-Host " Architecture invariant check FAILED ($($script:FailedChecks)/$($script:Checks) checks)." -ForegroundColor Red
    exit 1
} else {
    Write-Host " All architecture invariants PASSED ($($script:Checks) checks)." -ForegroundColor Green
    exit 0
}
