# VoxType Settings 界面现代化重构方案：现代 Win32 / C++23 Fluent 架构设计（权威复核版）

**文档状态**：已复核终版方案（深入调查与工业级架构蓝图）  
**文档路径**：`.plan/feat/modern-win32-fluent-settings-architecture.md`  
**基准日期**：2026-09-18  
**核心目标**：彻底解决 VoxType 现有 Settings 界面 3448 行单文件巨石、229 个绝对坐标常量、104 项配置修改动 10 处的结构性病灶；采用 Windows 11 Fluent 视觉标准（Mica/Mica Alt 材质 + Direct2D/DirectWrite 硬件加速卡片渲染 + 沉浸式暗色模式）；结合纯原生无边框 EDIT 穿透与 **GDI 实心画刷遮罩技术**，彻底消灭输入残影，达成 **100% 原生中文 IME 零风险**；引入 **自研轻量 C++23 StackLayout 布局引擎**（彻底排除 Meta Yoga 外部重型依赖，兼顾亚像素测量与弹性拉伸）；基于 **C++23 强类型元数据反射注册表 (Schema-Driven)** 实现数据双向绑定，使新增/修改配置项从 10 处机械修改骤降至 1 行声明；常驻内存保持 ~12MB 零新增开销。

---

## 目录
1. [现有 Settings 架构病灶深度取证与数据度量](#1-现有-settings-架构病灶深度取证与数据度量)
   - 1.1 [3448 行巨石单文件与职责失控](#11-3448-行巨石单文件与职责失控)
   - 1.2 [104 项配置与“新增配置改 10 处”的样板地狱](#12-104-项配置与新增配置改-10-处的样板地狱)
   - 1.3 [229 个手算坐标常量的脆弱性与正则脚本检查的困境](#13-229-个手算坐标常量的脆弱性与正则脚本检查的困境)
   - 1.4 [固定外框（缺少 WS_THICKFRAME）与不可拉伸体验](#14-固定外框缺少-ws_thickframe-与不可拉伸体验)
   - 1.5 [Windows 2000 时代的陈旧视觉质感](#15-windows-2000-时代的陈旧视觉质感)
2. [现代 Windows 32 UI 技术栈与架构选型终局权衡](#2-现代-windows-32-ui-技术栈与架构选型终局权衡)
   - 2.1 [六大技术栈横向对比全景](#21-六大技术栈横向对比全景)
   - 2.2 [为什么纯原生现代 Win32 + C++23 自绘是唯一最优解](#22-为什么纯原生现代-win32--c23-自绘是唯一最优解)
   - 2.3 [核心技术纠偏：自研 C++23 StackLayout vs. Meta Yoga 深度权衡](#23-核心技术纠偏自研-c23-stacklayout-vs-meta-yoga-深度权衡)
3. [目标架构设计：Win32 Modern Fluent 体系](#3-目标架构设计win32-modern-fluent-体系)
   - 3.1 [总体架构分层与数据流拓扑](#31-总体架构分层与数据流拓扑)
   - 3.2 [视觉与合成层：DWM Mica Alt + Direct2D 硬件加速卡片系统](#32-视觉与合成层dwm-mica-alt--direct2d-硬件加速卡片系统)
   - 3.3 [输入核心保障：无边框原生 EDIT 穿透与 GDI 实心画刷防重影机制（关键纠偏）](#33-输入核心保障无边框原生-edit-穿透与-gdi-实心画刷防重影机制关键纠偏)
   - 3.4 [几何与布局引擎：自研 C++23 StackLayout 声明式流式布局](#34-几何与布局引擎自研-c23-stacklayout-声明式流式布局)
   - 3.5 [数据契约层：C++23 强类型元数据描述符与注册表 (Schema-Driven)](#35-数据契约层c23-强类型元数据描述符与注册表-schema-driven)
   - 3.6 [自动化双向绑定器 (SettingsBinder)](#36-自动化双向绑定器-settingsbinder)
4. [模块化工程目录与分层解耦规划](#4-模块化工程目录与分层解耦规划)
5. [详细实现规范与示例代码](#5-详细实现规范与示例代码)
   - 5.1 [settings_schema.h 强类型元数据体系](#51-settings_schemah-强类型元数据体系)
   - 5.2 [settings_layout.h 自研 StackLayout 引擎](#52-settings_layouth-自研-stacklayout-引擎)
   - 5.3 [settings_card_view.cpp D2D 卡片与 EDIT 穿透宿主](#53-settings_card_viewcpp-d2d-卡片与-edit-穿透宿主)
   - 5.4 [fluent_toggle.cpp 现代药丸开关](#54-fluent_togglecpp-现代药丸开关)
6. [DPI 全局自适应（PerMonitorV2）与视觉规范](#6-dpi-全局自适应permonitorv2与视觉规范)
7. [平滑演进路线与阶段验收指标 (Milestones)](#7-平滑演进路线与阶段验收指标-milestones)
8. [关键决策与工程排坑要点 (Gotchas)](#8-关键决策与工程排坑要点-gotchas)
   - 8.1 [GDI WM_CTLCOLOREDIT 实心画刷与文本重影陷阱（深度解析）](#81-gdi-wm_ctlcoloredit-实心画刷与文本重影陷阱深度解析)
   - 8.2 [自绘输入框 vs. 原生 EDIT 穿透的 IME 致命泥潭](#82-自绘输入框-vs-原生-edit-穿透的-ime-致命泥潭)
   - 8.3 [DWM Mica 材质版本探测与优雅回退](#83-dwm-mica-材质版本探测与优雅回退)
   - 8.4 [ComboBox 下拉列表高度测量与窗口遮挡](#84-combobox-下拉列表高度测量与窗口遮挡)
   - 8.5 [Settings 打开时的全局热键协调](#85-settings-打开时的全局热键协调)
   - 8.6 [异步操作与 UI 线程解耦（网络探测与路径校验）](#86-异步操作与-ui-线程解耦网络探测与路径校验)
9. [总结与预期收益](#9-总结与预期收益)

---

## 1. 现有 Settings 架构病灶深度取证与数据度量

对 VoxType 现有 UI 代码库（`src/ui/settings.cpp`、`src/ui/settings_controls.cpp`、`src/ui/settings_dialogs.cpp`、`src/ui/ui_types.h`、`src/core/config_store.*`）进行端到端取证，总结出以下五大结构性病灶：

### 1.1 3448 行巨石单文件与职责失控
* **代码规模**：`src/ui/settings.cpp` 单文件高达 **3448 行**，`settings_dialogs.cpp` 包含 **1000 行**，`ui_types.h` 散落 **229 个硬编码常量**。
* **职责严重越界**：单个文件内交织了：
  1. 125 个控件 IDC 的绝对像素坐标硬编码；
  2. 15 个全局 `std::vector<HWND>` 容器的手工可见性遍历控制；
  3. 巨型 `switch (LOWORD(wParam))` 包含 53 个消息命令分支；
  4. 7 种云端 ASR Provider 的子界面切换、动态隐藏与提示更新；
  5. 甚至内联了网络连通性探测（`TestLlmConnection`）、路径探测与多线程同步逻辑。

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

### 1.2 104 项配置与“新增配置改 10 处”的样板地狱
实测 `struct Config` 拥有 **104 个配置字段**，在现有架构中，为设置界面新增一个简单的配置项（例如一个布尔开关或文本输入框），开发者必须在 4 个文件之间跳跃修改 9~10 处：
1. `src/core/config_store.h`：在 `struct Config` 中声明字段；
2. `src/core/config_store.cpp` (`LoadConfig`)：编写 JSON 解析逻辑（全仓累计 95 次 `ExtractJson*`）；
3. `src/core/config_store.cpp` (`SaveConfig`)：编写 JSON 序列化拼接（全仓累计 94 行流输出）；
4. `src/ui/ui_types.h`：手算并新增控件的 `X, Y, W, H` 物理/逻辑常量；
5. `src/ui/settings_controls.h`：新增控件 IDC 宏定义；
6. `src/ui/settings.cpp` (`WM_CREATE`)：调用 `CreateWindowExW` 传入绝对坐标；
7. `src/ui/settings.cpp`：调用 `ApplyUiFont` 绑定字体；
8. `src/ui/settings.cpp`：手动推入对应的可见性 `vector<HWND>` 容器；
9. `src/ui/settings.cpp` (`LoadSettingsControls`)：将 Config 内存值写入 HWND（411 行手写映射）；
10. `src/ui/settings.cpp` (`SaveSettingsControls`)：从 HWND 读取内容赋值回 Config（353 行手写读取）。

**代价**：90% 的代码是纯机械胶水。只要漏改一处，就会发生配置无法保存、UI 错位或句柄悬空的隐蔽 Bug。

### 1.3 229 个手算坐标常量的脆弱性与正则脚本检查的困境
* 布局完全基于硬编码的行高与索引：
  ```cpp
  constexpr int RowInputY(int row) { return FirstRowY + row * RowHeight; }
  ```
* **中间插行惩罚**：若在某个组的第 1 行与第 2 行之间插入新设置项，该组后续所有控件的 `RowInputY(n)` 索引必须全部手工 `+1`。
* **组高固定死**：`GeneralShortcutGroupH = 170`、`GeneralStartupGroupH = 112`、`GeneralDiagnosticsGroupH = 166` 等均为常量。增减内容后若未同步更新组高，窗口即刻发生重叠。
* **以毒攻毒的脚本**：为了防止高 DPI 下控件碰撞，项目中诞生了 `validate_settings_layout.ps1`——专门用**正则表达式扒 C++ 源码文本**来反向推算几何重叠。这是布局系统缺乏运行时求值能力的典型症状。

### 1.4 固定外框（缺少 WS_THICKFRAME）与不可拉伸体验
* 窗口样式被锁死在 `WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN`，没有 `WS_THICKFRAME`。
* 窗口大小硬编码为 `850x740`。用户无法根据屏幕分辨率或个人视力自由缩放窗口。
* 当用户需要输入较长的 LLM Prompt、系统提示词或敏感词库时，小尺寸不可拉伸的文本框体验极其受促。

### 1.5 Windows 2000 时代的陈旧视觉质感
* 采用系统原生 90 年代风格的 Checkbox（小方块打钩）、原生直角 ComboBox、顶部古旧的 `SysTabControl32`、灰白平板背景。
* 缺乏 Windows 11 现代 Fluent 设计规范（Mica/Mica Alt 材质、圆角卡片 Card 容器、现代 ToggleSwitch 药丸开关、微交互高亮、左侧垂直导航 Rail）。

---

## 2. 现代 Windows 32 UI 技术栈与架构选型终局权衡

### 2.1 六大技术栈横向对比全景

| 方案类别 | 代表技术 | 视觉效果 | 运行时内存 (RSS) | 中文 IME 支持度 | 维护成本与架构优势 | 综合裁定 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **A. 官方现代框架** | **WinUI 3 / Windows App SDK (XAML Islands)** | 极佳 (原生 Fluent) | **50 ~ 100 MB+** (宿主庞大) | 完善 | XAML 声明式组件化，但需要庞大框架依赖包，冷启动慢 | ❌ **否决**：严重违背 VoxType ~12MB 空闲基线与轻量单文件分发哲学 |
| **B. 内嵌 Web / 浏览器** | **WebView2 / Tauri / Electron** | 现代化 Web 生态 | **60 ~ 150 MB+** (多进程 Chromium) | 完善 | 前端生态极丰富，但常驻内存对系统后台输入法是灾难 | ❌ **绝对排除**：进程树臃肿，内存占用是现有程序的十倍 |
| **C. 第三方声明式 DSL** | **Slint C++** | 优秀 (现代声明式) | 14 ~ 18 MB (软件/Skia) | ⚠️ **存在在案未修复 Bug** (Issue #5982) | `.slint` 声明极其优雅，但构建依赖 Cargo，且 Windows 中文输入存在上游缺陷 | ⏸️ **暂不推荐**：作为中文字音输入法，IME 是致命生命线 |
| **D. 游戏/即时模式 UI** | **Dear ImGui** | 工具/调试器风格 | ~15 MB | 需外挂 IME 代码 | 极速无状态开发，但 UI 难以调教成 Windows 11 精致设置面板 | ❌ **否决**：不满足现代精致 Fluent 视觉诉求 |
| **E. 轻量嵌入式引擎** | **RmlUi (C++17/20)** | 优秀 (HTML/CSS Flex) | ~15 MB (轻量嵌入) | 良好 (2024 PR #541 已支持 Win32 IME) | 支持类 CSS 样式，但需维护模板胶水层与字体引擎适配 | 🔲 **备选**：适合需要复杂富文本排版的场景 |
| **F. 现代 Win32 Fluent 体系** | **Direct2D + DWM Mica + 原生 EDIT 穿透 + 自研 C++23 StackLayout + Schema** | **极佳 (Windows 11 原生 Fluent 卡片)** | **~12 MB (基线零增长)** | **100% 完美原生** (系统 IMM32 / TSF 零风险) | **声明式元数据注册表**，新增项仅需 1 行；自研轻量 StackLayout 零外部依赖；极致高内聚 | 🌟 **强烈推荐（唯一全优解）** |

### 2.2 为什么纯原生现代 Win32 + C++23 自绘是唯一最优解
1. **零运行时代价**：不引入 Node.js、Chromium、.NET CLR、XAML 运行时或 Rust 工具链。纯原生 C++23 编译，体积增加 < 100KB，空闲内存严格保持在 ~12MB 基线。
2. **IME 零风险**：文本框采用 Windows 原生 `EDIT` 控件（结合无边框穿透与实心画刷技术）。操作系统免费提供 100% 可靠的中文输入法候选框定位、光标闪烁、划词选区与辅助功能（Accessibility）。
3. **视觉直达 Windows 11 官方水准**：通过 DWM Mica 材质、Direct2D 圆角卡片、Segoe Fluent Icons 图标与左侧 Navigation Rail，呈现与 Windows 11 系统“设置”如出一辙的质感。
4. **统一项目现有渲染栈**：VoxType 已在 `src/ui/hud.cpp` 中建立完整的 Direct2D 1.1 / DirectWrite 基础设施，重用已有的渲染上下文与字体工厂，避免引入异构渲染引擎。

### 2.3 核心技术纠偏：自研 C++23 StackLayout vs. Meta Yoga 深度权衡

在初稿方案中曾提出引入 Meta 开源的 Yoga Flexbox 布局引擎。经过严谨的技术复核，我们对 **自研 C++23 StackLayout 引擎** 与 **Yoga** 进行了多维深度对比：

| 评估维度 | Meta Yoga Flexbox 引擎 | 自研 C++23 StackLayout 线性流式引擎 | 架构结论与推荐 |
| :--- | :--- | :--- | :--- |
| **外部代码与依赖规模** | 引入数十个源文件或 ~150KB 外部静态库，需管理 submodule/CMake 编译链 | **仅 ~200 行 C++23 紧凑代码**，无任何外部库依赖，零编译负担 | 🌟 **自研方案胜出**（保持仓库纯粹与编译极速） |
| **与业务场景匹配度** | 面向 Web/移动端任意嵌套 Flex 布局（grow, shrink, wrap, baseline） | **100% 贴合桌面 Settings 业务**：垂直卡片堆叠、左右分栏、多行展开 | 🌟 **自研方案胜出**（Settings 绝无复杂网格缠绕，过度设计无益） |
| **DWrite 文本测量集成** | 需向 Yoga 注册 C 样式测量回调（`YGMeasureFunc`），函数指针与上下文传递繁琐 | 直接在计算流中调用 `IDWriteTextLayout::GetMetrics`，亚像素高度即算即得 | 🌟 **自研方案胜出**（代码内聚、类型安全） |
| **物理/DIP 缩放解耦** | 节点树以 DIP 运行，需额外写适配层桥接 Win32 `DeferWindowPos` | 统一输入 DIP 宽度，输出物理像素 `RECT`，原子驱动 `DeferWindowPos` | 🌟 **自研方案胜出**（与现存 `UiStyle::Scale` 完美协同） |
| **内存与堆分配** | 每个 UI 节点均需分配 `YGNodeRef` 并构建树形拓扑 | **零堆分配**（基于 `std::span` 遍历），纯栈上计算即时释放 | 🌟 **自研方案胜出**（对内存零扰动） |

**终局决策**：  
放弃引入外部 Yoga 依赖，采用 **自研 C++23 强类型 StackLayout 引擎**。它以极简的代码量（~200 行）即可彻底根除 229 个绝对坐标常量，支持窗口拖拽拉伸自适应重排与高 DPI 动态换行，实现收益最大化与依赖最小化。

---

## 3. 目标架构设计：Win32 Modern Fluent 体系

### 3.1 总体架构分层与数据流拓扑

```
+---------------------------------------------------------------------------------------+
|                                 C++23 Settings Schema                                 |
|   - SettingRegistry: 静态元数据 (JSON Key, 成员指针, 控件类型, 默认值, 标签, 校验谓词) |
+---------------------------------------------------------------------------------------+
                                           |
                   +-----------------------+-----------------------+
                   | (双向绑定映射)                                 | (驱动布局结构)
                   v                                               v
+------------------------------------+           +--------------------------------------+
|       ConfigStore Persistence      |           |     C++23 StackLayout Engine         |
|   - 自动序列化 / 反序列化           |           |   - 纯几何求解 (DWrite 文本测量)      |
|   - 100% 兼容现有 config.json      |           |   - 窗口拉伸/DPI动态重排 (DIP->PX)   |
+------------------------------------+           +--------------------------------------+
                                                                   | (输出 RECT 树)
                                           +-----------------------+
                                           v
+---------------------------------------------------------------------------------------+
|                           Modern Win32 Fluent View Host                               |
|  +---------------------------+  +--------------------------------------------------+  |
|  |   Left Navigation Rail    |  |               Right Settings Content Area        |  |
|  |  - Segoe Fluent Icons     |  |  - DWM Mica Alt (DWMSBT_TABBEDWINDOW) 系统材质    |  |
|  |  - 激活 Accent 药丸指示条 |  |  - Direct2D Fluent Cards (半透明圆角自绘卡片)    |  |
|  |  - 页面路由与平滑滚动     |  |  - 现代 Toggle Switch (D2D 自绘无边框药丸开关)   |  |
|  |                           |  |  - 原生 EDIT 穿透 (配合 GDI 实心画刷遮罩防重影)  |  |
|  +---------------------------+  +--------------------------------------------------+  |
+---------------------------------------------------------------------------------------+
```

---

### 3.2 视觉与合成层：DWM Mica Alt + Direct2D 硬件加速卡片系统

#### 1. DWM 材质与暗色沉浸式系统
通过 Windows 11 原生 DWM API 接入 Mica Alt 材质与深色模式，使窗口与系统现代 Fluent 体验浑然一体：
```cpp
// 启用 Windows 11 官方 Mica Alt 材质 (DWMSBT_TABBEDWINDOW)
enum DWM_SYSTEMBACKDROP_TYPE {
    DWMSBT_AUTO = 0,
    DWMSBT_NONE = 1,
    DWMSBT_MAINWINDOW = 2,      // Mica (主窗口材质)
    DWMSBT_TRANSIENTWINDOW = 3, // Acrylic (亚克力弹窗)
    DWMSBT_TABBEDWINDOW = 4     // Tabbed Mica Alt (设置/选项卡窗口最佳质感)
};

void ApplyModernWindowAttributes(HWND hwnd, bool isDarkMode) {
    // 1. 设置圆角窗口
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    // 2. 沉浸式暗色标题栏
    BOOL dark = isDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    // 3. 启用 Mica Alt 材质 (Win11 22H2+)
    DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_TABBEDWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    // 4. 将系统边框延伸进客户区，使客户区透出 Mica
    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}
```

#### 2. 左侧导航面板 (Navigation Rail)
彻底抛弃顶部的 Windows 95 风格 `SysTabControl32`，改用 Windows 11 设置同款的**左侧垂直导航栏**：
* 宽度：固定 220 ~ 240 DIP。
* 字体图标：采用 Windows 11 内置的 **Segoe Fluent Icons**（回退至 Segoe MDL2 Assets），字符包括：
  * 常规设置：`\uE713` (Settings)
  * 本地语音识别：`\uE720` (Microphone)
  * 云端 ASR：`\uE753` (Cloud)
  * AI 纠错与大模型：`\uE99A` (Sparkle / Brain)
  * 提示词与词库：`\uE8D2` (Dictionary / Edit)
* 视觉交互：鼠标悬停（Hover）呈现柔和的半透明胶囊背景；选中项左侧绘制一条 3px 宽的 Windows 主题色竖条（Active Accent Pill），支持平滑切换。

#### 3. 设置卡片化布局 (Settings Card)
* **卡片底色**：
  * 暗色模式：`rgba(255, 255, 255, 0.05)`（微透，叠加底层 Mica Alt）。
  * 亮色模式：`rgba(255, 255, 255, 0.70)`。
  * 边框：`1px rgba(255, 255, 255, 0.08)`，圆角半径 `8px`。
* **卡片内部排版**：
  * 左侧垂直排列：**主标题**（Segoe UI Variable 10pt SemiBold） + **副标题说明**（Segoe UI 8.5pt Regular 灰色）。
  * 右侧居中对齐：**操作控件**（Toggle Switch 开关、ComboBox 下拉框、Action Button）。
  * 卡片高度由自研 StackLayout 引擎与 DWrite 文本测量自动推导，绝不手算坐标。

---

### 3.3 输入核心保障：无边框原生 EDIT 穿透与 GDI 实心画刷防重影机制（关键纠偏）

#### 1. 为什么自绘文本框在中文环境下是死胡同？
自研输入框在英文环境下尚可应付，但在中文桌面环境下极易崩盘：
* **输入法框架深水区**：必须完整实现 `ITextStoreACP`、`ITfContext`、`IMM32`；
* **组合串与光标定位**：微软拼音、微信输入法、搜狗输入法的候选窗口必须严丝合缝对齐自绘光标（Caret），若测量误差 1 像素，候选框就会飞到屏幕左上角；
* **极端用例**：复杂多语言混输、Emoji 变音符组合、分词下划线、双拼纠错。

#### 2. 原生 EDIT 穿透方案与 GDI 实心画刷防重影（核心技术点）
为了既享受 Fluent 现代无边框圆角外观，又 100% 拥有系统官方级 IME 稳定性，我们采用 **原生 EDIT 穿透技术**。但在实现过程中，**必须坚决避开初稿中关于空画刷的严重误区**：

* ❌ **致命陷阱（空画刷/透明画刷）**：
  若在 `WM_CTLCOLOREDIT` 中返回 `GetStockObject(NULL_BRUSH)` 或设置 `SetBkMode(hdc, TRANSPARENT)`，GDI 文本渲染在用户输入、退格删除或选中文本时，**不会自动清除前一个字符的残影**。新旧文字直接在像素上累加叠加，引发严重的**文字重影（Text Ghosting）、光标擦除失败与画面花屏**。
* ✅ **工业级正解（实心画刷颜色遮罩 + D2D 外轮廓协同）**：
  1. **无边框窗口化**：创建原生 `EDIT` 时去掉 `WS_BORDER` 与 `WS_EX_CLIENTEDGE`；
  2. **父窗口实心画刷匹配**：在父窗口处理 `WM_CTLCOLOREDIT` 时，**必须返回与卡片内部输入槽背景色 100% 严格一致的实心画刷 (`HBRUSH`)**，并同步设置 `SetBkColor` 与 `SetBkMode(hdc, OPAQUE)`；
  3. **外框 D2D 绘制**：原生 EDIT 仅作为纯粹的文字输入光标宿主，其外层的 8px 圆角轮廓、微光边框、Focus 激活时的 2px Windows Accent 强调高光环，全部由宿主卡片在 Direct2D 中绘制。

```cpp
// 保证无重影、无花屏的工业级 WM_CTLCOLOREDIT 处理规范
case WM_CTLCOLOREDIT: {
    HDC hdc = reinterpret_cast<HDC>(wParam);
    HWND hEdit = reinterpret_cast<HWND>(lParam);

    // 1. 设置文字颜色与输入槽背景色 (例如暗色卡片槽内色 #2D2D2D)
    SetTextColor(hdc, UiStyle::InputTextColor);
    SetBkColor(hdc, UiStyle::ControlBgColor);

    // 2. 关键：必须设置为 OPAQUE，确保 GDI 擦除旧字符背景，根除重影
    SetBkMode(hdc, OPAQUE);

    // 3. 关键：必须返回实心画刷 (Solid Brush)，严禁返回 NULL_BRUSH
    return reinterpret_cast<LRESULT>(ui_theme::ControlBgBrush());
}
```

---

### 3.4 几何与布局引擎：自研 C++23 StackLayout 声明式流式布局

为了彻底废除 229 个手算常量和正则校验脚本，自研一个仅 ~200 行的轻量 C++23 布局器。

#### 1. 核心布局契约
```cpp
// src/ui/settings/settings_layout.h
#pragma once
#include <span>
#include <vector>
#include <windows.h>
#include <dwrite.h>

struct LayoutRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct CardLayoutResult {
    LayoutRect cardRect;
    LayoutRect titleRect;
    LayoutRect descRect;
    LayoutRect controlRect;
};

class StackLayoutEngine {
public:
    static constexpr float kCardGapY = 8.0f;       // 卡片垂直间距 (DIP)
    static constexpr float kGroupGapY = 24.0f;     // 组间距 (DIP)
    static constexpr float kCardPadX = 16.0f;      // 卡片内部内边距 X
    static constexpr float kCardPadY = 12.0f;      // 卡片内部内边距 Y
    static constexpr float kMinCardHeight = 64.0f; // 最小标准卡片高度

    // 计算单个卡片内部几何结构
    static CardLayoutResult LayoutCard(
        float availableWidthDip,
        float currentYDip,
        const std::wstring& title,
        const std::wstring& desc,
        float controlWidthDip,
        float controlHeightDip,
        IDWriteFactory* dwriteFactory,
        IDWriteTextFormat* titleFormat,
        IDWriteTextFormat* descFormat
    );

    // 执行整页布局求值，并驱动 Win32 子控件批量更新
    static void ApplyPageLayout(
        HWND hHostWnd,
        float clientWidthDip,
        float clientHeightDip,
        float scale,
        std::span<const CardLayoutResult> layoutResults
    );
};
```

#### 2. DWrite 亚像素测量与弹性折行
```cpp
CardLayoutResult StackLayoutEngine::LayoutCard(
    float availableWidthDip,
    float currentYDip,
    const std::wstring& title,
    const std::wstring& desc,
    float controlWidthDip,
    float controlHeightDip,
    IDWriteFactory* dwriteFactory,
    IDWriteTextFormat* titleFormat,
    IDWriteTextFormat* descFormat
) {
    CardLayoutResult res;
    res.cardRect.x = 0.0f;
    res.cardRect.y = currentYDip;
    res.cardRect.width = availableWidthDip;

    // 左侧可用文本宽度 = 总宽 - 内边距 - 控件宽 - 间距
    float textWidth = availableWidthDip - (kCardPadX * 2.0f) - controlWidthDip - 16.0f;

    // 1. 测量主标题高度
    IDWriteTextLayout* titleLayout = nullptr;
    dwriteFactory->CreateTextLayout(title.data(), static_cast<UINT32>(title.size()),
                                   titleFormat, textWidth, 1000.0f, &titleLayout);
    DWRITE_TEXT_METRICS titleMetrics{};
    if (titleLayout) {
        titleLayout->GetMetrics(&titleMetrics);
        titleLayout->Release();
    }

    // 2. 测量描述副标题高度 (支持动态折行)
    float descHeight = 0.0f;
    if (!desc.empty()) {
        IDWriteTextLayout* descLayout = nullptr;
        dwriteFactory->CreateTextLayout(desc.data(), static_cast<UINT32>(desc.size()),
                                       descFormat, textWidth, 1000.0f, &descLayout);
        DWRITE_TEXT_METRICS descMetrics{};
        if (descLayout) {
            descLayout->GetMetrics(&descMetrics);
            descHeight = descMetrics.height + 4.0f; // 间距
            descLayout->Release();
        }
    }

    float textBlockHeight = titleMetrics.height + descHeight;
    float contentHeight = (std::max)(textBlockHeight, controlHeightDip);
    res.cardRect.height = (std::max)(kMinCardHeight, contentHeight + (kCardPadY * 2.0f));

    // 文本区域位置
    res.titleRect = { kCardPadX, currentYDip + kCardPadY, textWidth, titleMetrics.height };
    res.descRect = { kCardPadX, res.titleRect.y + titleMetrics.height + 4.0f, textWidth, descHeight };

    // 右侧操作控件居中对齐
    float ctlTop = currentYDip + (res.cardRect.height - controlHeightDip) / 2.0f;
    res.controlRect = { availableWidthDip - kCardPadX - controlWidthDip, ctlTop, controlWidthDip, controlHeightDip };

    return res;
}
```

#### 3. 驱动 DeferWindowPos 平滑无闪烁更新
在窗口拉伸（`WM_SIZE`）或 DPI 缩放（`WM_DPICHANGED`）时，调用 Win32 的 `BeginDeferWindowPos` 批量提交，绝无逐个控件重排引起的残影抖动：
```cpp
void StackLayoutEngine::ApplyPageLayout(
    HWND hHostWnd, float clientWidthDip, float clientHeightDip, float scale,
    std::span<const CardLayoutResult> layoutResults
) {
    HDWP hdwp = BeginDeferWindowPos(static_cast<int>(layoutResults.size()));
    for (size_t i = 0; i < layoutResults.size(); ++i) {
        HWND hCtrl = GetControlHandleByIndex(i);
        if (!hCtrl || !IsWindow(hCtrl)) continue;

        const auto& ctl = layoutResults[i].controlRect;
        int pxX = DipToPx(ctl.x, scale);
        int pxY = DipToPx(ctl.y, scale);
        int pxW = DipToPx(ctl.width, scale);
        int pxH = DipToPx(ctl.height, scale);

        hdwp = DeferWindowPos(hdwp, hCtrl, nullptr, pxX, pxY, pxW, pxH,
                              SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }
    EndDeferWindowPos(hdwp);
}
```

---

### 3.5 数据契约层：C++23 强类型元数据描述符与注册表 (Schema-Driven)

彻底消灭“新增配置动 10 处”的核心武器是 **C++23 强类型元数据注册表**。

#### 1. 结构化元数据定义
```cpp
// src/ui/settings/settings_schema.h
#pragma once
#include <string>
#include <vector>
#include <variant>
#include <functional>
#include "config_store.h"

enum class SettingCtrlType {
    Toggle,        // 药丸开关 (bool)
    TextEdit,      // 单行输入 (string / wstring)
    PasswordEdit,  // 密码输入框 (string / wstring)
    MultilineEdit, // 多行文本框 (string / wstring, 如 Prompt)
    NumberEdit,    // 数字输入框 (int / float)
    ComboBox,      // 下拉菜单 (int 枚举)
    Hotkey,        // 热键选择器 (Hotkey 复合类型)
    ActionButton   // 操作按钮 (触发回调)
};

struct SettingItem {
    std::string key;                   // JSON 键名 (如 "volc_enable_context")
    std::wstring label;                // 显示标题
    std::wstring description;          // 辅助说明
    SettingCtrlType ctrlType;          // 控件呈现形态

    // C++23 成员指针联合体，精准映射 Config 字段
    std::variant<
        std::monostate,
        bool Config::*,
        int Config::*,
        float Config::*,
        std::string Config::*,
        std::wstring Config::*
    > memberPtr = std::monostate{};

    // 可选：下拉菜单项候选列表 (显示文案, 数值)
    std::vector<std::pair<std::wstring, int>> comboOptions = {};

    // 可选：动作按钮回调
    std::function<void(HWND parentHwnd)> onActionClick = nullptr;

    // 可选：条件可见性断言 (例如仅当 Provider 为 Qwen 时显示)
    std::function<bool(const Config&)> isVisible = nullptr;
};

struct SettingGroup {
    std::wstring groupTitle;
    std::vector<SettingItem> items;
};

struct SettingPage {
    int pageId;
    std::wstring title;
    wchar_t iconGlyph;                 // Segoe Fluent Icons 码点
    std::vector<SettingGroup> groups;
};
```

#### 2. 单一数据源清单声明示例
新增一个配置项，**仅需在 Schema 表中追加一行声明**：
```cpp
// src/ui/settings/settings_schema.cpp
const std::vector<SettingPage>& GetSettingsPagesSchema() {
    static const std::vector<SettingPage> schema = {
        {
            .pageId = 0,
            .title = L"常规设置",
            .iconGlyph = L'\uE713',
            .groups = {
                {
                    .groupTitle = L"系统与快捷键",
                    .items = {
                        {
                            .key = "auto_start",
                            .label = L"开机自启动",
                            .description = L"在 Windows 登录时自动启动并在托盘常驻后台",
                            .ctrlType = SettingCtrlType::Toggle,
                            .memberPtr = &Config::autoStart
                        },
                        {
                            .key = "diagnostics_mode",
                            .label = L"诊断录音捕获",
                            .description = L"自动保留近几次录音 PCM 副本用于故障定位与排查",
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
            .iconGlyph = L'\uE720',
            .groups = {
                {
                    .groupTitle = L"本地识别引擎 (sherpa-onnx)",
                    .items = {
                        {
                            .key = "model_dir",
                            .label = L"模型存储目录",
                            .description = L"存放本地 Sherpa / SenseVoice 离线模型的本地路径",
                            .ctrlType = SettingCtrlType::TextEdit,
                            .memberPtr = &Config::modelDir
                        }
                    }
                }
            }
        }
    };
    return schema;
}
```

---

### 3.6 自动化双向绑定器 (SettingsBinder)

借助 Schema 注册表，所有的 UI 同步代码收敛为极简范型迭代，**彻底剔除 411 行手写 `LoadSettingsControls` 与 353 行手写 `SaveSettingsControls`**：

```cpp
// src/ui/settings/settings_binder.cpp
#include "settings_binder.h"
#include "settings_schema.h"

void SettingsBinder::LoadToUI(HWND hHostWnd, const Config& config, const std::vector<SettingPage>& pages) {
    for (const auto& page : pages) {
        for (const auto& group : page.groups) {
            for (const auto& item : group.items) {
                HWND hCtrl = FindControlByKey(hHostWnd, item.key);
                if (!hCtrl) continue;

                std::visit([&](auto&& ptr) {
                    using T = std::decay_t<decltype(ptr)>;
                    if constexpr (std::is_same_v<T, bool Config::*>) {
                        FluentToggle_SetChecked(hCtrl, config.*ptr);
                    } else if constexpr (std::is_same_v<T, std::wstring Config::*>) {
                        SetWindowTextW(hCtrl, (config.*ptr).c_str());
                    } else if constexpr (std::is_same_v<T, std::string Config::*>) {
                        std::wstring wide = Utf8ToWide(config.*ptr);
                        SetWindowTextW(hCtrl, wide.c_str());
                    } else if constexpr (std::is_same_v<T, int Config::*>) {
                        if (item.ctrlType == SettingCtrlType::ComboBox) {
                            SendMessageW(hCtrl, CB_SETCURSEL, config.*ptr, 0);
                        } else {
                            SetWindowTextW(hCtrl, std::to_wstring(config.*ptr).c_str());
                        }
                    }
                }, item.memberPtr);
            }
        }
    }
}

void SettingsBinder::SaveFromUI(HWND hHostWnd, Config& config, const std::vector<SettingPage>& pages) {
    for (const auto& page : pages) {
        for (const auto& group : page.groups) {
            for (const auto& item : group.items) {
                HWND hCtrl = FindControlByKey(hHostWnd, item.key);
                if (!hCtrl) continue;

                std::visit([&](auto&& ptr) {
                    using T = std::decay_t<decltype(ptr)>;
                    if constexpr (std::is_same_v<T, bool Config::*>) {
                        config.*ptr = FluentToggle_GetChecked(hCtrl);
                    } else if constexpr (std::is_same_v<T, std::wstring Config::*>) {
                        config.*ptr = GetWindowTextWString(hCtrl);
                    } else if constexpr (std::is_same_v<T, std::string Config::*>) {
                        config.*ptr = WideToUtf8(GetWindowTextWString(hCtrl));
                    } else if constexpr (std::is_same_v<T, int Config::*>) {
                        if (item.ctrlType == SettingCtrlType::ComboBox) {
                            config.*ptr = static_cast<int>(SendMessageW(hCtrl, CB_GETCURSEL, 0, 0));
                        } else {
                            config.*ptr = _wtoi(GetWindowTextWString(hCtrl).c_str());
                        }
                    }
                }, item.memberPtr);
            }
        }
    }
}
```

---

## 4. 模块化工程目录与分层解耦规划

将现有 3448 行的巨石单文件拆分为遵循单一职责原则的现代微模块，全部放置于 `src/ui/settings/` 目录下：

```
src/ui/settings/
├── settings_window.h/.cpp       # 顶层窗口宿主、DWM 材质、DPI 监听、WM_SIZE 分发 (~250 行)
├── settings_schema.h/.cpp       # C++23 强类型元数据描述表与清单定义 (~250 行)
├── settings_binder.h/.cpp       # Config 自动双向同步与反射赋值器 (~150 行)
├── settings_layout.h/.cpp       # 自研 C++23 StackLayout 引擎与 DeferWindowPos 提交 (~200 行)
├── settings_nav_rail.h/.cpp     # 左侧 Fluent 垂直导航栏组件与页面路由 (~200 行)
├── settings_card_view.h/.cpp    # 右侧 D2D 硬件加速卡片渲染与滚动宿主 (~250 行)
└── controls/                    # 现代 Fluent 细粒度子控件
    ├── fluent_toggle.h/.cpp     # 自绘平滑药丸开关 (~150 行)
    ├── fluent_edit.h/.cpp       # 无边框 EDIT 穿透与实心画刷防重影宿主 (~180 行)
    ├── fluent_combo.h/.cpp      # 现代 Fluent 下拉菜单封装 (~150 行)
    └── hotkey_picker.h/.cpp     # 录音快捷键捕获控件 (~150 行)
```

**解耦效果对比**：
* 拆分后每个源文件控制在 **150 ~ 250 行**，职责边界严密分明；
* 彻底斩断 UI 层对底层业务 Provider（如 `volcengine_asr`、`doubao_ime`）私有内部状态的依赖，连通性测试走统一服务接口；
* `main.cpp` 仅需包含 `settings_window.h` 并调用 `ShowSettingsWindow(hwndOwner)`。

---

## 5. 详细实现规范与示例代码

### 5.1 fluent_toggle.cpp 现代药丸开关实现

```cpp
// src/ui/settings/controls/fluent_toggle.cpp
#include "fluent_toggle.h"
#include <d2d1.h>
#include <windowsx.h>

constexpr wchar_t kToggleClassName[] = L"VoxType_FluentToggle";

LRESULT CALLBACK FluentToggleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<ToggleState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = new ToggleState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }
    case WM_NCDESTROY:
        delete state;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        // 调用 Direct2D 绘制现代药丸：
        // Off: 灰色边框 (1px) + 居左白圆点
        // On:  系统 Accent 实心胶囊背景 + 居右白圆点
        // 支持 Hover 光晕与过渡动画
        state->Render(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP:
        state->isChecked = !state->isChecked;
        InvalidateRect(hwnd, nullptr, FALSE);
        SendMessageW(GetParent(hwnd), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED),
                     reinterpret_cast<LPARAM>(hwnd));
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        state->isFocused = (msg == WM_SETFOCUS);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
```

---

## 6. DPI 全局自适应（PerMonitorV2）与视觉规范

严格遵循 VoxType 现行规范与 Windows 11 `PerMonitorV2` 规范：

1. **逻辑基准与物理映射**：
   * 界面统一以 **150% DPI（144 DPI，Scale = 1.0）** 为逻辑设计基准；
   * 物理像素转换统一且必须经由 `S(UiStyle::Constant)`（底层调用 `DipToPx(dip, UiStyle::Scale)`，其中 $\text{Scale} = \text{DpiScale} \times 96 / 144$）；
   * 严禁在控件布局中硬编码未经 `S()` 核算的低分辨率绝对数值。
2. **D2D 设备上下文与 DWrite 工厂**：
   * 渲染统一以 DIP（设备无关像素）进行度量与图元绘制；
   * 在处理 `WM_DPICHANGED` 时：
     ```cpp
     case WM_DPICHANGED: {
         UINT newDpi = HIWORD(wParam);
         RECT* prcNewWindow = reinterpret_cast<RECT*>(lParam);
         UpdateUiScaleForDpi(newDpi);
         SetWindowPos(hwnd, nullptr, prcNewWindow->left, prcNewWindow->top,
                      prcNewWindow->right - prcNewWindow->left,
                      prcNewWindow->bottom - prcNewWindow->top,
                      SWP_NOZORDER | SWP_NOACTIVATE);
         // 重新触发布局引擎流式求值与 D2D 渲染目标尺寸同步
         RecomputeLayoutAndRepaint(hwnd);
         return 0;
     }
     ```

---

## 7. 平滑演进路线与阶段验收指标 (Milestones)

采用**渐进替换法**，确保每一个 Milestone 均可通过 `build.bat` 验证，不打破现有编译系统：

### Milestone 1：基础设施与强类型元数据注册表（零 UI 风险）
* **任务**：
  1. 创建 `settings_schema.h` 与 `settings_binder.h`；
  2. 将 `Config` 现存的 104 个字段完整录入 `SettingItem` Schema 描述表；
  3. 编写单元测试验证 `SettingsBinder` 的双向读写与 `config.json` 保持 100% 格式兼容；
* **验收标准**：单元测试全通，现有 UI 代码分毫未改，配置契约坚固无误。

### Milestone 2：现代窗口外壳与 Navigation Rail
* **任务**：
  1. 创建 `settings_window.cpp`，接入 DWM Mica Alt 材质 (`DWMSBT_TABBEDWINDOW`) 与沉浸式暗色标题栏；
  2. 启用 `WS_THICKFRAME` 允许窗口拉伸，设置最小限制 `800x600`；
  3. 实现左侧自绘 Navigation Rail，接入 Segoe Fluent Icons 矢量码点；
* **验收标准**：窗口呈现纯正 Windows 11 Fluent 风格，左侧导航栏悬停与点击切换流畅。

### Milestone 3：自研 StackLayout 引擎与首个卡片页试点（General 页）
* **任务**：
  1. 落地 `settings_layout.h/.cpp` 自研流式引擎与 DWrite 文本测量；
  2. 接入 `fluent_toggle` 自绘药丸开关；
  3. 用现代圆角卡片重构“常规设置”页面（开机自启、快捷键、诊断录音）；
  4. 验证拉伸窗口与跨 DPI 拖拽时，卡片宽度与内部文字自动平滑重排；
* **验收标准**：在 96/144/192/288 DPI 下，文字零截断、控件零碰撞。

### Milestone 4：EDIT 穿透与多行文本输入（Prompt & Local ASR 页）
* **任务**：
  1. 落地 `fluent_edit`（无边框原生 EDIT 穿透 + GDI 实心画刷遮罩防重影 + D2D 外轮廓高光）；
  2. 迁移 LLM Prompt 编辑页与本地 sherpa 路径设置页；
  3. **IME 专项验收**：使用微软拼音、微信输入法进行长文本打字、退格、划词、回车测试，确保候选框定位 100% 准确，彻底杜绝重影；
* **验收标准**：输入体验完全原生化，多行 Prompt 支持随窗口高度垂直弹性伸展。

### Milestone 5：全量 Provider 迁移与旧代码彻底清退
* **任务**：
  1. 迁移 Cloud ASR 各 Provider（火山引擎、Qwen、百度、MiniMax、Doubao IME 等）至 Schema 体系；
  2. 利用 Schema 的 `isVisible` 谓词统一控制 Provider 条件展示，废除手写的 15 个 vector；
  3. 彻底删除旧版 `settings.cpp` 中的 229 个废弃布局常量；
  4. 更新 `tools/check_architecture.ps1` 守卫基线（`SettingsLines` 从 3450 下调至 300 以内）；
* **验收标准**：全仓编译绿灯，守卫检查通过，常驻内存保持 ~12MB。

---

## 8. 关键决策与工程排坑要点 (Gotchas)

### 8.1 GDI WM_CTLCOLOREDIT 实心画刷与文本重影陷阱（深度解析）
* **原理分析**：Win32 原生 `EDIT` 控件在绘制文本时，依赖其父窗口通过 `WM_CTLCOLOREDIT` 返回的画刷擦除文字背景。若返回 `NULL_BRUSH` 或设置 `TRANSPARENT`，GDI 仅仅是将新字形的点阵绘制在现有屏幕缓冲区之上。当用户按 Backspace 删除字符或光标闪烁重绘时，旧字符点阵未被擦除，导致严重的字符重影与光标拉花。
* **避坑法则**：**坚决杜绝返回 NULL_BRUSH**。必须创建一个与卡片内部输入背景色完全匹配的 Solid Brush（实心画刷），并调用 `SetBkColor` 与 `SetBkMode(hdc, OPAQUE)`。

### 8.2 自绘输入框 vs. 原生 EDIT 穿透的 IME 致命泥潭
* **致命风险**：对于桌面语音输入法软件，输入框若不支持中文输入法是毁灭性 Bug。自绘输入框涉及 IMM32/TSF 协议簇，处理候选窗口位置、组合串（Composition String）、属性下划线、分词替换等极其容易出现光标漂移、候选框停在屏幕左上角等顽疾。
* **正道抉择**：坚持使用无边框原生 `EDIT` 穿透，让 Windows 官方维护 IME 兼容性，UI 层仅负责外围的 Fluent 质感包装。

### 8.3 DWM Mica 材质版本探测与优雅回退
* **环境差异**：`DWMSBT_TABBEDWINDOW` (Mica Alt) 仅在 Windows 11 Build 22621 (22H2) 及以上系统生效。
* **优雅降级**：
  - Windows 11 22H2+：使用 `DWMSBT_TABBEDWINDOW`；
  - Windows 11 21H2：使用 `DWMSBT_MAINWINDOW`；
  - Windows 10 (Build 19041+)：优雅回退为深灰暗色背景（`#202020`），确保在所有受支持的系统上均稳定运行且不抛异常。

### 8.4 ComboBox 下拉列表高度测量与窗口遮挡
* **尺寸约束**：Win32 原生 `COMBOBOX` 的高度是由其内部字体决定的。在自研 StackLayout 引擎中，应使用 `CB_GETITEMHEIGHT` 动态测量其收起态高度，防止通过 `MoveWindow` 强行压缩导致文字降笔截断。

### 8.5 Settings 打开时的全局热键协调
* **互斥防吞**：当用户在 Settings 界面点击热键输入框并录入按键时，主程序的热键监听钩子（`RegisterHotKey` / `WH_KEYBOARD_LL`）必须临时暂停，防止用户的配置按键被主程序当场拦截消费。

### 8.6 异步操作与 UI 线程解耦（网络探测与路径校验）
* **禁止阻塞 UI**：旧版代码中存在点击“测试连接”时在主 UI 线程短暂调用网络探测的情况。重构后，所有 LLM / ASR 连通性测试一律通过 `std::jthread` 派发至后台任务队列，并通过自定义 Windows 消息（如 `WM_USER_TEST_COMPLETED`）异步回调更新按钮 Loading 状态与卡片提示。

---

## 9. 总结与预期收益

本方案经过严格的二次技术复核，彻底清除了早期草案中关于“盲目引入 Yoga”以及“WM_CTLCOLOREDIT 空画刷”的技术隐患：

1. **视觉跃升**：拥有纯正 Windows 11 Fluent 设计质感，Mica Alt 深度沉浸，支持自由拉伸窗口与 DPI 无损缩放；
2. **输入稳固**：坚持原生 EDIT 穿透结合实心画刷遮罩技术，实现 100% 零风险的官方中文 IME 支持；
3. **架构极简**：采用自研 ~200 行 C++23 StackLayout 引擎，零外部库依赖，架构高内聚且坚固；
4. **开发解放**：借助 C++23 强类型元数据注册表，消灭 104 项配置在 4 个文件间的 10 处机械样板，实现“1 行声明搞定持久化、UI 与布局”；
5. **合规守卫**：重构完成后 `settings.cpp` 代码行数将从 3448 行剧降至 300 行以下，有力守护 VoxType 极致轻量（~12MB）与优雅架构的长期演进。
