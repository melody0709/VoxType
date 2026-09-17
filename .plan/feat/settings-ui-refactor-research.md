# VoxType Settings 界面重构可行性研究

状态：研究结论（决策待定，未实施）
日期：2026-09-17
触发问题：现有 Win32 Settings 界面（`src/ui/settings.cpp`，4405 行）改动麻烦、容易出错，希望重构；倾向 Rust + Slint；硬约束是必须匹配项目现有的低内存占用；明确排除 C# / .NET 系大框架。
关联文档：`.plan/feat/auto-update-no-main-window.md`（自动更新方案，与本决策相互独立）

---

## 0. 结论摘要

| 判断 | 结论 |
| --- | --- |
| 排除 C# / .NET / WPF / WinForms | **正确**。但要额外排除一个常被误认为"轻量"的选项：Tauri。 |
| "低内存所以要 Rust" | **推论不成立**。决定内存的是**渲染器**，不是语言。四种 Slint 渲染器的内存差 3.7 倍（14.0 MB ↔ 52.6 MB），语言在这里几乎不起作用。 |
| 重构的正当性 | **成立**。229 个手算布局常量 + 用正则扫源码文本验布局的 `validate_settings_layout.ps1`，本身就是"手算坐标很痛"的客观证据。痛点在**声明式布局缺失**，不在 Win32 这个平台。 |
| 因此真正该换的是什么 | **不是语言，是 UI 描述方式**。把"手算坐标 + 逐控件 ShowWindow 路由"换成"声明式布局 + 数据绑定"。 |
| 本轮的意外发现 | **Slint 有官方 C++ API**（含 CMake 集成）。可以只换界面描述方式，不引入 Rust 作为运行时语言、不引入 IPC 边界。 |
| 建议路线 | **优先「不换语言的布局改造」组合（见 §12）**：布局引擎 + 表驱动声明 + 自绘 chrome。零新运行时、不碰 IME、不动空闲内存，且能解决全部三个痛点。 |
| 次选路线 | Slint C++ API（进程内 + 延迟加载）。增量收益只是"markup 比 C++ 表更舒服"，但代价是要过中文渲染与 IME 两道关。 |
| 开工前必须先做的 | 4 个 spike：中文渲染、**IME 中文输入**、DELAYLOAD 可行性、实测 RSS。第 2 项是唯一可能直接否决 Slint 路线的项。 |

---

## 1. 为什么排除 C# / .NET 是对的

同意，但要把理由说准，并补一个陷阱：

- .NET 系（WinForms / WPF）无论怎么裁剪都带一个托管运行时与 GC；WPF 的窗口栈更重。与"空闲 ~12 MB"的项目基线不匹配。
- .NET **NativeAOT** 能去掉运行时，但 WPF / WinForms 不支持 NativeAOT，所以这条路走不通。
- **陷阱：Tauri**。它经常被当成"轻量 Web 前端"，但它是 WebView2 宿主——你的进程很小，代价是 Windows 会为它 spawn 出 `msedgewebview2.exe` 子树，一个设置窗口实测 50–100 MB 级别。对内存约束而言比 .NET 更差。**不要用。**
- 同理排除 Electron、以及任何内嵌浏览器内核的方案。

所以"要低内存 ⇒ 排除托管/浏览器内核"这条推理链是成立的。它排除的是**运行时形态**。

---

## 2. 实测数据

### 2.1 Slint 四种渲染器（社区实测，1080p / Intel HD 530 / Win10）

| 渲染器 | 运行时内存 | 二进制体积 | C++ API 可用 |
| --- | --- | --- | --- |
| `winit-software` | 14.0 MB | 3.87 MB | 是 |
| `winit-skia`（D3D） | 17.5 MB | 20.3 MB | 是 |
| `winit-skia`（OpenGL） | 52.6 MB | 19.5 MB | 是 |
| `winit-femtovg` | 51.1 MB | 3.45 MB | **否（仅 Rust API）** |

三个必须记住的补充事实：

1. `femtovg` 与 `skia-opengl` **拖动窗口会飙到 200 MB 以上**，缩回去不一定回落。
2. 内存的大头是**显卡驱动按窗口尺寸分配的 back/front buffer**，不是 Slint 本身。Slint 核心运行时官方称 < 300 KiB。
3. 官方另有 `winit-skia-software`（Skia 软件光栅化），可避免常驻双缓冲，是介于 software 与 skia 之间的选项。

来源：Slint GitHub discussion #3376、issue #6072，以及官方 Backends & Renderers 文档。

### 2.2 你自己的 stock_new——现成的 Slint 实测样本

这比社区数据更可信，因为它就是你的代码、你的构建配置：

```toml
slint = { version = "1.17.1", default-features = false,
          features = ["std", "backend-winit", "renderer-software",
                      "software-renderer-systemfonts", "raw-window-handle-06", "compat-1-2"] }
```

```toml
[profile.release]
codegen-units = 1
lto = "thin"
opt-level = "z"
panic = "abort"
strip = "symbols"
```

我实际量了 `build/packages/0.3.7/`：

| 产出 | 大小 |
| --- | --- |
| `StockIpoReminder.exe`（自包含，**零 DLL**） | **15.13 MB** |
| `StockIpoReminder-0.3.7-win-x64.msi` | **6.5 MB** |

对比 VoxType 当前 `build/packages/VoxType-v0.9.27-win-x64-unsigned.msi` = **11.1 MB**。

**结论：用最轻的渲染器路线，一个完整 Slint 应用的发布体积比你现在还小。** 体积这个担忧可以划掉。

### 2.3 内存结论

- 你已经在生产里跑 `renderer-software`（14 MB 档，四种里最低）。
- 这 14 MB 主要发生在**窗口打开期间**。
- 若采用**进程内 + 延迟加载**，空闲态不必为 UI 付钱——与你现在对 `onnxruntime.dll` / `sherpa-onnx-cxx-api.dll` / `kaldi-native-fbank-core.dll` 用 `/DELAYLOAD` 是同一个哲学：不用不付钱。
- 若采用**独立进程**，空闲态为零，任何时候托盘进程都保持 ~12 MB。

两条路都能满足约束。差别在别处（见 §5）。

---

## 3. 关键发现一：软件渲染器默认不支持中文

官方文档在 Software Renderer 的未实现清单里明确写了：

> Text rendering currently limited to western scripts.

同时 C++ 可用性也不同（官方文档逐项标注）：

| 渲染器 | 公开 API |
| --- | --- |
| Software | **Rust 和 C++** |
| FemtoVG | **仅 Rust** |
| Skia | **C++** |

而 stock_new 之所以能用最轻的软件渲染器显示中文，是因为它开了 **`software-renderer-systemfonts`**（走系统字体）。

也就是说：

- 软件渲染器 **+ systemfonts** = 最轻 + 中文可用 ← stock_new 已验证
- 软件渲染器 **不加 systemfonts** = 中文不可用 ← 官方文档明确限制

**待确认**：`software-renderer-systemfonts` 在官方文档里是作为 Rust cargo feature 出现的。C++ 的 CMake 构建是否暴露等价开关（`SLINT_FEATURE_*` 形式），需要实测。如果不能，C++ 路线就只剩 Skia（20.3 MB 二进制 / 17.5 MB RSS）——仍然可接受，但比 software 重一档。

---

## 4. 关键发现二：Slint 有官方 C++ API

官方支持 Rust / C++ / JavaScript / Python 绑定，C++ 侧有官方 CMake 集成：

```cmake
FetchContent_MakeAvailable(Slint)     # 源码内联构建
# 或
find_package(Slint)                   # 预构建的 CMake 包
```

这件事改变了整个选项空间：

- **界面描述方式**换成 `.slint` 声明式标记（这正是你要的解药）
- **业务逻辑仍是 C++**，`g_config` 直连，不需要 IPC，不需要第二个进程
- **不需要把 Rust 作为运行时语言**引入应用
- 构建期需要 Rust 工具链（cargo）来编译 Slint 本身——这是**构建依赖**，不是运行依赖

代价：
- Slint C++ 要求 **C++20**；VoxType 当前是 C++17（`CMakeLists.txt` 第 7–8 行 `set(CMAKE_CXX_STANDARD 17)`）。升到 `/std:c++20` 是机械改动，但要全量重编验证。
- 需要把 Slint 的 DLL 纳入运行载荷（见 §7）。

---

## 5. 修正上一轮的一处夸大

上一轮方案对比图里我写了"125 个控件 ID 必须跨进程边界"，**这是夸大的**。准确情况：

- 配置本身已经是**一个 JSON 文档**（`config.json`），跨边界的是**一个 blob**，不是 125 条消息。
- 125 个控件 ID 只是这个 blob 里的字段名，不需要各自建协议。
- 真正需要独立的接口只有四类：

| 类别 | 内容 |
| --- | --- |
| 快照读写 | `LoadSnapshot()` / `ApplySnapshot(json)` —— 一个 JSON |
| 活状态查询 | Doubao IME 凭据状态、Qwen Free 状态、引擎就绪、provider 列表（`llmProvidersJson`） |
| 动作 | 连接测试、打开诊断目录、删除诊断录音 |
| **热键捕获** | 真难点，见下 |

**热键捕获是独立进程方案里最硬的一块。** 现在 `settings.cpp` 捕获按键时有 `kHotkeyEditClass` 自定义控件；而主进程装着**全局键盘钩子**。AGENTS.md 明确要求"不要在 Settings 打开时继续拦截录音快捷键"。在独立进程里做热键捕获，需要与主进程协调暂停全局钩子，否则两边抢键。这是必须提前设计的部分，不能事后补。

所以：IPC 没有我上一轮说的那么可怕，但它**不是零**，而且热键捕获这块需要真正的设计。

---

## 6. 三个方案对比

| 维度 | 路 A：Slint C++ API，进程内 + DELAYLOAD | 路 B：Rust + Slint 独立进程 | 路 C：留在 Win32，补布局引擎 |
| --- | --- | --- | --- |
| 声明式布局（真正的解药） | ✅ | ✅ | ⚠️ 自研，收益打折 |
| 空闲内存 | ~12 MB（延迟加载，未开 Settings 不付钱） | ~12 MB（永远） | ~12 MB |
| Settings 打开时峰值 | +17.5 MB（Skia）或 +14 MB（software） | 同左，但记在另一个进程 | 不变 |
| 新增语言 | 无（构建期需 cargo） | Rust | 无 |
| IPC 边界 | **无** | 需要（含热键捕获设计） | 无 |
| 托盘进程会不会被 UI 拖崩 | 会（同进程） | 不会 | 会 |
| 新增二进制 / MSI 组件 | 一个 DLL | 第二个 exe | 无 |
| 提交周期 | 中（可逐页迁移） | 长 | 中 |
| 遗留校验脚本 | 可删除 | 可删除 | 保留且需扩展 |

**建议：路 A 优先，路 B 作为 A 的退路。** 理由：你的痛点是"界面改不动"，解药是声明式布局；路 A 拿到同样的解药，却不用付语言分裂 + IPC + 热键捕获协调的代价。而且你已经会写 `.slint`（stock_new），`ui/main.slint` 就是现成范本，`build/packages/0.3.7` 就是现成体积基线。

路 C 只在 §8 的 spike 2（IME）否决前两者时才认真考虑。

---

## 7. 必须同步处理的既有约束

### 7.1 运行载荷与打包

AGENTS.md 规则：新增运行时 DLL 或资源必须更新 `cmake/VoxTypeRuntime.cmake` 的安装清单。该文件目前是一份显式白名单：

```cmake
set(_runtime_files
    "${_src}/dll/sherpa-onnx-cxx-api.dll"
    "${_src}/dll/sherpa-onnx-c-api.dll"
    "${_src}/dll/onnxruntime.dll"
    "${_src}/dll/kaldi-native-fbank-core.dll"
    ...
)
```

Slint DLL 要进这份白名单，并同步 MSI 组件（`packaging/windows/`）、portable 7z 与签名步骤。

另外：**静态链接 Slint 会破坏低内存目标**（DLL 常驻）。要动态链接并 `/DELAYLOAD`，才能做到"不用不付钱"。

### 7.2 许可证

Slint 采用 GPL / 商业双许可（官方另有面向桌面、移动、Web 的免版税许可，具体条款需逐条核对）。而：

- `melody0709/VoxType` **没有 LICENSE 文件**（`gh api` 的 `license` 字段为空）
- `stock_new` 同样没有

也就是说现在两个项目在许可证上都处于**未声明**状态。要正式采用 Slint，这件事必须先定，否则无论选哪条路都是法律悬空。这是决策的前置条件，不是收尾工作。

### 7.3 可以删掉的东西（净收益）

`scripts/validate_settings_layout.ps1` 用**正则解析 `src/ui/settings.cpp` 与 `globals.h` 的源码文本**，在 96 / 144 / 192 / 288 四个 DPI 下检查：

- Diagnostics 组触底、General 组超宽
- Qwen Advanced 敏感词字段与 footer 重叠
- MAI Azure Endpoint 溢出
- 启动项说明文本超宽

这些是真实高 DPI 事故留下的疤。有了声明式布局引擎，**"控件重叠"这一整类 bug 从根上消失**，这个脚本随之失去存在意义——不是"需要移植"，而是**直接删除**。同理，`globals.h` 里那 229 个 `constexpr int` 布局常量会大幅缩减。

这是本方案最实在的收益之一，值得在验收标准里单列。

---

## 8. 开工前的 4 个 Spike

每个都应在 1 小时内给出结论，总投入约半天。**未完成 spike 2 之前不应提交任何重构。**

| # | Spike | 方法 | 失败时的退路 |
| --- | --- | --- | --- |
| 1 | **中文渲染** | Slint C++ + software renderer，尝试启用等价于 `software-renderer-systemfonts` 的开关，渲染含中文的 Label 与 LineEdit placeholder | 退回 Skia 渲染器（20.3 MB 二进制 / 17.5 MB RSS，可接受） |
| 2 | **IME 中文输入** ⚠️ | 在 Slint `LineEdit` 里用真实中文输入法**输入**：候选词窗口是否出现、预编辑是否正常、上屏是否正确、失焦/切页是否丢字 | **这是唯一可能否决整个方案的项。** 失败则走 §6 的路 C |
| 3 | **DELAYLOAD** | 把 Slint DLL 配 `/DELAYLOAD`，确认空闲不加载、首次打开 Settings 才载入、关闭后进程不显著增长 | 改为独立进程（路 B） |
| 4 | **实测 RSS** | 量"空闲 / Settings 打开 / 关闭后"三个数，与当前 ~12 MB 对比 | 若空闲态被污染，改为独立进程 |

**对 spike 2 的特别说明**：stock_new 里 `LineEdit` 用到了中文，但那是 `placeholder-text`（**显示**中文），不能证明中文**输入**可用。VoxType 有 46 个 `EDIT` 输入框，其中 8 个多行，LLM Prompt、Qwen 敏感词表、热词表都要打中文。这一项必须真机实测，不能靠推断。

**对 spike 3 的特别说明**：Slint C++ 的 `.slint` 生成代码与头文件里有大量 inline/模板，DELAYLOAD 是导入表级别的机制，理论上可行但需要实测确认没有急切求值的符号把 DLL 提前拉起来。

---

## 9. 迁移策略（若 spike 通过）

分页渐进，不要一次性重写 4405 行。

1. **先冻结数据契约。** 定义 `SettingsSnapshot`（一个 JSON）：当前 `LoadConfig` 序列化出的全部字段 + 活状态 + 可用动作枚举。这一步在 C++ 侧完成，**与 UI 技术栈无关**，本身就有价值——它把"新增配置项要同步三处"（globals.h + engine.cpp + settings.cpp）收敛成一处。
2. **挑最容易的页做试点**：LLM Prompt 页（纯文本域，无 provider 分支，无活状态）。
3. **再做 General 页**（含启动项、诊断目录动作，需要动作通道）。
4. **Cloud ASR 页放最后**：7 个 provider 子页 + 嵌套条件显示（`ShowCloudSubPage` 的 `providerIdx == 0..5` 分支）+ 活状态刷新，是最复杂的一页。
5. **热键捕获最后做**，且必须先解决全局钩子协调。
6. 迁移期间保留 Win32 窗口作为回退，逐页替换，每页都要跑一次高 DPI 目视检查。

---

## 10. 风险登记

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| Slint `LineEdit` 的 Windows IME 不满足中文输入 | **方案否决级** | spike 2 前置；失败走路 C |
| C++ 无法启用 systemfonts → 必须用 Skia | 体积 +16 MB、内存 +3.5 MB | 可接受，但要在决策时知情 |
| Slint DLL 无法延迟加载 | 空闲内存被 UI 污染 | 改独立进程（路 B） |
| C++17 → C++20 升级引入编译问题 | 全量重编 | 独立提交，先只升标准不动代码 |
| 托盘进程与 UI 同进程，UI 崩溃会带走热键与托盘 | 可用性下降 | 独立进程可规避；若走路 A 需接受 |
| 许可证未定 | 法律悬空 | 决策前置条件 |
| MSI 体积变化影响下载体验 | 用户体验 | 已实测最轻路线体积下降，Skia 路线需重新评估 |

---

## 11. 决策清单（待你拍板）

| # | 问题 | 我的建议 |
| --- | --- | --- |
| D1 | 接受 Slint C++ API 作为首选路线吗？ | 建议接受，先做 4 个 spike 再定 |
| D2 | 项目 LICENSE 定哪个？ | 必须先定；Slint 的 GPL / 商业 / 免版税三种条款要逐条核对 |
| D3 | 若必须用 Skia（体积 +16 MB），可接受吗？ | 建议接受，这是中文渲染的最坏情况成本 |
| D4 | UI 崩溃要不要与托盘隔离？ | 若要隔离则选路 B，需额外设计热键协调 |
| D5 | 重构与自动更新的先后顺序？ | 建议自动更新先做（独立、收益快），UI 重构随后；两者不要混在一个 PR 里 |

---

## 12. 备选路线：不换语言的布局改造

用户提出的核心痛点是三条：**界面不好看**、**加一两行很困难**、**容易出错**。这三条里只有第一条与"平台"有关，后两条与"布局表示方式"有关。本节给出一组不引入新语言的方案，并说明为什么它们可能比换 Rust 更划算。

### 12.1 诊断：你已经走了一半

现有实现比想象的更接近解药。`src/app/globals.h` 里已经有：

```cpp
constexpr int ContentLeft = 42;
constexpr int InputLeft = 188;
constexpr int LabelWidth = 130;
constexpr int RowHeight = 52;
constexpr int FirstRowY = 76;
constexpr int LabelYOffset = 6;

constexpr int RowInputY(int row) { return FirstRowY + row * RowHeight; }
constexpr int RowLabelY(int row) { return FirstRowY + LabelYOffset + row * RowHeight; }

constexpr int GeneralShortcutGroupY  = RowInputY(0);
constexpr int GeneralStartupGroupY   = GeneralShortcutGroupY + GeneralShortcutGroupH + GeneralStartupGroupGap;
constexpr int GeneralDiagnosticsGroupY = GeneralStartupGroupY + GeneralStartupGroupH + GeneralDiagnosticsGroupGap;
```

也就是说：**已经有一个行游标函数，组之间也已经链式推导**。剩下的缺陷只有三条：

| # | 缺陷 | 后果 |
| --- | --- | --- |
| 1 | `RowInputY(n)` 是**索引寻址**，不是顺序堆叠 | 在中间插一行 → 该组内所有后续索引全部 +1（`settings.cpp` 里有 **137 处** `RowInputY`/`RowLabelY` 调用） |
| 2 | 组高是**手写常量**（`GeneralShortcutGroupH = 170`、`GeneralStartupGroupH = 112`、`GeneralDiagnosticsGroupH = 166`） | 组内增删行后必须手工同步组高，漏改就溢出或留白 |
| 3 | **没有运行时布局过程** | 位置在编译期算成常量，控件在运行时由一个巨型函数逐个创建，两者没有共同的真相来源；竖直方向也没有溢出检查 |

第 3 条正是 `scripts/validate_settings_layout.ps1` 存在的根本原因——因为没有可求值的布局，只能退化成**用正则解析源码文本**、在 96/144/192/288 四个硬编码 DPI 上事后验算。

### 12.2 方案菜单

| 方案 | 是什么 | 解决哪条痛点 | 代价 |
| --- | --- | --- | --- |
| **1. 顺序堆叠布局游标**（自研约 300 行） | 把索引寻址换成会推进的游标，组高由内容推导 | 2、3 | 无（C++17 即可，零依赖） |
| **2. Yoga 布局引擎** | Facebook 的 flexbox 布局引擎，只算矩形 | 2、3 | MIT，零依赖，约 600 KB，C++20 |
| **3. 表驱动控件声明** | 每页一张静态表，通用代码据此创建控件与布局 | 2、3 | 一次性重构 197 处创建调用 |
| **4. 自绘非文本 chrome** | 用已有 D2D/DWrite 画分组卡片、按钮、Tab 头 | 1 | 保留原生 EDIT 以保住 IME |
| **5. Slint C++ API** | 声明式 `.slint` + C++ 逻辑，进程内 | 1、2、3 | 需过中文渲染与 IME 两关（§8） |
| **6. Qt / wxWidgets / Sciter** | 完整 UI 框架 | 1、2 | **不推荐**：DLL 体积、LGPL 义务、事件循环接管或商用授权，均与 12 MB 空闲基线冲突 |

### 12.3 为什么 Yoga 特别合适

Yoga 的定位恰好卡在正确的位置上：**它只做几何计算，不渲染、不建事件循环、不接管控件。**

- 许可证 **MIT**，**零依赖**，发行包体积约 **600 KB**（含头文件约 600 KB 级，静态库约 125 KB 量级）
- 纯 C++ 实现、**C API**（`YGNodeNew` / `YGNodeInsertChild` / `YGNodeCalculateLayout`），C++ 侧直接 `#include <yoga/Yoga.h>`
- 支持 **增量布局**：标脏的子树连同其祖先重算，不动无关分支
- 生产验证：**Qt 的 QtQuick.Layouts 内嵌的就是 Yoga**（`qtdeclarative/src/3rdparty/yoga`）
- 在 vcpkg 里，CMake 集成成熟，与项目"CMake 是唯一编译权威"的约定天然契合

用法是最直白的形态：描述一棵节点树 → `YGNodeCalculateLayout()` → 读每个节点的 frame → 对所对应的 HWND 调 `MoveWindow`。

**关键优势在于它不改动任何现有行为。** 46 个原生 `EDIT` 及其免费获得的完整 IME 行为、所有现有消息处理与命令分支、Direct2D/DirectWrite 的渲染路径，全部不动。改的只有"矩形从哪里来"。

注意：Yoga 3.x 要求 C++20（VoxType 现为 C++17，见 §4）。若不想升标准，可用 Yoga 2.0.1，或走方案 1 自研（300 行、C++17 即可）。C++20 升级本身也是 Slint 路线的前提，可作为独立提交先落地。

### 12.4 "加一行很容易"的可测试定义

这是判断重构是否成功的**首要验收标准**，建议直接写进 AGENTS.md：

> 在 General 页第二个分组的中间插入一行新的配置项（标签 + 输入框 + 提示文本），改动范围应当仅限于：
> **1 处表项声明 + 1 处 Load/Save 字段 + 1 处配置结构体字段**，
> 且 **0 处坐标常量改动、0 处行索引调整、0 处组高调整、0 处布局校验脚本改动**。

今天在同一位置插一行，需要动：组内后续所有 `RowInputY` 索引、该组的 `*GroupH` 常量、可能还要调 `validate_settings_layout.ps1` 里对应的断言。这个对比就是重构收益的量化口径。

### 12.5 布局校验脚本的升级方向

无论选方案 1/2/5，`validate_settings_layout.ps1` 都应该改变形态，而不是被移植或保留：

- **现状**：正则解析 `settings.cpp` 与 `globals.h` 的**源码文本**，在 4 个硬编码 DPI 下复算数值断言。
- **目标**：布局已成为可求值的东西，校验脚本应**调用布局引擎求值布局树**，在任意 DPI 与任意窗口尺寸下拿到真实矩形，然后检测重叠与溢出。

这不只是"换个实现"，而是从"用文本正则猜几何"升级为"对真实几何做断言"——可以从脆弱的源码契约升级成稳固的几何契约。当前脚本里那些具名断言（Diagnostics 组触底、Qwen Advanced 敏感词字段与 footer 重叠、MAI Azure Endpoint 溢出）可以原样保留为**场景用例**，但断言对象从常量变成算出来的矩形。

### 12.6 推荐组合与顺序

```
第 1 步  布局引擎（方案 1 或 2）           ← 解决"加行难"
第 2 步  表驱动声明（方案 3）               ← 让"加一行"= 加一行表项
第 3 步  自绘 chrome（方案 4）              ← 解决"不好看"
第 4 步  校验脚本改为求值真实布局            ← 解决"容易出错"
第 5 步  （可选）Slint C++ 替换第 1 步      ← 只有想要 markup 时才做
```

第 1–4 步全部落在现有 C++/Win32 栈内：不引入运行时、不引入新语言、不动 IME、不增加空闲内存、不进 MSI 载荷。而第 5 步是可选的"锦上添花"——它的增量收益只是 markup 比 C++ 表描述更舒服，但代价是要先通过 §8 的中文渲染与 IME 两个 spike，并先解决 §7.2 的许可证问题。

**先做第 1–4 步，把第 5 步当成后续可选项。**

---



## 13. C++17 → C++20 升级可行性评估

### 13.1 结论（已实测验证，非静态推断）

**全部 36 个源文件在 `/std:c++20` 下编译通过，零失败、零警告。**

验证方式：绕过 `vcvars64.bat`（原因见 §15），直接用 `cl.exe` 显式指定 MSVC 与 Windows SDK 的 include 路径，对 `src/*/*.cpp` 全部 36 个文件以与正式构建一致的选项（`/O2 /EHsc /MT /utf-8 /DUNICODE`）逐个编译：

```
=== C++20 编译：通过 36 / 失败 0 ===
```

对照测试：`src/audio/engine.cpp` 在 `/std:c++17` 与 `/std:c++20` 下均为 **exit=0，零错误零警告**。

因此 C++20 升级**不是"风险很低"，而是已经跑通**。剩余工作只剩链接、测试与实机录音回归（这三步因沙箱限制未能执行，见 §15）。

### 13.2 逐项验证结果（本次实际扫描）

| 检查项 | 为什么危险 | 扫描结果 |
| --- | --- | --- |
| `u8"..."` 字面量 | C++20 起产生 `char8_t[]` 而非 `char[]`，赋给 `std::string` / `const char*` 全部报错。**实践中最常见的破坏点** | `src/` 与 `third_party/` **0 处** ✓；但 **`tests/` 有 2 处**（`llm_refine_test.cpp:111`、`qwen_free_protocol_test.cpp:299`）。上一轮只扫了 `src/`，此处为更正，见 §13.7 |
| 自定义 `operator==` / `operator!=` | C++20 引入反向候选与 `!=` 合成，成对定义可能产生**歧义**并静默改变重载决议。**最隐蔽的行为变化类** | **0 / 0 处** ✓ |
| 已移除设施（`result_of`、`unary_function`、`binary_function`、`auto_ptr`、`random_shuffle`、`std::rel_ops`、`allocator<void>`） | C++20 正式移除 | **0 处** ✓ |
| 动态异常规格 `throw()` | C++20 已从语言移除 | 仅 `third_party/onnxruntime/include/onnxruntime_c_api.h:137-141` 的 `NO_EXCEPTION` 宏，被 `(_MSC_VER >= 1900)` 分支挡住、实际展开为 `noexcept`，且该宏**全项目从未被使用** ✓ |
| `using namespace std` | C++20 注入的新名（`span`/`format`/`ranges` 等）可能与本地符号冲突 | 仅 `src/asr/doubao_ime_asr.cpp:227` 函数内 `using namespace std::chrono;`，只用到 `duration_cast`/`milliseconds`/`system_clock` ✓ |
| 本地同名函数 `contains` / `starts_with` / `ends_with` / `span` / `format` | 与 C++20 新增标准名冲突 | **0 处** ✓ |
| `codecvt` / `wstring_convert` / `std::bind` / `mem_fun` / `not1` | 已弃用或行为变化 | **0 处** ✓ |
| `std::filesystem` | 仅确认依赖情况 | 未使用 ✓ |
| `volatile` 与 `std::atomic` 混用 | C++20 弃用 `volatile` 限定的原子操作 | **0 处**（AGENTS.md 记录了历史上的 `volatile bool` 已修为 `std::atomic<bool>`）✓ |
| 代码依赖 `__cplusplus` 宏 | MSVC 默认报 `199711L`，需 `/Zc:__cplusplus` | 项目代码 **0 处**依赖 → 该开关属可选加固 ✓ |
| 第三方头是否有标准下限硬断言 | 可能直接 `#error` 阻断 | `onnxruntime_cxx_api.h`（3534 行）、`sherpa-onnx/c-api/cxx-api.h`（1745 行）、`kaldi-native-fbank/csrc/online-feature.h`（155 行）**均无** `__cplusplus` / `#error` 断言 ✓ |
| 工具链支持 | `/std:c++20` 需足够新的 MSVC | `build.bat:80` 使用 **Visual Studio 2022 Community**（MSVC 19.3x），`/std:c++20` 完整可用 ✓ |

### 13.3 "后面的算法会怎么样"

**算法行为完全不变。** C++20 是**编译期契约**的变化，不是运行时语义的变化：

- 无浮点语义改动 → WASAPI 重采样相位计算、VAD 的 int16/f32 量纲、fbank 特征提取，数值逐位相同
- 无求值顺序改动 → 现有表达式的副作用顺序不变
- **MSVC 的 ABI 不随 `/std:` 变化** → 混编 `/std:c++17` 与 `/std:c++20` 的 TU 到同一二进制是安全的

最后一条是关键逃生舱：**若某个第三方 TU 在 C++20 下出问题，可只把那个源文件留在 C++17**，无需回滚全局：

```cmake
set_source_files_properties(src/audio/firered_vad.cpp
    PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
```

因此这个升级**不是全有或全无的赌注**。

### 13.4 真实工作量在哪

1. **全量重编** —— 第一次会明显变慢；98 个源文件 + 3534 行的 ORT 头，编译时间会上升。
2. **新警告清理** —— MSVC 在 C++20 下开启一批新警告，应逐条判断而不是一律 `/wd` 关掉。
3. **第三方头警告噪声隔离** —— 建议同时引入 MSVC 的第三方头隔离机制，这是处理 ORT / sherpa 头的正道，项目目前似乎未使用：
   ```cmake
   target_include_directories(VoxType SYSTEM PRIVATE third_party/onnxruntime/include)
   # 或
   target_compile_options(VoxType PRIVATE /external:W0 /external:anglebrackets)
   ```
4. **ASR 链路回归** —— AGENTS.md 中 ASR / 音频相关的踩坑规则密度最高（WASAPI 生命周期、CapsLock 热键语义、微信粘贴、火山 WebSocket 状态、VAD 量纲），说明这条链的回归成本最高，必须实机跑一遍录音闭环。

### 13.5 顺带能拿到的收益

不是"为了 C++20 而 C++20"，以下几条对现有代码有直接价值：

| 特性 | 对现有代码的价值 |
| --- | --- |
| `std::wstring::starts_with` / `ends_with` / `contains` | 现在手写了 `Contains(lower, L"winhttp")` 这类辅助函数（见 `src/asr/asr_result.cpp`），可直接替换 |
| 指定初始化器（designated initializers） | `Config` 结构体与 `StageMetadata` / `StageTerminal` 这类聚合初始化会明显更好读 |
| `std::span` | 音频缓冲传参（`PcmToFloat`、VAD 切片路径）可去掉裸指针 + 长度对 |
| `<=>` 三路比较 | 简化版本号比较与未来任何排序键 |
| concepts | 未来模板代码的可读性，也便于约束 `BatchAsrSessionBase` 这类抽象 |

**按需引入，不要一次上满：** `<format>` 与 `<ranges>` 会显著增加编译时间，在 98 个源文件的规模下代价可见，等真正需要时再引入。

### 13.6 建议的迁移方式

```
第 1 步  独立提交：只把 CMAKE_CXX_STANDARD 17 → 20，不动任何代码
        同时加 /Zc:__cplusplus 与第三方头 /external 隔离
第 2 步  全量重编 + build.bat --test + 实机录音闭环回归
第 3 步  逐条处理新警告（不要批量 /wd）
第 4 步  在后续的布局重构中顺带用上 C++20 特性，而不是专门做一次"现代化"重构
```

### 13.7 全量实证矩阵（含 tests / tools，含 C++17 基线对照）

对 `tests/` 与 `tools/` 逐个做了 **C++17 / C++20 双标准对照**，用来区分"本来就坏"与"C++20 新引入"：

| 文件 | C++17 错误数 | C++20 错误数 | 判定 |
| --- | --- | --- | --- |
| `tests/llm_refine_test.cpp` | 0 | **1** | **C++20 新引入** |
| `tests/qwen_free_protocol_test.cpp` | 0 | **1** | **C++20 新引入** |
| `tests/qwen_audio_json_test.cpp` | 20 | 20 | **本来就坏**（与标准无关） |
| `tools/asr_audio_replay.cpp` | 1 | 1 | **本来就坏** |
| `tools/doubao_ime_probe.cpp` | 7 | 7 | **本来就坏** |

**结论：C++20 只在 `tests/` 引入 2 处错误，全部是同一个原因，且修复成本极小。**

```cpp
// tests/llm_refine_test.cpp:111
Expect(llm::WideToUtf8(llm::ParseResponse(escapedResponse)) == u8"你好\nA\"B\\C/D 😀", ...);
//                                                             ^^^ C++20 起为 const char8_t[]

// tests/qwen_free_protocol_test.cpp:299
std::string(u8"你好\n世界 😀"),
//           ^^^ 无法从 const char8_t[19] 构造 std::string
```

**修复方式：去掉 `u8` 前缀即可，行为完全等价。** 项目已启用 `/utf-8`，MSVC 在该开关下把窄字符串字面量按 UTF-8 编码，因此 `"你好"` 与 `u8"你好"` 的字节序列相同。这是两行改动。

**同时暴露的一个既存问题**：`tests/qwen_audio_json_test.cpp`（20 个错误，引用了已不存在的 `qwen_asr::BuildEndpointUrlForTest` / `BuildSessionUpdateMessageForTest`）、`tools/asr_audio_replay.cpp`、`tools/doubao_ime_probe.cpp` **在当前 C++17 下就已经编译不过**。它们不属于本次升级的范畴，但说明 `build.bat --test` 的测试面存在一些已经失效的目标，值得单独开一个任务清理——否则"跑一遍测试"这件事的说服力会被稀释。

**关于 `tools/`**：CMakeLists 中 `asr_audio_replay` 标为 `EXCLUDE_FROM_ALL`，默认不构建，实际影响有限。

**顺序建议：C++20 升级排在布局重构之前，作为独立的低风险前置提交。** 理由：它是 Yoga（flexbox 布局引擎，要求 C++20）与 Slint C++ API 的共同前提。先把它作为独立变更落地并验证，就不会出现"为了引入布局引擎而临时决定升标准"这种把两件事混在一起的改动——而混在一起的改动正是当前 UI 代码踩坑的成因。

---

## 14. 要不要追 C++23 / C++26

### 14.1 结论：停在 C++20，不要追新

用户提出"看到已经出到 C++23 还有 C++26，为了更好地应对以后迭代是不是该升级"。**建议不要。** 理由不是保守，而是三条硬事实。

### 14.2 事实一：即使装了最新工具链，`/std:c++23` 的稳定开关仍未就绪

**当前机器**（实测）：只装了一个 MSVC 工具集 `14.44.35207`（VS 2022 17.14.x），四个开关实测如下：

| 开关 | 当前机器结果 |
| --- | --- |
| `/std:c++17` | 接受，编译通过 |
| `/std:c++20` | 接受，编译通过（36/36） |
| `/std:c++23` | **`D9002: ignoring unknown option` —— 不被识别** |
| `/std:c++26` | **`D9002: ignoring unknown option` —— 不被识别** |
| `/std:c++latest` | 接受 |

**但即使假定已装最新的 VS 2026 / MSVC 14.5x，结论也只是稍微松动，不发生改变。** 依据 Microsoft C++ Team Blog（MSVC Build Tools 14.51 的 C++23 支持说明）：

- VS 2026（18.x）搭载 MSVC Build Tools 14.50 / 14.51，`cl.exe` 版本 ≥ 14.51.36231
- 官方原文：C++23 仍有两项特性未完成（**P2564R3** consteval 需向上传播、**P0533R9** constexpr）；**"`/std:c++23` 开关将在后续的 MSVC Build Tools 14.52 Preview 中加入"**，届时 `/std:c++23preview` 才会被废弃；完整支持要等 14.52 成为 VS 2026 Insiders 的默认版本
- 也就是说：**当前要用到 C++23，只能选 `/std:c++23preview`（预览开关）或 `/std:c++latest`（实验模式）**，两者都不适合用于要分发 MSI 的产物
- C++26：官方说明 STL 侧"尚未普遍接受 C++26 库特性的贡献，仅少量论文无条件实现"，无稳定开关，支持必然不完整

**结论：即便工具链最新，"升到 C++23"也不是一个干净可发布的选项。** 稳妥做法仍是 **`/std:c++20`**；等 14.52 稳定后，从 C++20 升到 C++23 几乎零成本（两个标准间几乎没有破坏性变更），届时再动即可。

补充：`/std:c++latest` 明确不是答案——它是 MSVC 的实验模式，行为随版本变化，对需要长期维护的分发产物是持续的不稳定源。

### 14.3 事实二：C++23 给你的东西，与你的痛点无关

C++23 的主要增量是 `std::expected`、`std::print`、`std::flat_map`、`if consteval`、deducing `this`、ranges 扩展等。逐条对照你的三个真实痛点：

| 痛点 | C++23 有帮助吗 |
| --- | --- |
| 加一行布局很困难 | 无 |
| 加一个配置项要动十处 | 无（`std::expected` 可以改善错误路径，但不消除样板） |
| 界面不好看 | 无 |

而 **C++20 已经解锁了真正有用的东西**：Yoga（flexbox 布局引擎）与 Slint C++ API 都要求 C++20。C++23 不解锁任何你现在需要的组件。

### 14.4 事实三：标准版本不是架构

这一点值得单独强调，因为它是对"长期迭代"这个目标最直接的回应：

> **决定未来迭代成本的是「信息组织方式」，不是「语言标准版本」。**

本项目 104 个配置字段、95 次 `ExtractJson*`、94 行序列化输出、10 处/字段的样板（见 §16）——这些成本在 C++17、C++20、C++23 下**完全一样**。把标准从 20 升到 23 不会让"加一个配置项"从 10 处变成 1 处；但一个字段注册表可以。

同理，布局从"手算常量"变成"从树算几何"也不需要新标准——自研游标在 C++17 下就能做，Yoga 要 C++20。

### 14.5 关键区分：升「标准」与升「工具链」是两件不同的事

用户问"工具链是不是太旧了也需要更新吗"。这两件事必须分开看，因为**风险来源完全不同**。

| | 升标准（C++17 → C++20） | 升工具链（MSVC 14.44 → 14.5x / VS 2026） |
| --- | --- | --- |
| 本质 | 编译期契约变化 | **新的代码生成器 + 新的 STL 实现** |
| 已验证程度 | **已实测：36/36 源文件通过，零错误零警告** | 未验证（本机只有 14.44） |
| 回归重点 | 编译能否通过 | **ASR/音频数值、性能、二进制体积** |
| 风险 | 低 | 中等，需要实机回归 |

**升工具链之所以风险更高**，是因为 MSVC 14.5x 带来的是行为层面的改动：

- 后端优化器大幅改动：SROA 更激进、CSE 改进、内联扩展、**新增 restrict 指针语义**、循环与谓词优化
- STL "now optimized for **speed** instead of **size**" —— 二进制体积可能变大（对你的 11 MB MSI 是可见指标）
- `<regex>` 大幅重写、修复了长期存在的栈溢出问题
- 移除了 `TR1`、`std::tr1`、`hash_map`、`hash_set`、`stdext::checked_array_iterator` / `unchecked_array_iterator`
- **MSVC STL 不再支持 Windows 7 / 8 / 8.1**，最低为 Windows 10 / Server 2016
- 二进制兼容性：官方说明 14.51 与 VS 2015 起的工具链保持二进制兼容（这一条是好的）

**针对本项目的前置检查（已做，全部不受影响）**：

| 检查项 | 结果 |
| --- | --- |
| `std::tr1` / `<hash_map>` / `<hash_set>` / `stdext::*`（14.5x 已移除） | **0 处**（src 与 third_party 均无）✓ |
| `std::regex` / `std::wregex`（14.5x 行为大幅变化） | **0 处** ✓ |
| 目标系统最低版本 | 清单声明 `minimumWindowsBuild = 19041`，高于 MSVC STL 的 Win10 下限 ✓ |

**建议**：工具链可以升（VS 2026 支持与 VS 2022 并存安装，试错成本低，且能拿到更好的诊断与代码生成），但**它是独立的一步，不要和标准升级混在同一个提交里**。升完之后必须做一次完整的实机录音回归 + 对比 MSI 体积与 ASR 数值。

**因此：工具链该更新就更新（为了编译器修复与更好的代码生成），但不要为了 C++23 而更新，也不要把"升到最新标准"当成架构投资。**

### 14.6 前瞻验证：第三方头文件能不能扛住更新标准

即使现在不用 C++23，也可以提前验证"将来升到 C++23 时第三方头会不会炸"。办法是用 `/std:c++latest`（C++23/26 特性的预览开关）编译全部源文件——第三方头文件（onnxruntime 3534 行、sherpa 1745 行、kaldi、opus）是同一个风险面。

```
=== /std:c++20     ：通过 36 / 失败 0 ===
=== /std:c++latest ：通过 36 / 失败 0 ===
```

**两个标准模式都是 36/36 全通过。** 这说明：

- ORT / sherpa-onnx / kaldi-native-fbank / opus 的头文件在最新的标准模式下没有问题
- 将来装上 VS 2026、把标准切到 C++23 时，**主产品代码（`src/` 的 36 个文件）大概率是零改动的**
- 唯一需要提前处理的仍是 `tests/` 里那 2 处 `u8` 字面量（§13.7），那个问题在 C++20 就已经出现，与 C++23 无关

这条也进一步支持 §14.1 的判断：**既然将来升 C++23 几乎零成本，现在就没必要急着升。**

---

## 15. 构建"报错"的真因：不是项目构建方法的问题

### 15.1 现象

在沙箱中执行 `build.bat --test` 时所有目标立刻失败：

```
fatal error C1083: Cannot open include file: 'windows.h': No such file or directory
```

### 15.2 真因

`build.bat` 本身**不使用** `reg.exe`。它的调用链是：

```
build.bat
  └─ %VS_PATH%\VC\Auxiliary\Build\vcvars64.bat     ← VS 提供
       └─ 内部调用 reg.exe 查询 WindowsSdkDir 等注册表值
            └─ 沙箱安全策略将 reg.exe 列入黑名单 → 被拦截
                 └─ SDK include 路径未设置 → windows.h 找不到
```

沙箱输出明确写了这一点：

```
PROGRAM BLOCKED BY SECURITY POLICY - The sandbox prevented a program on the
configured Program Blacklist from starting:
  - reg.exe (C:\windows\system32\reg.exe)
```

### 15.3 三条证据说明项目构建方法是好的

1. **`build/packages/` 下有十几个已发布的版本与 11.1 MB 的 MSI** —— 你在自己的环境里一直正常出包。
2. **绕过 vcvars 后一切正常** —— 直接用 `cl.exe` 显式指定 MSVC 与 Windows SDK 的 include 路径，36 个源文件在 C++20 下全部通过，零错误零警告。代码与构建配置本身没有任何问题。
3. **这个现象你自己的 skill 里已经记录过** —— `voxmic-release` 技能中写着「`PROGRAM BLOCKED BY SECURITY POLICY ... reg.exe` —— 沙箱黑名单拦了 reg.exe，非致命」。同一个环境限制的另一种表现，只不过对打包是非致命的，对编译是致命的。

### 15.4 结论

**不要去"修"`build.bat`。** 它没有 bug。出错的是我所在执行环境的沙箱策略，与项目、构建脚本、工具链都无关。

需要完整构建（链接 + 测试 + 实机回归）时，在正常环境里跑 `build.bat --test` 即可。本文档中凡依赖编译的验证，都改用"直接调 `cl.exe` 并显式指定 include 路径"的方式绕开该限制。

---

## 16. 长期迭代的真正摩擦点：配置字段样板

这一节是对"长期迭代、先弄好架构"这个目标的正面回应。它比标准版本重要得多。

### 16.1 实测规模

| 指标 | 数量 |
| --- | --- |
| `Config` 结构体字段 | **104** |
| `LoadConfig` 中的 `ExtractJson*` 调用 | **95** |
| `SaveConfig` 中的序列化输出行 | **94** |
| `LoadSettingsControls` 区间 | 411 行 |
| `SaveSettingsControls` 区间 | 353 行 |

### 16.2 加一个配置项，实际要动 10 处

以 `volcEnableContext` 为例，完整追踪：

| # | 位置 | 作用 |
| --- | --- | --- |
| 1 | `src/app/globals.h:400` | 结构体字段声明 |
| 2 | `src/app/globals.h` | 控件 ID（`IDC_VOLC_ENABLE_CONTEXT`）+ 可能的布局常量 |
| 3 | `src/audio/engine.cpp:580` | `LoadConfig`：`ExtractJsonBool(json, "volc_enable_context", false)` |
| 4 | `src/audio/engine.cpp:819` | `SaveConfig`：`<< "\"volc_enable_context\": " << ...` |
| 5 | `src/ui/settings.cpp:1692` | `LoadSettingsControls`：写回控件状态 |
| 6 | `src/ui/settings.cpp:2041` | `SaveSettingsControls`：读取控件状态 |
| 7 | `src/ui/settings.cpp:3493` | 创建控件 |
| 8 | `src/ui/settings.cpp:3495` | `ApplyUiFont` |
| 9 | `src/ui/settings.cpp:3496` | `AddVolcengineControl`（可见性分组） |
| 10 | 使用点（`main.cpp:265`、`volcengine_streaming_session.cpp:454` 等） | 业务逻辑 |

其中 **1–9 是纯样板**，只有第 10 项才是真正的新代码。

注意 3/4 与 5/6 两对是完全对称的机械重复：一个字段的"持久化"和"UI 同步"被手写两遍，104 个字段就是约 400 行纯样板。这正是 AGENTS.md 里"新增配置项同步三处"那条规则的由来，而实际是 **9 处**。

### 16.3 架构上的答案：字段注册表

一个字段注册表（member pointer + JSON key + 类型 + 默认值 + 控件 ID + 控件种类 + 标签 + 所属页/组）可以把第 1–9 项收敛成 **1 行表项**：

```cpp
{ &Config::volcEnableContext, "volc_enable_context", kBool, false,
  IDC_VOLC_ENABLE_CONTEXT, kCheckBox, L"Use history as context", kPageCloudAsr, kGroupVolcengine },
```

由此 `LoadConfig` / `SaveConfig` / `LoadSettingsControls` / `SaveSettingsControls` / 控件创建 / 可见性分组全部变成对同一张表的遍历。104 个字段从约 500 行手写样板变成 104 行声明。

**配置文件格式不变**（仍是 `config.json`，键名不变），因此**向后兼容，不影响已有用户的配置**。

### 16.4 与布局重构是同一个动作

字段注册表与 §12 的表驱动布局声明天然是**同一张表**——因为一行既有"持久化信息"也有"UI 与布局信息"。合并之后：

> **加一个配置项 = 表里加一行；插一行 UI = 什么都不用做（布局引擎自动排）。**

这才是"长期迭代"的架构答案。它的收益远大于任何语言标准升级。

### 16.5 推荐实施顺序（面向长期迭代）

```
第 0 步  C++20 升级（独立提交）  ← 已实测 36/36 通过
第 0b 步 工具链升级（独立提交）   ← 可选，与第 0 步分开；需实机回归 + 对比 MSI 体积
第 1 步  字段注册表              ← 消除 ~500 行样板，加配置项从 10 处 → 1 处
第 2 步  布局引擎（Yoga 或自研） ← 插行不再需要重排索引与组高
第 3 步  自绘 chrome             ← 解决"不好看"
第 4 步  校验脚本改为求值真实布局
第 5 步  （可选）Slint C++       ← 需先过中文渲染与 IME 两个 spike
```

第 1 步与第 2 步可以合并成一次重构（同一张表驱动两者），但建议**先做第 1 步**，因为它的收益更直接、风险更低，且不依赖 C++20 之外的任何新东西。

注意第 0 步与第 0b 步必须分开：前者只改编译期契约（已验证），后者会换掉代码生成器与 STL 实现（需实机回归）。混在一起就无法定位回归来自哪一边。

---



| 证据 | 位置 / 来源 |
| --- | --- |
| stock_new 的 Slint 渲染器与体积优化配置 | `stock_new/Cargo.toml:24,65-70` |
| stock_new 实测量：exe 15.13 MB / MSI 6.5 MB | `stock_new/build/packages/0.3.7/`（本次实测） |
| VoxType 当前 MSI 11.1 MB | `VoxType/build/packages/VoxType-v0.9.27-win-x64-unsigned.msi` |
| Slint 渲染器内存/体积实测 | Slint GitHub discussion #3376、issue #6072 |
| 软件渲染器限制与各渲染器公开 API | `https://docs.slint.dev/latest/docs/slint/guide/backends-and-renderers/backends_and_renderers/` |
| C++ CMake 集成方式 | `https://slint.dev/docs/cpp/`、Slint PR #3821 |
| VoxType 当前 C++ 标准 | `CMakeLists.txt:7-8` |
| VoxType 无 LICENSE 文件 | `gh api repos/melody0709/VoxType --jq .license` 返回空 |
| 运行载荷白名单 | `cmake/VoxTypeRuntime.cmake` |
| 现有布局校验脚本 | `scripts/validate_settings_layout.ps1` |
| Settings 界面规模 | `src/ui/settings.cpp`（125 个 IDC、197 次控件创建、53 个命令分支、46 个 EDIT） |
| 布局常量规模 | `src/app/globals.h`（229 个 `constexpr int`；`RowInputY`/`RowLabelY` 在 `settings.cpp` 被引用 137 次） |
| 组高常量为手写 | `src/app/globals.h:125-133`（`GeneralShortcutGroupH` / `GeneralStartupGroupH` / `GeneralDiagnosticsGroupH`） |
| Yoga 许可证与体积 | Meta `facebook/yoga`，MIT，零依赖；发行包约 600 KB 级；`qtdeclarative/src/3rdparty/yoga` 内嵌于 Qt Quick Layouts |
| Yoga 用法与 C++20 要求 | `https://www.yogalayout.dev/`、`yoga/YGNode.cpp`（`YGNodeCalculateLayout` 等 C API） |
