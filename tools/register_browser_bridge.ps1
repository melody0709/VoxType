param(
    [ValidateSet('Register', 'Unregister')][string]$Action = 'Register',
    [string]$RuntimeRoot = ''
)
# Development registration for the VoxType browser companion native messaging host.
#
# Scope contract (do not widen it):
#   - it owns exactly two HKCU values, one for Chrome and one for Edge;
#   - it records what it overwrote and restores it on Unregister;
#   - it refuses to replace a registration it did not create.
# It never touches browser flags, profiles, startup parameters or the extension
# itself, and it requires no administrator rights.
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 is required.' }

$hostName = 'com.voxtype.browser_bridge'
if ([string]::IsNullOrWhiteSpace($RuntimeRoot)) {
    # Installed layout: <runtime>/scripts/register_browser_bridge.ps1
    $RuntimeRoot = Split-Path $PSScriptRoot -Parent
}
$utf8 = [Text.UTF8Encoding]::new($false)
$runtime = [IO.Path]::GetFullPath($RuntimeRoot)
$manifest = Join-Path $runtime "native-messaging/$hostName.json"
$keys = @(
    "Software\Google\Chrome\NativeMessagingHosts\$hostName",
    "Software\Microsoft\Edge\NativeMessagingHosts\$hostName"
)
$root = [Microsoft.Win32.RegistryKey]::OpenBaseKey('CurrentUser', 'Registry64')
$statePath = Join-Path $env:LOCALAPPDATA 'VoxType/browser-bridge-registration.json'
try {
    if ($Action -eq 'Register') {
        $binary = Join-Path $runtime 'VoxType.exe'
        if (!(Test-Path -LiteralPath $binary) -or !(Test-Path -LiteralPath $manifest)) {
            throw "Use a CMake-installed VoxType runtime directory (looked in $runtime)."
        }
        $hostManifest = [IO.File]::ReadAllText($manifest, $utf8) | ConvertFrom-Json
        if ($hostManifest.name -ne $hostName -or $hostManifest.path -ne '../VoxType.exe' -or
            $hostManifest.type -ne 'stdio') {
            throw 'Unexpected native host manifest; refusing to register it.'
        }
        $saved = if (Test-Path -LiteralPath $statePath) {
            [IO.File]::ReadAllText($statePath, $utf8) | ConvertFrom-Json
        } else { $null }
        $records = @()
        foreach ($path in $keys) {
            $key = $root.OpenSubKey($path)
            $value = if ($key) { $key.GetValue('') } else { $null }
            $prior = if ($saved) { @($saved.records | Where-Object { $_.key -eq $path })[0] } else { $null }
            $priorManifest = if ($prior -and $prior.PSObject.Properties['priorManifest']) {
                $prior.priorManifest
            } else { $null }
            if ($null -ne $value -and $value -ne $manifest -and
                (!$prior -or ($value -ne $saved.manifest -and
                             ([string]::IsNullOrEmpty($priorManifest) -or $value -ne $priorManifest)))) {
                if ($key) { $key.Dispose() }
                throw "Refusing to replace an unknown registration: $path"
            }
            $records += @{
                key = $path
                existed = if ($prior) { $prior.existed } else { $null -ne $key }
                previous = if ($prior) { $prior.previous } else { $value }
                priorManifest = $value
            }
            if ($key) { $key.Dispose() }
        }
        [IO.Directory]::CreateDirectory((Split-Path $statePath -Parent)) | Out-Null
        $temporary = "$statePath.tmp"
        try {
            [IO.File]::WriteAllText($temporary,
                (@{manifest = $manifest; records = $records} | ConvertTo-Json -Depth 4), $utf8)
            [IO.File]::Move($temporary, $statePath, $true)
        } finally {
            if (Test-Path -LiteralPath $temporary) { [IO.File]::Delete($temporary) }
        }
        foreach ($path in $keys) {
            $key = $root.CreateSubKey($path)
            try { $key.SetValue('', $manifest, 'String') } finally { $key.Dispose() }
        }
        Write-Output "Registered Chrome and Edge native messaging host for: $runtime"
        Write-Output 'Next: load extensions/browser-bridge in chrome://extensions (Developer mode).'
    } else {
        if (!(Test-Path -LiteralPath $statePath)) { throw 'No owned registration record exists.' }
        $saved = [IO.File]::ReadAllText($statePath, $utf8) | ConvertFrom-Json
        # Preflight all keys before changing either; an interrupted uninstall can
        # also resume when one key already equals its original value.
        foreach ($record in $saved.records) {
            if ($record.key -notin $keys) { throw 'Unexpected registration key in saved record.' }
            $key = $root.OpenSubKey($record.key)
            if (!$key) { continue }
            try {
                $current = $key.GetValue('')
                $previous = if ($record.previous -eq $saved.manifest) { $null } else { $record.previous }
                $priorManifest = if ($record.PSObject.Properties['priorManifest']) { $record.priorManifest } else { $null }
                $owned = $current -eq $saved.manifest -or
                         (![string]::IsNullOrEmpty($priorManifest) -and $current -eq $priorManifest)
                if (!$owned -and $current -ne $previous) {
                    throw "Registration changed; leaving it untouched: $($record.key)"
                }
            } finally { $key.Dispose() }
        }
        foreach ($record in $saved.records) {
            if ($record.key -notin $keys) { throw 'Unexpected registration key in saved record.' }
            $key = $root.OpenSubKey($record.key, $true)
            if (!$key) { continue }
            try {
                $current = $key.GetValue('')
                $previous = if ($record.previous -eq $saved.manifest) { $null } else { $record.previous }
                if ($current -eq $previous) { continue }
                $priorManifest = if ($record.PSObject.Properties['priorManifest']) { $record.priorManifest } else { $null }
                $owned = $current -eq $saved.manifest -or
                         (![string]::IsNullOrEmpty($priorManifest) -and $current -eq $priorManifest)
                if (!$owned) {
                    throw "Registration changed; leaving it untouched: $($record.key)"
                }
                if ($null -ne $record.previous -and $record.previous -ne $saved.manifest) {
                    $key.SetValue('', [string]$record.previous, 'String')
                } else {
                    $key.DeleteValue('', $false)
                }
                $empty = $key.GetValueNames().Count -eq 0 -and $key.GetSubKeyNames().Count -eq 0
            } finally { $key.Dispose() }
            if ($empty -and !$record.existed) { $root.DeleteSubKey($record.key, $false) }
        }
        [IO.File]::Delete($statePath)
        Write-Output 'Removed the owned registration; unrelated values were preserved.'
    }
} finally { $root.Dispose() }
