# VoxType Settings 界面现代化重构方案：现代 Win32 / C++23 Fluent 架构设计

**文档状态**：方案设计（深度调研与架构蓝图）  
**文档路径**：`.plan/feat/modern-win32-fluent-settings-architecture.md`  
**基准日期**：2026-09-18  
**核心目标**：跳出陈旧参考项目框架，调研业界最先进、现代且轻量的 Windows 32 (Win32 / C++23) UI 设计方法，彻底解决 VoxType 现有 Settings 冗余复杂痛点，实现 **Windows 11 Fluent 现代视觉风格 + 极低常驻开销（~12MB） + 100% 原生中文 IME 零风险 + C++23 声明式数据驱动**。

---

## 目录
1. [现有 Settings 架构痛点深度取证](#1-现有-settings-架构痛点深度取证)
2. [业界现代 Windows 32 UI 技术栈与架构全景对比](#2-业界现代-windows-32-ui-技术栈与架构全景对比)
3. [目标架构设计：Win32 Modern Fluent 体系](#3-目标架构设计win32-modern-fluent-体系)
   - 3.1 [视觉层：Windows 11 Fluent 纯原生自绘系统](#31-视觉层windows-11-fluent-纯原生自绘系统)
   - 3.2 [核心输入保障：原生 EDIT 穿透与 IME 零风险架构](#32-核心输入保障原生-edit-穿透与-ime-零风险架构)
   - 3.3 [数据与声明层：C++23 强类型元数据注册表 (Schema-Driven)](#33-数据与声明层c23-强类型元数据注册表-schema-driven)
   - 3.4 [几何与布局层：轻量 Flexbox 约束布局引擎](#34-几何与布局层轻量-flexbox-约束布局引擎)
4. [模块化目录与组件解耦规划](#4-模块化目录与组件解耦规划)
5. [平滑迁移路线与阶段验收指标](#5-平滑迁移路线与阶段验收指标)
6. [关键决策点与排坑要点 (Gotchas)](#6-关键决策点与排坑要点-gotchas)

---

## 1. 现有 Settings 架构痛点深度取证

对 VoxType 现有 UI 代码库（`src/ui/settings.cpp`、`src/ui/settings_controls.cpp`、`src/ui/settings_dialogs.cpp`、`src/ui/ui_types.h`）进行端到端取证，总结出以下五大结构性病灶：

### 1.1 庞大的单文件巨石（3439 行 Monolith）
* **代码规模**：`src/ui/settings.cpp` 包含 **3439 行**，`settings_dialogs.cpp` 约 **1000 行**，`ui_types.h` 散落 **229 个硬编码常量**。
* **职责严重越界**：单个文件内交织了：
  1. 125 个控件 IDC 的绝对像素坐标硬编码；
  2. 15 个全局 `std::vector<HWND>` 容器的手工可见性遍历控制；
  3. 巨型 `switch (LOWORD(wParam))` 包含 50+ 个消息命令分支；
  4. 7 种云端 ASR Provider 的子界面切换、动态隐藏与提示更新；
  5. 甚至内联了网络测试（`TestLlmConnection`）、路径探测与正则校验逻辑。

```cpp
// src/ui/settings.cpp:802
void ShowSettingsPage(HWND hwnd, int page) {
    for (HWND control : g_generalControls) ShowWindow(control, page == 0 ? SW_SHOW : SW_HIDE);
    for (HWND control : g_recognitionControls) ShowWindow(control, page == 1 ? SW_SHOW : SW_HIDE);
    for (HWND control : g_cloudAsrControls) ShowWindow(control, page == 2 ? SW_SHOW : SW_HIDE);
    for (HWND control : g_llmControls) ShowWindow(control, page == 3 ? SW_SHOW : SW_HIDE);
    for (HWND control : g_promptControls) ShowWindow(control, page == 4 ? SW_SHOW : SW_HIDE);
    if (page == 2) {
        ShowCloudSubPage(hwnd, g_cloudProviderIdx);
    } else {
        for (HWND c : g_baiduControls) ShowWindow(c, SW_HIDE);
        for (HWND c : g_volcengineControls) ShowWindow(c, SW_HIDE);
        // ...逐个循环遍历隐藏
    }
}
```

### 1.2 "新增一个配置项需动 10 处"的样板地狱
在现有架构中，为设置界面新增一个简单的配置项（例如一个布尔开关或文本输入框），开发者必须在 4 个文件之间跳跃修改 9~10 处：
1. `src/core/config_store.h`：在 `struct Config` 中声明字段；
2. `src/core/config_store.cpp` (`LoadConfig`)：编写 JSON 解析逻辑；
3. `src/core/config_store.cpp` (`SaveConfig`)：编写 JSON 序列化拼接；
4. `src/ui/ui_types.h`：手算并新增控件的 `X, Y, W, H` 常量；
5. `src/ui/settings_controls.h`：新增控件 IDC 宏定义；
6. `src/ui/settings.cpp` (`WM_CREATE`)：调用 `CreateWindowExW` 传入绝对坐标；
7. `src/ui/settings.cpp`：调用 `ApplyUiFont` 绑定字体；
8. `src/ui/settings.cpp`：手动推入对应的可见性 `vector<HWND>` 容器；
9. `src/ui/settings.cpp` (`LoadSettingsControls`)：将 Config 内存值写入 HWND；
10. `src/ui/settings.cpp` (`SaveSettingsControls`)：从 HWND 读取内容赋值回 Config。

**代价**：90% 的代码是纯机械胶水。只要漏改一处，就会发生配置无法保存、UI 错位或句柄悬空的隐蔽 Bug。

### 1.3 静态索引寻址与手算坐标的脆弱性
* 布局完全基于硬编码的行高与索引：
  ```cpp
  constexpr int RowInputY(int row) { return FirstRowY + row * RowHeight; }
  ```
* **中间插行惩罚**：若在某个组的第 1 行与第 2 行之间插入新设置项，该组后续所有控件的 `RowInputY(n)` 索引必须全部手工 `+1`。
* **组高固定死**：`GeneralShortcutGroupH = 170` 等均为常量。增减内容后若未同步更新组高，窗口即刻发生重叠。
* **以毒攻毒的脚本**：为了防止高 DPI 下控件碰撞，项目中诞生了 `validate_settings_layout.ps1`——专门用**正则表达式扒 C++ 源码文本**来反向推算几何重叠。这是布局系统缺乏运行时求值能力的典型症状。

### 1.4 窗口无弹性，无法拉伸 (WS_THICKFRAME 缺失)
* 窗口样式被锁死在 `WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN`，没有 `WS_THICKFRAME`。
* 窗口大小硬编码为 `850x740`。用户无法根据屏幕分辨率或个人视力自由缩放窗口。
* 当用户需要输入较长的 LLM Prompt、系统提示词或敏感词库时，小尺寸不可拉伸的文本框体验极其受促。

### 1.5 视觉风格停留在 Windows 2000 时代
* 采用系统原生 90 年代风格的 Checkbox（小方块打钩）、原生直角 ComboBox、顶部古旧的 `SysTabControl32`、灰白平板背景。
* 缺乏 Windows 11 现代 Fluent 设计规范（Mica/Mica Alt 材质、圆角卡片 Card 容器、现代 ToggleSwitch 开关、微交互高亮、左侧垂直导航 Rail）。

---

## 2. 业界现代 Windows 32 UI 技术栈与架构全景对比

为打破局限，我们针对当前业界最先进、轻量的 Windows 桌面 UI 方案进行了横向技术评估：

| 方案类别 | 代表技术 | 视觉效果 | 运行时内存 (RSS) | 中文 IME 支持度 | 维护成本与架构优势 | 综合裁定 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **A. 官方现代框架** | **WinUI 3 / Windows App SDK (XAML Islands)** | 极佳 (原生 Fluent) | **50 ~ 100 MB+** (宿主庞大) | 完善 | XAML 声明式组件化，但需要庞大框架依赖包，冷启动慢 | ❌ **否决**：严重违背 VoxType ~12MB 空闲基线与轻量单文件分发哲学 |
| **B. 内嵌 Web / 浏览器** | **WebView2 / Tauri / Electron** | 现代化 Web 生态 | **60 ~ 150 MB+** (多进程 Chromium) | 完善 | 前端生态极丰富，但常驻内存对系统后台输入法是灾难 | ❌ **绝对排除**：进程树臃肿，内存占用是现有程序的十倍 |
| **C. 第三方声明式 DSL** | **Slint C++** | 优秀 (现代声明式) | 14 ~ 18 MB (软件/Skia) | ⚠️ **存在在案未修复 Bug** (Issue #5982) | `.slint` 声明极其优雅，但构建依赖 Cargo，且 Windows 中文输入存在上游缺陷 | ⏸️ **暂不推荐**：作为中文字音输入法，IME 是致命生命线 |
| **D. 游戏/即时模式 UI** | **Dear ImGui** | 工具/调试器风格 | ~15 MB | 需外挂 IME 代码 | 极速无状态开发，但 UI 难以调教成 Windows 11 精致设置面板 | ❌ **否决**：不满足现代精致 Fluent 视觉诉求 |
| **E. 轻量嵌入式引擎** | **RmlUi (C++17/20)** | 优秀 (HTML/CSS Flex) | ~15 MB (轻量嵌入) | 良好 (2024 PR #541 已支持 Win32 IME) | 支持类 CSS 样式，但需维护模板胶水层与字体引擎适配 | 🔲 **备选**：适合需要复杂富文本排版的场景 |
| **F. 现代 Win32 Fluent 纯原生自绘 + C++23 Schema** | **Direct2D + DWM Mica + 原生 EDIT 穿透 + Yoga Flexbox** | **极佳 (Windows 11 原生 Fluent 卡片)** | **~12 MB (基线零增长)** | **100% 完美原生** (直接复用系统 IMM32 / TSF) | **声明式元数据注册表**，新增项仅需 1 行；Flexbox 自动求解几何；零额外外部运行时 | 🌟 **强烈推荐（唯一全优解）** |

### 为什么方案 F 是当今 Windows 32 领域最先进且最优雅的路线？
1. **零运行时代价**：不引入 Node.js、Chromium、.NET CLR、XAML 运行时或 Rust 工具链。纯原生 C++23 编译，体积增加 < 200KB，空闲内存保持 ~12MB。
2. **IME 零风险**：文本框采用 Windows 原生 `EDIT` 控件（做无边框背景穿透处理）。操作系统免费提供 100% 可靠的中文输入法候选框、光标闪烁、划词选区与辅助功能（Accessibility）。
3. **视觉直达 Windows 11 官方水准**：通过 DWM Mica 材质、Direct2D 圆角卡片、Segoe Fluent Icons 图标与左侧 Navigation Rail，呈现与 Windows 11 系统“设置”如出一辙的质感。
4. **架构开发效率跃升**：借助 C++23 强类型元数据反射，彻底消灭 10 步冗余样板，实现“1 行声明搞定持久化、UI 与布局”。

---

## 3. 目标架构设计：Win32 Modern Fluent 体系

新架构由四大核心支柱构成：

```
+---------------------------------------------------------------------------------------+
|                                 C++23 Settings Schema                                 |
|   - SettingRegistry: 静态/反射元数据 (JSON Key, 成员指针, 类型, 默认值, 标签, 校验规则)   |
+---------------------------------------------------------------------------------------+
                                           |
                   +-----------------------+-----------------------+
                   | (双向绑定)                                     | (驱动布局)
                   v                                               v
+------------------------------------+           +--------------------------------------+
|       ConfigStore Persistence      |           |     Flexbox Layout Engine (Yoga)     |
|   - 自动序列化 / 反序列化           |           |   - 纯几何求解 (DIP 矩形树)           |
|   - 无缝兼容现有 config.json        |           |   - 窗口拉伸/DPI自适应重排            |
+------------------------------------+           +--------------------------------------+
                                                                   |
                                           +-----------------------+
                                           v
+---------------------------------------------------------------------------------------+
|                           Modern Win32 Fluent View Host                               |
|  +---------------------------+  +--------------------------------------------------+  |
|  |   Left Navigation Rail    |  |               Right Settings Content Area        |  |
|  |  - Segoe Fluent Icons     |  |  - DWM Mica / Mica Alt 系统材质背景               |  |
|  |  - 选中态 Accent Pill      |  |  - Direct2D Fluent Cards (半透明圆角卡片)        |  |
|  |  - 模块化分页路由          |  |  - 现代 Toggle Switch (自定义无边框自绘控件)       |  |
|  |                           |  |  - 原生 EDIT 穿透 (保留 100% 系统 IME 中文输入)   |  |
|  +---------------------------+  +--------------------------------------------------+  |
+---------------------------------------------------------------------------------------+
```

---

### 3.1 视觉层：Windows 11 Fluent 纯原生自绘系统

#### 1. DWM 材质与暗色沉浸式系统
通过 Windows 11 原生 DWM API 接入 Mica 材质与深色模式，摆脱传统 GDI 灰色窗口：
```cpp
// 启用 Windows 11 官方 Mica 材质 (DWMSBT_TABBEDWINDOW / DWMSBT_MAINWINDOW)
enum DWM_SYSTEMBACKDROP_TYPE {
    DWMSBT_AUTO = 0,
    DWMSBT_NONE = 1,
    DWMSBT_MAINWINDOW = 2,      // Mica (主窗口材质)
    DWMSBT_TRANSIENTWINDOW = 3, // Acrylic (亚克力弹窗)
    DWMSBT_TABBEDWINDOW = 4     // Tabbed Mica Alt (适合带导航栏的设置窗口)
};

void ApplyModernWindowAttributes(HWND hwnd, bool isDarkMode) {
    // 1. 设置圆角窗口
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    // 2. 沉浸式暗色标题栏
    BOOL dark = isDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    // 3. 启用 Mica 材质 (Win11 22H2+)
    DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_TABBEDWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    // 4. 将系统边框延伸进客户区，使客户区透出 Mica
    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}
```

#### 2. 左侧导航面板 (Navigation Rail)
彻底抛弃顶部的 Windows 95 风格 `SysTabControl32`，改用 Windows 11 设置同款的**左侧导航栏**：
* 宽度：固定 220 ~ 240 DIP。
* 字体图标：采用 Windows 11 内置的 **Segoe Fluent Icons**（或 Segoe MDL2 Assets），字符包括：
  * 常规设置：`\uE713` (Settings)
  * 本地语音识别：`\uE720` (Microphone)
  * 云端 ASR：`\uE753` (Cloud)
  * AI 纠错与大模型：`\uE99A` (Sparkle / Brain)
  * 高级选项：`\uE71D` (Developer Tools)
* 视觉交互：鼠标悬停（Hover）呈现柔和的灰色胶囊背景；选中项左侧绘制一条 3px 宽的 Windows 主题色竖条（Active Accent Pill），并带有平滑切换效果。

#### 3. 设置卡片化布局 (Settings Card & Expander)
Windows 11 Settings 最核心的视觉元素是**设置卡片（Settings Card）**：
* **卡片底色**：
  * 暗色模式：`rgba(255, 255, 255, 0.05)`（微透，叠加底层 Mica）。
  * 亮色模式：`rgba(255, 255, 255, 0.70)`。
  * 边框：`1px rgba(255, 255, 255, 0.08)`，圆角半径 `8px`。
* **卡片内部排版**：
  * 左侧垂直排列：**主标题**（Segoe UI Variable 10pt SemiBold） + **副标题说明**（Segoe UI 8.5pt Regular 灰色）。
  * 右侧居中对齐：**操作控件**（Toggle Switch 开关、ComboBox 下拉框、Action Button）。
  * 卡片高度由内部内容自动测量推导，多行文本自动折行，绝不手算坐标。

#### 4. Fluent 现代药丸开关 (Toggle Switch)
编写轻量自定义控件 `FluentToggle`，替代原生的方块 Checkbox：
* 尺寸：`44 x 22 DIP`。
* 状态自绘：
  * `Off` 状态：灰色中空边框（1px），白色滑块位于左侧。
  * `On` 状态：Windows Accent 色（如蓝色 `#0078D4`）实心填充，白色滑块滑向右侧。
  * 支持键盘 Tab 焦点与 Space 切换。

---

### 3.2 核心输入保障：原生 EDIT 穿透与 IME 零风险架构

#### 为什么必须保留原生 EDIT 控件？
* 中文语音输入法最关键的就是**文字输入**（Prompt 编写、敏感词替换、热词表）。
* 自绘文本框（如很多个人自研 GUI 或游戏 UI）最大的陷阱是 **Windows TSF / IMM32 中文输入法接口极其繁复**：
  * 预编辑字符串（Composition String）高亮下划线；
  * 候选框（Candidate Window）精确定位跟随光标；
  * 退格键、选区、分词替换；
  * 微软拼音、微信输入法、搜狗输入法兼容性各异。
* **破局妙招（穿透技术）**：
  1. 维持 Windows 原生 `EDIT` 控件句柄；
  2. 移除原生粗糙边框：去掉 `WS_BORDER` 与 `WS_EX_CLIENTEDGE`；
  3. 背景透明化：在父窗口处理 `WM_CTLCOLOREDIT`，返回空画刷或卡片画刷，让其融入卡片；
  4. 外层轮廓自绘：在 `EDIT` 的父窗口背景上，由 Direct2D 绘制现代圆角矩形输入框轮廓；当 `EDIT` 获得焦点（`WM_SETFOCUS`）时，父窗口在轮廓外圈绘制 `2px` 的 Windows Accent 强调发光外环。
* **成果**：**外观是现代 Fluent 输入框，底层是 100% 官方经过 30 年验证的原生中文 IME 体验**。

---

### 3.3 数据与声明层：C++23 强类型元数据注册表 (Schema-Driven)

彻底消灭“新增配置改动 10 处”的核心武器是 **C++23 元数据描述符与字段注册表**。

#### 1. 设置项元数据定义 (`SettingDescriptor`)
```cpp
// src/ui/settings/settings_schema.h
#pragma once
#include <string>
#include <functional>
#include <variant>
#include "config_store.h"

enum class SettingCtrlType {
    Toggle,        // 药丸开关
    TextEdit,      // 单行输入
    PasswordEdit,  // 密码输入 (带眼睛图标切换可见性)
    MultilineEdit, // 多行文本 (Prompt 等)
    ComboBox,      // 下拉菜单
    ActionButton   // 独立动作按钮
};

struct SettingItem {
    std::string key;                   // JSON key (如 "volc_enable_context")
    std::wstring label;                // 显示标题 (如 L"启用历史上下文")
    std::wstring description;          // 辅助说明副标题
    SettingCtrlType ctrlType;          // 控件类型
    
    // C++23 成员指针联合体，精准映射 Config 字段
    std::variant<
        bool Config::*,
        int Config::*,
        std::string Config::*,
        std::wstring Config::*
    > memberPtr;

    // 可选：下拉菜单项候选值
    std::vector<std::pair<std::wstring, int>> comboOptions = {};

    // 可选：动作按钮回调
    std::function<void(HWND parentHwnd)> onActionClick = nullptr;

    // 可选：动态可见性/可用性断言 (如仅当 provider == Qwen 时可见)
    std::function<bool(const Config&)> isVisible = nullptr;
};

struct SettingGroup {
    std::wstring groupTitle;
    std::vector<SettingItem> items;
};

struct SettingPage {
    int pageId;
    std::wstring title;
    wchar_t iconChar;                  // Segoe Fluent Icons 码点
    std::vector<SettingGroup> groups;
};
```

#### 2. 声明式注册清单 (`settings_schema.cpp`)
新增一个配置项，**仅需在对应的 Page 数组中追加一行描述符**：
```cpp
// src/ui/settings/settings_schema.cpp
const std::vector<SettingPage>& GetSettingsPagesDefinition() {
    static const std::vector<SettingPage> pages = {
        {
            .pageId = 0,
            .title = L"常规设置",
            .iconChar = L'\uE713',
            .groups = {
                {
                    .groupTitle = L"快捷键与交互",
                    .items = {
                        {
                            .key = "auto_start",
                            .label = L"开机自启动",
                            .description = L"登录 Windows 时静默启动并在托盘常驻",
                            .ctrlType = SettingCtrlType::Toggle,
                            .memberPtr = &Config::autoStart
                        },
                        {
                            .key = "diagnostics_mode",
                            .label = L"诊断录音捕获",
                            .description = L"保存近几次录音 PCM 副本用于问题排查",
                            .ctrlType = SettingCtrlType::Toggle,
                            .memberPtr = &Config::diagnosticsMode
                        }
                    }
                }
            }
        },
        {
            .pageId = 1,
            .title = L"语音识别引擎",
            .iconChar = L'\uE720',
            .groups = {
                {
                    .groupTitle = L"本地识别引擎 (sherpa-onnx)",
                    .items = {
                        {
                            .key = "model_dir",
                            .label = L"本地模型目录",
                            .description = L"存储 sherpa-onnx 识别模型及配置的文件夹",
                            .ctrlType = SettingCtrlType::TextEdit,
                            .memberPtr = &Config::modelDir
                        }
                    }
                }
            }
        }
        // ...其他页面
    };
    return pages;
}
```

#### 3. 自动双向绑定器 (`SettingsBinder`)
有了上述描述表后，所有的同步代码都变成极简的范型遍历，**彻底干掉 400 行手写的 `LoadSettingsControls` 与 350 行手写的 `SaveSettingsControls`**：
```cpp
// 自动加载：Config 结构体 -> UI 控件
void SettingsBinder::LoadToUI(const Config& config, const std::vector<SettingPage>& pages) {
    for (const auto& page : pages) {
        for (const auto& group : page.groups) {
            for (const auto& item : group.items) {
                HWND hCtrl = FindControl(item.key);
                if (!hCtrl) continue;

                std::visit([&](auto&& ptr) {
                    using T = std::decay_t<decltype(ptr)>;
                    if constexpr (std::is_same_v<T, bool Config::*>) {
                        FluentToggle_SetState(hCtrl, config.*ptr);
                    } else if constexpr (std::is_same_v<T, std::wstring Config::*>) {
                        SetWindowTextW(hCtrl, (config.*ptr).c_str());
                    } else if constexpr (std::is_same_v<T, std::string Config::*>) {
                        std::wstring wide = Utf8ToWide(config.*ptr);
                        SetWindowTextW(hCtrl, wide.c_str());
                    }
                }, item.memberPtr);
            }
        }
    }
}

// 自动保存：UI 控件 -> Config 结构体
void SettingsBinder::SaveFromUI(Config& config, const std::vector<SettingPage>& pages) {
    for (const auto& page : pages) {
        for (const auto& group : page.groups) {
            for (const auto& item : group.items) {
                HWND hCtrl = FindControl(item.key);
                if (!hCtrl) continue;

                std::visit([&](auto&& ptr) {
                    using T = std::decay_t<decltype(ptr)>;
                    if constexpr (std::is_same_v<T, bool Config::*>) {
                        config.*ptr = FluentToggle_GetState(hCtrl);
                    } else if constexpr (std::is_same_v<T, std::wstring Config::*>) {
                        config.*ptr = GetControlTextW(hCtrl);
                    } else if constexpr (std::is_same_v<T, std::string Config::*>) {
                        config.*ptr = WideToUtf8(GetControlTextW(hCtrl));
                    }
                }, item.memberPtr);
            }
        }
    }
}
```

---

### 3.4 几何与布局层：轻量 Flexbox 约束布局引擎

#### 彻底淘汰手算常量的根基
引入成熟且极轻量的 **Yoga 布局引擎**（Meta 开源，MIT 许可，纯 C++，静态库仅 ~150KB，零外部依赖）：
1. **纯几何计算**：Yoga **只算矩形坐标**（`x, y, width, height`），不负责任何绘制，不碰 Windows 消息循环，与 Win32 原生架构天然契合。
2. **响应式拉伸 (Responsive Resizing)**：
   * 窗口开启 `WS_THICKFRAME`。
   * 当用户拖动窗口触发 `WM_SIZE` 或在不同显示器间移动触发 `WM_DPICHANGED` 时：
     ```cpp
     // 1. 获取当前客户区 DIP 尺寸
     float clientWidthDip = PxToDip(rcClient.right, UiStyle::Scale);
     float clientHeightDip = PxToDip(rcClient.bottom, UiStyle::Scale);

     // 2. 传递给 Yoga 根节点
     YGNodeCalculateLayout(rootLayoutNode, clientWidthDip, YGUndefined, YGDirectionLTR);

     // 3. 递归遍历 Yoga 树，通过 DeferWindowPos 批量平滑更新所有子控件
     HDWP hdwp = BeginDeferWindowPos(controlCount);
     ApplyYogaLayoutToWindows(rootLayoutNode, hdwp);
     EndDeferWindowPos(hdwp);
     ```
3. **消除所有重叠与溢出 Bug**：
   * 卡片宽度设为 `100%`，自适应拉伸；
   * 卡片内左右两侧自动保持 `space-between` 对齐；
   * 提示文本（Hints）开启自动换行测量（DWrite `CreateTextLayout` 返回实际高度），卡片高度自动被撑大。
   * **`validate_settings_layout.ps1` 正则脚本可以直接退役**。

---

## 4. 模块化目录与组件解耦规划

将现有 3439 行的巨石拆分为具有清晰单一职责的模块，放置于 `src/ui/settings/` 目录下：

```
src/ui/settings/
├── settings_window.h/.cpp       # 顶层窗口生命周期、DWM 材质、DPI 监听、WM_SIZE 分发 (~250 行)
├── settings_schema.h/.cpp       # C++23 强类型设置项清单与描述表 (~300 行)
├── settings_binder.h/.cpp       # 自动化 Config 反射双向同步器 (~150 行)
├── settings_layout.h/.cpp       # Yoga 布局树构建与 DeferWindowPos 批量求值 (~200 行)
├── nav_rail.h/.cpp              # 左侧 Fluent 导航栏组件自绘与点击路由 (~200 行)
├── settings_card.h/.cpp         # Direct2D 圆角卡片容器绘制与控件宿主 (~250 行)
└── controls/                    # 现代化 Fluent 原生子控件封装
    ├── fluent_toggle.h/.cpp     # 自绘平滑药丸开关 (~150 行)
    ├── fluent_edit.h/.cpp       # 透明背景穿透 EDIT 宿主 (保留原生 IME) (~180 行)
    ├── fluent_combo.h/.cpp      # 现代 Fluent 样式下拉框封装 (~150 行)
    └── hotkey_picker.h/.cpp     # 录音快捷键捕获控件 (~150 行)
```

**对比**：
* 拆分后每个源文件代码均在 **150 ~ 300 行** 之间，结构严谨清晰。
* 彻底解除与业务 Provider（如 `volcengine_asr`、`doubao_ime`）的强耦合。
* `main.cpp` 只需包含 `settings_window.h` 并调用 `ShowSettingsWindow(hwndOwner)`。

---

## 5. 平滑迁移路线与阶段验收指标

为了确保项目持续处于可编译、可运行、可测试状态，重构采用**渐进替换法**，绝不搞“大爆炸式一次性推倒”：

### Milestone 1：基础设施与元数据注册表（零 UI 风险）
* **任务**：
  1. 创建 `settings_schema.h` 与 `settings_binder.h`；
  2. 将 `Config` 现有的 104 个字段用 `SettingItem` 描述表声明；
  3. 编写单元测试验证 `SettingsBinder` 的读取与保存与现有 `config.json` 100% 逐字节兼容；
* **退出判据**：单元测试通过，现有 UI 暂时不动，确保持久化契约坚如磐石。

### Milestone 2：现代窗口外壳与 Navigation Rail
* **任务**：
  1. 在 `settings_window.cpp` 中引入 `DwmSetWindowAttribute`（Mica 材质 + 沉浸式暗色模式）；
  2. 实现左侧自绘 Navigation Rail，接入 Segoe Fluent Icons 图标；
  3. 开启 `WS_THICKFRAME` 允许窗口拉伸；
* **退出判据**：窗口外观蜕变为 Windows 11 现代卡片质感，导航点击切换平滑响应。

### Milestone 3：Flexbox 引擎接入与首个页面迁移试点（General 页）
* **任务**：
  1. 引入 Yoga 布局引擎（静态集成至 `third_party/yoga`）；
  2. 接入 `fluent_toggle` 控件；
  3. 用现代卡片重构“常规设置”页（快捷键、开机自启动、诊断录音）；
  4. 验证窗口拖拉伸缩时，卡片与控件自动流畅重排；
* **退出判据**：在 96/144/192/288 DPI 及任意拉伸窗口尺寸下，首页面文字不截断、控件无重叠。

### Milestone 4：文本输入与复杂页面迁移（Prompt & Local ASR 页）
* **任务**：
  1. 落地 `fluent_edit`（无边框透明穿透 + 外层 D2D 发光轮廓）；
  2. 迁移 LLM Prompt 页面与本地 sherpa 页面；
  3. **核心验收**：使用微软拼音、微信输入法进行全流程长文本打字测试，确保候选框跟随、选区与换行 100% 正常；
* **退出判据**：中文输入体验与原生无异，Prompt 多行自适应拉伸。

### Milestone 5：收尾 Cloud ASR 复杂子页与旧代码清理
* **任务**：
  1. 迁移 Cloud ASR 各 Provider（火山引擎、Qwen、百度、MiniMax、Doubao IME 等）的配置项；
  2. 利用 Schema 的 `isVisible` 谓词替代手工 `ShowWindow` 隐藏；
  3. 彻底删除旧版 `settings.cpp` 中的 15 个全局 vector 与 229 个废弃布局常量；
  4. 移除旧的 `validate_settings_layout.ps1` 正则检查。
* **退出判据**：全项目编译通过，守卫脚本 `check_architecture.ps1` 绿灯，架构基线指标大幅下降。

---

## 6. 关键决策点与排坑要点 (Gotchas)

1. **【硬规则】必须保留原生 EDIT 控件以规避 IME 泥潭**：
   * 任何试图完全自绘文本框的方案在中文桌面环境下都是灾难。利用 Windows 原生 `EDIT` 控件做**无边框穿透**是兼顾“Fluent 现代外观”与“100% 完美中文输入法体验”的唯一正道。
2. **【DPI 与 DIP 严格隔离】**：
   * Yoga 布局引擎内部一律以 **DIP (设备无关像素，96 DPI 下的逻辑像素)** 运行。
   * 在最终调用 Win32 `MoveWindow` 或 `DeferWindowPos` 时，统一经由 `DipToPx(dip, scale)` 转换。切勿在布局树计算中夹杂物理像素。
3. **【原生 ComboBox 高度陷阱】**：
   * 原生 `COMBOBOX`（`CBS_DROPDOWNLIST`）的收起态高度是由系统字体强制推导的，不能随意用 `MoveWindow` 压缩。在 Yoga 节点中应将其设为固定内容高度（通过 `CB_GETITEMHEIGHT` 测量）。
4. **【Mica 材质兼容性回退】**：
   * `DWMSBT_TABBEDWINDOW` 仅在 Windows 11 Build 22621 (22H2) 及以上支持。对于 Windows 10 (19041+) 或低版本 Windows 11，代码中必须优雅回退为纯色现代深灰背景（`#202020`），绝不能发生 API 调用导致崩溃。
5. **【热键捕获与全局键盘钩子协调】**：
   * 当 Settings 窗口处于前台且用户正在配置热键时，必须通知全局按键捕获模块暂停拦截，避免快捷键被主程序吞掉导致设置框无法收录。

---

## 7. 总结

本方案跳出了“仅仅修补手算坐标”或“盲目引入几十兆重型框架”的两个极端：
- **在用户体验上**：直接跃升至 Windows 11 原生 Fluent 设置的高级质感，拥有可自由拉伸的弹性窗口、优雅的 Mica 材质与卡片布局；
- **在输入法可靠性上**：借助 EDIT 穿透技术，零风险保全 100% 原生中文 IME 体验；
- **在代码架构上**：通过 C++23 Schema 声明式注册表与 Yoga Flexbox 引擎，将庞大的 3400 行巨石化解为若干职责单一的微组件，让“新增配置项”的工作量从 10 处机械重复缩减至 1 行声明。
