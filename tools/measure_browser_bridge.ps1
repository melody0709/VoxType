param(
    [Parameter(Mandatory)][string]$OutputPath,
    [string]$Scenario = 'unspecified',
    [int]$Samples = 7,
    [int]$IntervalSeconds = 5
)
$ErrorActionPreference = 'Stop'
if ($Samples -lt 2 -or $IntervalSeconds -lt 1) { throw 'Use at least two samples and a positive interval.' }
$rows = @()
for ($i = 0; $i -lt $Samples; ++$i) {
    $processes = @(Get-Process VoxType,chrome,msedge -ErrorAction SilentlyContinue)
    $rows += [pscustomobject]@{
        utc = [DateTime]::UtcNow.ToString('o')
        scenario = $Scenario
        processes = @($processes | ForEach-Object {
            [pscustomobject]@{
                name = $_.ProcessName
                pid = $_.Id
                privateBytes = $_.PrivateMemorySize64
                workingSetBytes = $_.WorkingSet64
                cpuSeconds = $_.CPU
            }
        })
    }
    if ($i + 1 -lt $Samples) { Start-Sleep -Seconds $IntervalSeconds }
}
$directory = Split-Path ([IO.Path]::GetFullPath($OutputPath))
[IO.Directory]::CreateDirectory($directory) | Out-Null
[IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputPath), (ConvertTo-Json -InputObject $rows -Depth 5))
Write-Output "Saved $Samples samples ($Scenario): $OutputPath"
