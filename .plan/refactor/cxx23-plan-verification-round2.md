# 第二轮核验：《C++23 架构重构与模块化 AI 执行标准》是否可以开工

> 核验对象：`.plan/refactor/cxx23-architecture-refactor-plan.md`（2026-09-17 06:27 修订版，351 行）
> 核验日期：2026-09-17
> 核验方式：逐条对照磁盘源码、`CMakeLists.txt`、`CMakePresets.json`、`build.bat`、`ARCHITECTURE.md`、`AGENTS.md`；并对本机 MSVC 14.44.35207 + CMake（VS 内置）+ Ninja 做**实际编译 / 实际 configure / 实际运行**验证。每条结论附可复现证据。

---

## 0. 结论

**方案质量相比上一版是量级提升，但还不能开工。** 它把上一轮 12 条意见里至少 10 条真正落进了文档；然而新写的 CMake 骨架与 PCH 规范里，有 **4 处会立刻阻断施工的硬伤**（都是我这轮实测出来的，不是推测），另有 **2 个结构性事实仍未识别**。

三问三答：

| 问题 | 回答 |
| :--- | :--- |
| **是否符合"AI 执行标准"？** | 文档层面**基本符合**（机械契约、DoD、依赖优先顺序都到位）；但**配套的守卫脚本有 4 处结构性缺陷**，其中 1 处是死代码、1 处导致基线数字与文档不一致。守卫不符合。 |
| **可以实施了吗？** | **不可以。**先把第 2 节的 4 条修掉（都在 CMake/构建配置层，改动量约 15 行），再开工 P1。第 3 节的 2 个结构性问题会影响 P2/P4 的可行性，需要在开工前做出决策。 |
| **还需要加强防护边界吗？** | **需要，而且我已落地。**守卫从 6 项扩到 12 项，命名式检查 → 真违规检出（合成工程 6/6 命中），并新增 `-Stage P1..P6` 阶段闸门，让每个 Milestone 的 DoD 第一次变成机器判据。详见第 5 节。 |

---

## 1. 这版真正改对了什么（客观记录）

上一轮我提的意见，这版落地的部分（均已复核属实）：

| 上一轮意见 | 本版落地情况 |
| :--- | :--- |
| `<jthread>` 头不存在 | ✅ §4.1 已改为 `<thread>`，并加了说明 |
| CMake 清单 15 个幽灵文件 / 17 个遗漏文件 | ✅ 已按磁盘真实文件重写。**ASR 库 24 个源文件与磁盘逐一对应、零遗漏零虚增**；audio/ui/exe 三个 target 也准确 |
| 注入实现在 `settings.cpp:902`、托盘在 `hud.cpp`、main.cpp 是 2759 行 | ✅ §1 全部改正，并补了"Header-Only 模块确认"（baidu_asr.h / llm_refine.h / firered_vad.h） |
| 缺机械契约 | ✅ §0 立了"机械契约优先"原则，P0 产出 `tools/check_architecture.ps1` 并挂进 `build.bat --test`（`errorlevel` 正确传播，实测确实能阻断构建） |
| 顺序应按"删依赖"而非"改语法" | ✅ §0.2 与 §5 路线图已改为 P1 拆 hub → P2 engine.cpp → P3 main.cpp → P4 settings.cpp → P5 extern → P6 语法 |
| 错误模型应按线程分层 | ✅ §0.3 双轨错误模型（RT 零分配 / Non-RT `std::expected`） |
| main.cpp 要拆 6 个单元而非 3 个 | ✅ §5 P3 明确 6 单元，且命名与实测函数分布一致 |
| Streaming HUD 分页是现成最佳单测对象 | ✅ 采纳为"特别突破"，要求新增 `tests/hud_pagination_test.cpp` |
| 契约同步应是每阶段 DoD | ✅ §0.4 |
| `/O2` 硬编码与 debug 预设冲突 | ✅ §4.2 改为 `$<CONFIG:...>` 生成表达式（**实测该写法正确**：Ninja 的 FLAGS 行为 `/Od /Zi /EHsc /utf-8`，没有把 `/Od /Zi` 合成单个参数） |
| ASan + `/MT` 可用 | ✅ §4.2 记录 |
| `globals.h` 需拆 include hub | ✅ P1 单独立项 |

一个额外的好判断：§5 P3 把"新增 hud_pagination_test 并纳入 `--test`"写进了退出判据——这是本方案里第一条真正可自动判定的 DoD。

---

## 2. 开工前必须修的 4 处（全部实测，均为硬失败）

### A1. 5 个新静态库没有任何编译选项 → `voxtype_asr` 必然编译失败

**方案原文**（§3）：5 个 `add_library` / `add_executable` 块只写 `target_link_libraries`，**没有一处 `target_compile_options`**。§4.2 只给出了 `VoxType` 一个 target 的编译选项。

**为什么是硬失败**：`voxtype_asr` 必须包含 sherpa 头。实测（MSVC 14.44.35207，Windows SDK 10.0.26100.0）：

```
===== sherpa cxx-api.h + /utf-8 =====
sherpa_test.cpp                                    EXIT=0    （仅若干 C4305 告警）

===== sherpa cxx-api.h 无 /utf-8 =====
cxx-api.h(1):   warning C4819: ... cannot be represented in the current code page (936)
cxx-api.h(579): error C2001: newline in constant
cxx-api.h(581): error C2146 / C2061
cxx-api.h(583..594): error C2059 x5
                                                   EXIT=2
```

注意这不是"非 ASCII 注释"级别的告警——`cxx-api.h` 第 579 行附近有**非 ASCII 字符串字面量**，在 CP936 下被误读后直接吞掉引号，产生 `C2001 newline in constant`。`AGENTS.md` 里"编译需 `/utf-8`"这句是硬约束，而 §3 的骨架把它丢在了 `VoxType` target 上。

**修法**（推荐，一处修全局）：

```cmake
add_library(voxtype_build_flags INTERFACE)
target_compile_options(voxtype_build_flags INTERFACE /utf-8 /EHsc)
target_compile_definitions(voxtype_build_flags INTERFACE NOMINMAX)   # 见 A3
# 每个 target：
target_link_libraries(voxtype_asr PUBLIC voxtype_build_flags ...)
```

v2 守卫已加入硬约束：任何 target 若无 `/utf-8` 且未链接 `voxtype_build_flags`，直接判 FAIL。

---

### A2. `add_library(voxtype_platform STATIC)` 零源文件 → CMake generate 直接失败

**方案原文**（§3）：

```cmake
add_library(voxtype_platform STATIC
    # P4 阶段从 settings.cpp:902 与 selection_context.h 提取:
    # src/platform/text_injector.cpp
)
```

**实测**：

```
CMake Error at CMakeLists.txt:3 (add_library):
  No SOURCES given to target: voxtype_platform
CMake Generate step failed.
```

**修法**：P0~P3 期间把它声明成 `add_library(voxtype_platform INTERFACE)`，到 P4 真正有源文件时再改回 `STATIC` 并加源；或一开始就放进一个占位的 `src/platform/text_injector.cpp`（哪怕只有空实现）并同步进 P4 的迁移目标。

v2 守卫已加入硬约束："任何 `add_library`/`add_executable` 声明为 STATIC/SHARED/MODULE 却无源文件"判 FAIL（合成工程里已实测命中）。

---

### A3. PCH 清单含 `<windows.h>` 而全项目未定义 `NOMINMAX` → 54 处 `std::min/std::max` 编译失败

**方案原文**（§4.1）：

```cmake
target_precompile_headers(voxtype_core PUBLIC
    <windows.h>
    ...
)
```

§4.2 的编译选项里只有 `$<CONFIG:...>`、`/EHsc`、`/utf-8`，**没有 `NOMINMAX`**。

**机制**：MSVC 的 PCH 通过 `/FI` 强制包含头文件。`<windows.h>` 一旦在 PCH 里被解析，`min`/`max` 宏就固定下来了；项目里 45 个源文件各自写的 `#ifndef NOMINMAX / #define NOMINMAX` 此时已经太晚（`windows.h` 的 include guard 已置位），于是 `min`/`max` 宏污染每一个 TU。

**实测**（模拟 `/FI` 强制包含）：

```
===== 无 /FI =====
pch_test.cpp                         EXIT=0
===== /FIwindows.h =====
pch_test.cpp(4): error C2589: '(': illegal token on right side of '::'
pch_test.cpp(5): error C2589: '(': illegal token on right side of '::'
                                     EXIT=2
```

**影响面**：全仓 `std::min`/`std::max` 调用 **54 处，分布在 14 个文件**（实测统计）。

最坏的情况是：这一条在 P6（最后阶段）才会暴露，等于前面 5 个阶段的成果全部卡在最后一步。

**修法**：把 `NOMINMAX` 作为全局编译定义（放进 `voxtype_build_flags` INTERFACE 或顶层 `add_compile_definitions`），或者干脆**不要把 `<windows.h>` 放进 PCH**——后者更干净，因为 PCH 的收益主要来自 STL 与 `<format>`，而 `<windows.h>` 每个 TU 都要按需包含。二者选一，必须显式落地。

v2 守卫已加入硬约束：若存在 PCH 且其中含 `<windows.h>`，而 `CMakeLists.txt` 全文无 `NOMINMAX`，判 FAIL。

---

### A4. §4.2 的测试产物路径改法会**打断现有 `build.bat --test`**

**方案原文**（§4.2）：

> `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/artifacts/tests"`，杜绝 Release 与 Debug 测试产物同名覆盖。

**现状**是 `${CMAKE_BINARY_DIR}/../../artifacts/tests`。这多出来的 `../../` 正是让它落到 `build/artifacts/tests` 的关键，而 `build.bat` 里 5 个测试的路径全部写死指向那里：

```
set "VOXTYPE_TEST_EXE=%BUILD_ROOT%\artifacts\tests\qwen_free_protocol_test.exe"
...\artifacts\tests\qwen_audio_json_test.exe
...\artifacts\tests\llm_refine_test.exe
...\artifacts\tests\audio_diagnostics_test.exe
...\artifacts\tests\asr_json_protocol_test.exe
```

改成方案里的写法后，产物会落到 `<root>/build/cmake/x64-release/artifacts/tests/`，于是：

1. `build.bat --test` 报 `ERROR: ... test executable was not produced`（5 个测试全挂）；
2. 违反 `AGENTS.md` 的布局红线："`build/cmake/x64-release/` 只放 CMake/Ninja 编译中间产物"。

**修法**（隔离意图是对的，但必须是三方同步改）：产物路径改为 `${CMAKE_BINARY_DIR}/../../artifacts/tests/$<CONFIG>`，同时改 `build.bat` 里 5 条 `%BUILD_ROOT%\artifacts\tests\...` 为带配置子目录，并同步 v2 守卫的布局规则与基线。（注意 `${CMAKE_BINARY_DIR}/../../` 在 `--clean` 时与现有清理逻辑一致，别改坏。）

v2 守卫已加入硬约束：所有 `RUNTIME_OUTPUT_DIRECTORY` 解析后必须落在 `build/artifacts/` 之下，否则 FAIL。合成工程里已实测命中方案中的那个写法。

---

## 3. 方案仍未识别的两个结构性事实

### B1. 真实"分层越权"是 **46 处**，不是 22 处；其中 `audio ↔ asr` 目前是**双向环**

方案（和它的守卫）只盯 `globals.h` 的 22 个包含者。但把 include 按**真实归属层**解析后，全仓跨层越权引用共 **46 处**：

| 越权方向 | 处数 | 代表文件 | 与方案 §2.1 依赖矩阵的关系 |
| :--- | :--- | :--- | :--- |
| `asr → app`（globals.h） | 15 | 各 streaming session、proto | 矩阵里 ASR 只许依赖 Core/Audio |
| `audio → asr` | **7** | `engine.cpp`(5)、`wasapi_capture.cpp`(2) | **矩阵禁止**，且与 `asr → audio` 构成**双向环** |
| `ui → asr` | **14** | `settings.cpp`(14) | **矩阵禁止**（"严禁直接依赖 audio"，ASR 同理） |
| `ui → audio` | 3 | `hotkey.cpp` / `hud.cpp` / `settings.cpp` → `engine.h` | 矩阵明文"UI 严禁直接依赖 audio"——**今天就已经在违反** |
| `audio → app`（resource.h 等） | 3 | `engine.h`、`audio_diagnostics.cpp` | 矩阵禁止 |

**这意味着**：方案 §2.1 的目标矩阵在今天的代码里被违反 46 次，而守卫的度量口径只能看到其中 22 次。剩下 24 次（尤其是 `audio → asr` 这 7 处）**完全可以被新代码再复制一遍而不触发任何红灯**。

**为什么 `audio → asr` 特别要紧**：方案 P2 让 `engine.cpp` 解体，但 `engine.cpp` 现在 include 了 5 个 ASR 头（`asr_streaming_session.h`、`qwen_free_postprocess.h`、`qwen_audio_profile.h`、`qwen_special_word_filter.h`、`asr_runtime_log.h`），`wasapi_capture.cpp` 也引 `asr_runtime_log.h`。**独立出来的 `voxtype_audio` 静态库一旦要保持"可单独链接"，这 7 处必须先解决**，否则 P2 的 DoD"`src/audio/**` 不再反向依赖"实际上要求的是先把 ASR 依赖摘干净——这比方案写的"提取 4 类函数"多一步。

### B2. §2.2 的 `IAsrReloadListener` 解决不了 UI→ASR 的真实耦合

方案 §2.2 用一个回调接口来消解"Save 后 Reload"的环。方向对，但**耦合的真实形态不是事件，是数据模型**：

`src/ui/settings.cpp` 直接 include 了 **14 个 ASR 层头文件**：

```
mai_transcribe.h  doubao_ime_asr.h  mimo_asr.h  qwen_asr.h  qwen_audio_http.h
qwen_audio_json.h  qwen_audio_streaming.h  qwen_special_word_filter.h
qwen_free_proto_asr.h  qwen_free_proto_llm.h  qwen_free_postprocess.h
qwen_free_proto_unet.h  qwen_free_proto_utdid.h  volcengine_asr.h
```

原因是 Settings 要渲染各 provider 的选项（端点、token、模型名、上下文开关……），它必须认识这些 provider 的类型与常量。**加一个 `IAsrReloadListener` 并不会减少这 14 个 include 中的任何一个**，因此 §5 P4 的退出判据"`settings.cpp` 纯化为 Direct2D 布局与控件事件响应"在现结构下不可达。

**可行解法**（这也是你上一轮已经得出的结论，正好接上）：把 **provider 选项的数据模型下沉到 Core**，Settings 只依赖 Core 里的描述符/字段注册表，ASR provider 从同一份模型读取。也就是"加一个配置项 = 加一行"的那张表。开工前需要就这件事拍一个决策，否则 P4 会卡住。

---

## 4. 原守卫（v1）的 4 处结构性缺陷

`tools/check_architecture.ps1`（v1，已留档于 `.plan/refactor/archive/check_architecture.v1.other-ai.ps1`）能跑、能阻断构建，这点是实的。但它作为"机械契约"有 4 个问题：

### C1. 分层检查是**死代码**（最严重）

```powershell
if ($raw -match '#\s*include\s*[<"](asr|audio|ui|app|platform)/') {
```

这条正则要求 include 带**目录前缀**。实测：

```
$ grep -rn '#include "' src/ | grep -E '"(asr|audio|ui|app|platform|core)/'
（空）
```

全仓**没有任何一个带目录前缀的项目 include**——CMake 把所有 `src/*` 子目录都加进了 include path，因此代码一律使用裸文件名（`#include "asr_session.h"`）。这条规则**永远不会命中**，而它恰恰是"防止层次倒置"的唯一规则。

只有同一段里的另一半（`src/core` 不得 include `globals.h`）是真有效的。

**证据补充**：合成工程里给 `src/core/utils.h` 注入 `#include "asr_session.h"`，v1 的写法判 PASS；v2 按归属层解析后判 FAIL。

### C2. 用 `Get-Content` 读 UTF-8 源码 → 基线数字系统性偏小

`build.bat` 调用的是 `%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`（Windows PowerShell **5.1**）。PS 5.1 的 `Get-Content` 默认按 **ANSI 代码页（本机为 CP936）** 解码。UTF-8 源码里，**紧跟在非 ASCII 字节后面的换行会被解码器吞掉**，导致行数少算。

实测对照（同一台机器、同一时刻）：

| 指标 | v1 守卫报告 | 真实值（.NET `ReadAllLines` / `wc -l`） | 差值 |
| :--- | :--- | :--- | :--- |
| `main.cpp` 行数 | 2751 | **2759** | 8 |
| `settings.cpp` 行数 | 4394 | **4405** | 11 |
| `globals.h` extern 条数 | 90 | **91** | 1 |
| `globals.h` 包含者 | 22 | **22** | 0 |

机制验证：统计"前一个字节为非 ASCII 的换行"的位置数 → main.cpp 10、settings.cpp 13、globals.h 5，与观察到的偏移同数量级且方向一致。同一文件用 `[System.IO.File]::ReadAllLines()` 读，则精确得到 2759 / 4405 / 91。

**后果**：文档里写的基线（2759 / 4405 / 91）**与守卫实际强制执行的基线（2751 / 4394 / 90）不是同一组数**。`main.cpp` 悄悄长出 8 行不会失败——"只减不增"的承诺有 8 行漏洞。

**同一个坑还咬了我自己一次**：v2 初版我在注释里写中文，脚本直接解析失败——

```
Missing '=' operator after key in hash literal.
The hash literal was incomplete.
```

原因就是中文注释结尾的换行被吞，把下一行代码并进了注释，哈希表字面量从此不再闭合。**这就是为什么 v2 脚本被写成纯 ASCII**（v1 能幸免，只是因为它那些中文行恰好都在注释里，注释合并注释无害）。

> 推而广之：本机所有由 `build.bat` 调用的 `.ps1` 都应遵守"要么纯 ASCII，要么带 UTF-8 BOM"，否则是一颗随机引爆的雷。建议写进 `AGENTS.md`。

### C3. 基线表与实际口径不一致

方案 §1 表格写 `globals.h` 包含者 = "**23 个文件 (22 源码 + 1 测试)**"。实测：

- 严格 `#include` 形式的包含者是 **22 个，全部在 `src/` 下，0 个测试**；
- `tests/asr_json_protocol_test.cpp` 只是**注释里提到** globals.h：

  ```
  // 离线测试桩：不链接真实日志实现（其依赖 globals.h → sherpa-onnx 等重头文件），
  ```

因此文档基线（23）与守卫（22）也不一致。P0 的退出判据"基线严格锁定在 `23 / 91 / 2759 / 4405`"**无法被任何一次守卫运行复现**。

### C4. 只在 `--test` 下运行，且没有任何"阶段完成"判据

- 守卫当前只在 `build.bat --test` 里调用；日常 `build.bat`（更快、AI 更常用）完全不检查，回归可以静默通过。而方案 §5 又要求"所有阶段必须保持 `build.bat` 与 `build.bat --test` 全绿"——**前半句没有执行者**。
- 更本质的问题：ratchet 只表达"≤ 基线"。P1~P5 的 DoD（"globals.h 不再包含 sherpa 头"、"engine.cpp 解体"、"main.cpp < 250"、"settings.cpp < 3500"、"globals.h 被删除"）**没有一条能被这个脚本判定**。也就是说"P0 已完成 ✅"之后，P1 完成的判据仍然是"人觉得改完了"——这正是"AI 执行标准"要消灭的东西。

---

## 5. 我已经落地的加固（全部实测通过）

### 5.1 守卫 v2：12 项检查，正负双向验证

`tools/check_architecture.ps1` 已就地升级（v1 留档于 `archive/`）。改动要点：

| # | 检查项 | 类型 | v1 是否有 |
| :--- | :--- | :--- | :--- |
| 1 | globals.h 包含者 ratchet | 22 | 有（口径修正） |
| 2 | globals.h extern ratchet | 91 | 有（口径修正） |
| 3 | **globals.h 跨层 include hub ratchet** | 7 | 无 |
| 4 | main.cpp 行数 ratchet | 2759 | 有（口径修正） |
| 5 | settings.cpp 行数 ratchet | 4405 | 有（口径修正） |
| 6 | **跨层越权 include ratchet（按归属层解析）** | 46 | 有但是**死代码** |
| 7 | core 零越权（硬约束，无豁免） | 0 | 有（保留） |
| 8 | **无零源文件 target** | 0 | 无 |
| 9 | **所有 target 带 `/utf-8`** | 0 | 无 |
| 10 | **PCH `<windows.h>` 必须配 NOMINMAX** | 0 | 无 |
| 11 | **proto_sign 不得进入生产链接图（含经静态库间接）** | 0 | 有但只查 `add_executable(VoxType)` 的文本，**经 `voxtype_asr` 间接拉入时会漏** |
| 12 | **`RUNTIME_OUTPUT_DIRECTORY` 必须落在 `build/artifacts/` 下** | 0 | 无 |

另外新增 `-Stage P1..P6` 参数，把每个 Milestone 的 DoD 变成机器判据（P1 查 globals.h 是否还带 sherpa/provider 头；P2 查 `engine.cpp` 是否解体 + `path_service/config_store/engine_local` 是否就位；P3 查 `main.cpp < 250` + `hud_pagination_test` 是否同时进了 CMake 与 `build.bat --test`；P4 查 `settings.cpp < 3500` + `text_injector.cpp` 是否存在；P5 查 globals.h 是否已删且包含者归零；P6 查裸指针音频切片签名是否清零）。

**验证结果（正反两组都跑了）**：

```
【正】真实仓库
 All architecture invariants PASSED (12 checks).      EXIT=0
 Ratchet: globals.h includers: 22 / 22
 Ratchet: globals.h extern count: 91 / 91
 Ratchet: cross-layer include violations: 46 / 46

【负】合成工程（注入 6 类违规）
 [FAIL] Hard: src/core has zero upper-layer dependency: 2 violation(s)
     ! src/core/utils.h -> asr_session.h          ← v1 的死正则漏掉的那类
     ! src/core/utils.h -> audio_diagnostics.h
 [FAIL] Hard: no zero-source target: 1 empty target(s)
     ! voxtype_platform declares no sources        ← 对应 A2
 [FAIL] Hard: every target compiles with /utf-8: 2 target(s) missing /utf-8
     ! voxtype_asr / voxtype_platform               ← 对应 A1
 [FAIL] Hard: PCH <windows.h> requires NOMINMAX    ← 对应 A3
 [FAIL] Security: qwen_free_proto_sign ... reachable via production link graph  ← 对应间接拉入
 [FAIL] Layout: RUNTIME_OUTPUT_DIRECTORY ...       ← 对应 A4
 Architecture invariant check FAILED (6/12 checks).  EXIT=1
```

**"守卫必须能失败"这件事我当成交付的一部分**——一个只会 PASS 的守卫等于没有守卫。

### 5.2 `build.bat`：守卫从"仅 `--test`"改为"每次构建"

已把调用从 `--test` 分支移到主流程（紧跟 `validate_settings_layout.ps1` 之后、`write_layout_readme` 之前），全文只保留一处调用，`errorlevel` 照旧阻断。这样日常 `build.bat` 也会拦截回归，同时 P0"挂接守卫"的意图被强化而非削弱。

---

## 6. 建议补上但我没有替你动的防护

1. **静态库独立链接性守卫**（重要）。方案 P2 之后会出现"静态库引用了 exe target 拥有的符号"这类反向依赖（例如 `voxtype_audio` 通过 `#pragma comment(lib)` 与 `engine.cpp` 中的 `LoadConfig/RuntimeAssetDir` 纠缠）。这类问题**不会让主 exe 链接失败**，但会让"给某个库单独写离线测试"直接失败。建议做法：为每个库加一个 `EXCLUDE_FROM_ALL` 的链接冒烟目标（`add_executable(link_smoke_voxtype_audio ...)` + 一个空 `main`），纳入守卫的硬约束。这是成本最低、覆盖最广的一条。
2. **把 `#pragma comment(lib)` 里的库收归 CMake**。现状 `src/` 里有 22 个不同的 `#pragma comment(lib, ...)`（含 `advapi32/oleacc/oleaut32/uiautomationcore/shcore`）。它有两个后果：一是 CMake 的 target 链接列表**看起来**不完整但其实能链上（容易误判）；二是**链接契约无法在构建图层面被检查**——层次规则不只看 include，也要看库依赖。方案 §3 的 `target_link_libraries` 列表目前更像是装饰。
3. **行为回归基线**（P2/P4 之前必须补）。现有 5 个测试全是离线协议级，`P2` 要动 WASAPI 采集、`P4` 要动文本注入，这两块**零自动化覆盖**。仓库里已经具备条件：`tools/asr_audio_replay.cpp`（752 行，多后端回放）+ `models/**/*.wav`。用它们建"黄金样本 → 输出哈希"的基线，比任何架构断言都更能防住重构事故。
4. **commit / 回滚协议**。方案仍缺一节。本机有已知的 `.git` atomic-write 隐患（历史上出现过 `refs/` 丢失、`.pack` 删除后遗留孤立 `.idx`），因此应当明文规定：每阶段一个原子提交、禁止在流程中插入 `git stash`、每阶段开始前记录回滚点。目前工作区里 `AGENTS.md` / `CMakeLists.txt` / `build.bat` / 两个测试文件 都是未提交状态，`tools/check_architecture.ps1` 与 `.plan/refactor/` 还是 untracked ——**"P0 已完成"其实没有任何提交作为锚点**，建议立刻补一次提交。
5. **契约同步**：`AGENTS.md` 需要新增两条——"`.ps1` 必须纯 ASCII 或带 BOM"（见 C2）与"新 target 必须继承 `voxtype_build_flags`"（见 A1）。`ARCHITECTURE.md` 的 `Source Code Structure` 表与 `Text Injection` 章节要跟 P2/P4 同步。

---

## 7. 开工前 checklist

```
[ ] A1 新增 voxtype_build_flags INTERFACE 目标（/utf-8 /EHsc [/DNOMINMAX]），5 个库全部继承
[ ] A2 voxtype_platform 改为 INTERFACE（或补一个真实源文件），确保 configure 通过
[ ] A3 PCH 与 NOMINMAX 二选一：加全局 NOMINMAX，或把 <windows.h> 移出 PCH
[ ] A4 测试产物路径若要按配置隔离，必须同步改 build.bat 的 5 条路径 + 守卫布局规则
[ ] B1 把"跨层越权 46 处"写进方案作为 P1~P5 的真实收敛目标（尤其 audio→asr 的 7 处）
[ ] B2 就"provider 选项模型是否下沉到 Core"拍一个决策，否则 P4 不可达
[ ] C3 修正方案 §1 基线表的 globals.h 包含者：23 → 22（并注明测试文件只在注释中提到）
[ ] C4 在每个 Milestone 的 DoD 后注明 `tools/check_architecture.ps1 -Stage PX` 作为判据
[ ] 提交一次，给 P0 一个可回滚的锚点
[ ] 跑一次：build.bat --test  &&  tools/check_architecture.ps1 -Stage P1（此时 P1 应为 FAIL，预期）
```

做完以上，**P1 就可以开工**。P1 本身（把 9 个跨层 include 从 `globals.h` 摘出去、各归其层）风险很低，而且是后 5 个阶段的前提。

---

## 附录 A：本轮全部证据复现命令

```bash
# 规模与基线（真实值）
python -c "print(sum(1 for _ in open('src/app/main.cpp',encoding='utf-8')))"      # 2759
python -c "print(sum(1 for _ in open('src/ui/settings.cpp',encoding='utf-8')))"   # 4405
grep -c "^extern" src/app/globals.h                                              # 91
grep -rln "globals.h" src/ tests/ tools/                                         # 23 行，其中 22 个是真 include

# v1 死正则（结果为空 = 该规则永不命中）
grep -rn '#include "' src/ | grep -E '"(asr|audio|ui|app|platform|core)/'

# 跨层越权 46 处的解析方式
#   建立 src/**/*.h 的 basename→所属层 映射，再对每个 src 文件解析 include 并比对允许矩阵

# PS 5.1 编码口径差异（2759 vs 2751 / 4405 vs 4394 / 91 vs 90）
[System.IO.File]::ReadAllLines("src\app\main.cpp").Count      # 2759
(Get-Content -Path "src\app\main.cpp").Count                   # 在 powershell.exe(5.1) 下为 2751

# A1 sherpa 头无 /utf-8 = 硬错误
cl /nologo /std:c++latest /EHsc /W3 /c /I<...>/sherpa-onnx/include t.cpp
#   -> error C2001 newline in constant / C2146 / C2061 / C2059

# A3 PCH 强制包含 <windows.h> 破坏 std::min
cl /nologo /std:c++latest /utf-8 /EHsc /W3 /FIwindows.h /c pch_test.cpp
#   -> error C2589 '(': illegal token on right side of '::'

# A2 零源文件静态库
cmake -G Ninja -S . -B build      # add_library(x STATIC) 且无源
#   -> CMake Error: No SOURCES given to target: x

# 守卫（正）
tools/check_architecture.ps1                              # 12 PASS / exit 0
# 守卫（负，合成工程）
tools/check_architecture.ps1 -Root <synthetic>            # 6 FAIL / exit 1
# 阶段闸门
tools/check_architecture.ps1 -Stage P1                    # 当前应为 FAIL（P1 尚未做）
```

## 附录 B：本轮产生的文件变更

| 文件 | 变更 |
| :--- | :--- |
| `tools/check_architecture.ps1` | **重写为 v2**（纯 ASCII，12 项检查 + `-Stage` 阶段闸门 + `-Root` 便于做负向测试） |
| `build.bat` | 守卫调用从 `--test` 分支移到主流程，每次构建都跑（全文仅一处） |
| `.plan/refactor/archive/check_architecture.v1.other-ai.ps1` | v1 原样留档 |
| `.plan/refactor/cxx23-plan-verification-round2.md` | 本文档 |
