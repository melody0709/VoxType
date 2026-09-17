[CmdletBinding()]
param(
    [switch]$VerboseOutput
)

$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptRoot "..")).Path
Set-Location $RepoRoot

$script:HasFailure = $false

function Record-Result {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Details
    )
    if ($Passed) {
        Write-Host " [PASS] ${Name}: $Details" -ForegroundColor Green
    } else {
        Write-Host " [FAIL] ${Name}: $Details" -ForegroundColor Red
        $script:HasFailure = $true
    }
}

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " VoxType Architecture Invariants & Ratchet Check" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# -----------------------------------------------------------------------------
# 1. 检查指标基线（只减不增契约）
# -----------------------------------------------------------------------------
$BASELINES = @{
    GlobalsIncluders = 23;
    GlobalsExterns   = 91;
    MainLines        = 2759;
    SettingsLines    = 4405;
}

# 1.1 统计包含 globals.h 的文件数量
$allSourceFiles = @()
foreach ($dir in @("src", "tests", "tools")) {
    $targetDir = Join-Path $RepoRoot $dir
    if (Test-Path $targetDir) {
        $allSourceFiles += @(Get-ChildItem -Path $targetDir -Recurse -File | Where-Object { $_.Extension -in @(".h", ".cpp") })
    }
}

$globalsIncluders = @()
foreach ($file in $allSourceFiles) {
    $raw = [System.IO.File]::ReadAllText($file.FullName)
    if ($raw -match '#\s*include\s*[<"][^>"]*globals\.h[>"]') {
        $globalsIncluders += $file
    }
}

$currIncluders = $globalsIncluders.Count
$includerPassed = ($currIncluders -le $BASELINES["GlobalsIncluders"])
Record-Result -Name "globals.h Includers Ratchet" `
    -Passed $includerPassed `
    -Details "$currIncluders / $($BASELINES['GlobalsIncluders']) (current/max_allowed)"

if ($VerboseOutput -or (-not $includerPassed)) {
    foreach ($item in $globalsIncluders) {
        $rel = $item.FullName.Substring($RepoRoot.Length + 1)
        Write-Host "    - $rel" -ForegroundColor Gray
    }
}

# 1.2 统计 globals.h 中的 extern 声明条数
$globalsPath = Join-Path $RepoRoot "src\app\globals.h"
if (Test-Path $globalsPath) {
    $externLines = @(Get-Content -Path $globalsPath | Where-Object { $_ -match '^\s*extern\s+' })
    $currExterns = $externLines.Count
    $externPassed = ($currExterns -le $BASELINES["GlobalsExterns"])
    Record-Result -Name "globals.h Extern Count Ratchet" `
        -Passed $externPassed `
        -Details "$currExterns / $($BASELINES['GlobalsExterns']) (current/max_allowed)"
} else {
    Record-Result -Name "globals.h Extern Count Ratchet" `
        -Passed $true `
        -Details "globals.h eliminated! (0 / $($BASELINES['GlobalsExterns']))"
}

# 1.3 统计 src/app/main.cpp 行数
$mainPath = Join-Path $RepoRoot "src\app\main.cpp"
$currMainLines = (Get-Content -Path $mainPath).Count
$mainPassed = ($currMainLines -le $BASELINES["MainLines"])
Record-Result -Name "main.cpp Line Count Ratchet" `
    -Passed $mainPassed `
    -Details "$currMainLines / $($BASELINES['MainLines']) lines (current/max_allowed)"

# 1.4 统计 src/ui/settings.cpp 行数
$settingsPath = Join-Path $RepoRoot "src\ui\settings.cpp"
$currSettingsLines = (Get-Content -Path $settingsPath).Count
$settingsPassed = ($currSettingsLines -le $BASELINES["SettingsLines"])
Record-Result -Name "settings.cpp Line Count Ratchet" `
    -Passed $settingsPassed `
    -Details "$currSettingsLines / $($BASELINES['SettingsLines']) lines (current/max_allowed)"

# -----------------------------------------------------------------------------
# 2. 检查分层单向依赖规则（禁止反向依赖与层次倒置）
# -----------------------------------------------------------------------------
$coreViolations = @()
$coreDir = Join-Path $RepoRoot "src\core"
if (Test-Path $coreDir) {
    $coreFiles = @(Get-ChildItem -Path $coreDir -Recurse -File | Where-Object { $_.Extension -in @(".h", ".cpp") })
    foreach ($cf in $coreFiles) {
        $raw = [System.IO.File]::ReadAllText($cf.FullName)
        $rel = $cf.FullName.Substring($RepoRoot.Length + 1)
        if ($raw -match '#\s*include\s*[<"](asr|audio|ui|app|platform)/') {
            $coreViolations += "$rel includes upper layer"
        }
        if ($raw -match '#\s*include\s*[<"][^>"]*globals\.h[>"]') {
            $coreViolations += "$rel includes globals.h"
        }
    }
}
$corePassed = ($coreViolations.Count -eq 0)
Record-Result -Name "Layer Rule: src/core Anti-Dependency" `
    -Passed $corePassed `
    -Details "$($coreViolations.Count) violation(s)"

if (-not $corePassed) {
    foreach ($v in $coreViolations) {
        Write-Host "    ! $v" -ForegroundColor Red
    }
}

# -----------------------------------------------------------------------------
# 3. 检查安全与构建设计意图（设计契约不倒退）
# -----------------------------------------------------------------------------
$cmakePath = Join-Path $RepoRoot "CMakeLists.txt"
$cmakeContent = [System.IO.File]::ReadAllText($cmakePath)
$prodMatch = [regex]::Match($cmakeContent, 'add_executable\(VoxType\s+WIN32[\s\S]*?\n\)')
$signLinkedToProd = $false
if ($prodMatch.Success) {
    if ($prodMatch.Value -match 'qwen_free_proto_sign\.cpp') {
        $signLinkedToProd = $true
    }
}
$signPassed = (-not $signLinkedToProd)
$signDetail = if ($signPassed) { "Correctly excluded from VoxType target" } else { "LEAKED into production VoxType target!" }
Record-Result -Name "Security Contract: qwen_free_proto_sign isolated from production" `
    -Passed $signPassed `
    -Details $signDetail

# -----------------------------------------------------------------------------
# 汇总判断
# -----------------------------------------------------------------------------
Write-Host "------------------------------------------------------------" -ForegroundColor Cyan
if ($script:HasFailure) {
    Write-Host " Architecture invariant check FAILED. Please resolve regressions." -ForegroundColor Red
    exit 1
} else {
    Write-Host " All architecture invariants PASSED. Ready to proceed." -ForegroundColor Green
    exit 0
}
