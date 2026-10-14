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
    Write-Output 'browser_bridge_registration_test: preflight and interrupted-uninstall recovery passed'
} finally {
    # Delete only the unique test subtree created above, never native host keys.
    $registry.DeleteSubKeyTree($testRegistryPath, $false)
    $registry.Dispose()
    if (Test-Path -LiteralPath $testStatePath) { Remove-Item -LiteralPath $testStatePath }
}
