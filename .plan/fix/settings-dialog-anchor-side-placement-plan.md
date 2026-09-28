# Settings 二级对话框"贴主窗口侧边"锚定定位研究与修复方案

> **文档标识**：`PLAN-FIX-SETTINGS-DIALOG-ANCHOR-001`
> **创建日期**：2026-09-28
> **关联模块**：`src/ui/settings_dialogs.cpp`、`src/ui/settings_controls.{h,cpp}`、`src/ui/ui_types.h`、`src/ui/dialog_positioning.{h,cpp}`（新增）、`tests/dialog_positioning_test.cpp`（新增）、`CMakeLists.txt`
> **参考实现**：`D:\GITHUB_melody0709\zencrop_ocr_pxipin`（`src/core/Utils.cpp::PositionWindowNearAnchor`）
> **状态**：**已实施并验证通过（2026-09-28）**，随 **v0.11.5** 发布——构建 + 18 项架构守卫 + 全部离线回归测试（含新增 `dialog_positioning_test`）+ 96/144/192/288 DPI 静态布局校验均 PASS；GUI 观感（A1/A4 的人工确认）待人工复核。
> **D1 已确认**：启用"最小位移让位"，且**仅在空间不足时**触发——空间足够时主窗口严格零位移。

---

## 1. 现象复盘与取证

### 1.1 现象

在 Settings 中打开任一"二级对话框"（Qwen ASR 高级设置、火山引擎高级设置、System Prompt 管理、Add Provider 输入框）后：

- 对话框**出现在主 Settings 窗口正中央**，把主窗口完整遮住（截图 1：`Qwen ASR Advanced Settings` 覆盖整屏，仅露出主窗口标题栏碎片与左右边缘）；
- 用户看不到主窗口当前处于哪个 Tab、也看不到自己刚才改了什么，只能先关掉对话框；
- 对比参考项目 zencrop_ocr_pxipin（截图 2）：`Translation Providers` 面板稳定贴在主 Settings 窗口**右侧**，两者并排、互不遮挡。

### 1.2 像素级取证（基于截图 1，显示尺寸 1197×931，1:1 读取）

| 测量对象 | 实测 | 理论值 | 判定 |
| :--- | :--- | :--- | :--- |
| 主 Settings 外框宽（左右边缘外推） | ≈ 865 px | 850 DIP × Scale(144DPI)=**850** | 用户环境为 **144 DPI（150%）**，`UiScale::Scale = 1.0` |
| Qwen 高级对话框宽 | ≈ 796 px | 780 DIP × 1.0 = **780** | 与 `UiStyle::QwenAdvancedDialogW` 一致 |
| 主窗口水平中心 | ≈ 472 px | — | — |
| 对话框水平中心 | ≈ 482 px | — | **两者同心**（≈10px 视为测量误差）⇒ 证实"以父窗口为中心居中" |

### 1.3 代码取证

四个二级对话框的位置全部来自同一函数——**以父窗口矩形为中心居中**：

```138:155:src/ui/settings_dialogs.cpp
POINT CalculateCenteredDialogPos(HWND parent, int width, int height) {
    RECT work = GetWorkAreaForWindow(parent);
    RECT parentRect = {};
    if (parent) {
        GetWindowRect(parent, &parentRect);
    } else {
        parentRect = work;
    }
    int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
    int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;
    ...
}
```

调用点（唯 4 处，全量）与尺寸：

| 对话框 | 调用点 | 外框尺寸（DIP） | 主窗口（DIP） | 结论 |
| :--- | :--- | :--- | :--- | :--- |
| `ShowInputDialog` | `settings_dialogs.cpp:1190` | 440 × 190 | 850 × 740 | 小于主窗口，问题轻微 |
| `ShowVolcAdvancedDialog` | `settings_dialogs.cpp:1222` | **810 × 800** | 850 × 740 | 高度超出主窗口 60 DIP ⇒ 全遮 |
| `ShowQwenAdvancedDialog` | `settings_dialogs.cpp:1257` | **780 × 900** | 850 × 740 | 高度超出主窗口 160 DIP ⇒ 全遮 |
| `ShowPromptManageDialog` | `settings_dialogs.cpp:1298` | 760 × 580 | 850 × 740 | 宽度接近，居中后主窗口几乎不可见 |

尺寸常量定义：`src/ui/ui_types.h:26-27`（主窗口 850×740）、`:170-171`（Qwen 780×900）、`:244-245`（Volc 810×800）、`:280-281`（Prompt 760×580）。

### 1.4 生命周期取证（决定方案边界）

- 四个对话框均为**自建模态**：`RunModalDialogLoop()`（`settings_dialogs.cpp:157-183`）在显示后 `EnableWindow(parent, FALSE)`，关闭时恢复。
- ⇒ **对话框打开期间主窗口不可交互、不可拖动** ⇒ 不存在"主窗口移动后对话框需要跟随"的需求（zencrop 同为模态，亦无跟随逻辑）。
- 各对话框的 `WM_DPICHANGED` 使用系统 `suggested` rect（`settings_dialogs.cpp:480/654/885/1030`），该路径不受本次改动影响。

---

## 2. 根因

```
用户点击 "Advanced..."
        │
        ▼
ShowQwenAdvancedDialog(parent=g_settingsWindow, ...)
        │
        ├─ width  = S(780) = 780px   ┐  对话框外框 >= 主窗口外框
        ├─ height = S(900) = 900px   ┘  (780/850 宽, 900/740 高)
        │
        ▼
CalculateCenteredDialogPos(parent, 780, 900)
        │
        └─ x = parent.left + (parent.width  - 780)/2   ← 与主窗口同心
           y = parent.top  + (parent.height - 900)/2
        │
        ▼
以父窗口为中心叠加 ⇒ 对话框（780×900）完整覆盖主窗口（850×740）
```

**根因归纳**：定位策略只有"居中"一种，而交互上二级对话框应表现为主窗口的**附属面板**（参考 zencrop），二者语义不匹配。与 DPI、编码、布局计算均无关——是纯定位策略缺陷。

---

## 3. 参考实现分析（zencrop_ocr_pxipin）

### 3.1 形态与定位算法

| 维度 | zencrop 做法 | 证据 |
| :--- | :--- | :--- |
| 面板形态 | 系统单页模态属性表（`PropertySheetW`，owner = 主 Settings），**独立顶层窗口**，非子窗口、非内嵌页 | `src/translation/TranslationSettingsPage.cpp:422-442` |
| 定位 | 独立函数 `PositionWindowNearAnchor(hwnd, anchor)`：四向搜索 **右 → 左 → 下 → 上**，四向皆不可时取"剩余空间最大"的一侧，最后 clamp 进工作区 | `src/core/Utils.cpp:90-185` |
| 单轴钳位 | `ClampWindowCoordinate(coord, extent, workStart, workEnd, gap)`；`maximum < minimum` 时返回 `workStart` | `src/core/Utils.cpp:83-88` |
| 对齐规则 | 左右并排时**顶部对齐**（`pos.y = anchor.top`）；上下堆叠时**左对齐** | `src/core/Utils.cpp:140-177` |
| z-order | `SetWindowPos(..., SWP_NOSIZE \| SWP_NOZORDER \| SWP_NOACTIVATE)`，不抢焦点、不改层级 | `src/core/Utils.cpp:183-184` |
| 触发时机 | 三重保险：`PSCB_INITIALIZED`（覆盖系统自动居中）→ `WM_SHOWWINDOW` → 页面布局完成消息 | `TranslationSettingsPage.cpp:397-442` |

### 3.2 参考实现的"三重保险"是否要照搬？

**不需要**。zencrop 之所以要定位三次，是因为 `PropertySheetW` 会先把自己居中一次，必须在 `PSCB_INITIALIZED` 里覆盖它。VoxType 的窗口是自建 `CreateWindowExW`，**初始位置就是唯一位置**，只需在创建前算一次。照搬子类化只会引入无意义的复杂度。

### 3.3 参考实现的两个粗糙点（VoxType 不复刻）

1. `gap = 10` 是**裸像素**，不随 DPI 缩放（`Utils.cpp:127`）；zencrop 自己也在别处用 `ScaleForDpi(10, dpi)`。VoxType 必须走 `S()`。
2. 无单元测试覆盖（`tests/` 下无 `PositionWindowNearAnchor` 用例）。VoxType 应补纯函数测试（见 §5.5）。

### 3.4 方法差异清单

| 维度 | zencrop | VoxType 本次方案 |
| :--- | :--- | :--- |
| 定位入口 | 窗口创建/显示后 `SetWindowPos` | 创建前计算 `POINT`，随 `CreateWindowExW` 传入（保持现有结构，最小改动） |
| 触发次数 | 3 次 | 1 次 |
| 对齐 | 侧边并排时顶部对齐 | 侧边并排时**垂直居中**（理由见 §5.1） |
| 间距 | 固定 10px | `S(UiStyle::DialogAnchorGap = 12)`，DPI 自适应 |
| 空间不足 | 允许重叠（clamp 兜底） | 允许重叠 + **锚点最小位移让位**（§5.3，可选增强） |

---

## 4. 目标与验收标准

### 4.1 目标

二级对话框表现为主 Settings 窗口的**侧边附属面板**：

1. 默认出现在主窗口**右侧**，与主窗口垂直居中对齐，间距 12 DIP；
2. 右侧放不下 → 左 → 下 → 上；四向都放不下 → 取剩余空间最大的一侧；
3. 任何情况下对话框**完整位于工作区内**（工作区小于对话框的极端 DPI 除外，见 §6 P2）；
4. 不改变模态语义、不引入 `TOPMOST`、不抢焦点。

### 4.2 验收标准（可验证）

| 编号 | 判据 |
| :--- | :--- |
| A1 | 1920×1080@150%（主窗口居中）下打开 Qwen/Volc/Prompt/Input 任一对话框，**对话框矩形与主窗口矩形无交集**（让位策略生效，见 §5.3） |
| A2 | **零位移不变量**：只要四向锚定能给出与主窗口无交集的位置（含 2560×1440@150%、1920×1080@100% 等空间充足场景），打开/关闭对话框时主窗口位移必须为 **0**（无任何视觉跳变）；让位只在"空间不足"时发生 |
| A3 | 对话框完整位于 `GetWorkAreaForWindow(parent)` 内（x、y 与右/下边均不越界） |
| A4 | 关闭对话框后，主窗口回到打开前的像素位置（`GetWindowRect` 前后一致） |
| A5 | 模态行为不变：对话框期间主窗口 `IsWindowEnabled()==FALSE`；无 `HWND_TOPMOST`；对话框获得焦点 |
| A6 | `build.bat` 全量构建通过（含 `tools\check_architecture.ps1` 18 项守卫）；新增 `dialog_positioning_test` 通过；`validate_settings_layout.ps1` 在 96/144/192/288 DPI 下通过 |

---

## 5. 详细设计

### 5.1 新增纯计算模块 `src/ui/dialog_positioning.{h,cpp}`

抽出与 Win32 窗口管理无关的纯计算函数，便于单元测试（沿用 `hud_pagination` 的"UI 层纯逻辑模块"模式，该模块同时被主程序和独立测试 target 引用）：

```cpp
// src/ui/dialog_positioning.h
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// 四向锚定定位：Right -> Left -> Below -> Above -> 剩余空间最大者。
// 左右并排时与锚点垂直居中对齐；上下堆叠时与锚点左对齐。
// 结果始终 clamp 进 workArea（若窗口比工作区大，则贴工作区左上）。
POINT ComputeAnchoredDialogPos(const RECT& anchor, const RECT& workArea,
                               int width, int height, int gap);

// 让位计算：返回锚点窗口为"与对话框水平并排放下"所需的新 left 值。
// 若水平并排不可行（workArea 宽度不足），返回 anchor.left（不改动）。
int ComputeAnchorShiftForSideBySide(const RECT& anchor, const RECT& workArea,
                                    int dialogWidth, int gap);
```

算法（`dialog_positioning.cpp`）：

```cpp
namespace {
int ClampAxis(int coord, int extent, int workStart, int workEnd) {
    const int maximum = (std::max)(workStart, workEnd - extent);
    return std::clamp(coord, workStart, maximum);
}
}  // namespace

POINT ComputeAnchoredDialogPos(const RECT& anchor, const RECT& workArea,
                               int width, int height, int gap) {
    const int anchorH = anchor.bottom - anchor.top;
    const int centeredY = anchor.top + (anchorH - height) / 2;

    const bool rightFits = (anchor.right + gap + width <= workArea.right);
    const bool leftFits = (anchor.left - gap - width >= workArea.left);
    const bool belowFits = (anchor.bottom + gap + height <= workArea.bottom);
    const bool aboveFits = (anchor.top - gap - height >= workArea.top);

    POINT pos = {};
    if (rightFits) {
        pos = { anchor.right + gap, centeredY };
    } else if (leftFits) {
        pos = { anchor.left - gap - width, centeredY };
    } else if (belowFits) {
        pos = { anchor.left, anchor.bottom + gap };
    } else if (aboveFits) {
        pos = { anchor.left, anchor.top - gap - height };
    } else {
        // 四向皆不可：取剩余空间最大的一侧（并列时右侧优先）
        const int rightSpace = workArea.right - (anchor.right + gap);
        const int leftSpace = (anchor.left - gap) - workArea.left;
        const int belowSpace = workArea.bottom - (anchor.bottom + gap);
        const int aboveSpace = (anchor.top - gap) - workArea.top;
        int best = rightSpace;
        pos = { anchor.right + gap, centeredY };
        if (leftSpace > best) { best = leftSpace; pos = { anchor.left - gap - width, centeredY }; }
        if (belowSpace > best) { best = belowSpace; pos = { anchor.left, anchor.bottom + gap }; }
        if (aboveSpace > best) { best = aboveSpace; pos = { anchor.left, anchor.top - gap - height }; }
    }
    pos.x = ClampAxis(pos.x, width, workArea.left, workArea.right);
    pos.y = ClampAxis(pos.y, height, workArea.top, workArea.bottom);
    return pos;
}
```

**与参考实现的两处刻意差异**：

1. 侧边并排时用**垂直居中**（`centeredY`）而非顶部对齐：Qwen（900）/Volc（800）对话框高于主窗口（740），顶部对齐会让底部多溢出 80~160 DIP，垂直居中可最小化溢出量；
2. `gap` 作为参数传入（调用方用 `S()` 折算），避免 zencrop 的"裸像素间距"问题。

### 5.2 定位接入函数（`settings_dialogs.cpp` 匿名命名空间）

替换 `CalculateCenteredDialogPos`（`settings_dialogs.cpp:138-155`，全量 4 个调用点）：

```cpp
struct AnchoredPlacement {
    POINT dialogPos = {};        // 对话框位置
    bool moveAnchor = false;     // 是否启用让位（见 5.3）
    POINT anchorPos = {};        // 让位后锚点窗口位置
};

AnchoredPlacement CalculateAnchoredPlacement(HWND anchorWnd, int width, int height) {
    RECT work = GetWorkAreaForWindow(anchorWnd);
    RECT anchor = work;
    if (anchorWnd) GetWindowRect(anchorWnd, &anchor);
    const int gap = S(UiStyle::DialogAnchorGap);
    const int anchorW = anchor.right - anchor.left;

    AnchoredPlacement placement;
    placement.dialogPos = ComputeAnchoredDialogPos(anchor, work, width, height, gap);

    const RECT dlg = { placement.dialogPos.x, placement.dialogPos.y,
                       placement.dialogPos.x + width, placement.dialogPos.y + height };
    const bool overlaps = dlg.left < anchor.right && dlg.right > anchor.left &&
                          dlg.top < anchor.bottom && dlg.bottom > anchor.top;
    if (overlaps && anchorWnd) {
        const int shiftedLeft = ComputeAnchorShiftForSideBySide(anchor, work, width, gap);
        if (shiftedLeft != anchor.left) {
            placement.moveAnchor = true;
            placement.anchorPos = { shiftedLeft, anchor.top };
            const RECT shifted = { shiftedLeft, anchor.top, shiftedLeft + anchorW, anchor.bottom };
            placement.dialogPos = ComputeAnchoredDialogPos(shifted, work, width, height, gap);
        }
    }
    return placement;
}
```

### 5.3 让位策略（锚点最小位移）——**D1 已确认启用；仅"空间不足"时触发**

> **决策记录（D1，2026-09-28）**：启用让位，但**空间足够时主窗口不得有任何位移**。
> 让位是"四向锚定失败"之后的**唯一补救路径**，不是默认行为。

让位的进入条件是以下三条**同时成立**（缺一即完全不移动主窗口）：

1. 四向锚定（Right → Left → Below → Above）**全部失败**——只要任一向成功，其结果必然与锚点无交集，直接返回、主窗口零位移；
2. 按"剩余空间最大侧"摆放并经工作区 clamp 后，最终矩形与锚点**仍有交集**（以最终矩形为唯一事实来源，不做重复的方向判断）；
3. 水平并排**原理上可行**（`mainW + gap + dlgW ≤ workW`）。

满足三条后，把主窗口沿水平方向移动到满足"对话框恰好放进右侧"的**最小位移**位置：

```cpp
int ComputeAnchorShiftForSideBySide(const RECT& anchor, const RECT& workArea,
                                    int dialogWidth, int gap) {
    const int anchorW = anchor.right - anchor.left;
    if (anchorW + gap + dialogWidth > workArea.right - workArea.left) {
        return anchor.left;  // 水平并排不可行：保持不动，允许重叠（clamp 兜底）
    }
    const int latest = workArea.right - gap - dialogWidth - anchorW;
    return (std::min)(anchor.left, (std::max)(workArea.left, latest));
}
```

- 只需**左移**（缩小 `anchor.left` 总能满足右侧约束），因此用户不会看到"主窗口向右跳"；
- 垂直位置完全不动。

**回位**：用 RAII 保证所有返回路径（含 `CreateWindowExW` 失败提前 return）都恢复原位：

```cpp
class AnchorShiftGuard {
public:
    AnchorShiftGuard(HWND anchor, bool shift, POINT target) : anchor_(anchor) {
        if (!shift || !anchor_) return;
        RECT rc = {};
        if (!GetWindowRect(anchor_, &rc)) return;
        original_ = { rc.left, rc.top };
        moved_ = true;
        SetWindowPos(anchor_, nullptr, target.x, target.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ~AnchorShiftGuard() {
        if (moved_) {
            SetWindowPos(anchor_, nullptr, original_.x, original_.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    AnchorShiftGuard(const AnchorShiftGuard&) = delete;
    AnchorShiftGuard& operator=(const AnchorShiftGuard&) = delete;
private:
    HWND anchor_ = nullptr;
    POINT original_ = {};
    bool moved_ = false;
};
```

四个 `Show*` 的最终形态（以 Qwen 为例，其余三个同构）：

```cpp
    UpdateUiScale(parent);
    data.ok = false;
    const int width = S(UiStyle::QwenAdvancedDialogW);
    const int height = S(UiStyle::QwenAdvancedDialogH);
    const AnchoredPlacement placement = CalculateAnchoredPlacement(parent, width, height);
    AnchorShiftGuard anchorGuard(parent, placement.moveAnchor, placement.anchorPos);

    HWND dialog = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
                                  L"VoxTypeQwenAdvancedDlg", L"Qwen ASR Advanced Settings",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                  placement.dialogPos.x, placement.dialogPos.y,
                                  width, height, parent, nullptr, hInst, &state);
    if (!dialog) return false;                 // guard 析构自动回位
    RunModalDialogLoop(dialog, parent);
    return data.ok;                            // guard 析构自动回位
```

> 让位 + 回位意味着用户会看到"主窗口水平让开一点 → 对话框出现在右侧 → 关闭后主窗口回位"的两次位移。这是"完全不遮挡"在 1920 @150% 下的**唯一可行解**（数学证明见 §6 场景 2）。若不接受该行为，则 1920@150% 场景下只能退化为"对话框贴右但重叠 245px（约主窗口 29% 宽度）"。

### 5.4 新增 UiStyle 常量

```cpp
// ── 二级对话框锚定定位 ─────────────────────────────────────────────
// 对话框与主 Settings 窗口之间的间距（DIP）。必须经 S() 折算，禁止裸像素。
constexpr int DialogAnchorGap = 12;
```

放在 `src/ui/ui_types.h` 主窗口常量区（`SettingsWindowNonClientReserveH` 之后，`:29`）。

### 5.5 单元测试 `tests/dialog_positioning_test.cpp` + CMake 目标

仿 `hud_pagination_test` 模式（`CMakeLists.txt:320-333`）新增 target，编译 `src/ui/dialog_positioning.cpp`，输出到 `artifacts/tests`，显式带 `/O2 /EHsc /utf-8`（守卫硬性要求）：

```cmake
add_executable(dialog_positioning_test
    tests/dialog_positioning_test.cpp
    src/ui/dialog_positioning.cpp
)
target_include_directories(dialog_positioning_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/ui"
)
if(MSVC)
    target_compile_options(dialog_positioning_test PRIVATE /O2 /EHsc /utf-8)
endif()
set_target_properties(dialog_positioning_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../../artifacts/tests"
)
```

用例（纯整数断言，无窗口、无消息循环）：

| # | 输入 | 期望 |
| :--- | :--- | :--- |
| 1 | 右侧放得下 | `x == anchor.right + gap`，`y == anchor.top + (anchorH - h)/2` |
| 2 | 右侧不够、左侧够 | `x == anchor.left - gap - w` |
| 3 | 左右都不够、下方够 | `x == anchor.left`，`y == anchor.bottom + gap` |
| 4 | 仅上方够 | `y == anchor.top - gap - h` |
| 5 | 四向皆不可 | 取剩余空间最大侧 + 结果仍在工作区内 |
| 6 | 四向皆不可且窗口大于工作区 | 贴工作区左上角（`ClampAxis` 退化路径） |
| 7 | 让位：1920 工作区、anchor.left=535、主 850、dlg 780、gap 12 | 返回 `278` |
| 8 | 让位：水平并排不可行（工作区 1366） | 返回 `anchor.left`（不动） |
| 9 | 让位：anchor 已靠左（left=40，空间充足） | 返回 `40`（不动） |
| 10 | 用例 1~4 的输入（四向任一成功） | 结果矩形与 `anchor` **无交集**（测试内手写相交断言）⇒ 证明空间充足场景不会进入让位评估 |

同时更新 `settings_dialogs.cpp` 顶部 include 与本模块的文件头注释（说明模块职责与参考实现来源）。

### 5.6 不改动的部分（明确边界）

- `RunModalDialogLoop`（模态语义）、`WM_DPICHANGED` 各分支（系统 `suggested` rect）、窗口样式（`WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME`）、各对话框尺寸常量——全部不动；
- 不加"跟随主窗口移动"逻辑（模态期间主窗口不可拖）;
- 不引入非模态（`PSH_MODELESS` 等）语义。

---

## 6. 场景矩阵（数学推演，gap = 12 DIP）

> 假设工作区 = 屏幕 − 任务栏（示意值）；`S()` 用四舍五入。

| # | 分辨率/DPI | Scale | 主窗口（px）与位置 | 对话框（px） | 右侧剩余空间 | 结果 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1920×1080 @100%（96 DPI） | 0.6667 | 567×493 居中 @ (676, 273) | Qwen 520×600 | `1920−1243 = 677 ≥ 532` ✓ | **贴右，不遮挡，主窗口不动** |
| 2 | 1920×1080 @150%（144 DPI） | 1.0 | 850×740 居中 @ (535, 150) | Qwen 780×900 | `535 < 792` ✗；左 535 ✗；下 150 ✗；上 138 ✗ | 四向皆否 → **让位**：主窗口左移至 x=278（位移 257px），对话框 @ x=1140 恰好并排，**不遮挡** |
| 3 | 2560×1440 @150% | 1.0 | 850×740 居中 @ (855, 330) | Qwen 780×900 | `855 ≥ 792` ✓ | **贴右，不遮挡，主窗口不动** |
| 4 | 1920×1080 @200%（192 DPI） | 1.3333 | 1133×987 居中 @ (393, 26) | Qwen 1040×1200 | 高 1200 > 1040，**垂直溢出** | clamp：贴工作区顶 + 右侧最大空间（P2 已知限制，见 §7） |
| 5 | 3840×2160 @200% | 1.3333 | 1133×987 居中 @ (1353, 546) | Qwen 1040×1200 | `1354 ≥ 1052` ✓ | **贴右，不遮挡，主窗口不动** |

场景 2 关键不等式（让位可行性）：`mainW + gap + dlgW = 850 + 12 + 780 = 1642 ≤ 1920` ✓
场景 4 关键不等式（无解性）：`900 × 1.3333 = 1200 > 1040`，**任何定位策略都无法让对话框完整可见**。

---

## 7. 已知限制（P2，不在本次范围）

**对话框高度 > 工作区高度**（如 1920×1080@200%：Qwen 1200px > work 1040px，footer 上的 OK/Cancel 落在屏幕外）属于**既有缺陷**（当前居中策略下同样溢出，且上下同时裁掉），修复需"高度自适应收缩 + 内容区滚动"的独立方案，不建议并入本次定位改动。

本次对该场景只保证：不超出工作区顶部、水平方向取最大可用空间（比现状更好，但不消除溢出）。

---

## 8. 验证计划

1. **构建** ✅：`build.bat` 全量构建通过，18 项架构守卫全部 PASS（`main.cpp` 148/150、`settings.cpp` 387/400、跨层 include 1/2 均未变）。
2. **单元测试** ✅：`build.bat --test` 构建并运行 `build\artifacts\tests\dialog_positioning_test.exe` → `all checks passed`（10 组用例）。
   注：测试采用显式 `VT_CHECK` 宏而非 `assert()`——`CMAKE_BUILD_TYPE=Release` 下 `NDEBUG` 会禁用 `assert`，故既有测试风格在此不可靠。
3. **静态布局校验** ✅：`validate_settings_layout.ps1` 在 **96 / 144 / 192 / 288 DPI** 下通过（由 `build.bat` 主流程自动执行）。
4. **人工验证矩阵** ⏳ **待人工**（需真实 GUI 交互）：
   - Qwen / Volc / Prompt / Input 四个对话框 × {1920@100%、1920@150%、2560@150%}；
   - 逐个检查 A1~A5（尤其：关闭后主窗口回位是否像素一致、对话框是否抢焦点、模态是否生效）；
   - 将主窗口手动拖到屏幕最右 / 最左后再打开对话框，确认降级方向（左 / 下 / 上）正确；
   - 双显示器（不同 DPI，如 144 + 96）：确认对话框始终与主窗口同屏（`GetWorkAreaForWindow(parent)` 已按 parent 显示器取 work）。
5. **文档同步（DoD）**：`ARCHITECTURE.md` 的 UI 层模块清单补充 `src/ui/dialog_positioning.*` 一行与职责说明。

---

## 9. 风险与回滚

| 风险 | 影响 | 缓解 |
| :--- | :--- | :--- |
| 主窗口让位产生视觉位移（D1 启用时） | 用户观感变化 | 仅在"无重叠不可能"时触发；位移最小化；关闭后 RAII 回位；宽屏/低 DPI 场景零位移 |
| 让位与回位之间用户手动移动了主窗口 | 回位覆盖用户操作 | 模态期间主窗口被 `EnableWindow(FALSE)`，用户**无法**移动 ⇒ 不成立 |
| 定位后 `CreateWindowExW` 因 DPI 变化调整外框 | 位置偏差 | 外框尺寸即传入的 `width/height`（顶层窗口语义），与锚定计算同源，无偏差 |
| 多显示器 | 对话框被放到别屏 | work 与 anchor 均取自 `MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST)`，天然同屏 |

**回滚**：改动集中在 1 个新增模块 + 4 处一行替换 + 1 个常量，`git revert` 单提交即可完全复原。

---

## 10. 备选方案（已评估，不采用）

| 方案 | 说明 | 不采用理由 |
| :--- | :--- | :--- |
| B1 对话框改为主窗口内嵌侧栏（加宽主窗口 + 子窗口换页） | 彻底消除窗口间关系 | 需重写 4 个 `XxxWndProc` 与模态循环，改动量是本次的 10 倍以上 |
| B2 修改 `ShowSettingsWindow` 默认位置为"屏幕偏左"，为右侧预留空间 | 无跳变 | 改变所有用户的默认观感（主窗口不再居中），影响面超出本缺陷 |
| B3 缩小 Qwen/Volc 对话框尺寸到 ≤ 740 高 | 简单直接 | 控件是固定坐标布局，压缩需重排并删减留白，且 1920@150% 下宽度仍放不下 |
| B4 照搬 zencrop 的 `PropertySheet` + 三重定位 | "与参考一致" | VoxType 自建窗口无需三次定位，反而引入无谓复杂度 |

---

## 11. 待决策事项

| 编号 | 事项 | 选项 | 建议 |
| :--- | :--- | :--- | :--- |
| **D1** | 是否启用 §5.3 主窗口"最小位移让位 + 关闭回位" | **已确认（2026-09-28）：启用，并附加硬约束——空间足够时严格零位移**。让位仅在"四向锚定全部失败且结果仍重叠"时评估（§5.3 三条判据） | 决策已锁定，按 §5.3 实现 |
| D2 | 是否顺带移除二级对话框的 `WS_EX_APPWINDOW`（使其不在任务栏单独占位，贴近 zencrop 属性表行为） | A. 保留现状<br>B. 移除 | **A**（本次不动）：影响 Alt+Tab / 任务栏行为，属独立体验调整 |

---

## 12. 落地检查清单

- [x] `src/ui/dialog_positioning.h/.cpp` 新增（含文件头注释、`ComputeAnchoredDialogPos`、`ComputeAnchorShiftForSideBySide`）
- [x] `src/ui/ui_types.h` 新增 `DialogAnchorGap = 12`（并写明"必须经 `S()` 折算"）
- [x] `src/ui/settings_dialogs.cpp`：删除 `CalculateCenteredDialogPos`，新增 `AnchoredPlacement` + `CalculateAnchoredPlacement` + `AnchorShiftGuard`
- [x] 4 个 `Show*` 调用点替换（`settings_dialogs.cpp:1241 / :1275 / :1312 / :1355`）并传入 `AnchorShiftGuard`
- [x] `CMakeLists.txt` 新增 `dialog_positioning_test` target（含 `/O2 /EHsc /utf-8` 与 `RUNTIME_OUTPUT_DIRECTORY`），并加入 `build.bat --test` 的构建与运行列表（避免成为"无人构建/运行"的死测试）
- [x] `tests/dialog_positioning_test.cpp` 10 组用例（含 #10 空间充足场景的零位移交集断言）
- [x] `build.bat --test` 全量构建 + 全部离线回归测试 PASS（含 `dialog_positioning_test: all checks passed`）+ 18 项守卫 PASS
- [x] `validate_settings_layout.ps1` 96/144/192/288 DPI 通过
- [x] `ARCHITECTURE.md` UI 层清单同步；`AGENTS.md` 增补【A】类不变量
- [ ] 零位移不变量人工验证（A2/A4）**待人工**：空间充足场景（2560×1440@150% / 1920×1080@100%）下打开再关闭对话框，主窗口 `GetWindowRect` 前后完全一致；空间不足场景（1920×1080@150%）下确认主窗口仅左移、关闭后回位
