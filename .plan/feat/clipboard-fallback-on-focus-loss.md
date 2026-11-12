# 优化方案：焦点丢失 / 窗口切换未上屏时自动保留至剪贴板

- **状态**：已实施，离线回归通过；实机矩阵待验收（v0.13.1）
- **目标场景**：未勾选「快速粘贴 + 剪贴板历史」（`restore_clipboard_after_paste = true`）时，若发生焦点丢失、系统壳面/只读/提权窗口、或停录后切换窗口导致文本未能上屏，自动将识别结果安全保留到剪贴板并给出明确提示，同时支持托盘一键二次复制，避免重录。
- **架构与测试红线**：
  - `src/app/main.cpp` 当前实测 **150 / 150 行（余 0 行）**，本次**严禁改动 `main.cpp`**；
  - `tools/check_architecture.ps1` 规定 `$LAYER_ALLOW["platform"] = @("core")`，因此 `src/platform/` **只能 `#include` `src/core/`**，严禁引入 `src/asr/`、`src/ui/`、`src/app/`；
  - 严守 `AGENTS.md` Rule 【A】：任何情况下**不得**对浏览器进程调用 UIA/MSAA；
  - `tests/clipboard_test.cpp` 直接 `#include "../src/platform/text_injector.cpp"` 并使用 Win32 API 宏桩，新增 API 须与该测试桩及 `CMakeLists.txt` 链接边界兼容；
  - `TabGeneral` 当前底边 `614 <= 616`（**仅余 2 DIP**），如需 UI 开关不得强塞 Tab 1，应放入 `TabAdvanced`（底边 `518 <= 616`，余 **98 DIP**）。

---

## 1. 现状核查与问题根因

### 1.1 「未勾选快速粘贴」对应的真实代码路径
- 在 [src/core/config_store.h](../../src/core/config_store.h#L216) 中，`Config::restoreClipboardAfterPaste` 代码默认值为 `false`。
- 在 [src/ui/tabs/tab_general.cpp](../../src/ui/tabs/tab_general.cpp#L298-L314) 中，复选框 `IDC_RESTORE_CLIPBOARD`（「快速粘贴 + 剪贴板历史」）采用**反向映射**：
  - 未勾选 → `cfg.restoreClipboardAfterPaste = true`（保护模式，对应 `config_registry.cpp:278` 的 `restore_clipboard_after_paste`）。
- 在 [src/platform/text_injector.cpp](../../src/platform/text_injector.cpp#L391-L465) 的 `PasteTextImeAware()` 中：
  1. `restoreClipboardAfterPaste == true` 时：
     - 仅当 `IsStandardEditControl(focus)`（类名属于 `Edit` / `RichEdit*` / `Scintilla`）时，走 `PasteViaClipboard(focus, text, true)`（写入带历史排除标记的剪贴板 → 异步 `WM_PASTE` → `ClipboardPasteComplete` / `kClipboardRestoreTimer` 自动还原旧剪贴板）；
     - **其余所有现代应用**（浏览器、Electron、VS Code、终端等）全部走 `SendUnicodeText(text)`（`SendInput` + `KEYEVENTF_UNICODE`），**全程不写剪贴板**；
  2. `forceUnicodeInput == true` 时在 [text_injector.cpp:414-417](../../src/platform/text_injector.cpp#L414-L417) 直接早退调用 `SendUnicodeText()`，绕过了 `GetForegroundWindow()` 之后的 `focus`、微信判断及可编辑校验；
  3. `restoreClipboardAfterPaste == false`（已勾选）时，若 `fg == nullptr` 或 `PasteViaClipboard` 在写入前失败（如 `originalSequence == 0`），同样没有兜底写入。

### 1.2 文本丢失的 6 类真实断层

1. **假成功静默丢字（无焦点壳面 / 只读禁用控件 / UIPI 提权窗口）**：
   - `GetFocusedWindowForForeground(fg)`（`text_injector.cpp:92-100`）在无子焦点时直接回退返回顶层 `fg`；
   - **系统壳面**：焦点在桌面（`Progman` / `WorkerW` + `SHELLDLL_DefView` / `SysListView32`）、任务栏/托盘（`Shell_TrayWnd`、`Shell_SecondaryTrayWnd`、`NotifyIconOverflowWindow`、`TaskListThumbnailWnd`）、Alt+Tab/任务视图（`MultitaskingViewFrame`、`TaskSwitcherWnd`、`ForegroundStaging`、`XamlExplorerHostPopupWindow`）、锁屏（`LockApp`）、最小化窗口（`IsIconic(fg)`）或资源管理器文件列表（`CabinetWClass` 下的 `DirectUIHWND` / `SysListView32` / `SysTreeView32` / `SHELLDLL_DefView` / `ToolbarWindow32` / `ReBarWindow32`）时，`SendInput(2, ...)` 依然返回 `2`（成功），文本静默蒸发；
   - **UIPI 提权窗口**：普通权限 VoxType 对管理员/高完整性窗口调用 `SendInput` 时，Windows UIPI 会静默丢弃输入事件，但 **`SendInput` 仍常返回 `2`**（仅 `PostMessageW`/`SendMessageCallbackW` 会报 `ERROR_ACCESS_DENIED`），因此它同样属于**假成功静默丢字**，必须前置 Token Integrity Level 检查才能拦截；
   - **只读或禁用标准控件**：`IsStandardEditControl(focus)` 未检查 `ES_READONLY` 与 `IsWindowEnabled()`；命中只读/禁用 `Edit` / `RichEdit` 时，`PasteViaClipboard(..., true)` 发出的异步 `WM_PASTE` 无效，随后还会把剪贴板还原为旧内容；
   - **微信非聊天页**：`text_injector.cpp:432-441` 向顶层 `fg` 循环 `PostMessageW(fg, WM_CHAR, ...)`，在非输入界面同样返回 `TRUE`。
2. **显式失败无剪贴板兜底**：
   - `fg == nullptr`、`SendInput` 返回非 2、`SendUnicodeText` / `WeChat` 循环途中前台切换（`307`/`436`）、`PasteViaClipboard` 失败、或 `ReplaceSelectionTextImeAware()`（`467-579`）因焦点/选区变化跳过时，[src/app/main_window.cpp](../../src/app/main_window.cpp#L415-L442) 仅弹短暂 HUD 报错并丢弃文本。
   - （注：循环逐字途中切窗时前几个字符可能已打入旧窗口，兜底应保留完整 `text` 供用户粘贴替换。）
3. **缺少「`t2` 停录时刻」窗口漂移锚点（含 150ms 停录延迟窗口陷阱）**：
   - `t0 -> t2`（按住快捷键期间）允许用户「先按热键再点击输入框」，**不应限制** `t0 == t2`；
   - `t2 -> t3`（松键停录后等待 ASR/LLM 返回期间）切窗则极易将文本错灌到新窗口或丢入非输入框；
   - **关键时序**：`MainWndProc` 收到 `kHotkeyRecordingStop` / `kHotkeyCapsLockRecordingStop`（[main_window.cpp:318-326](../../src/app/main_window.cpp#L318-L326)）后，会经过 **`kRecordingStopDelayMs = 150` ms** 定时器才调用 `StopRecordingSession()`。若仅在 `StopRecordingSession()` 采点，用户在松键后 150ms 内立即 `Alt+Tab` 就会把新窗口误当成原目标。必须在收到停录热键消息的瞬间即同步捕获 `t2` 锚点。
4. **`kLlmResultMessage` 200ms 定时器覆盖 Bug（含 `LLM failed:` 分支）**：
   - [main_window.cpp:583-585](../../src/app/main_window.cpp#L583-L585) 在 `kLlmResultMessage` 末尾无条件执行 `SetTimer(g_hudWindow, kHudHideTimer, 200, nullptr)`，不仅覆盖了 `522`/`538` 行的 `2200ms` 失败提示，也导致 `LLM failed:`（不进 `507` 行 `if`）的报错在 `200ms` 内闪退，且原始 `rawAsrText` 未做保底留存。
5. **`InjectionContext::sensitive` 与 fail-closed 历史策略耦合风险**：
   - [main_window.cpp:430-431, 528-529](../../src/app/main_window.cpp#L430-L431) 已将 `injection.sensitive` 直接赋值为 `input_context::MustSkipHistoryForFocus(resultConfig.asrSensitiveProbe)`（服务 `learning_intake` 学习闭环）。
   - `MustSkipHistoryForFocus()` 在切窗（`kSensitiveProbeUnknownFocusMoved = 15`）、起录无焦点（`16`）或未连扩展（`18`）时均返回 `true`（fail-closed）。若剪贴板兜底误用它，**切窗与无焦点起录时兜底会全部静默失效**；此外还需防范 `t0` 非密码框、`t3` 切入 Win32 `ES_PASSWORD` 密码框的场景。
6. **挂起剪贴板恢复定时器与兜底写入的竞态**：
   - 上次 `PasteViaClipboard(..., true)` 提交的 `WM_PASTE` 可能仍在目标线程排队。所有者改变、定时器超时或应用退出均不能证明旧消息已消费；此时覆写剪贴板会使旧窗口读到后一次识别文本。

---

## 2. 优化方案设计：三层防线 + 托盘二次找回

### 2.1 第一层防线：注入前高置信度目标评估（在 `PasteTextImeAware()` 最顶部执行）

**模块落位决策**：直接内聚实现在现有 [src/platform/text_injector.h](../../src/platform/text_injector.h) 与 [src/platform/text_injector.cpp](../../src/platform/text_injector.cpp) 中，**不新建独立模块**（避免额外的 `CMakeLists.txt`、`ARCHITECTURE.md`、`doc/ARCHITECTURE_zh.md` 登记成本；若后续拆分为独立模块，必须在 Step 1 同步更新 `CMakeLists.txt` 与两份架构文档以确保 `DocEntriesMissing` 不反弹）。

在 `PasteTextImeAware()` 最顶部新增评估函数 `AssessInjectionTarget()`：

```cpp
enum class InjectionFailureReason {
    None,
    NoForegroundWindow,        // fg == nullptr || !IsWindow(fg) || IsIconic(fg)
    BlockedByUipi,             // 目标进程完整性级别高于当前进程且当前进程无 UIAccess
    NonEditableShellSurface,   // 桌面 / 任务栏 / Alt+Tab / 锁屏 / 资源管理器文件列表等
    DisabledOrReadOnlyControl, // !IsWindowEnabled(fg) || !IsWindowEnabled(focus) || (Edit/RichEdit 且 ES_READONLY)
    WindowSwitchedAfterStop,   // t2(松键/停录) -> t3(投递) 顶层前台窗口发生变化
    SelectionRewriteSkipped,   // ReplaceSelectionTextImeAware 因选区/焦点变化跳过
    DeliveryFailed,            // SendInput / PostMessageW / PasteViaClipboard 实际执行失败
};
```

**关键实施要点**：
1. **调用顺序修复**：在 `PasteTextImeAware()` **最顶部**执行预检——**排在 `capturePreSelection`（`399` 行）与 `if (forceUnicodeInput)`（`414` 行）之前**，确保既不浪费 UIA 选区快照开销，也让 `Force Unicode Input` 模式享受完整的不可达拦截与剪贴板兜底。
2. **UIPI 完整性级别预检**：
   - 使用 `GetWindowThreadProcessId(fg, &pid)` + `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)`；
   - 若 `OpenProcess` 因 `ERROR_ACCESS_DENIED` 失败（如受保护/高权限进程），或读取双方 `GetTokenInformation(..., TokenIntegrityLevel, ...)` 与当前进程 `TokenUIAccess` 后判定目标 IL > 当前 IL 且当前无 `UIAccess`，返回 `BlockedByUipi`。
3. **系统壳面精准识别（只拦 100% 非输入壳面，绝不误杀自定义控件）**：
   - 桌面：`Progman`；或 `WorkerW` 且子焦点为 `WorkerW` / `SHELLDLL_DefView` / `SysListView32`；
   - 任务栏与系统切换视图：`Shell_TrayWnd`、`Shell_SecondaryTrayWnd`、`NotifyIconOverflowWindow`、`TaskListThumbnailWnd`、`MultitaskingViewFrame`、`TaskSwitcherWnd`、`ForegroundStaging`、`XamlExplorerHostPopupWindow`、`LockApp`；
   - 资源管理器非编辑区：顶层 `CabinetWClass` 且焦点为 `DirectUIHWND` / `SysListView32` / `SysTreeView32` / `SHELLDLL_DefView` / `ToolbarWindow32` / `ReBarWindow32`（**重命名文件时子焦点为 `Edit`，正常放行**）。
4. **对浏览器保持保守策略（不新增同步 IPC，也不默认用 `23` 拦截）**：
   - **绝不在 UI 线程发起同步 `browser_bridge` 查询**：`PasteTextImeAware` 运行在 `MainWndProc` UI 线程，同步等待管道/WebSocket 往返有卡顿与重入风险；
   - **关于复用现成探针 `kSensitiveProbeUnknownBrowserNotTextInput`（值 23）的权衡**：该常量虽然无需额外 IPC，但存在两个已知边界——① [extensions/browser-bridge/worker.js:185-201](../../extensions/browser-bridge/worker.js#L185-L201) 对 closed Shadow DOM 与部分自定义网页编辑器同样返回 `textInput == false`（即探针值 23）；② 探针采样于 `t0`（起录时刻），若用户在网页空白处起录、随后在 `t0 -> t2` 点击进入 `<input>`，到 `t3` 时实际上已有真实焦点。因此默认方案**不把探针 23 作为硬拦截条件**（严守 Rule 【A】零 UIA/MSAA，浏览器仅做最小化、Shell、UIPI 与 `t2 -> t3` 窗口漂移检查）。

---

### 2.2 第二层防线：`t2` 停录焦点锚点与 `t2 -> t3` 窗口漂移防护

1. **精准捕获时机（消除 150ms 延迟漏洞 & 过滤自身辅助窗体）**：
   - 在 [src/app/main_window.cpp:318-326](../../src/app/main_window.cpp#L318-L326) 收到 `kHotkeyRecordingStop` / `kHotkeyCapsLockRecordingStop` 的**同一时刻**同步捕获 `stopRootWindow = selection_context::TopLevelWindow(GetForegroundWindow())`（写入会话待停录状态）；
   - **自身窗口过滤**：`g_hudWindow` 带 `WS_EX_NOACTIVATE` 通常不会成为前台窗口，但 `g_mainWindow`（隐藏托盘宿主窗，如刚关闭托盘菜单后）或不可见窗口不应作为有效输入目标锚点，捕获时需过滤 `fg == g_mainWindow || fg == g_hudWindow || !IsWindowVisible(stopRootWindow)`；若用户焦点在 `g_settingsWindow`（例如在 Settings 输入框中试听录音），则保留作为合法 `stopRootWindow`；
   - 若录音未经松键消息而直接通过 60s 看门狗（`OnRecordingWatchdogTimer`）进入 `StopRecordingSession()`，则在 `StopRecordingSession()` 入口补采 `stopRootWindow`。
2. **全链路透传与 `IsWindow` 句柄复用校验**：
   - 将 `HWND stopRootWindow = nullptr` 写入当前 attempt 上下文，并随 `AsrFinalMetadata`、`AsrFinalMessage`、`LlmFinalMessage`（[src/asr/asr_dispatcher.h](../../src/asr/asr_dispatcher.h)）一路透传至 `MainWndProc` 投递入口（含 fallback 重试路径）；
   - 在 `t3` 比对 `stopRootWindow` 与 `currentRoot` 前，先校验 `IsWindow(stopRootWindow)`：若原窗口在 `t2 -> t3` 间已销毁（即使极低概率发生 HWND 句柄值复用），也直接按 `WindowSwitchedAfterStop` 处理（配合 `t2` 时记录的进程/窗口校验更稳妥）。
3. **保留 `t0 -> t2` 自由度**：
   - 不比较录音开始（`t0`）与停录（`t2`）窗口，保证「先按住热键再点入输入框」完全正常；
   - 仅当 `t2` 停录后等待结果期间（`t2 -> t3`）顶层前台窗口发生变化时触发 `WindowSwitchedAfterStop` 并转为剪贴板保留（HUD 文案明确提示「因窗口切换」，若用户习惯停录后切到别的窗口等自动打字，可通过 `clipboard_fallback_on_window_switch = false` 关闭该项拦截）。

---

### 2.3 第三层防线：「未上屏必入剪贴板」闭环、隐私边界与定时器修复

1. **统一结构化投递结果 `InjectionOutcome`**：
   - 覆盖 `PasteTextImeAware()` 与 `ReplaceSelectionTextImeAware()` 两条路径（`selectionRewrite` 被跳过时同样进入剪贴板兜底，保留已生成的改写文本）；
   - 在 `MainWndProc` 抽离共享函数 `DeliverFinalTextToTarget(...)` 合并 `kAsrResultMessage` 与 `kLlmResultMessage` 重复代码。
2. **安全写入兜底剪贴板（3 条硬规则）**：
   - **Rule 1 — 等旧粘贴消费完毕再写入**：`s_clipboardPastePending` 为真时保留队列、token 和恢复状态，将最新兜底文本暂存为 `s_pendingFallbackCopy`，返回 `ClipboardCopyStatus::Pending`。即使所有者或序列号改变，也不得提前清除 pending。完成回调核对原剪贴板身份，仍匹配时发布兜底文本并丢弃旧快照；不匹配则保留外部复制，提示从托盘找回识别文本。无旧消息排队时才可作废恢复状态并立即复制。退出时若仍有旧消息排队，保持剪贴板内容不变。
   - **Rule 2 — 精确密码豁免（有意采用 fail-open，绝不复用 `InjectionContext::sensitive`）**：
     - **隐私取舍留档（有意为之，非遗漏）**：`MustSkipHistoryForFocus()` 为云端历史上传设计，采用 fail-closed（所有 `Unknown` 均视为敏感）。而剪贴板兜底路径**有意采用 fail-open**（`Unknown` 时仍允许兜底写入剪贴板）：原因在于语音识别已经完成，如果不触发兜底拦截，正常路径本身也会尝试将文本直接键入该窗口；更重要的是，本方案的核心场景（切窗导致 `unknown_focus_moved = 15`、在桌面/无焦点处起录导致 `unknown_no_start_focus = 16`、浏览器未连接扩展导致 `unknown_browser_not_connected = 18`）探针结果全部都是 `Unknown`，若采用 fail-closed 会直接导致整个兜底机制失效。后续维护不得将其"修复"回 `MustSkipHistoryForFocus()`。
     - 保留 `InjectionContext::sensitive` 给 `learning_intake`；新增独立标记 `confirmedSensitiveAtStart = (input_context::SensitiveFocusAnswer(resultConfig.asrSensitiveProbe) == input_context::kSensitiveProbeSensitive)`；
     - 写入前再对当前 `t3` 焦点补做一次同步 Win32 检查 `input_context::IsWin32PasswordEditWindow(focus)`（[input_context.h:634-640](../../src/core/input_context.h#L634-L640)，不触发 UIA）；
     - 仅当 `confirmedSensitiveAtStart || isCurrentWin32Password` 为真时跳过写入并提示 `"Input skipped (password field)"`。
   - **Rule 3 — 尊重历史记录设置（`excludeFromHistory`）**：
     - 当 `restoreClipboardAfterPaste == true`（未勾选「快速粘贴 + 剪贴板历史」）时，调用 `selection_context::SetClipboardText(text, g_mainWindow, seq, /*excludeFromHistory=*/true)`：写入 `CF_UNICODETEXT` + `ExcludeClipboardContentFromMonitorProcessing`，**用户立刻 `Ctrl+V` 完全可用，同时不进 `Win+V` 与云剪贴板**；
     - 当 `restoreClipboardAfterPaste == false` 时，传 `excludeFromHistory = false`。
   - **排队提示**：`clipboardCopyPending == true` 时提示“等待上次粘贴完成（文本已暂存）”；完成或外部复制取消后经 `kClipboardDiagnosticMessage` 提示实际结果。
3. **修复 `kLlmResultMessage` 200ms 覆盖 Bug 与 HUD 反馈**：
   - 删除/条件化 [main_window.cpp:583-585](../../src/app/main_window.cpp#L583-L585) 的无条件 `200ms` 定时器：
     - `delivered == true` → `200ms`；
     - `!delivered && copiedToClipboard == true` → **`2200ms ~ 3000ms`**，按原因显示明确提示（如 `"Copied to clipboard (no input focus)"` / `"Copied to clipboard (window switched)"` / `"Copied to clipboard (admin window)"` / `"Copied to clipboard (rewrite skipped)"` / `"Copied to clipboard (input failed)"`）；
     - `LLM failed:`（`507` 行外）→ 保留错误提示 **`2200ms`**（不再 200ms 闪退），且若 `result->rawAsrText` 非空（且非密码控件）同步放入下文单槽内存缓存（可选同时兜底写入剪贴板或交由托盘复制，方案建议**存入单槽缓存 + 提示可托盘复制**，避免误把失败前的原始未纠错文本冲掉剪贴板，或可按配置选择）。

---

### 2.4 补充保险：纯内存单槽位「最近识别结果」+ 托盘一键复制

为应对「兜底写入剪贴板后，因 `excludeFromHistory = true` 不在 `Win+V`，若用户中间又复制了其他内容或剪贴板被锁导致写入失败如何二次找回」的问题：
1. **纯内存单槽位缓存**：在 `main_window.cpp` 维护 `s_lastCopyableTranscript`（仅存内存、进程退出销毁、不落盘；若 `confirmedSensitiveAtStart || isCurrentWin32Password` 则不更新，且 `OnSessionLock()` 不写入磁盘）；
2. **托盘菜单入口**：
   - 在 [src/core/app_messages.h:101-106](../../src/core/app_messages.h#L101-L106) 新增 `ID_TRAY_COPY_LAST_TRANSCRIPT = 1007`；
   - 在 [ShowTrayMenu()](../../src/app/main_window.cpp#L191-L211) 增加菜单项 `Copy Last Transcript`（无缓存时 `MF_GRAYED`，保持与现有菜单项一致的英文风格，后续随托盘统一双语化）；
   - 点击后复用相同的排队/完成状态，按当前 `g_config.restoreClipboardAfterPaste` 对应的 `excludeFromHistory` 写入剪贴板；等待旧粘贴时明确提示暂存。

---

## 3. 配置项与产品决策点（实施前需确认的 2 个取舍）

建议在强类型配置注册表（`src/core/config_store.h` + `src/core/config_registry.cpp`）新增两项（**默认均 `true`**，零配置即生效）：
1. `copy_to_clipboard_on_failure`（`bool copyToClipboardOnFailure = true;`）：不可输入或注入/改写失败时自动写入剪贴板；
2. `clipboard_fallback_on_window_switch`（`bool clipboardFallbackOnWindowSwitch = true;`）：停录后到结果返回前（`t2 -> t3`）切换顶层窗口时转存剪贴板而不注入新窗口。

**实施前建议确认**：
- **决策点 A（`t2 -> t3` 切窗默认行为）**：若您习惯「松开快捷键后，趁 1~2 秒识别期间手动切去另一个聊天窗口等它自动打字上屏」，则 `clipboard_fallback_on_window_switch` 应默认 `false`（仅做不可输入面/UIPI/失败兜底）；若您更希望「切窗就别往新窗口乱打字，直接进剪贴板安全粘贴」，则默认 `true`（本方案默认推荐 `true`）。
- **决策点 B（是否在 Settings 暴露可见开关）**：由于 `TabGeneral`（Tab 1）仅余 **2 DIP** 空间，第一阶段建议先默认开启配置 + HUD 提示 + 托盘菜单，无需改动 Settings UI；若需可视化开关，第二阶段放置于 `TabAdvanced`（Tab 5，余 **98 DIP**）并同步更新 `src/ui/ui_strings.inc` 与 `scripts/validate_settings_layout.ps1`。

---

## 4. 实施清单与验证计划

| 步骤 | 改动文件 | 核心内容 |
|---|---|---|
| 1 | `src/core/config_store.h` / `config_registry.cpp` | 注册 `copy_to_clipboard_on_failure`、`clipboard_fallback_on_window_switch`（默认 `true`） |
| 2 | `src/core/app_messages.h`、`src/asr/asr_dispatcher.h`、`src/app/asr_attempt_manager.*`、`src/app/recording_session_controller.*` | 新增 `ID_TRAY_COPY_LAST_TRANSCRIPT = 1007`；透传 `stopRootWindow`（`t2` 锚点）与非 fail-closed 的密码状态 |
| 3 | `src/platform/text_injector.h/.cpp` | 目标预检后绑定 Unicode 发送窗口；失败/拦截走剪贴板兜底，旧粘贴 pending 时延后复制，完成后核对所有者与序列号（遵循 `excludeFromHistory` 与 `t0`/`t3` 密码门控） |
| 4 | `src/app/main_window.cpp` | 松键瞬间（`kHotkeyRecordingStop` / `kHotkeyCapsLockRecordingStop`）采集 `t2`；合并 ASR/LLM 投递；修复 583-585 行 200ms 覆盖 Bug（含 `LLM failed:`）；新增纯内存 `s_lastCopyableTranscript` 与托盘 `Copy Last Transcript`（**禁改 `src/app/main.cpp`**） |
| 5 | `tests/clipboard_test.cpp` | 验证 Shell/只读/禁用/UIPI 守卫、切窗保护、旧粘贴延后复制、外部复制与退出边界、Pending/Copied 提示语义、两条选区改写 Unicode 绑定，以及 `t0`/`t3` 密码门控 |
| 6 | 全量回归 | 执行 `build.bat --test`（含 `tools/check_architecture.ps1` 24 项守卫与 `scripts/validate_settings_layout.ps1` 全 DPI 校验）+ 实机桌面矩阵验收（桌面/任务栏/资源管理器/只读框/管理员记事本/松键 `Alt+Tab`/浏览器 closed Shadow DOM 不误拦截） |
