param([string]$SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path)

$controller = [System.IO.File]::ReadAllText((Join-Path $SourceRoot 'src/app/recording_session_controller.cpp'))
$hook = [System.IO.File]::ReadAllText((Join-Path $SourceRoot 'src/ui/hotkey.cpp'))
$start = $controller.IndexOf('void OnRecordingWatchdogTimer() {')
$end = $controller.IndexOf('void OnSessionLock() {', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Recording watchdog boundaries not found' }
$watchdog = $controller.Substring($start, $end - $start)
if ($watchdog -match '\b(GetAsyncKeyState|ModifiersMatch|SetActiveHotkeyKey|ResetCapsLockHotkeyState)\s*\(') {
    throw 'Recording watchdog must not infer release from a consumed hotkey'
}
if ($watchdog -notmatch 'kMaxRecordingDurationMs' -or $watchdog -notmatch 'StopRecordingSession') {
    throw 'Recording watchdog must retain the hard duration limit'
}
if ($hook -notmatch 'FinishCapsLockHotkeyPress\(\);' -or
    $hook -notmatch 'PostHotkeyRecordingCommand\(kHotkeyRecordingStop\);') {
    throw 'Keyboard hook must still handle key-up stopping'
}
Write-Output 'recording_watchdog_regression: PASS'

$capture = [System.IO.File]::ReadAllText((Join-Path $SourceRoot 'src/audio/audio_capture.cpp'))
$wasapi = [System.IO.File]::ReadAllText((Join-Path $SourceRoot 'src/audio/wasapi_capture.cpp'))
if ($capture -notmatch 'g_wasapiCapture.RequestResamplerReset\(\)' -or
    $wasapi -notmatch 'm_resamplerResetPending.exchange\(false' -or
    $wasapi -notmatch 'mono.assign\(numFrames, 0.0f\)') {
    throw 'Keep-alive resume must reset the filter; silent packets must retain their duration'
}
$sessionLock = $controller.Substring($end)
if ($sessionLock -notmatch 'SetStopDelayHeldForRepress\(false\)' -or
    $sessionLock -notmatch 'CloseAudioCapture\(\);' -or
    $sessionLock -notmatch 'ResetStreamingVadTrimmerState\(\);') {
    throw 'Session lock must clear re-press/VAD state and close the kept-alive capture'
}
$mainWindow = [System.IO.File]::ReadAllText((Join-Path $SourceRoot 'src/app/main_window.cpp'))
if ($mainWindow -notmatch 'WTS_REMOTE_DISCONNECT' -or $mainWindow -notmatch 'WTS_SESSION_LOGOFF' -or
    $mainWindow -notmatch 's_sessionLocked \|\| s_sessionDisconnected' -or
    $mainWindow -notmatch 'asr_runtime_log::WriteIf\(true, "event=clipboard_restore_retry' -or
    $mainWindow -notmatch 'asr_runtime_log::WriteIf\(true, "event=clipboard_paste_pending') {
    throw 'Session disconnects must cancel recording; clipboard diagnostics must reach the bounded file log'
}
Write-Output 'capture_session_regression: PASS'
