[CmdletBinding()]
param(
    [switch]$VerboseOutput,
    [ValidateSet("", "P1", "P2", "P3", "P4", "P5", "P6")]
    [string]$Stage = "",
    [string]$Root = ""
)

# Keep this script ASCII-only so it also works when run directly under legacy
# Windows PowerShell 5.1. build.bat now invokes PowerShell 7. Source line counts
# still use .NET ReadAllLines rather than shell-dependent text enumeration.

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
    SettingsLines           = 400
    LayerViolations         = 2
    GlobalsCrossLayerHeader = 0
    # Contract: every src module has an entry in the architecture documents.
    # Measured 2026-10-12 as UNIQUE-MODULE counts (basename without extension,
    # whole-word match), after the grouped ui/tabs and ui/providers rows were made
    # to name their members. The pre-existing undocumented modules are a named
    # debt in AGENTS.md; each round should ratchet these numbers DOWN, and the
    # ratchet only forbids NEW undocumented modules. `src/app/resource.*` is
    # build-derived and stays out.
    DocEntriesMissingEn     = 27
    DocEntriesMissingZh     = 34
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
# 3.5 Contract: every src module is listed in both architecture documents.
#     A new module that never reaches the file table is a silent DoD miss (the
#     build stays green while the written contract rots), and the two language
#     versions can rot independently, so both are checked.
#
#     Counted MODULES (unique basename without extension), not files, so a .h and
#     .cpp pair counts once. A module counts as documented when its basename
#     appears as a whole word - not inside a longer identifier - so a grouped row
#     like "`src/ui/tabs/` | panels: `tab_general`, ..." satisfies the check by
#     naming its members, while a name buried inside another identifier does not.
#     tests/ and the build-derived resource.* stay out. The known undocumented
#     modules are a named debt in AGENTS.md ("architecture documentation debt").
# -----------------------------------------------------------------------------
$archDocMissing = @()
$archZhDocMissing = @()
$archDocPath = Join-Path $PSScriptRoot "..\ARCHITECTURE.md"
$archZhDocPath = Join-Path $PSScriptRoot "..\doc\ARCHITECTURE_zh.md"
$contractSources = @(Get-ChildItem -Path (Join-Path $PSScriptRoot "..\src") -Recurse -Include *.h,*.cpp -File |
    Where-Object { $_.Name -notmatch '^resource\.' })
$contractModules = @($contractSources | ForEach-Object { [System.IO.Path]::GetFileNameWithoutExtension($_.Name) } |
    Sort-Object -Unique)
$archDocText = if (Test-Path $archDocPath) { [System.IO.File]::ReadAllText((Resolve-Path $archDocPath).Path) } else { "" }
$archZhDocText = if (Test-Path $archZhDocPath) { [System.IO.File]::ReadAllText((Resolve-Path $archZhDocPath).Path) } else { "" }
foreach ($m in $contractModules) {
    $moduleRe = '(?<![A-Za-z0-9_])' + [regex]::Escape($m) + '(?![A-Za-z0-9_])'
    if ($archDocText -notmatch $moduleRe) { $archDocMissing += $m }
    if ($archZhDocText -notmatch $moduleRe) { $archZhDocMissing += $m }
}

Record-Result -Name "Contract: src modules listed in ARCHITECTURE.md" `
    -Passed ($archDocMissing.Count -le $BASELINES["DocEntriesMissingEn"]) `
    -Details "$($archDocMissing.Count) / $($BASELINES['DocEntriesMissingEn']) missing (current/max_allowed)"
if ($VerboseOutput -or $archDocMissing.Count -gt $BASELINES["DocEntriesMissingEn"]) {
    foreach ($m in $archDocMissing) { Write-Host "    ! ARCHITECTURE.md lacks: $m" -ForegroundColor Red }
}

Record-Result -Name "Contract: src modules listed in doc/ARCHITECTURE_zh.md" `
    -Passed ($archZhDocMissing.Count -le $BASELINES["DocEntriesMissingZh"]) `
    -Details "$($archZhDocMissing.Count) / $($BASELINES['DocEntriesMissingZh']) missing (current/max_allowed)"
if ($VerboseOutput -or $archZhDocMissing.Count -gt $BASELINES["DocEntriesMissingZh"]) {
    foreach ($m in $archZhDocMissing) { Write-Host "    ! ARCHITECTURE_zh.md lacks: $m" -ForegroundColor Red }
}

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

# 3.6.6 Privacy wiring: the shared recognition history is uploaded to the cloud
# by every context consumer, so a transcript dictated into a password control
# must be excluded WHEN IT IS WRITTEN. Filtering later (while preparing a
# request) leaves the "dictated while the switch was off, switch turned on
# later" path open, and the same history is read by more than one provider.
$privacyMissing = @()
$dispatcherPath = Join-Path $RepoRoot "src\asr\asr_dispatcher.cpp"
$attemptPath = Join-Path $RepoRoot "src\app\asr_attempt_manager.cpp"
if (Test-Path $dispatcherPath) {
    $dispatcher = Read-Text -Path $dispatcherPath
    if ($dispatcher -notmatch 'ShouldRecordHistory') { $privacyMissing += "asr_dispatcher.cpp does not gate the history write through ShouldRecordHistory" }
    if ($dispatcher -notmatch 'MustSkipHistoryForFocus') { $privacyMissing += "asr_dispatcher.cpp does not apply the fail-closed focus mapping" }
    if ($dispatcher -notmatch 'SensitiveFocusAnswerName') { $privacyMissing += "asr_dispatcher.cpp does not log which answer skipped the write" }
} else {
    $privacyMissing += "src/asr/asr_dispatcher.cpp missing"
}
# The probe's Safe answer must come from pure tri-state mappings: a failed UIA
# password query or a missing start-of-recording focus control counts as unknown,
# never as "not sensitive".
$inputContextPath = Join-Path $RepoRoot "src\core\input_context.h"
if (Test-Path $inputContextPath) {
    $inputContext = Read-Text -Path $inputContextPath
    if ($inputContext -notmatch 'PasswordQueryState') { $privacyMissing += "input_context.h has no tri-state password property read" }
    if ($inputContext -notmatch 'SensitiveProbeAnswerFromUia') { $privacyMissing += "input_context.h maps UIA results without SensitiveProbeAnswerFromUia" }
    if ($inputContext -notmatch 'InitialSensitiveProbeAnswer') { $privacyMissing += "input_context.h decides the start snapshot without InitialSensitiveProbeAnswer" }
    if ($inputContext -notmatch 'kSensitiveProbeUnknownPasswordQuery') { $privacyMissing += "input_context.h does not treat a failed password read as unknown" }
    if ($inputContext -notmatch 'kSensitiveProbeUnknownNoStartFocus') { $privacyMissing += "input_context.h does not treat a missing start focus as unknown" }
} else {
    $privacyMissing += "src/core/input_context.h missing"
}
if (Test-Path $attemptPath) {
    $attempt = Read-Text -Path $attemptPath
    if ($attempt -notmatch 'BeginSensitiveFocusProbe') {
        $privacyMissing += "asr_attempt_manager.cpp does not start the sensitive-focus probe"
    } else {
        # Timing: the probe must be started before the focused-field read, because
        # that read can wait for its UI Automation timeout and the probe must
        # cross-check the focus it captured at recording start.
        $probeIndex = $attempt.IndexOf('BeginSensitiveFocusProbe')
        $fieldReadIndex = $attempt.IndexOf('qwenInputContextSnapshotCaptured = true')
        if ($fieldReadIndex -ge 0 -and $fieldReadIndex -lt $probeIndex) {
            $privacyMissing += "asr_attempt_manager.cpp starts the sensitive probe after the focused-field read"
        }
    }
} else {
    $privacyMissing += "src/app/asr_attempt_manager.cpp missing"
}
Record-Result -Name "Privacy: sendable history gated at write time" `
    -Passed ($privacyMissing.Count -eq 0) `
    -Details $(if ($privacyMissing.Count -eq 0) { "probe + write-time gate wired" } else { "$($privacyMissing.Count) missing wiring" })
foreach ($p in $privacyMissing) { Write-Host "    ! $p" -ForegroundColor Red }

# 3.6.6b Browser accessibility isolation. Chromium ORs every platform
# accessibility request into a PROCESS-wide mode, and releasing our objects does
# not clear it: only a browser restart restores performance. "Reduce the query
# count" therefore cannot fix it, and the gate has to be structural - both
# context modules must classify the target as a browser and route it to the
# companion bridge BEFORE the first accessibility call, and a missing companion
# must stay fail-closed (an unknown probe answer).
$browserGateMissing = @()
$inputContextGatePath = Join-Path $RepoRoot "src\core\input_context.h"
if (Test-Path $inputContextGatePath) {
    $gateText = Read-Text -Path $inputContextGatePath
    foreach ($needle in @('browser_target::IdentifyWindow',
                          'browser_bridge::Bridge::Instance().Query',
                          'SensitiveProbeAnswerFromBrowser',
                          'kSensitiveProbeUnknownBrowserNotConnected',
                          'kSensitiveProbeUnknownBrowserIdentity')) {
        if ($gateText -notmatch [regex]::Escape($needle)) {
            $browserGateMissing += "input_context.h lacks $needle"
        }
    }
    # The first classification must precede the first accessibility call, so the
    # browser short-circuit cannot drift below EnsureAccessibilityTree again.
    $gateIndex = $gateText.IndexOf('browser_target::IdentifyWindow')
    $accessibilityIndex = $gateText.IndexOf('EnsureAccessibilityTree(fgWnd)')
    if ($gateIndex -lt 0 -or $accessibilityIndex -lt 0 -or $gateIndex -gt $accessibilityIndex) {
        $browserGateMissing += "input_context.h reaches EnsureAccessibilityTree(fgWnd) before the browser gate"
    }
    if ($gateText -match '->(?:GetFocusedElement|ElementFromPoint)\s*\(') {
        $browserGateMissing += "input_context.h uses global UIA focus/point queries that can switch into a browser"
    }
    if ($gateText -notmatch 'accessibility_focus::FocusedElementWithinWindow') {
        $browserGateMissing += "input_context.h lacks HWND-scoped focus queries"
    }
} else {
    $browserGateMissing += "src/core/input_context.h missing"
}
Record-Result -Name "Contract: browser targets never reach accessibility from input_context.h" `
    -Passed ($browserGateMissing.Count -eq 0) `
    -Details $(if ($browserGateMissing.Count -eq 0) { "gate precedes accessibility read" } else { "$($browserGateMissing.Count) missing gate(s)" })
foreach ($g in $browserGateMissing) { Write-Host "    ! $g" -ForegroundColor Red }

$selectionGateMissing = @()
$selectionGatePath = Join-Path $RepoRoot "src\core\selection_context.h"
if (Test-Path $selectionGatePath) {
    $gateText = Read-Text -Path $selectionGatePath
    foreach ($needle in @('IsBrowserTarget', 'kBrowserSkippedReason')) {
        if ($gateText -notmatch [regex]::Escape($needle)) {
            $selectionGateMissing += "selection_context.h lacks $needle"
        }
    }
    $gateIndex = $gateText.IndexOf('IsBrowserTarget(focusWindow)')
    $accessibilityIndex = $gateText.IndexOf('CLSID_CUIAutomation')
    if ($gateIndex -lt 0 -or $accessibilityIndex -lt 0 -or $gateIndex -gt $accessibilityIndex) {
        $selectionGateMissing += "selection_context.h creates a UIA client before the browser gate"
    }
    if ($gateText -match '->(?:GetFocusedElement|ElementFromPoint)\s*\(') {
        $selectionGateMissing += "selection_context.h uses global UIA focus/point queries that can switch into a browser"
    }
    if ($gateText -notmatch 'accessibility_focus::FocusedElementWithinWindow') {
        $selectionGateMissing += "selection_context.h lacks HWND-scoped focus queries"
    }
} else {
    $selectionGateMissing += "src/core/selection_context.h missing"
}
Record-Result -Name "Contract: browser targets never reach accessibility from selection_context.h" `
    -Passed ($selectionGateMissing.Count -eq 0) `
    -Details $(if ($selectionGateMissing.Count -eq 0) { "gate precedes UIA client creation" } else { "$($selectionGateMissing.Count) missing gate(s)" })
foreach ($g in $selectionGateMissing) { Write-Host "    ! $g" -ForegroundColor Red }

# 3.6.6c The companion channel has two contracts that must hold together:
#   - identity: the host name and the extension id are written in four artifacts
#     (browser_bridge.h, the extension manifest's `key`, the host manifest's
#     allowed_origins, worker.js), and a rotation in one of them silently turns
#     the whole channel into "no context". The id is therefore re-derived from the
#     manifest `key` here instead of being trusted.
#   - privacy: the answer carries a field value, so a password value must never
#     reach this process. The extension has its own structural guard; this is the
#     desktop-side last line of defence.
$bridgePrivacyMissing = @()
$bridgeGuardPath = Join-Path $RepoRoot "src\core\browser_bridge.h"
$extensionManifestPath = Join-Path $RepoRoot "extensions\browser-bridge\manifest.json"
$workerPath = Join-Path $RepoRoot "extensions\browser-bridge\worker.js"
if (Test-Path $bridgeGuardPath) {
    $bridgeText = Read-Text -Path $bridgeGuardPath
    if ($bridgeText -notmatch [regex]::Escape('need == Need::Field && !result.isPassword')) {
        $bridgePrivacyMissing += "browser_bridge.h extracts the field value without the isPassword gate"
    }
    $hostNameMatch = [regex]::Match($bridgeText, 'kHostName\[\]\s*=\s*L"([^"]+)"')
    $idMatch = [regex]::Match($bridgeText, 'kExtensionIdUtf8\[\]\s*=\s*"([^"]+)"')
    if (!$hostNameMatch.Success) { $bridgePrivacyMissing += "browser_bridge.h has no pinned host name" }
    if (!$idMatch.Success) { $bridgePrivacyMissing += "browser_bridge.h has no pinned extension id" }
    # A CLOSED shadow root is indistinguishable from a plain non-editable element
    # from the outside, so "it does not look like a password" must never be enough
    # for Safe: the classification has to require a positively identified editable
    # control.
    if ($bridgeText -notmatch [regex]::Escape('!textInputIdentified')) {
        $bridgePrivacyMissing += "browser_bridge.h may answer Safe without identifying an editable control"
    }
    if ($hostNameMatch.Success) {
        $hostName = $hostNameMatch.Groups[1].Value
        if (!(Test-Path $workerPath) -or (Read-Text -Path $workerPath) -notmatch [regex]::Escape($hostName)) {
            $bridgePrivacyMissing += "worker.js does not use the pinned host name"
        }
        $hostManifestPath = Join-Path $RepoRoot "packaging\native-messaging\$hostName.json"
        if (!(Test-Path $hostManifestPath)) {
            $bridgePrivacyMissing += "native messaging manifest missing for '$hostName'"
        } elseif ($idMatch.Success) {
            $origin = "chrome-extension://$($idMatch.Groups[1].Value)/"
            if ((Read-Text -Path $hostManifestPath) -notmatch [regex]::Escape($origin)) {
                $bridgePrivacyMissing += "the native messaging manifest does not allow the pinned extension id"
            }
        }
    }
    if ($idMatch.Success) {
        # The extension id must really be derived from the manifest `key`, or
        # allowed_origins would pin an id the browser never assigns.
        if (!(Test-Path $extensionManifestPath)) {
            $bridgePrivacyMissing += "extensions/browser-bridge/manifest.json missing"
        } else {
            $keyMatch = [regex]::Match((Read-Text -Path $extensionManifestPath), '"key"\s*:\s*"([^"]+)"')
            if (!$keyMatch.Success) {
                $bridgePrivacyMissing += "the extension manifest has no `key`, so its id is not stable"
            } else {
                $derived = -join ([System.Security.Cryptography.SHA256]::HashData(
                        [Convert]::FromBase64String($keyMatch.Groups[1].Value))[0..15] |
                    ForEach-Object { 'abcdefghijklmnop'[($_ -shr 4)]; 'abcdefghijklmnop'[($_ -band 0x0f)] })
                if ($derived -ne $idMatch.Groups[1].Value) {
                    $bridgePrivacyMissing += "the manifest key derives id '$derived', not the pinned id"
                }
            }
        }
    }
} else {
    $bridgePrivacyMissing += "src/core/browser_bridge.h missing"
}
Record-Result -Name "Contract: browser companion identity pinned and never yields a password value" `
    -Passed ($bridgePrivacyMissing.Count -eq 0) `
    -Details $(if ($bridgePrivacyMissing.Count -eq 0) { "host/id derived and pinned; value gated on isPassword" } else { "$($bridgePrivacyMissing.Count) missing guard(s)" })
foreach ($g in $bridgePrivacyMissing) { Write-Host "    ! $g" -ForegroundColor Red }

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
