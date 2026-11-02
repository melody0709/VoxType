# Settings 界面语言切换（中文 / English）方案

状态：**已实施（v0.13.0），P0~P3 全部完成**，实施记录与偏差见文末 §8。范围决策（用户已确认）：
- 摆放位置：**General & Input 页**——Shortcut 组压缩 hint 为一行、**Startup 并入 Shortcut 组**（"Shortcut & Startup"），腾出的空间放新的 **Language 独立小组**（变体 2，见 §2.1）。
- Startup 的 Portable 提示文案**直接删掉**（不挪 tooltip）。
- 本地化范围：**Settings 界面先行**（五个页签 + 二级弹窗），HUD / 托盘 / 提示框后续阶段再做。

> 历史决策：初版方案曾选 Audio & Advanced 页（General 只余 18 DIP）；用户随后提出压缩 General 页 hint 行并合并 Startup 组，复算后 General 页可容纳 Language 组，入口改回 General 页（发现性更好）。Advanced 页方案作为回退备选保留（§4）。

## 1. 目标与决策

1. 新增 UI 界面语言配置：`auto`（跟随系统，默认）/ `en` / `zh`，切换后 Settings 界面文案中英互换。
2. 入口放在 General & Input 页：合并后的 "Shortcut & Startup" 组下方，Browser integration 组之后，新增 "Language" 分组（底边 502，可用 116 DIP，见 §2.1）。
3. 本地化机制选**编译期双语字符串表**（`src/ui/ui_strings.h/.cpp`，`std::wstring_view` 字段结构体 × 英/中两张表），不引入资源 DLL / 卫星程序集 / 运行时 JSON。理由见 §4。
4. 切换生效方式：**Save 后重建 Settings 窗口**（destroy + 重新 create），不重启进程。

## 2. 已确认的现状与证据

### 2.1 布局空间账（General 页压缩方案的硬依据）

全部常量在 [src/ui/ui_types.h](../../src/ui/ui_types.h)；工作区硬底 = `FooterMinTop`(632) − 4 = **628 DIP**（[validate_settings_layout.ps1](../../scripts/validate_settings_layout.ps1) L241 注释、L251 `Tab 1 reaches the tab work-area bottom` 断言）。

当前 General 页四组（底边 614，仅余 18，直接放新组不可行）：

| 组 | 高度 | 可压缩点 |
|---|---|---|
| Shortcut | 144 | 热键行(20~58) + **两行 hint**(68~98、98~128) → 可二合一行 |
| Input preview | 142 | 三行满排，不动 |
| Startup | 104 | 勾选(20~46) + **提示行**(52~100) → 提示可删 |
| Browser integration | 118 | 三行堆叠满排，不动 |

压缩/合并三个变体的复算（已选定**变体 2**）：

| 变体 | 做法 | General 底边 | Language 组可用 | 结论 |
|---|---|---|---|---|
| 1 只压缩 | hint 二合一(−30) + Startup 去提示(−48) | 536 | 82 | 勉强放下拉一行，无余量 |
| **2 Startup 并入**（选定） | "Shortcut & Startup" 合并组 + Language 独立组 | **502** | **116** | 语义清晰 + 空间宽裕 |
| 3 全并入 | 热键+启动+语言一组 | 540 | 组内 | 组语义变杂，否决 |

变体 2 的合并组几何（新常量进 `UiStyle`）：

```
"Shortcut & Startup" 组（H = 146）：
  热键行        20 ~ 58   （HotkeyEditH=38，label 不动）
  单行 hint     68 ~ 98   （两条旧 hint 二合为一，LabelH=30）
  启动勾选行   104 ~ 130  （CheckH=26，原 Startup 组勾选原样迁入）
  底衬          130 ~ 146
页序与 Y：  Shortcut&Startup (76~222) → Input preview (232~374)
          → Browser (384~502) → Language (512~628，可用 116)
```

- 合并组 146 < 原两组分立 144+10+104 = 258，净省 112 DIP。
- Language 组（H = 102）：label+combo 行 20~52、单行提示 56~86、底衬至 102；底边 512+102 = 614 ≤ 628，余 14 DIP。
- 单行 hint 的英文合并文案（约 80 字符："Click the field and press a key combination. Esc cancels; Backspace/Delete clears."）估算 96 DPI 下 ~440px < 740 宽；**脚本测不出文字渲染宽度**，P1 需在 96 DPI 目测确认（中文更短无风险）。

### 2.2 无任何 UI 本地化基础设施

- 全仓 `language` 命中均为 **ASR 识别语言**配置（`qwenLanguage`、`maiLanguage`、`mimoLanguage` 等，[config_registry.cpp](../../src/core/config_registry.cpp#L288-L346)），与界面语言无关。
- UI 文案全部是硬编码 `L"..."` 字面量：`src/ui` 下共 **879 处 / 31 文件**（含窗口类名、JSON key 等非文案，估算可翻译文案 **~400–500 条**）。分布：tabs 247（general 76 / advanced 61 / llm 46 / speech_engine 32 / vocabulary 32）、providers 287、[settings_dialogs.cpp](../../src/ui/settings_dialogs.cpp) 115、[settings_controls.cpp](../../src/ui/settings_controls.cpp) 71、[hotkey.cpp](../../src/ui/hotkey.cpp) 68、[settings.cpp](../../src/ui/settings.cpp) 19。
- 托盘菜单 6 条文案在 [main_window.cpp](../../src/app/main_window.cpp#L191-L209)（`Settings...` / `Reload ASR Engine` / `Quit` 等），属后续阶段。

### 2.3 Settings 窗口生命周期（切换生效机制的依据）

- Settings 是**非模态顶层窗口**，`ShowSettingsWindow()` 首次 `CreateWindowExW` 后复用 `g_settingsWindow`（[settings.cpp](../../src/ui/settings.cpp#L363-L382)）。销毁重建即可让 Tab 名、全部控件、二级弹窗（按需创建、创建时取文案）重新取字符串，**无需重启进程**。
- Tab 名在创建路径经 `TCM_INSERTITEM` 插入（[settings.cpp](../../src/ui/settings.cpp#L184-L200)），重建时自动换新语言。
- 底部 Save/Close 由 `LayoutSettingsWindow()` 按 `FooterHeight`/`FooterMinTop` 动态排布，不依赖固定窗口高度。
- Startup 勾选（`IDC_START_WITH_WINDOWS`）与 `RefreshStartupRegistrationControl()` 的动态状态逻辑**不改**，只是把控件挪进合并组。

### 2.4 机械守卫与预算约束（实施红线）

- `settings.cpp` 实测 387/400 行（余 13）；`main.cpp` 150/150（**余 0**）——语言接线**不得进 main.cpp**，settings.cpp 只留最小触发逻辑，字符串与分组代码进 `src/ui/ui_strings.*` 与 [tab_general.cpp](../../src/ui/tabs/tab_general.cpp)。
- 新源文件必须**显式**加进 `CMakeLists.txt`（禁 `file(GLOB)`），且新模块（`ui_strings`）必须登记 `ARCHITECTURE.md` 与 `doc/ARCHITECTURE_zh.md`，否则 `check_architecture.ps1` 的 `DocEntriesMissing` 基线反弹即 FAIL。
- 布局常量一律进 `UiStyle`（ui_types.h），弹窗/控件尺寸经 `S()` 折算；改动后跑 `scripts/validate_settings_layout.ps1`（96/144/192/288 DPI 静态校验）。合并组的行偏移目前在 [tab_general.cpp](../../src/ui/tabs/tab_general.cpp#L142-L148) 里是内联 `S(28)`/`S(68)`/`S(98)`，顺手收敛为命名常量。
- 含中文字符串字面量的源文件必须 `/utf-8`（所有目标已继承；新目标不得遗漏）；用常规 `"..."`/`L"..."` 字面量，**禁 `u8"..."`**。
- `check_architecture.ps1` 基线只降不升；本次不涉及 globals/main/settings 行数反弹（若 settings.cpp 紧张，优先把既有代码下放到模块，而非涨基线）。

## 3. 目标架构

### 3.1 字符串表（新模块，`src/ui` 根目录，不新建子目录）

```cpp
// src/ui/ui_strings.h  （示意）
struct UiStrings {
    std::wstring_view settingsTitle;          // "VoxType Settings"
    std::wstring_view tabGeneral;             // "General & Input"
    // ... 每个可翻译文案一个字段；英/中两表字段一致，
    // 缺翻译 = 编译错误，天然杜绝漏译
};
const UiStrings& UiText();                    // 取当前语言表
enum class UiLanguage { Auto, En, Zh };
UiLanguage EffectiveUiLanguage();             // 解析 auto → 实际语言
```

- 两张静态表 `kUiStringsEn` / `kUiStringsZh`（`ui_strings.cpp`），字段齐全是编译期保证。
- 动态拼接文案（如 `Chrome: paired (offline) / Edge: paired (offline)`）拆成片段字段，代码用 `std::format` 组合；en/zh 的占位符 `{}` 数量一致性由测试断言。
- 语言选择器本身永远显示**自称**：`Follow system / 跟随系统`、`English`、`中文`，不受当前语言影响。

### 3.2 配置项（强类型注册表三步规范）

1. [config_store.h](../../src/core/config_store.h)：`std::wstring uiLanguage = L"auto";`（合法值 `auto` / `en` / `zh`）。
2. [config_registry.cpp](../../src/core/config_registry.cpp)：`reg.Register({"ui_language", &Config::uiLanguage});`（无加密、无格式约束）。
3. [config_store.cpp](../../src/core/config_store.cpp) 校验：非法值回退 `auto`（参照 `maiLanguage` 的回退模式，L165-167）。
4. [tab_general.cpp](../../src/ui/tabs/tab_general.cpp)：Language 分组的 combo 走 `LoadControls` / `SaveControls`。

### 3.3 General 页新布局（全部新常量进 UiStyle）

```
GeneralShortcutStartupGroupH   = 146   （合并组，替代 GeneralShortcutGroupH=144）
GeneralShortcutHintOffsetY     = 68    （单行，二选一合并文案）
GeneralStartupCheckInShortcutOffsetY = 104
GeneralStartupGroupGap/H/CheckOffsetY/HintOffsetY/HintW   ← 删除（Startup 独立组与提示行不复存在）
GeneralLanguageGroupGap        = 10
GeneralLanguageGroupH          = 102
GeneralLanguageRowOffsetY      = 20    （label 用 LabelWidth，combo 用 ComboW）
GeneralLanguageHintOffsetY     = 56    （单行提示，LabelH=30，底 86）
```

- 页序：Shortcut & Startup → Input preview → Browser integration → Language（底边 614 ≤ 628）。
- 分组标题：`Shortcut & Startup`；Startup 勾选保留 `IDC_START_WITH_WINDOWS` 原 ID 与动态状态接线。
- Language 提示文案建议：`Applies after Save. Interface language only; recognition language is set per engine.`（中文：`保存后生效。仅切换界面语言；识别语言在各引擎页单独设置。`）——明确区分"界面语言"与既有各 provider 的 ASR 语言下拉。
- [validate_settings_layout.ps1](../../scripts/validate_settings_layout.ps1) 同步：删除 Startup 提示行两条断言（L260 `single-line box would clip`、L257 `exceeds group width`）与 `GeneralStartup*` 读取；`generalBottom` 计入合并组与 Language 组；新增 Language 组底边 ≤ 628 与组内行不重叠断言。**这是常量与校验同步更新，不是放宽校验**。

### 3.4 切换生效：Save 后重建窗口

- Save 流程中比较"本次选择"与"当前生效语言"：变化时 `DestroyWindow(g_settingsWindow)` 后重走 `ShowSettingsWindow(owner)`；无变化不动（其余保存逻辑照旧）。
- 二级弹窗（`settings_dialogs.cpp` / Qwen / Volcano 高级对话框）按需创建、创建时取文案，自然生效。
- Close/Escape 不 Save → 语言不变（与其它配置一致）。
- 重建触发代码放 `tab_general.cpp` 的 Save 侧或 `settings_controls`，`settings.cpp` 只留一个最小钩子（受 387/400 行预算约束）。

## 4. 已否决的替代方案（记录理由）

| 方案 | 否决理由 |
|---|---|
| General 页不压缩直接塞（初版结论） | 余 18 DIP 放不下；用户提出压缩 hint + 合并 Startup 后复算可行，本方案即压缩版 |
| Audio & Advanced 页 Language 分组（初版选定） | 可行（余 110 DIP）但发现性不如首页；**保留为回退备选**——若合并组实测渲染有意外（96 DPI hint 截断等），退回复杂度更低的该方案 |
| 变体 1（只压缩不合并） | Language 组仅 82 DIP，提示行放不下（溢出 4 DIP），无余量 |
| 变体 3（热键+启动+语言全并一组） | 组语义变杂；省一个组标题的收益不抵可读性损失 |
| Startup 提示挪 tooltip | 用户确认直接删掉；Portable 修正说明留在文档 |
| Footer 左侧快捷下拉 | 与 `Ready.` 状态文案挤同一排，不必要 |
| 字符串表资源 + 卫星 DLL（LoadString） | 为 2 种语言引入 .rc/卫星级构建复杂度，收益不匹配 |
| 运行时 JSON 语言包 | 可免重编译加语言，但引入 IO、缺 key 回退、打包（`cmake/VoxTypeRuntime.cmake` 安装清单）一整套机制；当前只需 zh/en |
| 新建 `src/ui/i18n/` 子目录 | 违反"不得在 src/ 下新建未在分层矩阵中声明的目录"；`ui_strings.*` 放 `src/ui` 根即可 |

## 5. 分阶段实施与退出条件

> 开工前先做备份到 `.bak\`（仓库根，gitignored）。

### P0：字符串盘点与基础设施（行为不变）

- 产出文案清单（文件 → 字段名 → en 原文），过滤窗口类名 / JSON key / 格式串等非文案。
- 新建 `src/ui/ui_strings.h/.cpp`（en 表完整、zh 表先留空串占位或与 en 相同），登记两份架构文档，显式加入 `CMakeLists.txt`。
- 退出判据：全量构建通过，`check_architecture.ps1` 全绿（基线未上调），界面行为零变化。

### P1：英文文案替换 + 中文表

- Settings 五个页签、二级弹窗、`settings_controls`、`hotkey` 中的 UI 文案改为 `UiText().xxx` 取值；中文表补全。
- 退出判据：`en` 下界面与改动前逐字一致（回归基线）；`zh` 下人工走查五个页签 + 弹窗无漏译、无截断。

### P2：General 页布局改造 + Language 分组 + 配置项 + 重建生效

- §3.3 布局：Shortcut hint 二合一行、Startup 勾选并入合并组（Portable 提示删除）、新增 Language 分组；内联偏移收敛为 `UiStyle` 命名常量。
- §3.2 配置三步 + §3.4 重建逻辑。
- 同步 [validate_settings_layout.ps1](../../scripts/validate_settings_layout.ps1)（删 Startup 提示断言、合并组/Language 组底边与不重叠断言）。
- 退出判据：合并组与 Language 组在 96/144/192/288 DPI 静态校验通过且 96 DPI 目测单行 hint 无截断；切中文 → Save → 窗口重建后全界面中文；切回英文同理；重启进程后语言保持；`auto` 在 zh 系统语言下显示中文、en 系统下英文。

### P3：测试、文档与收尾

- 新增 `tests/ui_strings_test.cpp`：两表字段非空、`std::format` 占位符数量 en/zh 一致、非法 `ui_language` 回退 `auto`；随 `build.bat --test` 构建。
- 契约同步：`ARCHITECTURE.md` + `doc/ARCHITECTURE_zh.md` 登记 `ui_strings`、`AGENTS.md` 补"界面语言配置项"约定；CHANGELOG.md + doc/CHANGELOG_zh.md + 两份 README 版本行 + `src/app/resource.h` 版本号；发布收尾打附注 tag。
- 退出判据：`build.bat` 主流程全绿（含架构守卫与布局校验）、`build.bat --test` 全绿。

### 后续（不在本次范围）

- HUD（`hud.cpp` / 分页）、托盘菜单、启动/错误提示框本地化；更多语言（按需再评估 JSON 语言包）。
- 可选增强：Footer 语言快捷入口。

## 6. 验收矩阵

| 项 | 判据 |
|---|---|
| 布局 | General 页含合并组 + Language 组后底边 ≤ 628 DIP；`validate_settings_layout.ps1` 四档 DPI 全过；96 DPI 目测单行 hint 不截断 |
| 合并组回归 | Startup 勾选功能（含 `RefreshStartupRegistrationControl` 动态状态、Portable 修正）行为不变，仅位置变化 |
| 切换 | 选中文 → Save → 窗口即时变中文（无需重启）；不 Save 关闭则不变 |
| 持久化 | 重启后语言保持；非法配置值回退 `auto` |
| 回归 | `en` 界面文案与改动前一致（除删去的 Startup 提示与合并的 hint）；录音/识别链路零行为变化 |
| 守卫 | `check_architecture.ps1` 24 项全绿、基线未上调、新模块已登记两份架构文档 |
| 测试 | `build.bat` + `build.bat --test` 通过；`tests/ui_strings_test.cpp` 覆盖表完整性与占位符一致性 |

## 7. 当前未决项

1. 中文表用简体即可？繁体暂不做（默认简体，待确认）。
2. `auto` 的系统语言判定用 `GetUserDefaultUILanguage()` 主语言是否为 zh（含 zh-CN/zh-TW/zh-HK 都映射到简体表）——默认按此，繁中用户后续再议。
3. 合并组标题用 "Shortcut & Startup"（默认）还是更中性的 "General"——翻译时中文拟用「快捷键与启动」，实现时可再定。

## 8. 实施记录（v0.13.0）与相对方案的偏差

已落地内容与 §3/§5 一致，差异与补充如下：

1. **表字段类型**：用 `const wchar_t*`（`struct UiStrings` 由 `ui_strings.inc` 的 X-macro 生成）而非方案里的 `std::wstring_view`。两张表都是 `constexpr`，字段齐全是编译期保证；`tests/ui_strings_test.cpp` 断言非空、英/中 `{}` 数量一致、换行数一致、语言解析与组合框序数往返。
2. **重建保留窗口摆放与保存反馈**（方案未写、审查中补，且有两次返工）：`RebuildSettingsWindowForLanguage()`（`settings_controls.cpp`）在销毁前 `GetWindowPlacement` **整份** `WINDOWPLACEMENT`，`ApplyCapturedSettingsPlacement()`（同文件）在新窗口创建后用 `SetWindowPlacement` 恢复，再由调用方 `ShowWindow(SW_SHOWMAXIMIZED/SW_SHOW)` 决定最大化态；旧位置若不在任何显示器上（`GetWindowRect` + `MonitorFromRect`）回落到居中。重建后补写 `Saved...` 状态，避免新窗口 `WM_CREATE` 的 `Ready.` 盖掉保存反馈。触发消息 `kUiLanguageReapplyMessage`（`WM_APP + 17`）用 `wParam` 传 `fallbackAdjusted`，不引入跨调用静态状态。
   > 返工记录（评审 P2）：①最初把 `rcNormalPosition` 直接喂给 `CreateWindowExW`——它是**工作区坐标**，任务栏在顶部/左侧时窗口每切一次语言就漂移；改为 `SetWindowPlacement` 由系统换算。②最初把下载完成通知固定投递到启动时捕获的 Settings 句柄——重建后该句柄失效，2GB 下载的完成结果（模型目录字段、成功提示）静默丢失且新窗口按钮恢复可点；改为 `kModelDownloadDoneMessage` 先发**主窗口**（`SetModelDownloadNotifyWindow` 在 `WM_CREATE` 登记），主窗口再转给当前 `g_settingsWindow`，并用 `path_service::IsModelDownloadRunning()` 在重建后的 `LoadControls` 里保持按钮禁用（`RunModelDownloader` 内另有 CAS 兜底）。③主窗口那一步最初仍用 `PostMessage` 转发：`DestroyWindow` 会把该窗口队列里未派发的消息连同 payload 一起丢弃（投递返回 TRUE，处理器却永不执行），改为 `app_messages.h` 的 `SendToWindow()`（同线程 `SendMessage`），并新增 `tests/message_forward_test.cpp` 用隐藏窗口钉住"触发转发时销毁消息已在队列"的顺序；把 `SendToWindow` 改回 `PostMessage` 会直接 FAIL。
3. **布局守卫补强**（方案只要求"删 Startup 提示断言 + Language 组断言"）：`validate_settings_layout.ps1` 另加合并组三行堆叠（热键行/标签/hint/启动勾选）与组底不越界断言、`GeneralGroupHintW` 宽度断言、Language 下拉宽度断言。组内单行 hint 宽收敛为 `UiStyle::GeneralGroupHintW = 770`（组内可用宽 776），比原内联 `S(740)` 多 4% 余量。
4. **行数预算**：`settings.cpp` 实测 **369/400**（比方案记录时更少）；`LayoutSettingsWindow`、`SetStatus`、语言重建的摆放/状态处理全部在 `settings_controls.cpp`，`main.cpp` 未动（150/150）。
5. **文案审计结论**：`ui_strings.inc` 共 405 条，全部有代码引用（无死条目）；`src/ui` 下已无残留英文界面句（仅产品名 `FireRedASR2 CTC/AED`、`Azure Fast Transcription`、`Segoe UI`、热键名 `Page Up/Down`、语言自选项保留）。
6. **顺序不变式**：千问语言表（28 项）、MAI 语言、MiMo 预设 URL、火山语言/模式、百度 DevPID 的顺序与配置码逐项比对与改动前**完全一致**；所有下拉都按索引/值（`ComboBox_SetItemData` / `FormBinder` 的 value 匹配）回写配置，不按可见文案反查。
