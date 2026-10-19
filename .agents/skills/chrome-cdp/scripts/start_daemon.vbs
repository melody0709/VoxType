' 隐藏启动 chrome-cdp 守护进程（无控制台窗口），可放进 shell:startup 常驻。
' 脚本自动定位同目录下的 cdp.js，因此仓库副本与安装副本都能直接用；
' 守护进程日志：%TEMP%\chrome-cdp-daemon.log
Option Explicit

Dim fso, sh, here, script, nodeExe, candidates, i
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh = CreateObject("WScript.Shell")

here = fso.GetParentFolderName(WScript.ScriptFullName)
script = fso.BuildPath(here, "cdp.js")

If Not fso.FileExists(script) Then
  MsgBox "cdp.js not found next to this script: " & script, 16, "chrome-cdp"
  WScript.Quit 1
End If

nodeExe = "node.exe"
candidates = Array( _
  "C:\nvm4w\nodejs\node.exe", _
  sh.ExpandEnvironmentStrings("%ProgramFiles%\nodejs\node.exe"), _
  sh.ExpandEnvironmentStrings("%APPDATA%\npm\node.exe") _
)
For i = 0 To UBound(candidates)
  If fso.FileExists(candidates(i)) Then
    nodeExe = candidates(i)
    Exit For
  End If
Next

' 0 = 隐藏窗口, False = 不等待返回
sh.Run """" & nodeExe & """ """ & script & """ __daemon__", 0, False
