# Settings 界面休眠唤醒/关屏后布局异常研究与修复方案

> **文档标识**：`PLAN-FIX-SETTINGS-DPI-001`  
> **创建日期**：2026-09-24  
> **更新记录**：已吸纳审查意见（补全 WM_DESTROY 清理 Tab static 控件、同步收敛 IDC_CANCEL 路径、简化 CloseSettingsWindow 避免双重执行）  
> **关联模块**：`src/ui/settings.cpp`, `src/ui/settings_controls.cpp`, `src/ui/ui_types.h`  
> **审查状态**：评审通过（Approved by Reviewer）

---

## 1. 现象复盘与问题陈述

### 1.1 触发场景
用户在日常使用中报告以下现象：
1. 打开 VoxType Settings 窗口或在后台常驻运行；
2. 关闭显示器电源，然后再打开；或电脑进入睡眠（Sleep / Modern Standby），然后再唤醒启动；
3. 重新打开或查看 Settings 界面时，窗口呈现严重排版畸变：
   - 窗口外框显著缩小，右侧内容被直接腰斩切断；
   - Tab 标签栏出现左右滚动箭头（`Audio ◀ ▶`）；
   - 底部操作栏（`Save` / `Close` 按钮）完全消失不可见；
   - Tab 标题中的 `&` 丢失（如 `General & Input` 显示为 `General  Input`）；
4. 退出软件并彻底重启后，Settings 界面完全恢复正常。

---

## 2. 图像像素级逆向取证与数学验证

对用户提供的异常状态截图（794×677）进行坐标测量与比对：

| 测量对象 | 截图中实测物理像素 | 96 DPI (100% 缩放) 理论值 | 144 DPI (150% 缩放) 理论值 | 事实判定 |
| :--- | :--- | :--- | :--- | :--- |
| **窗口外部宽度** | **548 px**（含外边框约 567 px） | $850 \times \frac{96}{144} = \mathbf{567\text{ px}}$ | $850 \times 1.0 = \mathbf{850\text{ px}}$ | **外框完全处于 96 DPI** |
| **窗口外部高度** | **487 px**（含外边框约 493 px） | $740 \times \frac{96}{144} = \mathbf{493\text{ px}}$ | $740 \times 1.0 = \mathbf{740\text{ px}}$ | **外框完全处于 96 DPI** |
| **内部编辑框起点 (InputLeft)** | **~195 px** | $188 \times \frac{96}{144} = \mathbf{125\text{ px}}$ | $188 \times 1.0 = \mathbf{188\text{ px}}$ | **控件完全以 144 DPI 布局** |
| **GroupBox 物理宽度** | **788 px** | $788 \times \frac{96}{144} = \mathbf{525\text{ px}}$ | $788 \times 1.0 = \mathbf{788\text{ px}}$ | **控件完全以 144 DPI 布局** |

### 连锁破坏效应推导
1. **右侧截断**：内部 GroupBox 宽 788px，但窗口客户区总宽只有 548px，超出的 240px 内容（包含 `Pad start` 单位、`Smooth win` 控件右侧）被物理截断；
2. **底栏消失**：代码设定 `FooterMinTop = 632`，在 144 DPI 下底栏按钮起始坐标为 $Y \approx 640\text{px}$；而当前窗口物理高度仅 487px，**底栏被画在可视区域外 150px 以下**；
3. **Tab 滚动箭头**：Win32 `WC_TABCONTROLW` 宽度仅 524px，无法容纳 5 个标签，底层 Common Control 自动激活左右滚动按钮；
4. **助记键前缀吞字符**：Win32 TabControl 默认将单 `&` 视作键盘加速键（Accelerator），未转义的 `&` 导致文本变成 `General  Input`。

---

## 3. 根因深度剖析（Root Cause Analysis）

```
[用户熄屏 / 休眠]
       │
       ▼
[显卡 HPD Drop / 物理断开] ──> DWM 切换到 Fallback 虚拟显示 (1024x768, 96 DPI)
       │
       ├─ [情况 A: Settings 窗口在后台隐藏 SW_HIDE]
       │     └─ Windows 调整隐藏窗口 Rect 为 96 DPI (567x493)
       │     └─ PerMonitorV2 规范: 操作系统【绝不】向隐藏窗口发送 WM_DPICHANGED!
       │
       └─ [情况 B: Settings 窗口前台显示]
             └─ 收到 WM_DPICHANGED(96 DPI)，窗口缩至 567x493
       │
[用户开屏 / 唤醒]
       │
       ▼
[物理显示器恢复 144 DPI (150%)]
       │
       ├─ [对于隐藏窗口]: Windows 依然【不发送】WM_DPICHANGED！外框永久定格在 567x493！
       │
       └─ [用户点击托盘 "Settings..."]
             └─ ShowSettingsWindow() 执行:
                   • if (!g_settingsWindow) 跳过 (窗口依然存在)
                   • 完全不更新 UiScale，不校验当前显示器 DPI
                   • 读取窗口被缩小的 Rect (567x493)
                   • 调用 SetWindowPos(..., SWP_NOSIZE) ── 强制保留 567x493 脏尺寸！
                   • 内部控件保持 144 DPI 布局 ──【形成严重的 DPI 撕裂】
```

### 根本缺陷列表
1. **隐藏常驻盲区（`SW_HIDE`）**：
   `HideSettingsWindow` 使用 `ShowWindow(hwnd, SW_HIDE)` 保活窗口句柄。PerMonitorV2 明确规定：隐藏窗口不接收 `WM_DPICHANGED`。跨越电源事件后，隐藏窗口沦为"盲窗"，完全脱离 DPI 状态机追踪。
2. **`ShowSettingsWindow` 盲目使用 `SWP_NOSIZE`**：
   在已有窗口重新展示时，`ShowSettingsWindow` 从不重新核算当前显示器 DPI，而是直接用 `SWP_NOSIZE` 继承脏尺寸。
3. **`WM_DPICHANGED` 盲信 `lParam (suggested RECT)`**：
   在某些显卡驱动唤醒瞬间，Windows 提供的 `suggested RECT` 可能为空或包含过时宽高；代码未以 `S(UiStyle::SettingsWindowW)` 和 `S(UiStyle::SettingsWindowH)` 进行权威尺寸守卫。
4. **TabControl 文本缺乏 `&&` 转义**：
   Win32 标准规范要求显示字面量 `&` 时必须转义为 `&&`。

---

## 4. 审查反馈与设计完善

在方案审查中，评审人指出了两个必须补充的关键遗漏以及一个简化项：

1. **遗漏 1：`WM_DESTROY` 必须显式调用 `DestroyControls()`**：
   `s_tabs`（`TabGeneral`, `TabSpeechEngine`, `TabVocabulary`, `TabLlm`, `TabAdvanced`）为 TU 级别 `static` 变量（L40-48）。
   窗口销毁后，Tab 对象依然存活；其内部持有的子控件句柄（`m_controls`、`m_vadFireredControls` 等 `vector<HWND>`）将全部沦为**悬空引用（Dangling HWND）**。若不在 `WM_DESTROY` 中显式调用 `DestroyControls()` 清空内部状态，下次打开重新调用 `CreateControls` 或响应消息时可能引发句柄悬空异常。
2. **遗漏 2：`WM_COMMAND / IDC_CANCEL` 路径必须同步收敛**：
   用户点击界面底部的 "Close" 按钮走的是 `IDC_CANCEL` 消息路径（L218-221）。原代码调用的是 `HideSettingsWindow`，若只修改 `WM_CLOSE`，点击 Close 按钮依然会走旧的隐藏保活路径。必须将 `IDC_CANCEL` 与 `WM_CLOSE` 统一路由至销毁通道。
3. **架构简化：单点收敛，杜绝双重执行**：
   原 `HideSettingsWindow` 中有 `g_sharedTestGeneration.fetch_add` 与 `ui_provider::CancelQwenFreeTests()`。
   如果新建的 `CloseSettingsWindow` 再次调用它们，将与 `WM_DESTROY` 形成**双重执行**。
   **最佳实践**：`CloseSettingsWindow` 只做一件事——`DestroyWindow(hwnd)`。所有状态重置（自增 test generation、取消测试、`t->DestroyControls()`、清空 `g_settingsWindow`、`InstallKeyboardHook()`）**全部收敛于 `WM_DESTROY` 单点处理**。

---

## 5. 具体代码变更规划

### 5.1 `src/ui/settings.cpp`

#### (1) 替换 `HideSettingsWindow` 为极简的 `CloseSettingsWindow`
```cpp
void CloseSettingsWindow(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
}
```

#### (2) 统一收敛 `WM_COMMAND (IDC_CANCEL)` 与 `WM_CLOSE`
```diff
         if (controlId == IDC_SAVE || controlId == IDOK) {
             SaveSettingsControls(hwnd);
             return 0;
         }
         if (controlId == IDC_CANCEL || controlId == IDCANCEL) {
-            HideSettingsWindow(hwnd);
+            CloseSettingsWindow(hwnd);
             return 0;
         }
...
     case WM_CLOSE:
-        HideSettingsWindow(hwnd);
+        CloseSettingsWindow(hwnd);
         return 0;
```

#### (3) 完善 `WM_DESTROY` 清理职责（清理静态 Tab 控件 + 恢复钩子）
```cpp
    case WM_DESTROY:
        g_sharedTestGeneration.fetch_add(1, std::memory_order_relaxed);
        ui_provider::CancelQwenFreeTests();
        for (auto* t : s_tabs) t->DestroyControls();
        if (g_settingsWindow == hwnd) g_settingsWindow = nullptr;
        InstallKeyboardHook();
        return 0;
```

#### (4) 修正 Tab 标题中的 `&` 助记符转义
```diff
-        for (auto* name : {L"General & Input", L"Speech Engine", L"Vocabulary", L"LLM", L"Audio & Advanced"}) {
+        for (auto* name : {L"General && Input", L"Speech Engine", L"Vocabulary", L"LLM", L"Audio && Advanced"}) {
             item.pszText = const_cast<LPWSTR>(name);
             TabCtrl_InsertItem(tab, 100, &item);
         }
```

#### (5) `ShowSettingsWindow` 强制基准尺寸居中构建
```cpp
void ShowSettingsWindow(HWND owner) {
    UninstallKeyboardHook();
    if (!g_settingsWindow) {
        UpdateUiScale(owner);
        const int w = S(UiStyle::SettingsWindowW);
        const int h = S(UiStyle::SettingsWindowH);
        RECT work = GetWorkAreaForWindow(owner);
        int x = work.left + (work.right - work.left - w) / 2;
        int y = work.top + (work.bottom - work.top - h) / 2;

        g_settingsWindow = CreateWindowExW(
            WS_EX_APPWINDOW, kSettingsClass, L"VoxType Settings",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
            x, y, w, h,
            owner, nullptr, g_instance, nullptr);
    }
    if (g_settingsWindow) {
        ui_tab::RefreshStartupRegistrationControl(g_settingsWindow, true);
        ShowWindow(g_settingsWindow, SW_SHOW);
        SetForegroundWindow(g_settingsWindow);
    } else {
        InstallKeyboardHook();
    }
}
```

### 5.2 `src/ui/settings_controls.cpp`
1. 在 `HandleSettingsDpiChanged` 中加入权威尺寸兜底：
```cpp
void HandleSettingsDpiChanged(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    const UINT newDpi = HIWORD(wParam);
    UpdateUiScaleForDpi(newDpi);
    const int targetW = S(UiStyle::SettingsWindowW);
    const int targetH = S(UiStyle::SettingsWindowH);
    RECT* suggested = reinterpret_cast<RECT*>(lParam);
    if (suggested) {
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     targetW, targetH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowPos(hwnd, nullptr, 0, 0, targetW, targetH,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    HFONT newFont = ui_theme::UiFontForDpi(newDpi);
    EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(newFont));
    LayoutSettingsWindow(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
}
```
2. 在 `CreateLabel` 与 `CreateHint` 中追加 `SS_NOPREFIX`，一次性根治全仓静态标签吃 `&` 的问题：
```cpp
HWND CreateLabel(HWND parent, int x, int y, int w, int h, const wchar_t* text) {
    HWND hwnd = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, x, y, w, h, parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(hwnd);
    return hwnd;
}

HWND CreateHint(HWND parent, int x, int y, int w, int h, const wchar_t* text) {
    HWND hwnd = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                              x, y, w, h, parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(hwnd);
    MarkSettingsHint(hwnd);
    return hwnd;
}
```

### 5.3 `src/ui/settings_dialogs.cpp`
为 GroupBox（BUTTON 类别）中的 `&` 增加转义：
```diff
- state->group2 = CreateWindowW(L"BUTTON", L"Acoustics & context", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
+ state->group2 = CreateWindowW(L"BUTTON", L"Acoustics && context", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
```

---

## 6. 架构守卫与基线合规性核对

按照 `AGENTS.md` 约束规则：
1. **守卫脚本基线检查**（`tools/check_architecture.ps1`）：
   - `SettingsLines` 基线：$\le 400$ 行。
   - 当前行数：实测 379 行（基线 400 行，余量 21 行），**完全不反弹，保持在基线安全余量内**；
2. **用户感知行为变更记录**：
   - 旧实现通过 `SW_HIDE` 隐藏保活，使得输入但未保存的草稿在再次打开时依然留在控件中；新实现采用标准的 Win32 对话框直觉（Close = 取消/丢弃未保存变更，Save = 持久化保存），关闭后重新打开将从 `g_config` 干净加载；
3. **静态布局校验**（`scripts/validate_settings_layout.ps1`）：
   - 保持 96/144/192/288 DPI 全尺度校验通过；
4. **C++23 规范与标准宏**：
   - 保持严格 Win32 DPI 规范与 `/utf-8` 支持。

---

## 7. 测试与验证计划

1. **静态验证**：
   - 运行 `tools/check_architecture.ps1` 确保 17 项检查全部通过；
   - 运行 `scripts/validate_settings_layout.ps1` 确保 4 个尺度静态布局断言全绿；
2. **编译验证**：
   - 运行 `build.bat`，确保 MSVC C++23 编译无警告无报错；
3. **动态场景验证**：
   - **用例 1（关闭并重开）**：分别测试点击右上角 "X" 与点击底栏 "Close" 按钮，确认窗口均完全销毁；下次打开时正常重新居中弹出，Tab 内部控件状态干净；
   - **用例 2（Tab 控件无悬空句柄）**：在反复打开/关闭设置窗口 5 次以上后，确认内存稳定，无句柄泄露，Tab 切换与控件响应完全正常；
   - **用例 3（静态标签与 Tab 标题 & 检查）**：确认全仓 Static 标签、GroupBox 以及 Tab 标题中的 `&` 正常显示，不再被作为助记键吃掉；
   - **用例 4（底栏完整性）**：确认 `Save`、`Close` 按钮与 `Status` 文本在所有分辨率下均清晰可见且边距符合设计规范；
   - **用例 5（退出路径钩子测试）**：托盘退出（Quit）时，确认全局键盘钩子被干净卸载，不会被 Settings 销毁误重新挂载。
