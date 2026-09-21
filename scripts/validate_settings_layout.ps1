[CmdletBinding()]
param(
    [string]$SourceRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $SourceRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
}
$ErrorActionPreference = 'Stop'

$uiTypesPath = Join-Path $SourceRoot 'src\ui\ui_types.h'
$globalsPath = Join-Path $SourceRoot 'src\app\globals.h'
$layoutConstPath = if (Test-Path -LiteralPath $uiTypesPath -PathType Leaf) { $uiTypesPath } else { $globalsPath }
$settingsPath = Join-Path $SourceRoot 'src\ui\settings.cpp'
foreach ($path in @($layoutConstPath, $settingsPath)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Settings layout source is missing: $path" }
}
$globals = Get-Content -LiteralPath $layoutConstPath -Raw -Encoding UTF8
$settingsFiles = @($settingsPath) + @(Get-ChildItem -Path (Join-Path $SourceRoot 'src\ui') -Recurse -File -Filter *.cpp | Where-Object { $_.FullName -ne $settingsPath } | Select-Object -ExpandProperty FullName)
$probePath = Join-Path $SourceRoot 'src\app\asr_probe_service_impl.cpp'
if (Test-Path -LiteralPath $probePath -PathType Leaf) { $settingsFiles += $probePath }
$settings = ($settingsFiles | ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 }) -join "`n"

function Get-UiInt([string]$Name) {
    $match = [regex]::Match($globals, 'constexpr\s+int\s+' + [regex]::Escape($Name) + '\s*=\s*([0-9]+)\s*;')
    if (!$match.Success) { throw "UiStyle::$Name must remain a numeric layout constant" }
    [int]$match.Groups[1].Value
}
function Scale([int]$DesignPx, [double]$Scale) { [int][Math]::Ceiling($DesignPx * $Scale) }

$firstRowY = Get-UiInt 'FirstRowY'
$settingsWindowWidth = Get-UiInt 'SettingsWindowW'
$settingsWindowHeight = Get-UiInt 'SettingsWindowH'
$settingsNonClientReserveHeight = Get-UiInt 'SettingsWindowNonClientReserveH'
$shortcutHeight = Get-UiInt 'GeneralShortcutGroupH'
$startupGap = Get-UiInt 'GeneralStartupGroupGap'
$startupHeight = Get-UiInt 'GeneralStartupGroupH'
$diagnosticsGap = Get-UiInt 'GeneralDiagnosticsGroupGap'
$diagnosticsHeight = Get-UiInt 'GeneralDiagnosticsGroupH'
$diagnosticsModeWidth = Get-UiInt 'GeneralDiagnosticsModeW'
$diagnosticsOpenWidth = Get-UiInt 'GeneralDiagnosticsOpenButtonW'
$diagnosticsDeleteWidth = Get-UiInt 'GeneralDiagnosticsDeleteButtonW'
$diagnosticsButtonGap = Get-UiInt 'GeneralDiagnosticsButtonGap'
$diagnosticsHintWidth = Get-UiInt 'GeneralDiagnosticsHintW'
$groupX = Get-UiInt 'GeneralGroupX'
$groupWidth = Get-UiInt 'GeneralGroupW'
$contentLeft = Get-UiInt 'ContentLeft'
$inputLeft = Get-UiInt 'InputLeft'
$hintWidth = Get-UiInt 'GeneralStartupHintW'
$footerMinTop = Get-UiInt 'FooterMinTop'
$footerHeight = Get-UiInt 'FooterHeight'
$margin = Get-UiInt 'Margin'
$qwenAdvancedHintY = Get-UiInt 'QwenAdvancedHintY'
$qwenHintH = Get-UiInt 'QwenHintH'
$qwenDialogW = Get-UiInt 'QwenAdvancedDialogW'
$qwenDialogH = Get-UiInt 'QwenAdvancedDialogH'
$qwenDialogNonClientReserveH = Get-UiInt 'QwenAdvancedDialogNonClientReserveH'
$qwenDialogInputLeft = Get-UiInt 'QwenAdvancedDialogInputLeft'
$qwenDialogInputW = Get-UiInt 'QwenAdvancedDialogInputW'
$qwenDialogSpecialY = Get-UiInt 'QwenAdvancedDialogSpecialY'
$qwenDialogSpecialH = Get-UiInt 'QwenAdvancedDialogSpecialH'
$qwenDialogFooterY = Get-UiInt 'QwenAdvancedDialogFooterY'
$actionButtonH = Get-UiInt 'ActionBtnH'
$inputWidthFull = Get-UiInt 'InputWFull'
$rowHeight = Get-UiInt 'RowHeight'
$qwenHint2LineHeight = Get-UiInt 'QwenHint2LineH'
$inputDlgW = Get-UiInt 'InputDlgW'
$inputDlgH = Get-UiInt 'InputDlgH'
$inputDlgEditW = Get-UiInt 'InputDlgEditW'
$inputDlgBtnY = Get-UiInt 'InputDlgBtnY'
$volcDlgW = Get-UiInt 'VolcExtraDlgW'
$volcDlgH = Get-UiInt 'VolcExtraDlgH'
$volcDlgEditW = Get-UiInt 'VolcExtraDlgEditW'
$volcDlgEditH = Get-UiInt 'VolcExtraDlgEditH'
$volcDlgBtnY = Get-UiInt 'VolcExtraDlgBtnY'
$promptDlgW = Get-UiInt 'PromptDlgW'
$promptDlgH = Get-UiInt 'PromptDlgH'
$promptDlgEditW = Get-UiInt 'PromptDlgEditW'
$promptDlgEditH = Get-UiInt 'PromptDlgEditH'
$promptDlgBtnY = Get-UiInt 'PromptDlgBtnY'

$startupY = $firstRowY + $shortcutHeight + $startupGap
$startupBottom = $startupY + $startupHeight
$diagnosticsY = $startupBottom + $diagnosticsGap
$diagnosticsBottom = $diagnosticsY + $diagnosticsHeight
if ($diagnosticsBottom -gt ($footerMinTop - 4)) {
    throw "General diagnostics group reaches the footer: bottom=$diagnosticsBottom, contentBottom=$($footerMinTop - 4)"
}
if (($groupX + $groupWidth) -gt ($settingsWindowWidth - $margin)) {
    throw 'General group exceeds the Settings design width'
}
if (($contentLeft + $hintWidth) -gt ($groupX + $groupWidth)) {
    throw 'Startup explanatory text exceeds the General group width'
}
if (($inputLeft + $diagnosticsModeWidth) -gt ($groupX + $groupWidth)) {
    throw 'Diagnostics mode selector exceeds the General group width'
}
if (($contentLeft + $diagnosticsOpenWidth + $diagnosticsButtonGap + $diagnosticsDeleteWidth) -gt ($groupX + $groupWidth)) {
    throw 'Diagnostics action buttons exceed the General group width'
}
if ($diagnosticsOpenWidth -lt 220 -or $diagnosticsDeleteWidth -lt 240) {
    throw 'Diagnostics action buttons do not leave enough room for their English labels'
}
if (($contentLeft + $diagnosticsHintWidth) -gt ($groupX + $groupWidth)) {
    throw 'Diagnostics privacy hint exceeds the General group width'
}
if (($qwenAdvancedHintY + (2 * $qwenHintH)) -gt ($footerMinTop - 4)) {
    throw 'Qwen basic settings reach the footer'
}
if (($qwenDialogInputLeft + $qwenDialogInputW) -gt ($qwenDialogW - $margin)) {
    throw 'Qwen Advanced input fields exceed the dialog width'
}
$qwenDialogClientBottom = $qwenDialogH - $qwenDialogNonClientReserveH
$settingsClientBottom = $settingsWindowHeight - $settingsNonClientReserveHeight
$minimumFooterRoom = $actionButtonH + (2 * $margin)
if (($settingsClientBottom - $footerMinTop) -lt $minimumFooterRoom) {
    throw "Settings window does not leave enough client height for footer buttons: available=$($settingsClientBottom - $footerMinTop), required=$minimumFooterRoom"
}
if ($footerHeight -lt $minimumFooterRoom) {
    throw 'Settings footer height does not leave vertical padding around its buttons'
}
if (($qwenDialogSpecialY + $qwenDialogSpecialH) -gt ($qwenDialogFooterY - 8)) {
    throw 'Qwen Advanced sensitive-word fields overlap the footer buttons'
}
if (($qwenDialogFooterY + $actionButtonH) -gt ($qwenDialogClientBottom - $margin)) {
    throw 'Qwen Advanced footer buttons exceed the estimated client area'
}
$maiHintBottom = $firstRowY + (5 * $rowHeight) + $qwenHint2LineHeight
if ($maiHintBottom -gt ($footerMinTop - 4)) {
    throw 'MAI settings hint reaches the footer'
}
if (($inputLeft + $inputWidthFull) -gt ($settingsWindowWidth - $margin)) {
    throw 'MAI Azure Endpoint field exceeds the Settings design width'
}
if (($inputDlgEditW + 36) -gt $inputDlgW) {
    throw 'Input dialog edit field exceeds the dialog width'
}
if (($inputDlgBtnY + $actionButtonH) -gt ($inputDlgH - 48)) {
    throw 'Input dialog buttons exceed the estimated client area'
}
if (($volcDlgEditW + 36) -gt $volcDlgW) {
    throw 'Volcengine extra edit field exceeds the dialog width'
}
if (($volcDlgBtnY + $actionButtonH) -gt ($volcDlgH - 48)) {
    throw 'Volcengine extra dialog buttons exceed the estimated client area'
}
if (($promptDlgEditW + 60) -gt $promptDlgW) {
    throw 'Prompt dialog edit field exceeds the dialog width'
}
if (($promptDlgBtnY + $actionButtonH) -gt ($promptDlgH - 48)) {
    throw 'Prompt dialog buttons exceed the estimated client area'
}
$llmActionBottom = $firstRowY + (7 * $rowHeight) + 6 + $actionButtonH
if ($llmActionBottom -gt ($footerMinTop - 4)) {
    throw 'LLM settings action row reaches the footer'
}

foreach ($dpi in @(96, 144, 192, 288)) {
    $scale = $dpi / 144.0
    if ((Scale $diagnosticsBottom $scale) -gt (Scale ($footerMinTop - 4) $scale)) {
        throw "Diagnostics group overlaps the footer at $dpi DPI"
    }
    if ((Scale ($groupX + $groupWidth) $scale) -gt (Scale ($settingsWindowWidth - $margin) $scale)) {
        throw "General group overflows the Settings width at $dpi DPI"
    }
    if ((Scale ($qwenAdvancedHintY + (2 * $qwenHintH)) $scale) -gt (Scale ($footerMinTop - 4) $scale)) {
        throw "Qwen basic settings overlap the footer at $dpi DPI"
    }
    if ((Scale ($qwenDialogInputLeft + $qwenDialogInputW) $scale) -gt (Scale ($qwenDialogW - $margin) $scale)) {
        throw "Qwen Advanced inputs overflow the dialog at $dpi DPI"
    }
    if ((Scale ($qwenDialogSpecialY + $qwenDialogSpecialH) $scale) -gt (Scale ($qwenDialogFooterY - 8) $scale)) {
        throw "Qwen Advanced sensitive-word fields overlap the footer at $dpi DPI"
    }
    if ((Scale ($qwenDialogFooterY + $actionButtonH) $scale) -gt (Scale ($qwenDialogClientBottom - $margin) $scale)) {
        throw "Qwen Advanced footer overflows the estimated client area at $dpi DPI"
    }
    if ((Scale ($inputDlgEditW + 36) $scale) -gt (Scale $inputDlgW $scale)) {
        throw "Input dialog edit field exceeds the dialog width at $dpi DPI"
    }
    if ((Scale ($inputDlgBtnY + $actionButtonH) $scale) -gt (Scale ($inputDlgH - 48) $scale)) {
        throw "Input dialog buttons exceed the estimated client area at $dpi DPI"
    }
    if ((Scale ($volcDlgEditW + 36) $scale) -gt (Scale $volcDlgW $scale)) {
        throw "Volcengine extra edit field exceeds the dialog width at $dpi DPI"
    }
    if ((Scale ($volcDlgBtnY + $actionButtonH) $scale) -gt (Scale ($volcDlgH - 48) $scale)) {
        throw "Volcengine extra dialog buttons exceed the estimated client area at $dpi DPI"
    }
    if ((Scale ($promptDlgEditW + 60) $scale) -gt (Scale $promptDlgW $scale)) {
        throw "Prompt dialog edit field exceeds the dialog width at $dpi DPI"
    }
    if ((Scale ($promptDlgBtnY + $actionButtonH) $scale) -gt (Scale ($promptDlgH - 48) $scale)) {
        throw "Prompt dialog buttons exceed the estimated client area at $dpi DPI"
    }
    if ((Scale $llmActionBottom $scale) -gt (Scale ($footerMinTop - 4) $scale)) {
        throw "LLM settings action row overlaps the footer at $dpi DPI"
    }
    if ((Scale $maiHintBottom $scale) -gt (Scale ($footerMinTop - 4) $scale)) {
        throw "MAI settings overlap the footer at $dpi DPI"
    }
    if ((Scale ($inputLeft + $inputWidthFull) $scale) -gt (Scale ($settingsWindowWidth - $margin) $scale)) {
        throw "MAI Azure Endpoint field overflows the Settings width at $dpi DPI"
    }
}

function Strip-Comments([string]$code) {
    $noBlock = [regex]::Replace($code, '(?s)/\*.*?\*/', '')
    return [regex]::Replace($noBlock, '//[^\r\n]*', '')
}

$cleanGlobals = Strip-Comments $globals
$cleanSettings = Strip-Comments $settings

foreach ($required in @(
        'IDC_START_WITH_WINDOWS',
        'AddGeneralControl(startupCheckbox)',
        'RefreshStartupRegistrationControl(g_settingsWindow, true)',
        'SaveStartupRegistrationControl(hwnd)',
        'IDC_DIAGNOSTIC_AUDIO_MODE',
        'AddGeneralControl(diagnosticsGroup)',
        'DiagnosticAudioModeFromControl(hwnd)',
        'OpenDiagnosticAudioFolder(hwnd)',
        'DeleteDiagnosticAudioFiles(hwnd)',
        'IDC_QWEN_ADVANCED',
        'ShowQwenAdvancedDialog(hwnd, data)',
        'IDC_MAI_API_PROVIDER',
        'AddMaiControl(CreateCombo(hwnd, IDC_MAI_API_PROVIDER',
        'ShowMaiApiSubPage(hwnd)',
        'cfg.maiOpenRouterApiKey = QwenControlText(',
        'mai_transcribe::TestConnection')) {
    if (!$cleanGlobals.Contains($required) -and !$cleanSettings.Contains($required)) {
        throw "Startup Settings wiring is missing: $required"
    }
}

Write-Output 'Validated Settings and Qwen Advanced layouts at 96/144/192/288 DPI design scales.'
