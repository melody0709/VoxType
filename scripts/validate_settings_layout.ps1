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
$uiCppPath = Join-Path $SourceRoot 'src\ui'
$uiCppFiles = @(Get-ChildItem -Path $uiCppPath -Recurse -File -Filter *.cpp | Select-Object -ExpandProperty FullName)
$uiHeaderFiles = @(Get-ChildItem -Path $uiCppPath -Recurse -File -Filter *.h | Select-Object -ExpandProperty FullName)
$settingsFiles = @($settingsPath) + @($uiCppFiles | Where-Object { $_ -ne $settingsPath })
$probePath = Join-Path $SourceRoot 'src\app\asr_probe_service_impl.cpp'
if (Test-Path -LiteralPath $probePath -PathType Leaf) { $settingsFiles += $probePath }
$settings = ($settingsFiles | ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 }) -join "`n"
$settingsWithHeaders = ($settingsFiles + $uiHeaderFiles | Select-Object -Unique |
    ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 }) -join "`n"

function Get-UiInt([string]$Name) {
    $match = [regex]::Match($globals, 'constexpr\s+int\s+' + [regex]::Escape($Name) + '\s*=\s*([0-9]+)\s*;')
    if (!$match.Success) { throw "UiStyle::$Name must remain a numeric layout constant" }
    [int]$match.Groups[1].Value
}
function Scale([int]$DesignPx, [double]$Scale) { [int][Math]::Ceiling($DesignPx * $Scale) }

$firstRowY = Get-UiInt 'FirstRowY'
$rowHeight = Get-UiInt 'RowHeight'
$labelHeight = Get-UiInt 'LabelH'
$editHeight = Get-UiInt 'EditH'
$checkHeight = Get-UiInt 'CheckH'
$actionButtonH = Get-UiInt 'ActionBtnH'
$settingsWindowWidth = Get-UiInt 'SettingsWindowW'
$settingsWindowHeight = Get-UiInt 'SettingsWindowH'
$settingsNonClientReserveHeight = Get-UiInt 'SettingsWindowNonClientReserveH'
$margin = Get-UiInt 'Margin'
$footerMinTop = Get-UiInt 'FooterMinTop'
$footerHeight = Get-UiInt 'FooterHeight'
$contentLeft = Get-UiInt 'ContentLeft'
$inputLeft = Get-UiInt 'InputLeft'
$labelWidth = Get-UiInt 'LabelWidth'
$groupX = Get-UiInt 'GeneralGroupX'
$groupWidth = Get-UiInt 'GeneralGroupW'

# Tab 1 (General & Input)
$shortcutHeight = Get-UiInt 'GeneralShortcutGroupH'
$inputGroupGap = Get-UiInt 'GeneralInputGroupGap'
$inputGroupH = Get-UiInt 'GeneralInputGroupH'
$startupGap = Get-UiInt 'GeneralStartupGroupGap'
$startupHeight = Get-UiInt 'GeneralStartupGroupH'
$startupHintOffsetY = Get-UiInt 'GeneralStartupHintOffsetY'
$hintWidth = Get-UiInt 'GeneralStartupHintW'

# Tab 2 (Speech Engine): Row 0 routing and the Qwen / Volcano compact grids
$primaryBackendComboW = Get-UiInt 'PrimaryBackendComboW'
$fallbackLabelX = Get-UiInt 'FallbackLabelX'
$fallbackComboX = Get-UiInt 'FallbackComboX'
$fallbackComboW = Get-UiInt 'FallbackComboW'
$qwenHintH = Get-UiInt 'QwenHintH'
$qwenHint2LineHeight = Get-UiInt 'QwenHint2LineH'
$qwenHintW = Get-UiInt 'QwenHintW'
$qwenKeyY = Get-UiInt 'QwenKeyY'
$qwenKeyEditW = Get-UiInt 'QwenKeyEditW'
$qwenShowBtnX = Get-UiInt 'QwenShowBtnX'
$qwenShowBtnW = Get-UiInt 'QwenShowBtnW'
$qwenUrlY = Get-UiInt 'QwenUrlY'
$qwenUrlEditW = Get-UiInt 'QwenUrlEditW'
$qwenModelY = Get-UiInt 'QwenModelY'
$qwenModelComboW = Get-UiInt 'QwenModelComboW'
$qwenLogBtnX = Get-UiInt 'QwenLogBtnX'
$qwenLogBtnW = Get-UiInt 'QwenLogBtnW'
$qwenLanguageY = Get-UiInt 'QwenLanguageY'
$qwenLanguageComboW = Get-UiInt 'QwenLanguageComboW'
$qwenHintsLabelX = Get-UiInt 'QwenHintsLabelX'
$qwenHintsEditX = Get-UiInt 'QwenHintsEditX'
$qwenHintsEditW = Get-UiInt 'QwenHintsEditW'
$qwenHintsResetX = Get-UiInt 'QwenHintsResetX'
$qwenHintsResetW = Get-UiInt 'QwenHintsResetW'
$qwenLanguageHintY = Get-UiInt 'QwenLanguageHintY'
$qwenChunkY = Get-UiInt 'QwenChunkY'
$qwenChunkEditW = Get-UiInt 'QwenChunkEditW'
$qwenChunkHintY = Get-UiInt 'QwenChunkHintY'
$qwenInputContextY = Get-UiInt 'QwenInputContextY'
$qwenInputContextX = Get-UiInt 'QwenInputContextX'
$qwenInputContextW = Get-UiInt 'QwenInputContextW'
$qwenInputContextHintY = Get-UiInt 'QwenInputContextHintY'
$qwenAdvancedButtonY = Get-UiInt 'QwenAdvancedButtonY'
$qwenAdvancedHintY = Get-UiInt 'QwenAdvancedHintY'
$qwenTestBtnX = Get-UiInt 'QwenTestBtnX'
$qwenTestBtnW = Get-UiInt 'QwenTestBtnW'
$volcKeyY = Get-UiInt 'VolcKeyY'
$volcKeyEditW = Get-UiInt 'VolcKeyEditW'
$volcModelY = Get-UiInt 'VolcModelY'
$volcModeY = Get-UiInt 'VolcModeY'
$volcModeComboW = Get-UiInt 'VolcModeComboW'
$volcLanguageComboX = Get-UiInt 'VolcLanguageComboX'
$volcLanguageComboW = Get-UiInt 'VolcLanguageComboW'
$volcContextY = Get-UiInt 'VolcContextY'
$volcReuseVocabW = Get-UiInt 'VolcReuseVocabW'
$volcInputContextY = Get-UiInt 'VolcInputContextY'
$volcInputContextX = Get-UiInt 'VolcInputContextX'
$volcInputContextW = Get-UiInt 'VolcInputContextW'
$volcActionY = Get-UiInt 'VolcActionY'
$volcTestBtnX = Get-UiInt 'VolcTestBtnX'
$volcTestBtnW = Get-UiInt 'VolcTestBtnW'
$volcHintY = Get-UiInt 'VolcHintY'

# Tab 5 (Audio & Advanced)
$advancedVadGroupH = Get-UiInt 'AdvancedVadGroupH'
$advancedDiagnosticsGap = Get-UiInt 'AdvancedDiagnosticsGroupGap'
$advancedDiagnosticsHeight = Get-UiInt 'AdvancedDiagnosticsGroupH'
$advancedDiagnosticsModeWidth = Get-UiInt 'AdvancedDiagnosticsModeW'
$advancedDiagnosticsOpenWidth = Get-UiInt 'AdvancedDiagnosticsOpenButtonW'
$advancedDiagnosticsDeleteWidth = Get-UiInt 'AdvancedDiagnosticsDeleteButtonW'
$advancedDiagnosticsButtonGap = Get-UiInt 'AdvancedDiagnosticsButtonGap'
$advancedDiagnosticsHintWidth = Get-UiInt 'AdvancedDiagnosticsHintW'

# Dialogs
$qwenDialogW = Get-UiInt 'QwenAdvancedDialogW'
$qwenDialogH = Get-UiInt 'QwenAdvancedDialogH'
$qwenDialogNonClientReserveH = Get-UiInt 'QwenAdvancedDialogNonClientReserveH'
$qwenDialogInputLeft = Get-UiInt 'QwenAdvancedDialogInputLeft'
$qwenDialogInputW = Get-UiInt 'QwenAdvancedDialogInputW'
$qwenDialogSpecialY = Get-UiInt 'QwenAdvancedDialogSpecialY'
$qwenDialogSpecialH = Get-UiInt 'QwenAdvancedDialogSpecialH'
$qwenDialogFooterY = Get-UiInt 'QwenAdvancedDialogFooterY'
$volcDialogW = Get-UiInt 'VolcAdvancedDialogW'
$volcDialogH = Get-UiInt 'VolcAdvancedDialogH'
$volcDialogNonClientReserveH = Get-UiInt 'VolcAdvancedDialogNonClientReserveH'
$volcDialogMarginX = Get-UiInt 'VolcAdvancedDlgMarginX'
$volcDialogGroupW = Get-UiInt 'VolcAdvancedDlgGroupW'
$volcDialogGroup4Y = Get-UiInt 'VolcAdvancedDlgGroup4Y'
$volcDialogGroup4H = Get-UiInt 'VolcAdvancedDlgGroup4H'
$volcDialogJsonLabelH = Get-UiInt 'VolcAdvancedDlgJsonLabelH'
$volcDialogJsonLabelW = Get-UiInt 'VolcAdvancedDlgJsonLabelW'
$volcDialogLabelX = Get-UiInt 'VolcAdvancedDlgLabelX'
$volcDialogLabelW = Get-UiInt 'VolcAdvancedDlgLabelW'
$volcDialogInputX = Get-UiInt 'VolcAdvancedDlgInputX'
$volcDialogIdEditW = Get-UiInt 'VolcAdvancedDlgIdEditW'
$volcDialogNameLabelX = Get-UiInt 'VolcAdvancedDlgNameLabelX'
$volcDialogNameLabelW = Get-UiInt 'VolcAdvancedDlgNameLabelW'
$volcDialogNameEditX = Get-UiInt 'VolcAdvancedDlgNameEditX'
$volcDialogNameEditW = Get-UiInt 'VolcAdvancedDlgNameEditW'
$volcDialogSwitchCol1X = Get-UiInt 'VolcAdvancedDlgSwitchCol1X'
$volcDialogSwitchCol2X = Get-UiInt 'VolcAdvancedDlgSwitchCol2X'
$volcDialogSwitchW = Get-UiInt 'VolcAdvancedDlgSwitchW'
$volcDialogSnippetBtnX = Get-UiInt 'VolcAdvancedDlgSnippetBtnX'
$volcDialogSnippetBtnW = Get-UiInt 'VolcAdvancedDlgSnippetBtnW'
$volcDialogResetBtnX = Get-UiInt 'VolcAdvancedDlgResetBtnX'
$volcDialogResetBtnW = Get-UiInt 'VolcAdvancedDlgResetBtnW'
$volcDialogJsonLabelOffsetY = Get-UiInt 'VolcAdvancedDlgJsonLabelOffsetY'
$volcDialogJsonEditOffsetY = Get-UiInt 'VolcAdvancedDlgJsonEditOffsetY'
$volcDialogJsonEditH = Get-UiInt 'VolcAdvancedDlgJsonEditH'
$btnHeight = Get-UiInt 'BtnH'
$volcDialogBtnY = Get-UiInt 'VolcAdvancedDlgBtnY'
$inputDlgW = Get-UiInt 'InputDlgW'
$inputDlgH = Get-UiInt 'InputDlgH'
$inputDlgEditW = Get-UiInt 'InputDlgEditW'
$inputDlgBtnY = Get-UiInt 'InputDlgBtnY'
$promptDlgW = Get-UiInt 'PromptDlgW'
$promptDlgH = Get-UiInt 'PromptDlgH'
$promptDlgEditW = Get-UiInt 'PromptDlgEditW'
$promptDlgEditH = Get-UiInt 'PromptDlgEditH'
$promptDlgBtnY = Get-UiInt 'PromptDlgBtnY'

# Inner right edge of the tab work area, shared by every provider panel.
$workAreaRight = $settingsWindowWidth - $margin
# Hard bottom limit for tab content: the tab control ends at footerMinTop - 4 and
# keeps $margin of inner padding, so no control may extend past this line.
$tabContentBottom = $footerMinTop - 4 - $margin

# --- Tab 1: General & Input -------------------------------------------------
$generalInputY = $firstRowY + $shortcutHeight + $inputGroupGap
$generalStartupY = $generalInputY + $inputGroupH + $startupGap
$generalBottom = $generalStartupY + $startupHeight
if ($generalBottom -gt $tabContentBottom) {
    throw "Tab 1 reaches the tab work-area bottom: bottom=$generalBottom, limit=$tabContentBottom"
}
if (($groupX + $groupWidth) -gt $workAreaRight) {
    throw 'General group exceeds the Settings design width'
}
if (($contentLeft + $hintWidth) -gt ($groupX + $groupWidth)) {
    throw 'Startup explanatory text exceeds the General group width'
}
if (($startupHintOffsetY + $qwenHint2LineHeight) -gt $startupHeight) {
    throw 'Startup explanatory text keeps a single-line box and would clip its second line'
}

# --- Tab 5: Audio & Advanced ------------------------------------------------
$advancedDiagnosticsY = $firstRowY + $advancedVadGroupH + $advancedDiagnosticsGap
$advancedBottom = $advancedDiagnosticsY + $advancedDiagnosticsHeight
if ($advancedBottom -gt $tabContentBottom) {
    throw "Tab 5 reaches the tab work-area bottom: bottom=$advancedBottom, limit=$tabContentBottom"
}
if (($inputLeft + $advancedDiagnosticsModeWidth) -gt ($groupX + $groupWidth)) {
    throw 'Diagnostics mode selector exceeds the General group width'
}
if (($contentLeft + $advancedDiagnosticsOpenWidth + $advancedDiagnosticsButtonGap + $advancedDiagnosticsDeleteWidth) -gt ($groupX + $groupWidth)) {
    throw 'Diagnostics action buttons exceed the General group width'
}
if ($advancedDiagnosticsOpenWidth -lt 220 -or $advancedDiagnosticsDeleteWidth -lt 240) {
    throw 'Diagnostics action buttons do not leave enough room for their English labels'
}
if (($contentLeft + $advancedDiagnosticsHintWidth) -gt ($groupX + $groupWidth)) {
    throw 'Diagnostics privacy hint exceeds the General group width'
}

# --- Tab 2: Row 0 must stay a single, non-overlapping routing row -----------
if (($contentLeft + $labelWidth) -gt $inputLeft) {
    throw 'ASR Backend label overlaps its combo box'
}
if (($inputLeft + $primaryBackendComboW) -gt $fallbackLabelX) {
    throw 'ASR Backend combo overlaps the Fallback label'
}
if (($fallbackLabelX + 68) -gt $fallbackComboX) {
    throw 'Fallback label overlaps the Fallback combo box'
}
if (($fallbackComboX + $fallbackComboW) -gt $workAreaRight) {
    throw 'Fallback combo box exceeds the Settings design width'
}

# --- Tab 2: Qwen ASR panel --------------------------------------------------
if ($qwenHintH -lt 24) {
    throw "Single-line hints need at least 24 design px (QwenHintH=$qwenHintH); 20 px collapses to 14 px at 96 DPI"
}
if (($qwenKeyY + $editHeight) -gt $qwenUrlY) { throw 'Qwen API Key row overlaps the Base URL row' }
if (($qwenUrlY + $editHeight) -gt $qwenModelY) { throw 'Qwen Base URL row overlaps the Model row' }
if (($qwenModelY + $actionButtonH) -gt $qwenLanguageY) { throw 'Qwen Model row overlaps the Language row' }
if (($qwenLanguageY + $editHeight) -gt $qwenLanguageHintY) { throw 'Qwen Language row overlaps its hint line' }
if (($qwenLanguageHintY + $qwenHintH) -gt $qwenChunkY) { throw 'Qwen language hint overlaps the chunk row' }
if (($qwenChunkY + $editHeight) -gt $qwenChunkHintY) { throw 'Qwen chunk row overlaps its hint line' }
if (($qwenChunkHintY + $qwenHintH) -gt $qwenInputContextY) { throw 'Qwen chunk hint overlaps the input-context row' }
if (($qwenInputContextY + $checkHeight) -gt $qwenInputContextHintY) { throw 'Qwen input-context row overlaps its hint line' }
if (($qwenInputContextHintY + $qwenHintH) -gt $qwenAdvancedButtonY) { throw 'Qwen input-context hint overlaps the action row' }
if (($qwenAdvancedButtonY + $actionButtonH) -gt $qwenAdvancedHintY) { throw 'Qwen action row overlaps its hint line' }
if (($qwenAdvancedHintY + $qwenHintH) -gt $tabContentBottom) {
    throw "Qwen basic settings reach the tab work-area bottom: bottom=$($qwenAdvancedHintY + $qwenHintH), limit=$tabContentBottom"
}
if (($inputLeft + $qwenHintW) -gt $workAreaRight) { throw 'Qwen hint lines exceed the Settings design width' }
if (($qwenShowBtnX + $qwenShowBtnW) -gt $workAreaRight) { throw 'Qwen Show button exceeds the Settings design width' }
if (($inputLeft + $qwenKeyEditW) -gt $qwenShowBtnX) { throw 'Qwen API Key edit overlaps the Show button' }
if (($inputLeft + $qwenUrlEditW) -gt $workAreaRight) { throw 'Qwen Base URL edit exceeds the Settings design width' }
if (($qwenLogBtnX + $qwenLogBtnW) -gt $workAreaRight) { throw 'Qwen Open log button exceeds the Settings design width' }
if (($inputLeft + $qwenModelComboW) -gt $qwenLogBtnX) { throw 'Qwen Model combo overlaps the Open log button' }
if (($inputLeft + $qwenLanguageComboW) -gt $qwenHintsLabelX) { throw 'Qwen Language combo overlaps the Hints label' }
if (($qwenHintsEditX + $qwenHintsEditW) -gt $qwenHintsResetX) { throw 'Qwen language hints edit overlaps its Reset button' }
if (($qwenHintsResetX + $qwenHintsResetW) -gt $workAreaRight) { throw 'Qwen language hints Reset button exceeds the Settings design width' }
if (($qwenInputContextX + $qwenInputContextW) -gt $workAreaRight) { throw 'Qwen input-context checkbox exceeds the Settings design width' }
if (($qwenInputContextX -ne $inputLeft) -or ($qwenInputContextW -lt 400)) { throw 'Qwen input-context checkbox must sit on its own row in the input column with room for its label' }
if (($qwenTestBtnX + $qwenTestBtnW) -gt $workAreaRight) { throw 'Qwen Test Connection button exceeds the Settings design width' }

# --- Tab 2: Volcano Engine panel -------------------------------------------
if (($volcKeyY + $editHeight) -gt $volcModelY) { throw 'Volcano API Key row overlaps the Model row' }
if (($volcModelY + $actionButtonH) -gt $volcModeY) { throw 'Volcano Model row overlaps the mode row' }
if (($volcModeY + $editHeight) -gt $volcContextY) { throw 'Volcano mode row overlaps the context row' }
if (($volcContextY + $checkHeight) -gt $volcInputContextY) { throw 'Volcano vocabulary row overlaps the input-context row' }
if (($volcInputContextY + $checkHeight) -gt $volcActionY) { throw 'Volcano input-context row overlaps the action row' }
if (($volcActionY + $actionButtonH) -gt $volcHintY) { throw 'Volcano action row overlaps its hint line' }
if (($volcHintY + $qwenHintH) -gt $tabContentBottom) {
    throw "Volcano basic settings reach the tab work-area bottom: bottom=$($volcHintY + $qwenHintH), limit=$tabContentBottom"
}
if (($inputLeft + $volcKeyEditW) -gt $qwenShowBtnX) { throw 'Volcano API Key edit overlaps the Show button' }
if (($inputLeft + $volcModeComboW) -gt $volcLanguageComboX) { throw 'Volcano mode combo overlaps the Language combo' }
if (($volcLanguageComboX + $volcLanguageComboW) -gt $workAreaRight) { throw 'Volcano language combo exceeds the Settings design width' }
if (($volcInputContextX + $volcInputContextW) -gt $workAreaRight) { throw 'Volcano input-context checkbox exceeds the Settings design width' }
if (($inputLeft + $volcReuseVocabW) -gt $workAreaRight) { throw 'Volcano vocabulary checkbox exceeds the Settings design width' }
if (($volcTestBtnX + $volcTestBtnW) -gt $workAreaRight) { throw 'Volcano Test Connection button exceeds the Settings design width' }

# --- Tab 2: every provider panel bottom --------------------------------
# Derived from the panel layouts; each one must stay under the tab content limit
# even though only the active panel is visible.
$providerBottoms = @(
    @{ Name = 'Local';        Bottom = $firstRowY + (4 * $rowHeight) + $labelHeight },
    @{ Name = 'Baidu';        Bottom = $firstRowY + (4 * $rowHeight) + $actionButtonH },
    @{ Name = 'Doubao IME';   Bottom = $firstRowY + (4 * $rowHeight) + $actionButtonH },
    @{ Name = 'MiMo';         Bottom = $firstRowY + (5 * $rowHeight) + $actionButtonH },
    @{ Name = 'MAI';          Bottom = $firstRowY + (5 * $rowHeight) + $qwenHint2LineHeight },
    @{ Name = 'Qwen IME';     Bottom = $firstRowY + (5 * $rowHeight) + $qwenHintH },
    @{ Name = 'Qwen ASR';     Bottom = $qwenAdvancedHintY + $qwenHintH },
    @{ Name = 'Volcano';      Bottom = $volcHintY + $qwenHintH }
)
foreach ($panel in $providerBottoms) {
    if ($panel.Bottom -gt $tabContentBottom) {
        throw "Provider panel $($panel.Name) reaches the tab work-area bottom: bottom=$($panel.Bottom), limit=$tabContentBottom"
    }
}

# --- Dialogs ----------------------------------------------------------------
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
if (($volcDialogMarginX * 2 + $volcDialogGroupW) -gt $volcDialogW) {
    throw 'Volcano Engine Advanced groups exceed the dialog width'
}
if (($volcDialogMarginX + $volcDialogJsonLabelW) -gt $volcDialogSnippetBtnX) {
    throw 'Volcano Engine Advanced JSON label overlaps the snippet buttons'
}
# Column geometry: every label keeps its own gap to the field next to it, so no
# text can be clipped by the control that follows (GUIDs and long field names).
if (($volcDialogLabelX + $volcDialogLabelW) -gt $volcDialogInputX) {
    throw 'Volcano Engine Advanced field labels overlap their input column'
}
if (($volcDialogInputX + $volcDialogIdEditW) -gt $volcDialogNameLabelX) {
    throw 'Volcano Engine Advanced ID edits overlap the Name column'
}
if (($volcDialogNameLabelX + $volcDialogNameLabelW) -gt $volcDialogNameEditX) {
    throw 'Volcano Engine Advanced Name labels overlap their edit boxes'
}
if (($volcDialogNameEditX + $volcDialogNameEditW) -gt ($volcDialogMarginX + $volcDialogGroupW)) {
    throw 'Volcano Engine Advanced Name edits exceed the group width'
}
if (($volcDialogSwitchCol1X + $volcDialogSwitchW) -gt $volcDialogSwitchCol2X) {
    throw 'Volcano Engine Advanced switch columns overlap'
}
if (($volcDialogSwitchCol2X + $volcDialogSwitchW) -gt ($volcDialogMarginX + $volcDialogGroupW)) {
    throw 'Volcano Engine Advanced switch columns exceed the group width'
}
if (($volcDialogSnippetBtnX + (2 * $volcDialogSnippetBtnW) + 12) -gt $volcDialogResetBtnX) {
    throw 'Volcano Engine Advanced snippet buttons overlap the Reset button'
}
if (($volcDialogResetBtnX + $volcDialogResetBtnW) -gt ($volcDialogMarginX + $volcDialogGroupW)) {
    throw 'Volcano Engine Advanced snippet buttons exceed the dialog width'
}
if (($volcDialogGroup4Y + $btnHeight) -gt ($volcDialogGroup4Y + $volcDialogJsonEditOffsetY)) {
    throw 'Volcano Engine Advanced snippet buttons overlap the JSON editor'
}
if (($volcDialogGroup4Y + $volcDialogJsonLabelOffsetY + $volcDialogJsonLabelH) -gt ($volcDialogGroup4Y + $volcDialogJsonEditOffsetY)) {
    throw 'Volcano Engine Advanced JSON label overlaps the JSON editor'
}
if (($volcDialogGroup4Y + $volcDialogJsonEditOffsetY + $volcDialogJsonEditH) -gt ($volcDialogBtnY - $margin)) {
    throw 'Volcano Engine Advanced JSON editor overlaps the footer buttons'
}
$volcDialogClientBottom = $volcDialogH - $volcDialogNonClientReserveH
if (($volcDialogBtnY + $actionButtonH) -gt ($volcDialogClientBottom - $margin)) {
    throw 'Volcano Engine Advanced footer buttons exceed the estimated client area'
}
if (($volcDialogGroup4Y + $volcDialogGroup4H) -gt $volcDialogBtnY) {
    throw 'Volcano Engine Advanced JSON region overlaps the footer buttons'
}
$maiHintBottom = $firstRowY + (5 * $rowHeight) + $qwenHint2LineHeight
if ($maiHintBottom -gt $tabContentBottom) {
    throw 'MAI settings hint reaches the tab work-area bottom'
}
if (($inputDlgEditW + 36) -gt $inputDlgW) {
    throw 'Input dialog edit field exceeds the dialog width'
}
if (($inputDlgBtnY + $actionButtonH) -gt ($inputDlgH - 48)) {
    throw 'Input dialog buttons exceed the estimated client area'
}
if (($promptDlgEditW + 60) -gt $promptDlgW) {
    throw 'Prompt dialog edit field exceeds the dialog width'
}
if (($promptDlgBtnY + $actionButtonH) -gt ($promptDlgH - 48)) {
    throw 'Prompt dialog buttons exceed the estimated client area'
}
$llmActionBottom = $firstRowY + (7 * $rowHeight) + 6 + $actionButtonH
if ($llmActionBottom -gt $tabContentBottom) {
    throw 'LLM settings action row reaches the tab work-area bottom'
}

# The scaling loop below multiplies both sides of each inequality by the same
# factor, so it can never fail once the unscaled checks pass. It is kept for
# historical parity only: the real low-DPI defence is the 96 DPI screenshot pass.
foreach ($dpi in @(96, 144, 192, 288)) {
    $scale = $dpi / 144.0
    if ((Scale $generalBottom $scale) -gt (Scale $tabContentBottom $scale)) {
        throw "Tab 1 overlaps the tab work-area bottom at $dpi DPI"
    }
    if ((Scale $advancedBottom $scale) -gt (Scale $tabContentBottom $scale)) {
        throw "Tab 5 overlaps the tab work-area bottom at $dpi DPI"
    }
    if ((Scale ($qwenAdvancedHintY + $qwenHintH) $scale) -gt (Scale $tabContentBottom $scale)) {
        throw "Qwen basic settings overlap the tab work-area bottom at $dpi DPI"
    }
    if ((Scale ($volcHintY + $qwenHintH) $scale) -gt (Scale $tabContentBottom $scale)) {
        throw "Volcano basic settings overlap the tab work-area bottom at $dpi DPI"
    }
    if ((Scale ($groupX + $groupWidth) $scale) -gt (Scale $workAreaRight $scale)) {
        throw "General group overflows the Settings width at $dpi DPI"
    }
    if ((Scale ($fallbackComboX + $fallbackComboW) $scale) -gt (Scale $workAreaRight $scale)) {
        throw "Fallback combo box overflows the Settings width at $dpi DPI"
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
    if ((Scale ($volcDialogGroup4Y + $volcDialogJsonEditOffsetY + $volcDialogJsonEditH) $scale) -gt (Scale ($volcDialogBtnY - $margin) $scale)) {
        throw "Volcano Engine Advanced JSON editor overlaps the footer at $dpi DPI"
    }
    if ((Scale ($volcDialogBtnY + $actionButtonH) $scale) -gt (Scale ($volcDialogClientBottom - $margin) $scale)) {
        throw "Volcano Engine Advanced footer overflows the estimated client area at $dpi DPI"
    }
    if ((Scale ($inputDlgEditW + 36) $scale) -gt (Scale $inputDlgW $scale)) {
        throw "Input dialog edit field exceeds the dialog width at $dpi DPI"
    }
    if ((Scale ($inputDlgBtnY + $actionButtonH) $scale) -gt (Scale ($inputDlgH - 48) $scale)) {
        throw "Input dialog buttons exceed the estimated client area at $dpi DPI"
    }
    if ((Scale ($promptDlgEditW + 60) $scale) -gt (Scale $promptDlgW $scale)) {
        throw "Prompt dialog edit field exceeds the dialog width at $dpi DPI"
    }
    if ((Scale ($promptDlgBtnY + $actionButtonH) $scale) -gt (Scale ($promptDlgH - 48) $scale)) {
        throw "Prompt dialog buttons exceed the estimated client area at $dpi DPI"
    }
    if ((Scale $llmActionBottom $scale) -gt (Scale $tabContentBottom $scale)) {
        throw "LLM settings action row overlaps the tab work-area bottom at $dpi DPI"
    }
    if ((Scale $maiHintBottom $scale) -gt (Scale $tabContentBottom $scale)) {
        throw "MAI settings overlap the tab work-area bottom at $dpi DPI"
    }
}

function Strip-Comments([string]$code) {
    $noBlock = [regex]::Replace($code, '(?s)/\*.*?\*/', '')
    return [regex]::Replace($noBlock, '//[^\r\n]*', '')
}

$cleanGlobals = Strip-Comments $globals
$cleanSettings = Strip-Comments $settings
$cleanSettingsWithHeaders = Strip-Comments $settingsWithHeaders

foreach ($required in @(
        'IDC_START_WITH_WINDOWS',
        'AddGeneralControl(startupCheckbox)',
        'AddGeneralControl(partialCheckbox)',
        'RefreshStartupRegistrationControl(g_settingsWindow, true)',
        'SaveStartupRegistrationControl(hwnd)',
        'IDC_DIAGNOSTIC_AUDIO_MODE',
        'AddAdvancedControl(diagnosticsGroup)',
        'DiagnosticAudioModeFromControl(hwnd)',
        'OpenDiagnosticAudioFolder(hwnd)',
        'DeleteDiagnosticAudioFiles(hwnd)',
        'IDC_QWEN_ADVANCED',
        'ShowQwenAdvancedDialog(hwnd, data)',
        'IDC_VOLC_ADVANCED',
        'ShowVolcAdvancedDialog(parent, edited, ValidateVolcAdvancedData)',
        'case IDC_VOLC_EXTRA_HOTWORDS:',
        'case IDC_VOLC_EXTRA_CONTEXT:',
        'case IDC_VOLC_EXTRA_RESET:',
        'ICloudProviderPanel',
        'LocalProviderPanel',
        'IDC_MAI_API_PROVIDER',
        'AddMaiControl(CreateCombo(hwnd, IDC_MAI_API_PROVIDER',
        'void ShowMaiApiSubPage(HWND hwnd)',
        'ProviderMai::ShowSubPage(',
        'cfg.maiOpenRouterApiKey = QwenControlText(',
        'L"Input context"',
        'mai_transcribe::TestConnection')) {
    if (!$cleanGlobals.Contains($required) -and !$cleanSettingsWithHeaders.Contains($required)) {
        throw "Startup Settings wiring is missing: $required"
    }
}

# --- Control ownership: each id is created once, and where it belongs -------
function Get-CreatedControlIds([string]$Code) {
    $ids = @()
    foreach ($m in [regex]::Matches($Code, 'Create(?:Combo|Button|CheckBox|HotkeyEdit)\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*,\s*(IDC_[A-Z0-9_]+)')) {
        $ids += $m.Groups[1].Value
    }
    foreach ($m in [regex]::Matches($Code, 'static_cast<INT_PTR>\s*\(\s*(IDC_[A-Z0-9_]+)\s*\)\s*\)')) {
        $ids += $m.Groups[1].Value
    }
    return $ids
}

$tabsPath = Join-Path $SourceRoot 'src\ui\tabs'
$providersPath = Join-Path $SourceRoot 'src\ui\providers'
$ownedFiles = @()
foreach ($dir in @($tabsPath, $providersPath)) {
    if (Test-Path -LiteralPath $dir -PathType Container) {
        $ownedFiles += @(Get-ChildItem -Path $dir -Recurse -File -Filter *.cpp | Select-Object -ExpandProperty FullName)
    }
}

$idOwners = @{}
$duplicateIds = @()
foreach ($file in $ownedFiles) {
    $clean = Strip-Comments (Get-Content -LiteralPath $file -Raw -Encoding UTF8)
    foreach ($id in (Get-CreatedControlIds $clean)) {
        if ($idOwners.ContainsKey($id)) {
            $duplicateIds += "$id in $([System.IO.Path]::GetFileName($file)) and $($idOwners[$id])"
        } else {
            $idOwners[$id] = [System.IO.Path]::GetFileName($file)
        }
    }
}
if ($duplicateIds.Count -gt 0) {
    throw "Control ids must be created exactly once across tabs and providers: $($duplicateIds -join '; ')"
}
foreach ($requiredOwner in @(
        @{ Id = 'IDC_ASR_BACKEND'; File = 'tab_speech_engine.cpp' },
        @{ Id = 'IDC_ASR_FALLBACK_BACKEND'; File = 'tab_speech_engine.cpp' },
        @{ Id = 'IDC_PARTIAL'; File = 'tab_general.cpp' },
        @{ Id = 'IDC_POSTPROCESS'; File = 'provider_local.cpp' },
        @{ Id = 'IDC_MODEL'; File = 'provider_local.cpp' },
        @{ Id = 'IDC_THREADS'; File = 'provider_local.cpp' },
        @{ Id = 'IDC_DIAGNOSTIC_AUDIO_MODE'; File = 'tab_advanced.cpp' },
        @{ Id = 'IDC_VAD_MODEL'; File = 'tab_advanced.cpp' })) {
    if (!$idOwners.ContainsKey($requiredOwner.Id)) {
        throw "$($requiredOwner.Id) is no longer created anywhere"
    }
    if ($idOwners[$requiredOwner.Id] -ne $requiredOwner.File) {
        throw "$($requiredOwner.Id) must be created by $($requiredOwner.File), found in $($idOwners[$requiredOwner.Id])"
    }
}

# Row 0 belongs to the port of TabSpeechEngine: no provider panel may place a
# control there, or the Test buttons would collide with the Fallback combo.
foreach ($file in @(Get-ChildItem -Path $providersPath -Recurse -File -Filter *.cpp | Select-Object -ExpandProperty FullName)) {
    $clean = Strip-Comments (Get-Content -LiteralPath $file -Raw -Encoding UTF8)
    if ($clean.Contains('RowInputY(0)')) {
        throw "$([System.IO.Path]::GetFileName($file)) places a control in Row 0, which is reserved for the ASR Backend / Fallback selectors"
    }
}

# Each provider keeps its Test Connection button on a row that is free in its own
# layout; the exact row is pinned so a copy/paste cannot move it back to Row 0.
$testPlacements = @(
    @{ File = 'provider_baidu.cpp'; Text = 'IDC_BAIDU_TEST, S(500), S(UiStyle::RowInputY(4))' },
    @{ File = 'provider_mimo.cpp'; Text = 'IDC_MIMO_TEST, S(500), S(UiStyle::RowInputY(5))' },
    @{ File = 'provider_doubao.cpp'; Text = 'IDC_DOUBAO_IME_TEST, S(500), S(UiStyle::RowInputY(4))' },
    @{ File = 'provider_mai.cpp'; Text = 'IDC_MAI_TEST, S(500), S(UiStyle::RowInputY(4))' },
    @{ File = 'provider_qwen_free.cpp'; Text = 'IDC_QWEN_FREE_TEST, S(500), S(UiStyle::RowInputY(4))' },
    @{ File = 'provider_qwen.cpp'; Text = 'IDC_QWEN_TEST, S(UiStyle::QwenTestBtnX), S(UiStyle::QwenAdvancedButtonY)' },
    @{ File = 'provider_volcengine.cpp'; Text = 'IDC_VOLC_TEST, S(UiStyle::VolcTestBtnX), S(UiStyle::VolcActionY)' }
)
foreach ($placement in $testPlacements) {
    $target = Join-Path $providersPath $placement.File
    if (!(Test-Path -LiteralPath $target -PathType Leaf)) {
        throw "Provider panel is missing: $($placement.File)"
    }
    $clean = Strip-Comments (Get-Content -LiteralPath $target -Raw -Encoding UTF8)
    if (!$clean.Contains($placement.Text)) {
        throw "$($placement.File) must place its Test Connection button at $($placement.Text)"
    }
}

Write-Output 'Validated Settings, provider panels and advanced dialogs at 96/144/192/288 DPI design scales.'
