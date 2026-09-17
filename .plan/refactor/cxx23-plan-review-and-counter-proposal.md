# 对《VoxType 全面现代化与 C++23 架构重构方案》的技术评审与替代思路

> 评审对象：`.plan/refactor/cxx23-architecture-refactor-plan.md`
> 评审日期：2026-09-17
> 评审方式：全部结论基于对本仓库源码、`CMakeLists.txt`、`CMakePresets.json`、`build.bat`、`ARCHITECTURE.md`、`AGENTS.md` 的实际读取，以及对本机 MSVC 14.44.35207 工具链的实测编译。每条结论附可复现证据。

---

## 0. 总体结论

**方向对，地基错。**

方案的三条主线——「分层解耦 / 模块化 CMake target / 分阶段不中断」——是正确的。但它写给 AI 当执行标准，就必须满足一个更高的门槛：**文中出现的每个路径、每个 API、每个构建命令都要能被机器验证通过**。当前文档在这三点上都不成立：

| 维度 | 结论 |
| :--- | :--- |
| 现状诊断 | **半数错误**。main.cpp / 文本注入 / 托盘的责任描述与源码不符（详见 §1） |
| 改造清单 | **不可执行**。CMake 清单中 15 个文件不存在，17 个真实存在的生产源文件完全缺席（详见 §2） |
| 分层契约 | **自相矛盾**。分层图含环；且把一个横跨 4 层的文件整体划给单层（详见 §3） |
| 最大风险项 | **完全未识别**。`globals.h` 的 91 条 `extern` + main.cpp 的 ~89 个全局变量定义，是「main.cpp 无法瘦身」的真正原因（详见 §4） |
| 可验证性 | **不达标**。作为 AI 执行标准，缺少客观判据（检查脚本、PASS 定义、回滚点），验收项退回人工（详见 §5） |
| 契约同步 | **缺失**。删除 `globals.h` 会让 `AGENTS.md` / `ARCHITECTURE.md` 的既有规则失效，只在 Phase 5 提了一句更新文档（详见 §6） |

一句话：**这份文档更适合作为「愿景白皮书」，不适合作为「AI 执行标准」。** 两者对精确度的要求差一个数量级。

---

## 1. 现状诊断中的事实性错误

方案 §1.1 列举了 5 条痛点。其中 2 条方向正确、3 条与源码不符。

### 1.1 规模严重低估

| 方案表述 | 实际 | 证据 |
| :--- | :--- | :--- |
| "`main.cpp` 超过 1200 行" | **2759 行** | `wc -l src/app/main.cpp` |
| 全文只提 main.cpp 臃肿 | `src/ui/settings.cpp` **4405 行**，是仓库最大文件 | 方案对它的唯一处置是「保持高 DPI 布局常数」 |

`settings.cpp` 单文件 4405 行、含 11 处 Clipboard API、5 处 `WM_CHAR`、3 处 `SendInput`——它同时承载 UI 布局、配置读写、provider 管理、以及**文本注入实现**。这个文件才是最大债务，方案几乎没看它。

### 1.2 「文字注入混在 main.cpp」——错误

```
$ grep -rn "PasteTextImeAware" src/
src/app/main.cpp:2348:                    PasteTextImeAware(text);   ← 调用点
src/app/main.cpp:2432:                PasteTextImeAware(text);       ← 调用点
src/ui/settings.cpp:902:void PasteTextImeAware(const std::wstring& text) {  ← 实现
src/ui/settings.h:10:void PasteTextImeAware(const std::wstring& text);
```

注入实现在 **`src/ui/settings.cpp`**。main.cpp 只有 2 个调用点。

连带影响：方案 §7 把 `TextInjector` 标为「(混在 main.cpp) → [NEW] `src/platform/text_injector.cpp`」。按这个指引，执行 AI 在 main.cpp 里只会找到 2 行调用，**会误判为"已完成"或凭空重写一份**，而真正的 4405 行拆分工作被漏掉。

真实的抽取源头是三个文件，方案一个都没点名：
- `src/ui/settings.cpp`（`PasteTextImeAware` 实现，含微信 `WM_CHAR` 分支）
- `src/core/selection_context.h`（370 行，38 处 Clipboard API、`OleSetClipboard`、2 处 `GetForegroundWindow`）
- `src/core/input_context.h`（502 行，4 处 `GetForegroundWindow`，UIA/MSAA/WM_GETTEXT 分层回退）

### 1.3 「托盘逻辑混在 main.cpp」——错误

```
$ grep -rn "Shell_NotifyIcon" src/
src/ui/hud.cpp: 3 处        ← 托盘实现在 HUD 里
src/app/main.cpp: 0 处
```

托盘在 `hud.cpp`（与 `ARCHITECTURE.md` L64 记载一致：「HUD window, Direct2D/DirectWrite rendering, **tray icon**」）。方案 §7 又把 `tray_manager` 标为「(混在 main.cpp) → [NEW]」。同样会导致执行 AI 找错文件。

### 1.4 「36 个编译单元全量重编译」——数字来源错误

`globals.h` 的直接包含者是 **23 个文件**（10 个 `.cpp` + 12 个 `.h` + 1 个测试），而不是 36 个。36 是 `src/` 下 `.cpp` 的总数（实测 36 个），两者不是一回事。

更要紧的是，方案没看出**耦合的真正机制**。`globals.h` 不只是常量集合，它是一个 include hub：

```cpp
// src/app/globals.h L25-31（实测）
#include "sherpa-onnx/c-api/cxx-api.h"   // 拉进整个 sherpa C++ API
#include "firered_vad.h"
#include "llm_refine.h"
#include "baidu_asr.h"
#include "audio_diagnostics.h"
#include "wasapi_capture.h"
#include "input_context.h"
#include "qwen_free_postprocess.h"
#include "resource.h"
```

任何一个 TU 只要碰 `globals.h`，就顺带拉进 sherpa 的 `cxx-api.h`（按 `AGENTS.md` 记载含非 ASCII 注释、必须 `/utf-8`）和 4 个 provider 头。**这才是「改一处、全量重编」的物理原因。**

方案给出的补救是「拆为 `config.h`、`ui_types.h`、`audio_types.h`」——但换名字不解决 include 问题：只要新头里还留着上面这 9 行，重编译范围一点没变。**方案缺的是「这 9 个 include 各自归还给哪一层」这一条，而它恰好是整件事的关键。**

---

## 2. 改造清单不可执行（最硬的伤）

我把方案 §3.1 的 CMake 代码块逐条与磁盘比对：

```
方案中声明的源文件条目： 35
磁盘上不存在：           15
磁盘上有、方案里完全没提的生产 .cpp： 17
```

### 2.1 不存在的 15 个文件

| 目标库 | 方案写的 | 实际 |
| :--- | :--- | :--- |
| `voxtype_core` | `src/core/error.cpp` `config.cpp` `path_service.cpp` `logger.cpp` `utf_util.cpp` | 全部不存在（属新建文件，但文档没标 `[NEW]`，会被当成现状） |
| `voxtype_audio` | `src/audio/resampler.cpp` | 不存在，无此源文件 |
| `voxtype_asr` | `src/asr/baidu_asr.cpp` | 不存在。`baidu_asr.h` 是 **header-only**（482 行） |
| `voxtype_asr` | `src/asr/llm_refine.cpp` | 不存在。`llm_refine.h` 是 **header-only**，且位于 `src/core/`（835 行，`ARCHITECTURE.md` L68 明确记载 header-only） |
| `voxtype_ui` | `src/ui/settings_window.cpp` `hotkey_manager.cpp` `tray_manager.cpp` | 不存在（前两个是重命名，第三个是新建，文档混在一起写） |
| `voxtype_platform` | `src/platform/text_injector.cpp` `process_util.cpp` | 不存在（新建，但 `src/platform/` 目录本身也不存在） |
| `VoxType` | `src/app/workflow_manager.cpp` | 不存在（新建） |

**直接后果**：AI 照抄这段 CMake，`cmake --preset x64-release` 立刻报缺文件；AI 若"聪明地"删掉缺的条目照常构建，就会丢掉 `engine.cpp` / `settings.cpp` / `hotkey.cpp` 等真实源文件，链接期爆炸。

### 2.2 完全缺席的 17 个生产源文件

```
src/asr/asr_diagnostics.cpp        src/asr/asr_runtime_log.cpp
src/asr/doubao_ime_asr.cpp         src/asr/qwen_asr.cpp
src/asr/qwen_audio_http.cpp        src/asr/qwen_audio_streaming.cpp
src/asr/qwen_audio_streaming_session.cpp
src/asr/qwen_free_proto_asr.cpp    src/asr/qwen_free_proto_llm.cpp
src/asr/qwen_free_proto_sign.cpp   src/asr/qwen_free_proto_unet.cpp
src/asr/qwen_free_proto_utdid.cpp  src/asr/qwen_free_streaming_session.cpp
src/asr/volcengine_net_diag.cpp    src/audio/engine.cpp
src/ui/hotkey.cpp                  src/ui/settings.cpp
```

其中 `engine.cpp`、`settings.cpp`、`hotkey.cpp` 是仓库第 2、1、4 大的源文件（1497 / 4405 / 388 行）。一个「文件映射表 (Before & After)」把最大的三个生产文件放在映射表之外，这份表就不能用于施工。

补充：`qwen_free_proto_sign.cpp` 在当前 `CMakeLists.txt` L133-136 有明确注释——*"The generic HMAC implementation is not linked into the production VoxType executable"*，它只进 `qwen_free_protocol_test`。方案对这条**刻意的链接排除**只字未提。重构时若改成 `voxtype_asr` 库并顺手纳入，虽然静态库按需拉取不至于立刻出问题，但这种安全设计意图被无声吞掉，正是后续事故的温床。

### 2.3 PCH 清单里有一个编译不过的头文件

方案 §3.2：

```cmake
target_precompile_headers(voxtype_core PUBLIC
    ...
    <jthread>     # ← 这个头文件不存在
)
```

实测（MSVC 14.44.35207，Windows SDK 10.0.26100.0）：

```
probe.cpp(7): fatal error C1083: Cannot open include file: 'jthread': No such file or directory
```

C++20 起 `std::jthread` 定义在 **`<thread>`** 中，标准从未提供 `<jthread>` 头。改掉后同一份源码在 `/std:c++latest` 与 `/std:c++23preview` 下均编译通过。

一个被指定为「AI 执行标准」的文档出现这种错误，代价不只是编译失败——它会让后续 AI 学会"文档里的东西不一定对，可以自己判断"，契约的权威性就没了。**这是把文档当标准时最不能容忍的一类错误。**

---

## 3. 分层设计的三处缺陷

### 3.1 分层图里有一个环

```
App --> UI
UI  -.-> |Event Feedback| App      ← 与上一行构成环
```

方案自己在 §2 写着「单向依赖，禁止反向环形依赖」，同一张图里就画了一条反向虚线。虚线标注为"Event Feedback"，说明作者知道这里是回调——但**没有给出接口形态**。结果：AI 实现 `hud.cpp` 通知主状态机时，会合理地选择直接 `#include "main.h"` 或调用 app 层函数，环就真实成立了。

现实约束比这更硬：`AGENTS.md` 明文要求「Save 后 Reload + 预加载」，即 **Settings（UI 层）保存配置后必须触发 ASR 引擎重载**——这是一条真实的 UI → ASR 依赖。方案 §2 的依赖表里 `voxtype_ui` 只依赖 `voxtype_core`。**方案与实际需求直接冲突，且没有给出解法（接口/事件总线/回调注册）。**

### 3.2 把一个跨 4 层的文件整体划给 ASR 层

方案 §7：`src/audio/engine.cpp` → `src/asr/engine_local.cpp`，说明是「纯粹作为本地 Sherpa 引擎」。

`ARCHITECTURE.md` L55 对它的定义是：「**string/path utilities, JSON config persistence, audio capture, `AsrEngine` class, `PreloadAsrEngine()`**」。实际函数清单（实测）：

| engine.cpp 内的职责 | 应归层 | 方案归属 |
| :--- | :--- | :--- |
| `AppRootDir` `RuntimeAssetDir` `MutableDataDir` `ConfigPath` `LogDir` `IsPortableMode` `DefaultModelDir` … | Core（PathService） | ❌ 被划入 ASR |
| `LoadConfig` `SaveConfig` `SaveCurrentProvider` `ApplyPreset` | Core（Config） | ❌ 被划入 ASR |
| `ExtractJsonString/Bool/Int` | Core | ❌ 被划入 ASR |
| `StartAudioCapture` `StopAudioCapture` `CloseAudioCapture` `WaveInProc` `CalculateAudioLevel` | Audio | ❌ 被划入 ASR |
| `DpiScaleForWindow` `DipToPx` | UI | ❌ 被划入 ASR |
| `AsrEngine` `PreloadAsrEngine` `PcmToFloat` | ASR | ✅ 唯一正确的部分 |

即：这个改动会把**路径服务、配置读写、音频采集、UI DPI 助手**四类东西一起搬进 `voxtype_asr`。这与方案 §2.1 的模块职责表（PathService 属 Core、采集属 Audio）和 §8 Phase 1（配置剥离进 Core）**自相矛盾**，同时制造出 `voxtype_asr → voxtype_ui` 这种反向依赖。

同理，`src/audio/engine.h` 当前位置在 audio 层，但它的内容（配置、路径、采集）属于 core/audio 混合，且 include 了 `globals.h`（app 层）。这是**已存在的层次倒置**——方案把它当成"重命名"处理，等于把一个结构性错误固化。

### 3.3 `voxtype_platform` 的分层定位含糊

方案 §2.1 让 platform 层只依赖 Core，但 `input_context.h` / `selection_context.h`（UIA/MSAA/WM_GETTEXT、剪贴板、`GetForegroundWindow`）目前住在 `src/core/`，是注入与上下文探测的基础。方案**没有给这两个文件任何去处**（872 行代码在整份文档里失踪）。它们究竟在 Core 还是 platform？依赖方向由这个决定。AI 遇到这个空白必然自选，然后 layer 契约就废了。

---

## 4. 方案完全未识别的最大障碍：91 条 extern 与 ~89 个全局变量

`ARCHITECTURE.md` L89 写着：

> Global variables are defined in `main.cpp` and accessed by other modules via `extern` declarations in `globals.h`.

实测：

```
src/app/globals.h  中 "extern" 开头的声明：91 条
src/app/main.cpp   中全局定义：~89 个
```

这才是 **main.cpp 无法瘦身的第一性原因**：语言上，其他模块通过 `extern` 直接读写 main.cpp 拥有的状态（录音会话、ASR attempt、配置快照、剪贴板旧值、HUD 状态……）。只要这些裸全局还在，main.cpp 就不能"收缩至纯净入口"——它必须持有全部状态的所有权。

方案的处置是表格里一行 `[DELETE]`：

> `src/app/globals.h` | 彻底拆分并废除 | **[DELETE]** | 拆为 `config.h`、`ui_types.h`、`audio_types.h`。

- 缺**迁移策略**：91 条 extern 对应的状态，改成谁拥有？单例？`AppContext`？依赖注入？方案只字未提。
- 缺**顺序**：§8 把「废弃 globals.h」放在 Phase 4 最后一步，而 Phase 2/3 又要改 audio/asr 的函数签名——那些签名里带着 `const Config&`（来自 globals.h），跨阶段必然反复返工。
- 拆成 3 个头也不够：`globals.h` 里的东西是 5 类，不是 3 类——① 窗口类名与 `WM_APP` 消息 ID（13 个，实测）、② 定时器 ID 与超时值、③ HUD 布局 DIP 常量、④ `UiStyle` Settings 布局常量、⑤ `Config` 与 91 条 extern。其中①②是 **App 工作流概念**，放进「`voxtype_core` 基础库」是层次倒置。

**结论**：`globals.h` 的废除是本项目重构的真正工程量所在，也是"方案里 1 行、实际 1 个月"的差距。方案没有把它识别成里程碑。

---

## 5. 作为「AI 执行标准」的可验证性缺口

用户的目标是"方便后面的 AI 开发"。以这个目标衡量，方案最大的短板不是技术细节，而是**缺少客观判据**。

### 5.1 缺「完成判据」

方案每阶段的验证项：

| 阶段 | 方案的验证 | 问题 |
| :--- | :--- | :--- |
| P1 | `build.bat` 与 `build.bat --test` 保持 100% PASS | 现状 `build.bat --test` 只跑 5 个**离线协议**测试（`qwen_free_protocol_test` / `qwen_audio_json_test` / `llm_refine_test` / `audio_diagnostics_test` / `asr_json_protocol_test`，实测于 build.bat L148-190）。它们**不覆盖** UI 布局、注入、WASAPI 硬件、托盘。改 `globals.h` / `engine.cpp` 这类结构改动，这些测试几乎必然继续 PASS——"100% PASS"等于没有约束力 |
| P2 | 执行 `audio_diagnostics_test` 确认无回退 | 同上，该测试不碰 VAD span 化的调用点 |
| P4 | **手动**运行完整输入法，验证普通软件粘贴与微信打字正常 | 这是把判据退回人工，与 §1.2「测试全覆盖」的承诺冲突 |

方案 §1.2 承诺「绝不搞推倒重来式中断」「测试全覆盖」，但 P2/P4 的验收恰恰是人工。**文档对自己的承诺没有兑现机制。**

### 5.2 缺「禁止事项 + 回滚点」

用户环境有已知的 **atomic-write 文件系统问题**（可致 `.git/refs/` 丢失、`.pack`/`.idx` 孤立），因此多步操作需原子化、避免 `git stash` 类中间销毁动作。方案全文没有：
- 每阶段的 commit 粒度与回滚方式
- 禁止的操作（`git stash`、跨阶段混改、`file(GLOB)` 收源文件）
- 每阶段的"可回滚点"定义

对"给 AI 当标准"来说，这一节的缺失比任何技术细节都危险：AI 在执行 5 个阶段时，一旦某步失败，没有规定的退路。

### 5.3 缺构建系统同步（`build.bat` 被完全忽略）

方案 §3.3 要加 `x64-debug` / `x64-asan` 预设。但：

1. **`build.bat` 写死了 `x64-release`**：`BUILD_TREE` / `RUNTIME_DIR` / 5 个测试 exe 路径全部硬编码。新增预设若不改 `build.bat`，这两个预设**永远不会被用到**（`AGENTS.md` 规定 `build.bat` 是唯一构建入口）。
2. **测试产物目录会撞车**：现有 `RUNTIME_OUTPUT_DIRECTORY` 是 `${CMAKE_BINARY_DIR}/../../artifacts/tests`，即 `build/artifacts/tests`。新增 `build/cmake/x64-asan` 预设后，两个配置的同名测试 exe 会写进同一目录互相覆盖。方案没意识到这一点。
3. **ASan 运行时不在 PATH**：实测 `/fsanitize=address` + `/MT`（项目当前 `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded"`）**可以编译、链接、运行成功**（`/MT` 走静态 ASan 运行时，无需 DLL）；但 `/MD` 变体运行时需要 `clang_rt.asan_dynamic-x86_64.dll`，该 DLL 只在 MSVC 的 `bin\Hostx64\x64\` 下，而 `build.bat` 目前只把 Ninja 目录加进 PATH。**方案要落地 ASan 预设，必须同步改 `build.bat` 的 PATH**——文档没提。
4. **`/O2` 硬编码会与 debug 预设打架**：`CMakeLists.txt` L121 是 `target_compile_options(VoxType PRIVATE /O2 /EHsc /utf-8)`，无条件。加 `x64-debug`（`/Od`）后，两个优化开关同时存在，实际行为取决于顺序。正确做法是按 `$<CONFIG>` 生成表达式或交给 `CMAKE_BUILD_TYPE`，方案把 3 组 flag 并列写出来，没说明这个坑。

---

## 6. 删除 `globals.h` 会让现有契约文件失效——与"方便 AI 开发"直接冲突

这一点值得单独强调，因为它**与用户的目标反向**。

当前有两份 AI 会读的契约文件，都强绑定 `globals.h`：

`AGENTS.md`：
- 「Settings 布局常量在 `src/app/globals.h` 的 `UiStyle` 命名空间，不要硬编码魔法数字」
- 「新增配置项同步三处：`src/app/globals.h` Config 字段 + `src/audio/engine.cpp` LoadConfig/SaveConfig + `src/ui/settings.cpp` UI 控件」
- 「版本号只改 `src/app/resource.h`」

`ARCHITECTURE.md`（478 行）：
- L40-88 的 `Source Code Structure` 文件职责表，逐文件描述了 `engine.cpp`、`globals.h`、`hotkey.cpp`、`settings.cpp` 的位置与职责
- L330-338 `Text Injection` 章节
- L89 全局变量所有权说明

另有一条**契约本身已经失真**的实证：`AGENTS.md` 写「新增配置项同步三处」，实测以 `volcEnableContext` 追踪，实际需要动 **10 处 / 4 个文件**（`Config` 字段约 98 行、`engine.cpp` 内 `ExtractJson*` 调用 92 次、`SaveConfig` 从 L762 起）。也就是说，方案赖以"防止 AI 改错"的那条规则，**现在就已经是错的**。方案若只是"再补一份文档"而不修这条规则，等于在失真契约上再叠一层。

方案 Phase 1 就动 `Config`，Phase 4 才删 `globals.h`，而**两份契约文件只在 Phase 5 有一句「更新 `ARCHITECTURE.md` 与开发文档」**。也就是说在 Phase 1~4 的整个周期里，AI 读到的 `AGENTS.md` / `ARCHITECTURE.md` 会持续与代码矛盾。

**对"方便 AI 开发"这个目标，契约文件过期比代码丑危险得多**：AI 会忠实地按过期规则操作（例如继续往"已废弃的 globals.h"加字段、按旧路径找 `PasteTextImeAware`）。

---

## 7. 方案中应当保留的部分

为避免结论被误读为"全盘否定"，以下内容我认为是正确且有价值的，替代方案应当继承：

1. **5 层分层模型本身**（Core → Audio → ASR → UI/Platform → App），方向清晰，比现状的无分层强得多。
2. **模块化 CMake target** 与「先在 CMake 里建库、再动代码」的顺序（Phase 1 的思路）。这对"测试只链接纯逻辑子集"的收益是真实的。
3. **`PathService` 统一路径服务**（Portable / LocalAppData 判定）。现状路径逻辑散在 `engine.cpp` 的 14 个函数里，抽出来确实正确。
4. **`std::span` 用于音频切片**、`std::jthread` 用于长跑线程、`std::format` 替换手写拼接——作为**方向**正确，作为**每阶段硬指标**过重（见 §8.2）。
5. **§9 的 6 条红线**（微信必须 `WM_CHAR`、WASAPI 生命周期、音频实时线程禁堆分配、DPI 单位、DLL 延迟加载 SEH、UTF-8 字面量）。这 6 条与 `AGENTS.md` 一致，是本方案质量最高的部分，应原文保留。
6. **不推倒重来、分阶段推进**的原则。

---

## 8. 替代思路：把"标准"拆成「机械契约」+「风格建议」

核心差异点一句话：**现在的方案把风格当标准、把契约当建议；应当反过来。**

AI 执行标准能否被遵守，取决于判据是**机器可判定**还是**需要理解文档**。`std::expected` 该不该用是判断题（AI 每次都可能判断不同）；`src/core/**` 不许 include `src/asr/**` 是机器可判定的（一条 grep 就能定对错）。**把精力投到后者，前者降级为建议。**

### 8.1 第一步：先立不变量检查，再动一行代码

新增 `tools/check_architecture.ps1`（与 `scripts/` 下现有 ps1 保持同一风格），断言若干条只减不增的白名单规则，例如：

| 规则 | 判据 |
| :--- | :--- |
| `src/core/**` 不得 include `src/{asr,audio,ui,app,platform}/**` | 反向依赖检查 |
| `src/audio/**`、`src/asr/**` 不得 include `globals.h` | 消除层次倒置 |
| `globals.h` 的包含者数量，只能 ≤ 当前基线 23 且必须逐阶段下降 | 可量化的收敛目标 |
| `globals.h` 的 `extern` 条数，只能 ≤ 当前基线 91 且逐阶段下降 | 让「废 globals」变成有刻度的任务 |
| `src/app/main.cpp` 行数只减不增 | 防止重构中途反弹 |
| 生产 target 不得包含 `qwen_free_proto_sign.cpp` | 固化现有安全设计意图 |
| `git grep` 禁止在 `src/` 出现新增裸全局 | 后续守卫 |

**这份脚本是本方案唯一需要"一次写对"的东西**，其余全部可以渐进。它的价值：AI 每次改动后跑一条命令就知道对不对，不需要重新理解一份 12KB 的文档。这才叫"方便后面的 AI 开发"。

### 8.2 第二步：按「删依赖」排序，而不是按「改语法」排序

现方案的 Phase 2（span 化）/ Phase 3（expected 化）本质是**跨全仓的语法风格翻新**，diff 巨大、功能收益为零、且要在 36 个 TU 上重跑。这是投入产出比最低、回归风险最高的做法。

建议顺序（每一步的判据都是 §8.1 的检查由红转绿，而不是"我改完了"）：

| 阶段 | 内容 | 判据 |
| :--- | :--- | :--- |
| **P0** | 立 `check_architecture.ps1` + 记录基线（23 / 91 / 2759 / 4405）；把 6 条红线补进 `AGENTS.md` | 脚本可运行，基线可复现 |
| **P1** | 拆 `globals.h` 的 include hub：9 个 include 各归其层。先拆头，不动结构 | `globals.h` 不再包含 sherpa/provider 头；全量构建时间可测下降 |
| **P2** | `Config` 迁 Core；`LoadConfig`/`SaveConfig`/路径函数从 `engine.cpp` 按职责拆出（PathService / ConfigStore 进 Core，采集进 Audio，`AsrEngine` 留 ASR，DPI 助手进 UI） | `engine.cpp` 消失；`src/audio/**`、`src/asr/**` 不再 include `globals.h` |
| **P3** | 拆 main.cpp。**注意：不是 3 个组件，实测至少 6 个** | main.cpp 行数逐阶段下降 |
| **P4** | 拆 `settings.cpp`（4405 行）：注入实现迁 platform，配置读写走 Core，布局/控件留 UI | 注入行为有独立可测入口 |
| **P5** | 处理 globals.h 残部（91 条 extern → 显式所有权对象）；`globals.h` 删除 | extern 条数归零 |
| **P6** | 风格收敛：span / expected / jthread / format，**逐文件做，做完一个提交一个** | 编译 + 测试双绿 |

### 8.3 P3 的正确拆分粒度（实测得出，不是 3 个组件）

按 main.cpp 的实际函数分布，`< 150 行` 的目标需要拆出至少 6 个单元：

| 拆分单元 | 实测内容 | 规模 |
| :--- | :--- | :--- |
| ASR attempt / fallback 生命周期 | `ActiveAsrAttempt*`、`CancelActiveAsrAttempt`、`MarkActiveAttemptFinalHandled`、`HandleAsrAttemptFinal`、`AsrAttemptFinalSourceName` | ~900 行 |
| 录音会话控制 | `StartRecordingSession` / `StopRecordingSession` / `BeginCaptureOnly` / `DiscardPendingCapture` | ~440 行 |
| 主窗口过程 | `MainWndProc`（含 13 个 `WM_APP` 消息分发） | ~440 行 |
| Streaming HUD 文本分页排版 | `BuildStreamingPartialHudText`、`StreamingHudPageFits`、`FormatStreamingPartialHudText` 等 11 个纯函数 | ~170 行 |
| 调试诊断输出 | `DebugModeOpenConsole`、`DebugPrint*` 共 9 个 | ~130 行 |
| 其余（波形/托盘菜单/窗口类注册/入口） | — | 余量 |

**其中「Streaming HUD 文本分页」是 11 个不依赖 Win32 的纯函数——现成的最佳单测对象，方案完全没提。** 拆出来就能立刻补自动化测试，正好补上 §5.1 的"判据不足"。

### 8.4 错误模型按线程分层（解决方案内部矛盾）

方案 §4.1 要求全量 `std::expected<T, AppError>`，而 `AppError` 持两个 `std::string`；方案 §9.3 又明文禁止音频实时线程内堆分配。**这两条规则在同一段代码里冲突**：WASAPI 回调里的任何失败返回都会构造两个字符串。

建议明确分两层，写进标准：

- **RT 路径**（WASAPI 回调、waveIn 回调）：`enum class` + 固定宽度字段，`static_assert` 断言 `sizeof` 且 `trivially_copyable`，禁止 `std::string`/`std::format`。
- **非 RT 路径**：`std::expected<T, AppError>`，`AppError` 可自由持字符串。

### 8.5 `std::print` 的正确适用范围

方案 §4.4 用 `std::println(stdout, ...)` 作为日志示例。但 `VoxType` 是 `add_executable(VoxType WIN32 ...)` —— **WIN32 子系统，stdout 不接控制台**，输出无处可见（现有 `DebugModeOpenConsole()` 就是为此专门建控制台的）。建议写明：

- 应用内日志：继续走现有文件日志 + `DebugMode*` 调试通道；
- `std::print/println`：仅用于**离线测试工具**（`tests/`、`tools/`）与构建期脚本。

### 8.6 契约同步 = 每个阶段的 DoD（不可选）

每阶段收尾必须同时更新，否则该阶段不算完成：

1. `ARCHITECTURE.md`（结构表 + 相关章节）
2. `AGENTS.md`（配置同步三处规则、`UiStyle` 位置、新分层红线）
3. `check_architecture.ps1` 基线数字
4. 若涉及版本 → `src/app/resource.h` + `README.md` + `CHANGELOG.md`
5. commit 粒度、禁止 `git stash`、单阶段原子提交（对应用户环境的文件系统约束）

### 8.7 顺手借鉴 `.plan/ref/AriaType/context/` 的治理结构

仓库里已有一份同类项目的成熟范式（`.plan/ref/AriaType/`），它的 `context/` 目录正是"AI 可执行标准"的形态：

```
context/architecture/layers.md          ← 分层契约
context/architecture/decisions/00X-*.md ← ADR（决策 + 理由 + 后果）
context/spec/engine-api-contract.md     ← 引擎接口契约
context/conventions/                    ← 代码风格
context/plans/active|completed/         ← 计划生命周期
context/guides/adding-stt-provider.md   ← 扩展指路
```

注意它把**「引擎接口契约」**和**「分层」**写成了独立文件——本方案的 `IAsrSession` / `IStreamingAsrSession` / `TextInjector` 恰好需要这种契约（状态机迁移合法路径、失败分类、线程归属、超时语义），而目前只有散文描述。

**这是本方案最值得补的一块**：不是再加一张架构图，而是把三个接口的行为契约写成可核对、可写测试的文档。AriaType 的 `src/text_injector/` 是独立模块且有 `docs/text_injection_fix.md`——正是本方案 `voxtype_platform` 的目标形态。

---

## 9. 如果坚持沿用现方案：最小修补清单（按优先级）

若不想重写，至少必须先改这 6 处，否则不能开工：

| # | 必须修 | 原因 |
| :--- | :--- | :--- |
| 1 | `<jthread>` → `<thread>` | 编译直接失败 |
| 2 | CMake 清单：删除 15 个不存在路径，补齐 17 个真实源文件 | 照抄必失败 / 丢功能 |
| 3 | 修正 3 处现状误诊（注入在 `settings.cpp`、托盘在 `hud.cpp`、main.cpp 是 2759 行） | 执行 AI 会找错文件 |
| 4 | 补 `globals.h` 迁移策略（91 条 extern / ~89 全局的所有权方案）+ 把它提为独立里程碑 | 否则 main.cpp 无法瘦身 |
| 5 | 修分层图环 + 补 UI→ASR 重载路径的接口契约 + 给 `input_context.h`/`selection_context.h` 定层 | 分层契约不成立 |
| 6 | 补 `build.bat` 同步（预设生效、测试目录隔离、ASan PATH、`/O2` 与 `/O0` 冲突） | 新增预设形同虚设 |

---

## 附录 A：评审证据复现命令

```bash
# 规模基线
wc -l src/app/main.cpp src/ui/settings.cpp src/audio/engine.cpp src/ui/hotkey.cpp

# globals.h 直接包含者（基线 23）
grep -rln "globals.h" src/ tests/ tools/ | wc -l

# globals.h 的 extern 条数（基线 91）
grep -c "^extern" src/app/globals.h

# 注入实现位置（在 settings.cpp，不在 main.cpp）
grep -rn "PasteTextImeAware" src/

# 托盘实现位置（在 hud.cpp，不在 main.cpp）
grep -rn "Shell_NotifyIcon" src/

# 方案清单 vs 磁盘比对（15 缺失 / 17 未提及）
#   见本评审 §2，比对脚本按 add_library/add_executable 块解析后与 src/** 求差集

# C++23 工具链实测（MSVC 14.44.35207 + Windows SDK 10.0.26100.0）
#   <jthread>            → fatal error C1083（该头不存在）
#   <thread> + expected + format + print + span + ranges
#     /std:c++latest     → EXIT 0
#     /std:c++23preview  → EXIT 0
#   /fsanitize=address + /MT   → 编译/链接/运行均成功（静态 ASan 运行时）
#   /fsanitize=address + /MD   → 需 clang_rt.asan_dynamic-x86_64.dll 在 PATH
```

## 附录 B：现有测试资产（`build.bat --test`，实测）

| 测试 | 产物路径 | 覆盖范围 |
| :--- | :--- | :--- |
| `qwen_free_protocol_test` | `build/artifacts/tests/` | Qwen Free 协议签名（HMAC 测试基元） |
| `qwen_audio_json_test` | 同上 | Qwen Audio JSON/HTTP |
| `llm_refine_test` | 同上 | LLM 纠错请求/响应（仅 `src/core`） |
| `audio_diagnostics_test` | 同上 | 音频诊断与 PCM 度量 |
| `asr_json_protocol_test` | 同上 | 共享 JSON、百度、火山、MAI 助手 |

**均为离线协议级**。UI 布局、文本注入、WASAPI 硬件、托盘、热键钩子**无自动化覆盖**——任何声称"测试全覆盖"的阶段验收都必须先补这部分，或在方案中明确承认其为人工验收。
