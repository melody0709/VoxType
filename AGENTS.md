# AGENTS.md
详细架构见 `ARCHITECTURE.md`，版本历史见 `CHANGELOG.md`。

## 项目概要

Windows 11  语音输入法工具：托盘常驻，按住快捷键录音松开识别，本地 ASR（sherpa-onnx），可选云端 ASR（百度/火山引擎/Qwen）和 LLM 纠错。

# 参考项目
- C:\Program Files\QianwenIME 只读 需要逆向。
- /reverse-skill 涉及到取证，请采用相关的技能。
- 获取到相关证据，必须更新 /reverse 目录下的相关文档以及证据，以便后面研究。


## Command execution

`CMakeLists.txt` 与 `CMakePresets.json` 是唯一编译权威；`build.bat` 负责准备 MSVC、调用 CMake/Ninja 并安装运行载荷。

唯一可直接运行的开发程序是 `build\run\x64-release\VoxType.exe`，不能运行 `build\cmake\x64-release\VoxType.exe` 或手工向运行目录复制文件。

## 架构重构（2026-09 起，进行中）

- **权威方案**：`.plan\refactor\cxx23-architecture-refactor-plan.md`（含分层契约、CMake 目标骨架、分阶段 Milestone 与退出判据）。
- **机械守卫**：`tools\check_architecture.ps1`（当前 **17 项检查**，含 5 项防绕过）。`build.bat` 已在主流程内置调用，**每次构建都会执行**；任何导致「globals.h 包含者 / extern 数 / 跨层越权 include / main.cpp 行数 / settings.cpp 行数」反弹，或产生零源文件 target、缺 `/utf-8` 的 target、越界产物目录的改动，一律视为构建失败。
- **禁止绕过守卫**：不得删测试、不得注释掉 `build.bat` 里的守卫调用、不得用 `file(GLOB)`、不得提高守卫基线（相对上一提交上调即 FAIL，需人工确认）、不得在 `src/` 下新建未在分层矩阵中声明的目录。
- **P6 语法收敛的排除清单**（不得以"统一风格"为名去动）：`src/asr/volcengine_asr.h` 协议层、`src/asr/qwen_free_proto_*` 系列、`src/asr/doubao_ime_asr.cpp` 的 protobuf/Opus 部分。这些受"踩坑规则【B】"保护——**收敛前必须先有回归测试**。
- **阶段判据**：各阶段 P1~P6 均已完成并通过。当前最新基线：`0 / 0 / 0 / 142 / 3448 / 14`（`globals.h` 彻底消除，`main.cpp` 降至 142 行，`settings.cpp` 降至 3448 行）。
- **契约同步是每个阶段的 DoD**：涉及类名、路径、配置项搬迁时，必须同步更新 `ARCHITECTURE.md`、`AGENTS.md`（本文）与守卫脚本里的基线数值。
- **开工前先做备份**：`.bak\`（仓库根，已在 `.gitignore` 中）。禁止把人工备份放进 `build\`。

## `build/` 生成目录边界

- `build/` 完全是可删除、gitignored 的生成输出；禁止存放源码、手工脚本、截图、输入数据、用户数据或人工备份。
- 顶层白名单仅为 `cmake/`、`run/`、`artifacts/`、`logs/`、`packages/` 和 `README.txt`。新增顶层项必须同步更新 `build.bat`、布局校验脚本和本文档。
- `build/cmake/x64-release/` 只放 CMake/Ninja 编译中间产物；`build/run/x64-release/` 是唯一规范运行载荷；发布包只进入 `build/packages/`。
- 测试、诊断和显式日志分别放入 `build/artifacts/`、`build/artifacts/diagnostics/` 和 `build/logs/`。
- `build.bat --clean` 清理 `cmake`、`run`、`artifacts`、`logs` 和布局说明，但保留已验证的 `packages/`。未知项必须由布局校验报错，不能自动删除或打包。

## 开发约定

- 编辑文件优先用精确替换（SearchReplace / apply_patch），避免全文覆写。
- **项目强制使用 C++23 标准**：`CMakeLists.txt` 统一配置 `CMAKE_CXX_STANDARD 23`。后续所有新功能开发与重构必须遵循 C++23 规范，优先采用 `std::expected<T, E>` 错误处理、`std::span` 零拷贝音频切片、`std::format` 格式化、`std::jthread` 协作式线程管理。详细指引见 `.agents/skills/modern-cpp23-guidance/SKILL.md`（**注意：`.agents/` 目前是 untracked，需先提交，否则该指引随工作区丢失**）。
- **`std::print` / `std::println` 仅限离线测试工具与构建期脚本**。`VoxType` 是 `add_executable(... WIN32)` 子系统程序，**没有控制台**，stdout 无处可见；应用内日志继续走文件日志与 `DebugModeOpenConsole()` 调试通道。
- **C++20/C++23 字符串字面量规范**：因标准中 `char8_t` 为独立类型，在 MSVC `/utf-8` 编译选项下，与 `std::string` 交互直接使用常规字符串字面量 `"..."`，避免使用 `u8"..."` 导致无法隐式转换。
- C++ 新增依赖同步更新：`#pragma comment(lib)` + `CMakeLists.txt`；若新增运行时 DLL 或资源，还要更新 `cmake/VoxTypeRuntime.cmake` 的安装清单。
- **版本号只改 `src/app/resource.h` 的 APP_VERSION_MAJOR/MINOR/PATCH/BUILD**，再同步 `README.md` 版本和 `CHANGELOG.md` 记录。`src/app/main.cpp` / `src/app/resources.rc` 用宏自动派生。
- sherpa-onnx `cxx-api.h` **含非 ASCII 字符串字面量**（不只是注释），编译必须 `/utf-8`：缺此项不是告警而是**硬错误**（`error C2001: newline in constant` 及连锁的 C2146/C2061/C2059，实测 MSVC 14.44.35207 + SDK 10.0.26100.0）。
- **每个 `add_library` / `add_executable` 都必须继承 `/utf-8` 与 `/EHsc`**：新拆出的静态库若只写在 `VoxType` 上就会编译失败。统一走 `voxtype_build_flags` INTERFACE 目标（见重构方案 §3）。守卫对此硬 FAIL。
- **`add_library(X STATIC/SHARED/MODULE)` 必须至少声明一个源文件**，否则 CMake 在 generate 阶段直接失败（`No SOURCES given to target`）。需要"先占位后填内容"时用 `INTERFACE`。
- **本仓库的 `.ps1` 脚本必须纯 ASCII，或存为 UTF-8 with BOM**。`build.bat` 用的是 Windows PowerShell **5.1**，它按 ANSI(CP936) 解码**无 BOM** 的 UTF-8 脚本，会吞掉"紧跟非 ASCII 字节的换行"，症状是与编码毫无关联的 `Missing '=' operator after key in hash literal` 一类解析错误。同理，统计源码行数/条目**禁止用 `Get-Content`**（会少算），必须用 `[System.IO.File]::ReadAllLines()`。
- **HUD、Settings 与所有弹窗必须严格遵守 DPI 自适应架构（硬性规范）**：
  - 应用清单启用了 `PerMonitorV2`。全局 UI 统一以 150% DPI（144 DPI）为逻辑设计基准。
  - 所有控件尺寸、间距、窗口外框常量必须集中在 `src/ui/ui_types.h` 的 `UiStyle` 命名空间维护，**绝对禁止**在子窗口/独立对话框（如 `settings_dialogs.cpp`）中私自另设 `k*` 局部常数覆盖全局设计。
  - 物理像素转换统一且必须经由 `S(UiStyle::Constant)`（底层调用 `DipToPx(px, UiStyle::Scale)`，其中 $\text{Scale} = \text{DpiScale} \times 96 / 144$）。**严禁**向 `S()` 传入未经 144 基准核算的低分辨率绝对数值（例如在 96 DPI 下乘 $0.6667$ 会直接导致多行提示腰斩、文字降笔截断、按钮溢出客户区）。
  - 所有窗体与对话框在创建控件前必须先调用 `UpdateUiScale(hwnd / parent)` 同步当前窗口 DPI；顶层窗体与弹窗必须实现 `WM_DPICHANGED` 重新计算缩放并调用 `SetWindowPos` 更新尺寸。
  - 字体应用必须经由 `ApplyUiFont`，且其内部必须保证 `HFONT` 永不为 NULL（默认回退至 Segoe UI 9pt），严禁向控件发送空字体句柄导致 Windows 降级为粗体点阵系统字。
  - UI 修改后必须通过 `build.bat` 构建并在 96/144/192/288 DPI 下执行静态布局校验（`validate_settings_layout.ps1`）。
- 新增配置项同步：`src/core/config_store.h` 的 `Config` 字段、`src/core/config_store.cpp` 的 `LoadConfig` / `SaveConfig`、`src/ui/settings.cpp` 的控件创建与 `LoadSettingsControls` / `SaveSettingsControls`。
- DLL 延迟加载：`onnxruntime.dll`、`sherpa-onnx-cxx-api.dll`、`kaldi-native-fbank-core.dll` 通过 `/DELAYLOAD` 延迟加载，纯云端模式空闲 ~12 MB。`TryLoadAsrDlls()` 用 SEH 安全检测。
- 本地模式启动时 `PreloadAsrEngine()` 后台线程预加载模型，Save 后 Reload + 预加载。

## 新增云端 ASR 接入规则

- 先判断新后端是 batch 还是 streaming，不要直接在 `src/app/main.cpp` 写大段 provider 逻辑。
- Batch 后端优先走 `IAsrSession` / `BatchAsrSessionBase`；streaming 后端优先走 `IStreamingAsrSession` / `StreamingAsrSessionBase`。
- 复用已有公共层：`asr_dispatcher` / `asr_result` / `cloud_http_common` / `cloud_asr_common` / `PendingPcmBuffer` / VAD trimmer。只有 provider 协议差异留在各自 client/session 内。
- 音频回调只做轻量采集、可选 VAD trim、`EnqueuePcmChunk()`；不要在 WASAPI/waveIn 回调里做网络请求或 provider 协议逻辑。
- Streaming 停止录音用 `StopInput()` 通知 session，不要让 `StopRecordingSession()` 同步等待云端 final。
- 新 provider 如果有跨录音 session 的连接预热/复用句柄，先保留清晰生命周期，不要强行收进单次录音 session。
- Settings 新增配置项仍必须按"新增配置项同步 9~10 处 / 4 个文件"的规则执行（见"开发约定"一节）；在字段注册表落地前不要只改三处。

## 不要轻易做的事

- 不要把 LLM 接成默认纠错，容易乱改用户意思。
- 不要让 UI 线程加载模型或等待 ASR。
- 不要在 Settings 打开时继续拦截录音快捷键。
- 不要依赖固定窗口高度放底部按钮。
- 不要为了美观牺牲控件可读性，高 DPI 优先留空间。
- 不要新增第四个 JSON 取值函数。项目里已有三族：`ExtractJsonString(json, key, fallback)`（`src/audio/engine.cpp`，声明在 `engine.h`）、`ExtractJsonString(json, key)`（`src/asr/qwen_free_proto_llm.cpp`，2 参重载）、以及 `src/core/utils.h` 的转义感知族（`DecodeJsonStringAt` / `ExtractJsonStringDecoded` / `ExtractJsonArrayFirstStringDecoded`）。新增前先查重；P2 之后 `engine.cpp` 那一族应迁入 Core 并合并为单一定义。
- 不要在火山引擎 `bigmodel_nostream` 模式下对中间 chunk 调用 `ReceiveResult`——服务器返回空文本，浪费时间。
- 不要在 Extra Params 和代码生成的 `corpus` 中同时写 `corpus`——代码已跳过 Extra Params 中的 `corpus` key，手动编辑 config 需注意。
- 不要新增云端 ASR 时绕过 `IAsrSession` / `IStreamingAsrSession`，否则 `main.cpp` 会重新膨胀。
- 不要把 Qwen / 火山 / 未来 streaming provider 的 send loop、drain thread、retry 强行做成一个复杂模板；公共层负责生命周期和通用策略，协议差异留在各自 session/client 内。

## 踩坑规则

> AI 在完成重大修改或解决复杂报错后，可追加规则。

### 规则分类（新增条目必须标注类别）

本节的规则不是同一类东西，混在一起会让"该守的守不住、该放的放不掉"。新增或修改规则时，**必须**标注它属于哪一类：

| 类别 | 含义 | 重构时的处置 | 例 |
| :--- | :--- | :--- | :--- |
| **A · 不变量** | 描述**外部世界的行为**（OS / 协议 / 硬件 / 第三方控件）。语言标准升级、文件搬家都不改变它，违反即 bug。 | **永久保留**，重构中作为"不得破坏"的红线 | WASAPI 生命周期；重采样相位公式；微信必须 `WM_CHAR`；FireRedVAD 的 int16 范围 |
| **B · 条件约束** | 因为**当前某个前提**才成立（没有测试覆盖 / 还是全局变量 / 还是单文件）。前提一旦解除，约束即失效。 | **必须写明条件与解除条件**；条件解除后重写为"先补测试再重构"，而不是当永久禁令 | "火山协议层不要在无测试覆盖时重写" |
| **C · 实现现状** | 描述**当前代码长什么样**（某文件里某函数、某签名、某全局变量还在）。 | 重构会让它过期，**每个 Milestone 收尾必须重审**；与现实不符的**立即删除**，不得留作"历史注释" | 「`IVadDetector` / `FireRedVad` 使用 `std::span<const float>`」 |

**元规则（重要）**：规则必须可证伪。凡是描述"当前实现"而非"外部行为"的条目，在对应 Milestone 收尾时逐条重审；**已与现实不符的规则是负资产**——它会让执行者建立错误的前置认知，比没有规则更糟。**禁止**把过期规则保留为"历史说明"或注释掉。

- **【A】CapsLock 热键**：短按必须补发 `CapsLock` 保持系统切换，长按录音结束后必须恢复原 Caps Lock 状态。
- **【A】HUD 与 Settings DPI 全局自适应不变量**：
  1. DirectWrite/Direct2D（HUD）使用 DIP 测量，`SetWindowPos` 使用物理像素，跨屏移动必须重新根据目标显示器 DPI 计算缩放。
  2. Win32 窗口（Settings / 对话框）逻辑设计基准为 144 DPI（Scale = 1.0），在 96 DPI 标准屏下缩放系数为 0.6667。控件与文字高度必须预留安全容限（单行标签高不低于 30、两行提示不低于 48、按钮不低于 34），严禁在局部私自缩减，否则在 96 DPI 下字体行高将超越控件物理边框，引发文字横向腰斩或 descender（y/g/p 下延）硬件级裁切。
  3. 任何新建弹窗（Dialog/Prompt）均需统一继承 `UpdateUiScale`、`UiStyle::*` 与 `WM_DPICHANGED` 机制，严禁使用固定写死的外框与控件绝对像素。
- **【A】FireRedVAD**：fbank 期望 int16 范围（-32768~32767），不是归一化 float，传入前必须乘 32768。
- **【A】公共 VAD trim 只裁剪头尾静音**，不能裁掉中间停顿；`VadTrimCore::ProcessChunk()` 是**追加输出语义**，调用方要自己清空/使用局部 `outputs`。
- **【A】Streaming VAD 的 no-speech 判断用 `StreamingVadTrimmer::DetectedSpeech()`**，不要重新引入 provider 专属 `g_xxxVadState`。
- **【A】转义 JSON 解码**：处理 `\\"` 时不能只看前一个字符，必须统计连续反斜杠数量——偶数个后的 `"` 才是结束符。现状实现在 `src/core/utils.h`（`DecodeJsonStringAt` / `ExtractJsonStringDecoded`）。
- **【A】火山引擎 WebSocket `connected` 必须用 `std::atomic<bool>`**，不能用 `volatile bool`。
- **【A】火山引擎 `context` 字段值必须是 JSON 字符串**（内部引号转义），不能是原始 JSON 对象。
- **【B】火山引擎协议层**：**在当前（无测试覆盖）前提下**，不要重写 frame 编解码、三模式 send/drain、`PrewarmConnection()` / `CloseSession()` 行为；可以机械拆薄。**解除条件**：一旦 P3/P6 为该协议层建立了回归测试，本条自动作废，替换为"协议层重构前必须先补测试"。
- **【B】Streaming provider 的 send loop / drain thread / retry**：不要强行做成一个复杂模板。**解除条件**：公共层的生命周期与通用策略在 P3 收敛并有测试保护后，可重新评估。
- **【A】百度 ASR** transient 失败应重发同一段 PCM；token/auth 错误应清 token 后刷新重试，避免用户必须重新说第二遍。
- **【A】WASAPI 生命周期**必须是 `Init → Start → Stop → Release`，`Stop()` 只停线程不清资源，没有 `Release()` 第二次录音会卡死。`Init()` 成功但 `Start()` 失败时也必须 `Release()`。
- **【A】WASAPI 重采样相位更新**必须用 `m_resamplePhase -= written / m_resampleRatio`（实际消耗的源样本数），不能用 `m_resamplePhase -= numFrames`（输入帧数），否则非整数采样率比会越界。
- **【A】微信粘贴**：微信（`Weixin.exe`）用自定义 Qt 控件，`GetFocus()` 返回 NULL 且 IME 拦截 Ctrl+V。必须用 `WM_CHAR` 逐字符发送，不能用剪贴板+Ctrl+V。其他应用用剪贴板+Ctrl+V+IMM32 切换。
- **守卫基线只许下调，禁止上调**。`tools/check_architecture.ps1` 里的基线数值代表"当前债务上限"，只减不增。任何上调必须由**用户人工确认**，且提交信息里写明理由。守卫自带完整性检查：相对 `HEAD` 上调基线即判 FAIL。
- **不要为了让守卫变绿而删测试、注释掉守卫调用、或放宽校验规则**。这是最容易被自主执行者选择的"捷径"，属于禁止行为。守卫会校验 `build.bat` 仍在主流程（而非仅 `--test`）调用自己，以及 5 个既有测试目标仍在。
- **不要用 `file(GLOB ...)` 收集源文件**：会让"新增文件忘进 CMake""删除文件忘出 CMake"双双变成静默行为，并让 target 结构检查失真。守卫已禁止 `GLOB`。
- **拆 `globals.h` 时不要把它的 include 换个新头继续集中**。把 9 个跨层 `#include`（sherpa / provider / 音频 / UIVAD 头）搬到 `config.h` 只是改名字，重编译爆炸圈一点不变——必须让每个 include 回到真正使用它的那一层。
- **PCH 里放 `<windows.h>` 必须同时全局定义 `NOMINMAX`**：MSVC 用 `/FI` 强制包含 PCH，`min`/`max` 宏会污染每个 TU，使全仓 54 处 `std::min`/`std::max`（14 个文件）报 `error C2589`。各文件里本地的 `#ifndef NOMINMAX` 在 PCH 下**已经太晚**。守卫对此硬 FAIL。
