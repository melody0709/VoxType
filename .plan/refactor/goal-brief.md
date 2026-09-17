# 重构 Goal 执行简报（供自主执行者使用）

> **用途**：本文件把 `.plan/refactor/cxx23-architecture-refactor-plan.md` 的每个 Milestone 转成**可直接粘贴的 Goal 文本**。
> **背景**：本次重构将由 AI 自主执行，执行者可能在没有现场监督的情况下连续动作。因此每个 Goal 都自带：范围边界、禁止项、验证命令、预期输出、回滚方式、**停止条件**。
> **制定日期**：2026-09-17

---

## 0. 每个 Goal 都隐含的通用约束

以下条款对 P1~P6 全都生效，无需在每个 Goal 里重复粘贴，但执行者必须遵守。

### 0.1 权威文件（先读再动）

| 文件 | 作用 |
| :--- | :--- |
| `.plan/refactor/cxx23-architecture-refactor-plan.md` | 分层契约、CMake 骨架、Milestone 与 DoD。**本简报的上位文件** |
| `AGENTS.md` | 硬约束与踩坑规则（【A】不变量 /【B】条件约束 /【C】实现现状三类） |
| `ARCHITECTURE.md` | 现状结构说明（随每个阶段同步更新） |
| `tools/check_architecture.ps1` | 机械守卫，17 项检查 |

### 0.2 验证命令（每个 Goal 收尾必须逐条执行并贴出输出）

```bat
build.bat --test
tools\check_architecture.ps1 -Stage PX      :: X = 当前阶段号
```

判定：`build.bat` 退出码 0；守卫 `-Stage PX` 输出 `PASS`；且**阶段归一化条件成立**（见各 Goal）。

### 0.3 绝对禁止（违反任一条即视为任务失败）

1. **禁止修改 `tools/check_architecture.ps1` 的基线数值上限**。基线只能下调。
2. **禁止删除、跳过、注释掉任何既有测试**（`qwen_free_protocol_test` / `qwen_audio_json_test` / `llm_refine_test` / `audio_diagnostics_test` / `asr_json_protocol_test`），也禁止修改 `build.bat` 里对它们的调用。
3. **禁止注释掉或移出 `build.bat` 主流程中的守卫调用**。
4. **禁止使用 `file(GLOB ...)` 收集源文件**。
5. **禁止在 `src/` 下新建未在分层矩阵中声明的目录**（合法层：`core` / `platform` / `audio` / `asr` / `ui` / `app`）。
6. **禁止 `git stash`**（本机存在文件系统 atomic-write 问题，历史上曾致 `.git` 损坏）。**禁止 `git reset --hard`、`git checkout -- .`、`git clean -fd`** 这类会丢工作区的操作。
7. **禁止"顺手优化"**：不在本次范围内重命名其他符号、不整体格式化、不调整无关策略常量。
8. **禁止在无测试覆盖时重写协议层**：`src/asr/volcengine_asr.h`、`src/asr/qwen_free_proto_*`、`src/asr/doubao_ime_asr.cpp` 的 protobuf/Opus 部分（见 `AGENTS.md` 踩坑规则【B】）。
9. **禁止破坏【A】类不变量**（`AGENTS.md` 踩坑规则里带【A】标记的条目）：WASAPI 生命周期、重采样相位公式、微信 `WM_CHAR`、FireRedVAD 的 int16 范围、VAD trim 的头尾语义、火山 `connected` 的 `std::atomic`、火山 `context` 必须是 JSON 字符串。
10. **禁止在 `.ps1` 里写非 ASCII 字符**（`build.bat` 用 PowerShell 5.1，无 BOM 的 UTF-8 会被按 ANSI 解码并吞换行）。统计源码禁止用 `Get-Content`，用 `[System.IO.File]::ReadAllLines()`。

### 0.4 提交规范

- 每个 Milestone **一个原子提交**，格式：`refactor(PX): <一句话结果>`。
- 提交前先把回滚锚点写入 `.plan/refactor/rollback-anchors.md`：`PX start = <HEAD sha>`。
- 提交内容必须包含阶段内的**契约文件同步**（`ARCHITECTURE.md` / `AGENTS.md` / 守卫基线），否则该阶段不算完成。

### 0.5 停止条件（遇到即停，向用户报告，不要自行决断）

1. 守卫报出的失败**无法通过正当修改代码解决**（例如它要求的两条约束互相冲突）。
2. 需要**上调任何基线**。
3. 需要**删除或改写测试**。
4. 发现方案与 `AGENTS.md` 的【A】类不变量冲突。
5. 需要新建 `src/` 下的目录层级。
6. `build.bat` 连续两次失败且原因不明确。
7. 任何涉及 `CMakePresets.json` 新增预设或改动 `build.bat` **构建路径**的变更（会波及 `scripts/validate_build_layout.ps1` 与守卫的布局规则，须先确认）。

**报告格式**：`停止原因 / 已完成的改动 / 尝试过的方案 / 建议的下一步 / 需要用户决定的具体问题（附选项）`。

---

## Goal P1：解耦 `globals.h` 的 Include Hub

### 目标

把 `src/app/globals.h` 中 **7 个跨层 `#include`** 从该文件移除，让每个头回到真正使用它的层；`globals.h` 只保留标量常量与 `Config`/extern 声明。

### 范围（只允许动这些文件）

- `src/app/globals.h`
- 因 include 移除而**必须**补 include 的 `.cpp` / 局部 `.h`（逐个最小化补充，不要顺手整理）

### 当前状态（执行前自行复核）

`globals.h` 的 21 个 include 中，7 个属于跨层项目头：

| 归属层 | 头文件 |
| :--- | :--- |
| `asr` | `baidu_asr.h`、`qwen_free_postprocess.h` |
| `audio` | `firered_vad.h`、`audio_diagnostics.h`、`wasapi_capture.h` |
| `core` | `llm_refine.h`、`input_context.h` |

（另有 `resource.h` 属 app 层同层，不动。）

### 禁止

- **不要把这些 include 搬到 `config.h` 或任何新头里集中**——那只是改名字，重编译爆炸圈不变。必须让每个 TU 自己包含它真正需要的头。
- 不要在本阶段拆分 `Config`、不要动 `extern`、不要动 `engine.cpp`（那是 P2）。

### 完成判据

```bat
build.bat --test
tools\check_architecture.ps1 -Stage P1
```

- `-Stage P1` 输出 PASS（`globals.h` 不再含 sherpa / onnx / provider 头）；
- 守卫基线**没有上升**（`globals.h Includers ≤ 22`、`Cross-layer include hub ≤ 7`，理想情况已下降）；
- 全量编译通过（这是本阶段的主要收益，若编译时间可测，记录前后值）。

---

## Goal P2：拆分 `src/audio/engine.cpp`（1497 行）并按职责归位

### 目标

`engine.cpp` 解体为四部分：路径服务 → `voxtype_core`；配置存储 → `voxtype_core`；WaveIn 采集 → `voxtype_audio`；本地 sherpa 引擎 → `voxtype_asr`。同时**清零 `audio → asr` 的 7 处反向依赖**。

### 范围

- 拆分对象：`src/audio/engine.cpp` / `engine.h`
- 新增：`src/core/path_service.{h,cpp}`、`src/core/config_store.{h,cpp}`、`src/asr/engine_local.{h,cpp}`、`src/audio/` 下的采集相关文件
- 配套：`src/audio/engine.cpp` 与 `engine.h` 上引用了 asr 头的 7 处（`engine.cpp` 5 处 + `wasapi_capture.cpp` 2 处）
- `CMakeLists.txt`（按方案 §3 建立 `voxtype_build_flags` 与 5 个 target）

### 关键约束

- **建立 `voxtype_build_flags` INTERFACE 目标**（`/utf-8 /EHsc` + `NOMINMAX`），所有 target 继承。否则 `voxtype_asr` 编译 sherpa 头会直接报 `C2001`（已实测）。
- `voxtype_platform` 若此时仍无源文件，**必须声明为 `INTERFACE`**（STATIC 零源会让 CMake generate 失败）。
- 保留 `TryLoadAsrDlls()` 的 SEH 保护语义与 `/DELAYLOAD` 三个 DLL。
- **`AGENTS.md`【A】WASAPI 生命周期与重采样相位公式必须逐字保持**。

### 完成判据

```bat
build.bat --test
tools\check_architecture.ps1 -Stage P2
```

- `-Stage P2` 输出 PASS（`engine.cpp` 消失；`core/path_service.h`、`core/config_store.h`、`asr/engine_local.h` 就位）；
- 守卫的 `cross-layer include violations` **从 46 下降到 39 或更低**（`audio → asr` 那 7 处清零）；
- 新增 `EXCLUDE_FROM_ALL` 链接冒烟目标，证明 `voxtype_audio` 能独立链接；
- **行为回归**：用 `tools/asr_audio_replay` + `models/**/*.wav` 跑一次本地识别，输出与改动前一致（若该基线尚未建立，**先停下来向用户确认是否先建基线**）。

---

## Goal P3：拆解 `src/app/main.cpp`（2759 行）为 6 个单元

### 目标

按方案 §5 P3 拆出 6 个单元，`main.cpp` 收到 **< 250 行**；其中 HUD 分页单元**同时新增自动化测试**。

### 范围

新增：`src/app/asr_attempt_manager.*`、`src/app/recording_session_controller.*`、`src/app/main_window.*`、`src/ui/hud_pagination.*`、`src/core/debug_logger.*`、`tests/hud_pagination_test.cpp`，并改 `CMakeLists.txt` + `build.bat --test`。

### 关键约束

- **HUD 分页的 4 个常量必须随函数迁入 `src/ui/hud_pagination.h`**：`kStreamingPartialHudMaxLines`、`kStreamingPartialHudMaxScreenFraction`、`kStreamingPartialHudMaxWidthDip`、`kStreamingPartialHudTailChars`。否则测试 TU 会 include `globals.h`，把 sherpa 与全部 provider 头拖进"纯函数单测"。
- `hud_pagination_test` 必须**同时**进入 `CMakeLists.txt` 与 `build.bat --test`（守卫会检查两处）。
- 拆分方式：**先搬移（纯移动 + 补 include），再加测试**，两个独立提交更好。不要在同一步里改逻辑。

### 完成判据

```bat
build.bat --test
tools\check_architecture.ps1 -Stage P3
```

- `-Stage P3` 输出 PASS（`main.cpp < 250` 行；`hud_pagination.h` 与 `hud_pagination_test.cpp` 存在且两处被引用）；
- `hud_pagination_test` 在 `build.bat --test` 中被实际执行且 PASS（不是只编译）；
- 人工核对：录音 → 识别 → 注入 全链路可用（本阶段改动面最大）。

---

## Goal P4：拆解 `src/ui/settings.cpp`（4405 行）并抽离文本注入

### 目标

`voxtype_platform` 落地：注入逻辑从 `settings.cpp` 迁出；`input_context.h` / `selection_context.h` 从 `core` 迁到 `platform`；`settings.cpp` 收到 **< 3500 行**。

### 前置依赖（**不满足则停止**）

`settings.cpp` 目前 include **14 个 ASR provider 头**。若"provider 选项模型下沉 Core"的决策**尚未做出/尚未落地**，则：
1. 本 Goal **只做注入逻辑迁移**（`PasteTextImeAware` → `platform/text_injector.cpp`，`voxtype_platform` 由 INTERFACE 改 STATIC）；
2. **不要**承诺 `settings.cpp < 3500` 这一判据——移除注入后必然因 provider 选项代码回涨。

**执行者必须在动手前确认该决策状态；不确定就停下来问用户。**

### 关键约束

- 【A】微信必须 `WM_CHAR` 逐字符；其它应用剪贴板 + `Ctrl+V` + IMM32。**逐字保持行为**，不要"顺手统一"成一种方式。
- 迁移 `input_context.h` / `selection_context.h` 后，给 `voxtype_platform` 补 `oleacc` / `uiautomationcore` / `oleaut32`。
- 注入行为**有平台差异**，必须做人工核对：微信 / 记事本 / VS Code / 浏览器 四类目标各一次，且在改动前后各跑一次并记录。

### 完成判据

```bat
build.bat --test
tools\check_architecture.ps1 -Stage P4
```

- `-Stage P4` 输出 PASS（`settings.cpp < 3500`、`text_injector.cpp` 存在）；
- 人工核对清单四条全部通过；
- 若采纳了 provider 模型下沉决策：守卫的 `ui → asr` 越权从 14 处降到 0。

---

## Goal P5：消灭 91 条 `extern` 与终结 `globals.h`

### 目标

把 `globals.h` 的 91 条 `extern` 涉及的状态归入其所属控制对象（`RecordingSessionController` / `AsrAttemptManager` / `ConfigStore` 等），改为显式传递或依赖注入，最终**删除 `src/app/globals.h`**。

### 范围

- `src/app/globals.h`（删除）
- 所有因 extern 消失而需要改读写方式的文件（`main.cpp`、`engine_local`、各 session、`hotkey.cpp`、`hud.cpp`、`settings.cpp`）
- `AGENTS.md`（"新增配置项同步 9~10 处"规则改为新模式的写法）
- `ARCHITECTURE.md` L89 附近"全局变量定义在 main.cpp"整段描述

### 关键约束

- 这是**最难、最容易出错**的阶段：状态所有权变化会引入**生命周期与线程安全**问题。建议按"一组相关变量"分批改，**每批一次提交 + 一次 `build.bat --test`**，不要一次全改。
- **注意区分"裸全局"与"有存在理由的全局"**：例如火山 streaming 相关状态有跨录音 session 复用的需求，若拆成成员需额外保证预热连接不失效（见 `AGENTS.md` 踩坑规则【B】/【A】）。
- `globals.h` 里的 `WM_APP` 消息 ID、定时器 ID、窗口类名属于 **App 层工作流概念**，不要放进 `voxtype_core`。

### 完成判据

```bat
build.bat --test
tools\check_architecture.ps1 -Stage P5
```

- `-Stage P5` 输出 PASS（`globals.h` 已删除；包含者 0 / extern 0）；
- 守卫的 `globals.h` 相关三项自动变为 0；
- 人工核对：完整输入法链路 + 托盘 + 设置保存后 ASR 重载 全部可用。

---

## Goal P6：现代 C++23 语法逐文件渐进收敛

### 目标

在依赖完全解耦的前提下，**逐文件**做现代语法替换。允许范围：`std::span<const float>` 替换裸指针切片、Non-RT 路径的 `std::expected<T, AppError>`、`std::jthread` + `std::stop_token`、`std::format` 替换 `snprintf`。

### 排除清单（**不得**以"统一风格"为名去动）

`src/asr/volcengine_asr.h` 协议层、`src/asr/qwen_free_proto_*` 系列、`src/asr/doubao_ime_asr.cpp` 的 protobuf/Opus 部分。理由见 `AGENTS.md` 踩坑规则【B】。

### 关键约束

- **一次一个文件、一次一个提交**。禁止跨全仓替换。
- **RT 路径禁止 `std::expected<T, AppError>`**（`AppError` 含两个 `std::string`，会堆分配）。WASAPI / waveIn 回调只能用无分配的错误表示。
- `std::print` / `std::println` **只能用于 `tests/` 与 `tools/`**——`VoxType` 是 WIN32 子系统，没有控制台。
- 改 `VadTrimCore` 签名为 `std::span` 时，**"追加输出语义"必须保持并补单测**（见 `AGENTS.md`【A】）。
- 改 `wasapi_capture.cpp` 时，**重采样相位公式逐字保持**。

### 完成判据

```bat
build.bat --test
tools\check_architecture.ps1 -Stage P6
```

- `-Stage P6` 输出 PASS（裸指针音频切片签名清零）；
- ASan 预设置若已启用：`x64-asan` 下跑通全部测试；
- 守卫的 `main.cpp` / `settings.cpp` / 跨层越权三项应已处于历史低位。

---

## 附录：一次性交付前的最终检查

```bat
build.bat --test
tools\check_architecture.ps1
tools\check_architecture.ps1 -Stage P6
```

并确认：

- [ ] `globals.h` 已删除，`src/` 分层与 `AGENTS.md` 的矩阵一致
- [ ] `ARCHITECTURE.md` 的 `Source Code Structure` 表与实际文件逐一对应
- [ ] `AGENTS.md` 的【B】【C】类规则已逐条重审（条件解除的改写、与现实不符的删除）
- [ ] `CHANGELOG.md` + `README.md` + `src/app/resource.h` 版本已同步
- [ ] `.agents/` 已纳入版本控制（否则 `modern-cpp23-guidance` 指引会丢）
- [ ] 一次完整的打包验证：`build.bat --package`
