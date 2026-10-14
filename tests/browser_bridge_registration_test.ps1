param([string]$RuntimeRoot = (Join-Path $PSScriptRoot '../build/run/x64-release'))
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 is required.' }
$taskId = [Guid]::NewGuid().ToString('N')
$testRegistryPath = "Software\VoxTypeTests\BrowserBridge-$taskId"
$testStatePath = Join-Path ([IO.Path]::GetTempPath()) "voxtype-registration-$taskId.json"
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '../tools/register_browser_bridge.ps1'))
$source = $source.Replace('Software\Google\Chrome\NativeMessagingHosts\$hostName', "$testRegistryPath\Chrome")
$source = $source.Replace('Software\Microsoft\Edge\NativeMessagingHosts\$hostName', "$testRegistryPath\Edge")
$source = $source.Replace("Join-Path `$env:LOCALAPPDATA 'VoxType/browser-bridge-registration.json'",
                         "'$testStatePath'")
if ($source.Contains('NativeMessagingHosts') -or $source.Contains('browser-bridge-registration.json')) {
    throw 'Test isolation replacement failed; refusing to run.'
}
$script = [ScriptBlock]::Create($source)
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey('CurrentUser', 'Registry64')
function Read-Default([string]$path) {
    $key = $registry.OpenSubKey($path)
    if (!$key) { return $null }
    try { return $key.GetValue('') } finally { $key.Dispose() }
}
function Set-Default([string]$path, [string]$value) {
    $key = $registry.CreateSubKey($path)
    try { $key.SetValue('', $value, 'String') } finally { $key.Dispose() }
}
try {
    & $script -Action Register -RuntimeRoot $RuntimeRoot | Out-Null
    $owned = Read-Default "$testRegistryPath\Chrome"
    if (!$owned) { throw 'Registration fixture did not initialize.' }
    Set-Default "$testRegistryPath\Edge" 'external-registration'
    $rejected = $false
    try { & $script -Action Unregister -RuntimeRoot $RuntimeRoot | Out-Null } catch { $rejected = $true }
    if (!$rejected -or (Read-Default "$testRegistryPath\Chrome") -ne $owned -or
        (Read-Default "$testRegistryPath\Edge") -ne 'external-registration') {
        throw 'Unregister changed a key before completing its preflight.'
    }
    Set-Default "$testRegistryPath\Edge" $owned
    $registry.DeleteSubKeyTree("$testRegistryPath\Chrome", $false)
    & $script -Action Unregister -RuntimeRoot $RuntimeRoot | Out-Null
    if ((Read-Default "$testRegistryPath\Edge") -or (Test-Path -LiteralPath $testStatePath)) {
        throw 'Interrupted uninstall could not resume.'
    }
    foreach ($resume in @('Register', 'Unregister')) {
        foreach ($firstKeyUpdated in @($false, $true)) {
            $oldManifest = 'previous-owned-runtime'
            Set-Default "$testRegistryPath\Chrome" $(if ($firstKeyUpdated) { $owned } else { $oldManifest })
            Set-Default "$testRegistryPath\Edge" $oldManifest
            $pending = @{
                manifest = $owned
                records = @(
                    [ordered]@{previous=$null; existed=$true; priorManifest=$oldManifest; key="$testRegistryPath\Chrome"},
                    [ordered]@{existed=$false; priorManifest=$oldManifest; previous=$null; key="$testRegistryPath\Edge"}
                )
            }
            [IO.File]::WriteAllText($testStatePath, ($pending | ConvertTo-Json -Depth 4))
            if ($resume -eq 'Register') {
                & $script -Action Register -RuntimeRoot $RuntimeRoot | Out-Null
                if ((Read-Default "$testRegistryPath\Chrome") -ne $owned -or
                    (Read-Default "$testRegistryPath\Edge") -ne $owned) {
                    throw 'Interrupted move could not resume registration.'
                }
            }
            & $script -Action Unregister -RuntimeRoot $RuntimeRoot | Out-Null
            $chrome = $registry.OpenSubKey("$testRegistryPath\Chrome")
            $edge = $registry.OpenSubKey("$testRegistryPath\Edge")
            try {
                if (!$chrome -or $null -ne $chrome.GetValue('') -or $edge -or
                    (Test-Path -LiteralPath $testStatePath)) {
                    throw 'Interrupted move did not restore the original empty/absent keys.'
                }
            } finally {
                if ($chrome) { $chrome.Dispose() }
                if ($edge) { $edge.Dispose() }
            }
        }
    }
    Write-Output 'browser_bridge_registration_test: preflight, interrupted move/register/unregister recovery passed'
} finally {
    # Delete only the unique test subtree created above, never native host keys.
    $registry.DeleteSubKeyTree($testRegistryPath, $false)
    $registry.Dispose()
    if (Test-Path -LiteralPath $testStatePath) { Remove-Item -LiteralPath $testStatePath }
    if (Test-Path -LiteralPath "$testStatePath.tmp") { Remove-Item -LiteralPath "$testStatePath.tmp" }
}
