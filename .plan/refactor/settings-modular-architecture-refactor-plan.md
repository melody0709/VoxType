# VoxType Settings 界面模块化与数据解耦架构方案（修订版）

> **状态**：方案深化（已融合 5 大硬伤攻关、7 项设计缺陷修补与 7 项遗漏补充）
> **日期**：2026-09-20
> **核心定位**：**100% 保持现有 Win32 原生界面样式、视觉布局与交互行为不变**，聚焦于内部架构、模块划分与数据绑定的彻底重构。
> **核心解决目标**：消除“修改/新增一条配置需要牵扯 10 处”的雪崩效应；消除 3438 行巨石单文件的守卫红线危机；消除 13 处跨层 include 违规。

---

## 0. 方案背景与核心转变

### 0.1 历史方案的偏差与纠偏
在早期方案（`.plan/feat/settings-ui-refactor-plan.md`）中，曾试图引入 Yoga flexbox 布局引擎、RmlUi/Slint、自绘 D2D 卡片、窗口可拉伸等重型外部依赖。
经用户重新审视后，明确指出：
> **“只是想简单的保持现有的界面，是重构它的那个架构、模块，以方便以后修改调整。不要修改一条信息要动用很多，或者牵连很多。”**

### 0.2 核心原则
1. **视觉与原生体验 100% 冻结（Zero UI Change, Zero IME Risk）**：
   - 不引入任何外部 UI 框架（零新增第三方依赖，产物零膨胀）；
   - 不重绘控件（完全保留原生 `EDIT`、`COMBOBOX`、`BUTTON`、`CHECKBOX`）；
   - 彻底避免自绘文本框接管带来的输入法（IME）上屏、光标、候选窗等产品级风险。
2. **消解“改一动十”（Declarative Binding）**：
   - 建立轻量级字段注册表（Config Registry）与表单绑定器（Form Binder）；
   - 新增/修改字段由“4 文件改 10 处”收敛为“3 处显式声明”。
3. **分层合规与模块物理隔离（Decoupling & Modularization）**：
   - 3438 行巨石单文件拆分为独立的 Tab 与 Provider 模块；
   - ASR 探针行为通过 `Core` 抽象解耦，UI 彻底断开对 13 个 ASR 内部头文件的违规引用。

---

## 1. 深度审查：5 大落地硬伤与针对性工程解法

在初版构想中，存在 5 个直接导致编译失败或破坏现有行为的硬伤，本修订版给出了严谨的工程解法：

### 硬伤 1：`void* memberPtr` 编译硬伤
- **问题根因**：C++ 标准（§7.3.12）严格规定，类成员指针（`Type Class::*`）不是对象指针，不能隐式或显式强转为 `void*`。初版方案标注“类型安全”但使用了 `void*`，在 MSVC 下直接报 `C2440: cannot convert from pointer-to-member to void*`。
- **工程解法**：采用 C++23 强类型 `std::variant` 保存合法的成员指针：
  ```cpp
  using ConfigMemberPtr = std::variant<
      std::wstring Config::*,
      bool Config::*,
      int Config::*,
      float Config::*
  >;
  ```
  在序列化与反序列化时，使用 `std::visit` 实现零开销、零警告、强类型的访问。

### 硬伤 2：加密字段语义丢失
- **问题根因**：`Config` 内部有 9 个敏感字段（`llmApiKey`, `baiduSecretKey`, `maiOpenRouterApiKey`, `maiAzureApiKey`, `volcApiKey`, `qwenApiKey`, `mimoApiKey`, `doubaoImeToken`, `qwenFreeUtdidOverride`），在内存中均为普通 `std::wstring`。单纯依靠字段类型无法区分是否需要 DPAPI 加密。
- **工程解法**：元数据中显式定义 `CryptoPolicy` 枚举：
  ```cpp
  enum class CryptoPolicy { None, Dpapi };
  ```
  在读取时自动调用 `llm::DecryptString`，在写入时自动调用 `llm::EncryptString`，严格杜绝明文落盘。

### 硬伤 3：JSON 中 bool 的三种异构编码格式
- **问题根因**：实测现存 `SaveConfig`（`config_store.cpp`）中，布尔字段存在三种异构编码：
  1. **字面量布尔**：`\"enable_vad\": true/false`（绝大多数）
  2. **带引号字符串**：`\"volc_enable_nonstream\": \"1\"/\"0\"`（火山引擎系列参数）
  3. **未加引号数字**：`\"qwen_free_polish\": 1/0`（Qwen Free 系列参数）
  若统一强制序列化为 `true/false`，不仅无法达成“字节级等价”的重构安全判据，还会破坏与外部配置解析器的兼容性。
- **工程解法**：在元数据中引入 `BoolJsonFormat` 枚举：
  ```cpp
  enum class BoolJsonFormat {
      Literal,    // true / false
      QuotedInt,  // "1" / "0"
      RawInt      // 1 / 0
  };
  ```
  加载时由 `ExtractJsonBool` 宽松兼容解析；保存时严格按注册的 `BoolJsonFormat` 还原历史格式。

### 硬伤 4：`llmProvidersJson` 嵌套 JSON 与业务生命周期钩子
- **问题根因**：`Config` 并非纯粹的扁平数据结构：
  - `llm_providers_json` 是一个转义的嵌套 JSON 字符串（JSON in JSON）；
  - 保存前必须执行业务收敛钩子：`SaveCurrentProvider(cfgCopy)`、`NormalizeQwenFreePostProcessConfig(cfgCopy)`、`NormalizeDiagnosticAudioMode(cfgCopy)`；
  - 加载后必须执行版本迁移逻辑（`configVersion < kCurrentConfigVersion`）。
- **工程解法**：**双轨制设计（表驱动 + 生命周期钩子）**：
  1. 注册表负责管理常规扁平字段（占 95% 以上）；
  2. 注册表支持 `PreSaveHook` 与 `PostLoadHook` 扩展点；
  3. `llmProvidersJson` 提供专用的 `CustomSerializer`，在主注册表迭代完成后由专用钩子处理其嵌套同步。

### 硬伤 5：运行时瞬态字段（Runtime-Only Fields）混淆
- **问题根因**：`Config` 结构体内混入了 5 个仅在运行时使用的瞬态字段：
  - `uint64_t asrAttemptId`
  - `audio_diagnostics::StageKind asrDiagnosticStageKind`
  - `unsigned asrDiagnosticStageIndex`
  - `bool qwenInputContextSnapshotCaptured`
  - `std::wstring qwenInputContextSnapshot`
  这些字段不应落盘，更不能被粗暴强塞入注册表。
- **工程解法**：在 `struct Config` 中用注释明确区隔：
  `// === Persistent Config Fields ===` 与 `// === Runtime Transient State ===`。
  注册表仅对持久化字段进行登记，运行时字段完全排除在序列化管线之外。

---

## 2. 7 项设计缺陷与 7 项遗漏补充

### 2.1 设计缺陷修补
1. **IDC_ 分配依然需要手工维护**：
   - *修补*：表单绑定器提供 `AutoId()` 机制（在特定保留段如 `4000~4999` 内自动递增分配），对于不需要在 `WM_COMMAND` 单独拦截普通事件的静态展示控件（如只读标签、常规输入框），免去在 `ui_types.h` 中逐个取名分配 ID 的负担。
2. **WM_COMMAND / WM_NOTIFY 路由分发机制缺失**：
   - *修补*：定义 `ITabPanel` 接口中的虚方法：
     `virtual bool OnCommand(HWND hwnd, WORD id, WORD code) { return false; }`
     主窗体 WndProc 接收到消息时，仅需将消息分发给当前激活的 Tab 实例，若子模块消费则返回，彻底消灭外壳文件的几百行 `switch-case`。
3. **跨层违规数实测校准**：
   - *修补*：实测 `check_architecture.ps1` 输出为 14 处违规（1 处 `audio_diagnostics.cpp -> resource.h`，其余 13 处均在 `settings.cpp`）。解除 UI 依赖后，全仓违规数直接由 14 降至 1。
4. **Qwen Free UTDID / Shell 路径等重度异步逻辑解耦**：
   - *修补*：将 `QwenFreeStatusMessage` 及其工作线程一并移至 `provider_qwen_free.cpp`，对外仅通过回调交互。
5. **模态弹窗与主设置窗的交互规范**：
   - *修补*：保留现有的 `settings_dialogs.cpp`，通过数据结构体传参，弹窗作为叶子节点，不得直接访问全局变量。
6. **热键捕获与全局快捷键冲突**：
   - *修补*：保持现有 `HotkeyEditProc` 逻辑，将其封装进 `GeneralTab` 内部。
7. **多 Tab 间数据依赖联动**：
   - *修补*：如 Fallback Backend 联动限制，通过 `ITabPanel::OnConfigChanged` 广播机制进行局部刷新。

### 2.2 遗漏项工程补齐
1. **浮点数格式化精度控制**：
   - 在注册表中增加 `floatPrecision` 属性（如 `vadThreshold` 指定为 `%.2f`），避免浮点数序列化产生 `0.15000000596` 的脏文本。
2. **CMakeLists.txt 改造计划**：
   - 仓库守卫**硬性禁止 `file(GLOB)`**。所有新拆分出的 `src/ui/tabs/*.cpp` 与 `src/ui/providers/*.cpp`，必须在 `CMakeLists.txt` 的 `VoxType` 源文件列表中**逐个显式罗列**。
3. **18 个全局 `vector<HWND>` 平滑退役路径**：
   - 不能直接粗暴删除导致全仓编译崩溃；在过渡期让各 TabPanel 拥有自己的私有 `std::vector<HWND> m_controls`，逐步将全局向量废弃。
4. **字符串 Trim 与边界规范**：
   - 在 FormBinder 提取文本时统一执行 `Trim`，与现有业务行为保持一致。
5. **DPI 变化（WM_DPICHANGED）全链条重测**：
   - Tab 切换与子面板显隐必须全部经由 `S()` 物理像素推导，字体统一走 `ApplyUiFont`。
6. **配置版本迁移（Migration）保持**：
   - 现存 `configVersion < 15` 的升级分支原样保留在 `config_store.cpp` 的 `PostLoadHook` 中。
7. **只读测试桩解耦**：
   - 保证拆分后的单元测试（`tests/`）能够无缝链接 `config_registry` 进行离线验证。

---

## 3. 核心架构详细设计

### 3.1 强类型字段注册表契约（`src/core/config_registry.h`）

```cpp
#pragma once
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <functional>
#include <optional>

struct Config;

namespace config_registry {

enum class CryptoPolicy { None, Dpapi };
enum class BoolJsonFormat { Literal, QuotedInt, RawInt };

using MemberPtr = std::variant<
    std::wstring Config::*,
    bool Config::*,
    int Config::*,
    float Config::*
>;

struct FieldEntry {
    std::string_view jsonKey;
    MemberPtr member;
    CryptoPolicy crypto = CryptoPolicy::None;
    BoolJsonFormat boolFormat = BoolJsonFormat::Literal;
    int floatPrecision = 2; // 仅对 float 生效
    // 默认值用于重置或缺省填充
    std::variant<std::wstring, bool, int, float> defaultValue;
};

// 预处理与后处理钩子
using LifecycleHook = std::function<void(Config&)>;

class Registry {
public:
    static Registry& Instance();

    void Register(FieldEntry entry);
    void SetPreSaveHook(LifecycleHook hook);
    void SetPostLoadHook(LifecycleHook hook);

    void LoadJson(Config& cfg, const std::string& json) const;
    std::string SaveJson(const Config& cfg) const;

private:
    std::vector<FieldEntry> m_entries;
    LifecycleHook m_preSaveHook;
    LifecycleHook m_postLoadHook;
};

} // namespace config_registry
```

---

### 3.2 UI 表单绑定器与自动游标（`src/ui/form_builder.h`）

```cpp
#pragma once
#include <windows.h>
#include <string>
#include <variant>
#include <vector>
#include <span>
#include "ui_types.h"

struct Config;

namespace ui_form {

class LayoutCursor {
public:
    explicit LayoutCursor(int startY = UiStyle::FirstRowY, int rowHeight = UiStyle::RowHeight);

    int NextRowY();
    int CurrentY() const { return m_currentY; }
    void Advance(int designPx);

    void BeginGroup(HWND parent, const wchar_t* title, int width = UiStyle::GeneralGroupW);
    void EndGroup();

private:
    int m_currentY;
    int m_rowHeight;
    HWND m_groupHwnd = nullptr;
    int m_groupStartY = 0;
};

class FormBinder {
public:
    using BoundMember = std::variant<
        std::wstring Config::*,
        bool Config::*,
        int Config::*,
        float Config::*
    >;

    struct Binding {
        int idc;
        BoundMember member;
        enum class ControlType { Edit, CheckBox, ComboBox, Password } type;
    };

    HWND AddEditRow(HWND parent, LayoutCursor& cursor, int idc,
                   const wchar_t* label, std::wstring Config::* field,
                   int editW = UiStyle::InputW);

    HWND AddPasswordRow(HWND parent, LayoutCursor& cursor, int idcEdit, int idcToggle,
                       const wchar_t* label, std::wstring Config::* field);

    HWND AddCheckBox(HWND parent, LayoutCursor& cursor, int idc,
                     const wchar_t* text, bool Config::* field);

    HWND AddComboRow(HWND parent, LayoutCursor& cursor, int idc,
                    const wchar_t* label, std::wstring Config::* field,
                    std::span<const std::pair<std::wstring, std::wstring>> options);

    void LoadFromConfig(HWND parent, const Config& cfg);
    void SaveToConfig(HWND parent, Config& cfg);

    const std::vector<HWND>& GetControls() const { return m_controls; }

private:
    std::vector<Binding> m_bindings;
    std::vector<HWND> m_controls;
};

} // namespace ui_form
```

---

### 3.3 ASR 探针解耦契约（`src/core/asr_probe_service.h`）

```cpp
#pragma once
#include <string>
#include <functional>
#include "config_store.h"

namespace asr_probe {

struct ProbeRequest {
    std::wstring provider;
    Config configSnapshot; // 隔离当前界面未保存的临时输入
};

struct ProbeResult {
    bool ok = false;
    std::wstring message;
};

using ProbeCallback = std::function<void(const ProbeResult&)>;

class IAsrProbeService {
public:
    virtual ~IAsrProbeService() = default;
    virtual void ProbeAsync(const ProbeRequest& req, ProbeCallback callback) = 0;
};

// 全局注册点（定义在 core，由 app 层启动时注入）
void RegisterProbeService(IAsrProbeService* service);
IAsrProbeService* GetProbeService();

} // namespace asr_probe
```

---

### 3.4 模块拆解物理拓扑与 CMake 映射

```
src/ui/
├── settings_window.cpp           # 主窗体外壳: TabControl 切换、Footer 按钮、保存协调 (~250 行)
├── settings_window.h            # 外部仅暴露 ShowSettingsWindow / HideSettingsWindow
├── form_builder.h / .cpp        # 原生 Win32 控件表单绑定器与自动游标 (~250 行)
├── tabs/
│   ├── settings_tab_base.h      # ITabPanel 纯虚基类
│   ├── tab_general.cpp          # 常规设置 (~260 行)
│   ├── tab_recognition.cpp      # 本地识别 (~320 行)
│   ├── tab_cloud_asr.cpp        # 云端识别总控 (~180 行)
│   ├── tab_llm.cpp              # LLM 纠错 (~280 行)
│   └── tab_prompt.cpp           # 提示词 (~100 行)
└── providers/
    ├── provider_volcengine.cpp  # 火山引擎子面板 (~160 行)
    ├── provider_qwen.cpp        # 通义千问 (~220 行)
    ├── provider_qwen_free.cpp   # Qwen IME 逆向通道 (~180 行)
    ├── provider_baidu.cpp       # 百度 ASR (~110 行)
    ├── provider_mimo.cpp        # 小米 MiMo (~110 行)
    ├── provider_mai.cpp         # MAI (~160 行)
    └── provider_doubao.cpp      # 豆包 IME 通道 (~100 行)
```

**CMakeLists.txt 明确同步**：所有新建 `.cpp` 文件逐行写入 `CMakeLists.txt` 的 `VoxType` 目标源文件清单，守卫脚本对 `file(GLOB)` 违规硬拦截。

---

## 4. 重构后新增/修改配置项的标准工作流

以“在火山引擎面板新增一个开关 `volc_enable_new_feature`”为例：

| 步骤 | 现存流程（痛苦、易漏） | 重构后新流程（极简、安全） |
|---|---|---|
| **第 1 步** | `config_store.h` 增加字段 | `config_store.h` 增加字段（1 行） |
| **第 2 步** | `config_store.cpp` 手写 JSON 解析 | `config_registry.cpp` 注册该字段（指定 BoolJsonFormat）（1 行） |
| **第 3 步** | `config_store.cpp` 手写 JSON 保存拼接 | *(自动表驱动，无需手写)* |
| **第 4 步** | `ui_types.h` 算坐标常量、调组高 | *(游标自动推导，无需手算)* |
| **第 5 步** | `validate_settings_layout.ps1` 改正则断言 | *(无需改动)* |
| **第 6 步** | `settings.cpp` WM_CREATE 手写 CreateWindow | `provider_volcengine.cpp` 中调用 `builder.AddCheckBox(...)`（1 行） |
| **第 7 步** | `settings.cpp` 手写 LoadSettingsControls | *(自动双向绑定，无需手写)* |
| **第 8 步** | `settings.cpp` 手写 SaveSettingsControls | *(自动双向绑定，无需手写)* |
| **总改动量** | **4 个文件，10 处手动编码** | **3 个文件，各写 1 行，共 3 行显式声明** |

彻底实现**“修改一条信息不要动用很多、不要牵连很多”**的核心诉求！

---

## 5. 实施路线图（五阶段稳健演进）

### Milestone M1：字段注册表与序列化收敛
- 实现 `src/core/config_registry.h/.cpp`（强类型 variant 成员指针、3 种 bool 格式支持、DPAPI 加密策略、生命周期钩子）。
- 接入现有持久化字段，保留 `PreSaveHook` / `PostLoadHook` 兼容既有业务逻辑。
- 运行 `asr_json_protocol_test` 与离线配置单测，验证配置读写 100% 格式兼容。

### Milestone M2：UI 表单绑定器与布局游标实现
- 实现 `src/ui/form_builder.h/.cpp`。
- 遵循 `UiStyle` 144 DPI 规范与 `ApplyUiFont`。
- 验证控件属性、窗口样式与原生 `CreateWindowExW` 一致。

### Milestone M3：ASR 探针服务解耦（消除 13 处跨层违规）
- 在 `src/core/asr_probe_service.h` 定义抽象服务接口。
- 在 `src/app/asr_probe_service_impl.cpp` 实现具体探针调用。
- 清除 UI 层对 13 个 ASR 头文件的 include。
- 运行 `check_architecture.ps1`，验证 cross-layer violations 降为 1。

### Milestone M4：Tab 与 Provider 模块化拆解
- 逐个迁移 Prompt Tab -> LLM Tab -> General Tab -> Recognition Tab -> Cloud ASR Tab。
- 将主窗口精简为 `settings_window.cpp`（~250 行）。
- 更新 `CMakeLists.txt`，逐一登记新源文件。
- 守卫基线 `SettingsLines` 从 3450 大幅下调至 400 以下。

### Milestone M5：全功能回归与 DPI 验证
- 在 96 DPI、144 DPI、192 DPI、288 DPI 下进行目视与交互回归。
- 验证中文输入法候选窗、光标、上屏零异常。
- 执行 `validate_settings_layout.ps1` 与 `build.bat --test`。

---

## 6. 当前实施进度与交接备忘（Handover Checklist）

> **更新时间**：2026-09-20 22:15
> **当前状态**：**代码重构核心工程（M1 ~ M4）已全部竣工并全绿通过自动化验证；进程已安全暂停，供后续接力**。

### 6.1 各 Milestone 完成状态
| 里程碑 | 状态 | 交付成果与实测指标 |
|---|:---:|---|
| **M1: 强类型 Config 字段注册表** | **100% 完成** | `src/core/config_registry.h/.cpp`；覆盖全部 91 个持久化字段；支持 3 种布尔 JSON 格式（Literal/QuotedInt/RawInt）；支持 DPAPI 加密策略；`config_store.cpp` 减少约 300 行死代码；新增 15 个独立回归单元测试全 PASS。 |
| **M2: UI FormBuilder 与自动游标** | **100% 完成** | `src/ui/form_builder.h/.cpp`；提供 144 DPI `S()` 标定、AutoId 机制、两向数据绑定与分组框高度自适应。 |
| **M3: ASR 探针服务解耦** | **100% 完成** | `src/core/asr_probe_service.h/.cpp` + `src/app/asr_probe_service_impl.h/.cpp`；在 `main.cpp` 中初始化并注入；UI 层彻底清除 13 个 ASR 底层协议头文件。 |
| **M4: Tab 与 Provider 模块化拆分** | **100% 完成** | `src/ui/tabs/`（5 个 TabPanel 模块）+ `src/ui/providers/`（7 个 Cloud Provider Panel 模块）；`src/ui/settings.cpp` 由 **3438 行暴降至 383 行**；`CMakeLists.txt` 严格显式登记新源文件（零 GLOB 违规）。 |
| **M5: 静态与单测全量回归** | **自动化已全绿** | `build.bat --test` 全量编译成功，所有离线单测（ASR JSON、Qwen 协议、音频诊断、LLM 纠错、HUD 分页等）全部 PASS；17 项架构守卫全部 PASS；96/144/192/288 DPI 静态布局全 PASS。 |

### 6.2 关键架构指标前后对比
- **`src/ui/settings.cpp` 行数**：`3438 行` ➔ **`383 行`**（守卫基线已下调至 400，安全余量充足）。
- **跨层 Include 违规数**：`14 处` ➔ **`1 处`**（全仓仅剩 `audio_diagnostics.cpp -> resource.h`，UI 层的 13 处全部清零）。
- **`CMakeLists.txt` 源文件清单**：显式补充 11 个 tabs 文件与 15 个 providers 文件，无任何零源文件 target，继承 `/utf-8`。

### 6.3 后续接力 AI / 审查人员的验证任务清单
1. **真实桌面 GUI 交互验证**：
   - 启动 `build\run\x64-release\VoxType.exe`。
   - 打开 Settings 窗口，在 100%、150%（或其它 DPI）缩放屏幕上点击各个 Tab（General, Recognition, Cloud ASR, LLM, Prompt），目视确认控件间距与字体渲染与旧版 100% 一致。
   - 在 Cloud ASR 页签切换下拉框（火山、千问、百度等），确认子面板显示/隐藏动画与控件联动正确。
2. **连接测试与持久化落地**：
   - 点击各个 Provider 的 “Test Connection”，确认异步探针正常路由并弹窗/更新状态条。
   - 修改参数后点击 Save，退出并重启程序，确认 `config.json` 保持格式规范且正确加载。
3. **完成 Git Commit**：
   - 确认工作区无悬挂调试文件，执行 git commit 归档此阶段重构成果。
