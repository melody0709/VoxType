# Qwen Audio 3 超时未触发 fallback — 根因与修复方案

状态：**已实施**（Phase 1 / 2 / 3 已落地，Phase 5 已按收窄范围落地，Phase 4 明确不实施；
`build.bat` 本机未能执行，原因见 §9.6）
日期：2026-09-23
适用基线：工作区当前 HEAD（守卫基线 `0 / 0 / 0 / 148 / 400 / 2`，即
`globals.h` 包含者 / `extern` 数 / 跨层越权 include / `main.cpp` 行数 / `settings.cpp` 行数 / 越界产物）

---

## 0. 结论

1. **直接原因**：看门狗合成的超时文案 `Qwen Audio 3 ASR error: timeout` 不在
   `asr_result_policy::LooksLikeOperationalPrefix()` 的白名单里（白名单写的是
   `Qwen Audio ASR error:`，少一个 `3`）。于是 `ClassifyAsrResult()` 判为
   `AsrResultKind::UsableText`，而 `ShouldRunFallback()` 要求
   `AsrResultKind::OperationalError`，回退被**静默跳过**；同时该文案被当作识别结果
   下发，违反了立项文档"HTTP 失败时 fallback 只使用原始 PCM，并且**不粘贴错误文本**"
   （`.plan/feat/QWEN_AUDIO_3_ASR_INTEGRATION_PLAN.md:709`）。
2. **不是重构引入的**。缺陷诞生于 `9887ab7`（v0.9.24，2026-08-11，*add Qwen Audio 3
   support*）：`ProviderName()` 与白名单条目在同一提交里写成两个不同的字符串。
   C++23 架构重构（P0–P6）只是把这段代码从 `main.cpp` 搬到了 `main_window.cpp`，
   **逐字节未改**。
3. **必修范围**：Phase 1（文案构造）+ Phase 2（表驱动回归测试）。Phase 3 是
   "下次还能查得到"的前提。Phase 4/5 可选，不做也不影响本 bug 的修复。

---

## 1. 判定链（唯一判据）

```
最终文本 text
  └─ asr_result_policy::LooksLikeOperationalPrefix(text)      src/asr/asr_result_policy.h
       ├─ true  → AsrResultKind::OperationalError
       └─ false → 落到 UsableText（除非是 No speech / Too short / aborted）
                    ↓
  ShouldRunFallback(primary, text, ...) 要求 kind == OperationalError
                    ↓                                      src/asr/asr_result.cpp:240
  HandleAsrAttemptFinal → DispatchStreamingFallbackAsync      src/app/asr_attempt_manager.cpp:900-918
```

**误分类是完全静默的**：`src/app/asr_attempt_manager.cpp:907-913` 那条
`event=fallback_suppressed` 日志本身也要求 `kind == OperationalError`，
所以既没有 `fallback_start`，也没有 `fallback_suppressed`。

---

## 2. 实证（编译真实源码，非阅读推断）

用 clang++ 直接编译工作区**真实**的 `src/asr/asr_result.cpp`（只桩掉它唯一的外部符号
`ModelDisplayName`），以用户实际配置
（`asrBackend=qwen` / `qwenModel=qwen-audio-3.0-asr-flash-streaming` / `fallbackAsrBackend=mimo`）
跑 `ClassifyAsrResult()` + `ShouldRunFallback()`：

| 输入文案 | kind | fallback |
|---|---|---|
| `Qwen Audio 3 ASR error: timeout`（看门狗合成，**线上实际值**） | `usable_text` | **SKIP** |
| `Qwen Audio ASR error: timeout`（白名单里真正有的） | `operational_error` | RUN |
| `Local ASR error: timeout` | `usable_text` | SKIP |
| `Baidu Cloud error: timeout` | `usable_text` | SKIP |
| `Microsoft MAI Transcribe 2 error: timeout` | `usable_text` | SKIP |
| `Qwen ASR error: timeout` | `operational_error` | RUN |
| `Doubao IME error: timeout` | `operational_error` | RUN |
| `Qwen IME (Free) error: timeout` | `operational_error` | RUN |
| `ASR failed: VolcEngine timeout` | `operational_error` | RUN |

复现命令见 §8。注意 `std::wprintf` 输出的中文字节在 Git Bash 下可能显示为乱码，
**只看 `kind=` / `fallback=` 两个 ASCII 字段**即可。

只有 `qwen-audio-3.0-asr-flash-streaming` / `qwen-audio-3.0-asr-flash` 的真实出口会中招，
细节见 §4。

---

## 3. 版本考证：不是重构引入的

### 3.1 三要素在 `9887ab7`（v0.9.24）同时存在

```
$ git grep -n 'error: timeout' 9887ab7 -- src/
9887ab7:src/app/main.cpp:2094:  std::wstring timeoutText = std::wstring(providerName) + L" error: timeout";

$ git grep -n 'Qwen Audio 3 ASR' 9887ab7 -- src/
9887ab7:src/asr/asr_session.cpp:302:                  const wchar_t* ProviderName() const override { return L"Qwen Audio 3 ASR"; }
9887ab7:src/asr/qwen_audio_streaming_session.cpp:112:   const wchar_t* ProviderName() const override { return L"Qwen Audio 3 ASR"; }

$ git show 9887ab7:src/asr/asr_result_policy.h | grep -n "Qwen Audio"
18:  || StartsWith(text, L"Qwen Audio ASR error:")     ← 没有 "3"
19:  || StartsWith(text, L"Qwen Audio ASR failed:")
```

**v0.9.24 自带的白名单对该文案返回 0**（用 `9887ab7` 的 `asr_result_policy.h` 编译实测）：

```
v0.9.24 policy: "Qwen Audio 3 ASR error: timeout" -> operational=0
v0.9.24 policy: "Qwen Audio ASR error: timeout"   -> operational=1
```

且 `9887ab7` 的 `ClassifyAsrResult()` 与今天**逐行相同**（空/No speech/Too short →
`LooksLikeOperationalPrefix` → aborted/cancelled → 兜底 `UsableText`）。

### 3.2 看门狗文案本身比 Qwen Audio 3 更早，且重构未改其行为

| 提交 | 日期 | 动作 |
|---|---|---|
| `66f429c` | 2026-06-10 | `refactor: restructure source tree` —— `providerName + L" error: timeout"` 首次出现在 `src/app/main.cpp:787` |
| `9fade0d` | 2026-09-17 | `refactor(P3): decompose main.cpp` —— 原样搬到 `src/app/main_window.cpp:552` |

`9fade0d` 前后该代码块逐字节相同（仅行号不同）：

```
before (9fade0d^ src/app/main.cpp:2544-2555)     after (9fade0d src/app/main_window.cpp:550-561)
  CancelActiveAsrAttempt(attemptId, false);         CancelActiveAsrAttempt(attemptId, false);
  session->Abort();                                 session->Abort();
  std::wstring timeoutText = ... + L" error: timeout";   std::wstring timeoutText = ... + L" error: timeout";
  if (providerName == L"Volcano Engine") { ... }    if (providerName == L"Volcano Engine") { ... }
  msg->source = AsrAttemptFinalSource::Watchdog;    msg->source = AsrAttemptFinalSource::Watchdog;
```

### 3.3 同一个坑在 v0.9.23 已经踩过一次，并且只针对 qwen_free 打了补丁

| 提交 | 日期 | 内容 |
|---|---|---|
| `97c86c2` | 2026-08-06 | v0.9.23 加 qwen_free：白名单**写对了** `L"Qwen IME (Free) error:"`，并同时加了一条回归测试 |
| `9887ab7` | 2026-08-11 | v0.9.24 加 Qwen Audio 3：白名单**写错**，且**没有**加对应测试 |

`97c86c2` 留下的那条测试至今仍在（`tests/qwen_free_protocol_test.cpp:49-51`）：

```cpp
Expect(asr_result_policy::LooksLikeOperationalPrefix(L"Qwen IME (Free) error: timeout"),
       "Qwen Free watchdog failures are operational errors, not text to paste");
```

**这条测试是绿的同时，同一类缺陷在 Qwen Audio 3 上一直存在**——因为它是硬编码单条文案，
没有把"每个后端的 `ProviderName()` 都必须能被分类"变成断言。这正是 Phase 2 要修的东西。

### 3.4 结论

- **与 C++23 架构重构（P0–P6）无关**。重构只搬了位置，没有改行为。
- 缺陷自 **v0.9.24（2026-08-11）** 起一直存在，属于 Qwen Audio 3 立项实现时的遗漏，
  与立项文档 §10.3/§10.4 的验收项（"按现有 operational error 分类进入 fallback"、
  "不粘贴错误文本"）直接冲突。

---

## 4. 影响面与影响时间

**受影响的只有通过流式看门狗产出 final 的出口。** 判据：
`ActivateStreamingSession()` 只在 `src/app/recording_session_controller.cpp` 的
470 / 496 / 526 / 555 四处调用，即 `qwen` / `doubao_ime` / `qwen_free` / `volcengine`
四个流式后端。其中：

| 后端 | 看门狗文案 | 分类 | 状态 |
|---|---|---|---|
| Qwen Audio 3（`qwen` + audio 模型） | `Qwen Audio 3 ASR error: timeout` | `usable_text` | **正在被踩，v0.9.24 起** |
| Qwen 实时（`qwen` + realtime 模型） | `Qwen ASR error: timeout` | `operational_error` | 正常 |
| Doubao IME | `Doubao IME error: timeout` | `operational_error` | 正常 |
| Qwen IME (Free) | `Qwen IME (Free) error: timeout` | `operational_error` | 正常 |
| Volcengine | 被 `main_window.cpp:559` 特判改写成 `ASR failed: VolcEngine timeout` | `operational_error` | 正常 |

`Local ASR` / `Baidu Cloud` / `Microsoft MAI Transcribe 2` 三者的合成文案同样是
`usable_text`（§2 表格），但这三个是 batch 后端，不注册流式 session，
`TakeActiveStreamingSession()` 取不到它们，**当前不可达**。属于潜在雷：
一旦将来给它们加流式实现，就会立刻复现。

**不受影响**的是非超时类失败：`qwen_audio_streaming_session.cpp:70-73` 的
`AudioErrorText()` 会把内部错误统一包装成 `Qwen Audio ASR error: ...`，
前缀在白名单里，回退正常。**所以"回退从来没生效"是过度概括，准确表述是
"凡最终由 `kStreamingWatchdogTimer` 产出的超时，回退一律不触发"**。

**附加损害**：该文案会走 `DispatchAsrFinalText()` 下发 → 注入到焦点窗口，
用户看到的是把报错当文字打进去；且 `IsUsableAsrTextForContext()` 为真，
LLM refine 分支可能对它跑一次纠正（取决于 `enableLlm` 配置）。

---

## 5. 修复方案

### Phase 1（必选）看门狗文案不再用显示名承担分类语义

**改动点**：`src/app/main_window.cpp:555-561`

现状：

```cpp
std::wstring providerName = session->ProviderName();
...
std::wstring timeoutText = std::wstring(providerName) + L" error: timeout";
if (providerName == L"Volcano Engine") {
    timeoutText = L"ASR failed: VolcEngine timeout";
}
```

改为：

```cpp
std::wstring providerName = session->ProviderName();
...
std::wstring timeoutText = L"ASR failed: " + std::wstring(providerName) + L" timeout";
```

- 采用 `L"ASR failed: "` 前缀：**它已经在本仓库白名单里**（`asr_result_policy.h:12`），
  且这正是 `main_window.cpp:560` 早已为 Volcengine 用过的形态——本 Phase 只是把它推广，
  不是引入新约定。
- **同时删除 Volcano 特判**：改写后特判恒等冗余，留着反而会让人以为需要按后端逐条列举。
- 保持"先取 `ProviderName()`、后 `Abort()`"的顺序不变（`session->Abort()` 会 join worker）。
- 保持 `msg->source = AsrAttemptFinalSource::Watchdog` 与
  `allowCancelledAttempt = true` 不变。

**收益**：看门狗路径从此**结构上不可能**再落到 `usable_text`——不管后端叫什么名字、
`ProviderName()` 怎么改。这比"再往白名单里加一条 `Qwen Audio 3 ASR error:`"稳，
后者把同样的耦合留给了下一个后端（§3.3 就是证据）。

**顺带可选**：把 `L"Fallback failed: " + AsrBackendDisplayName(...)`
（`asr_attempt_manager.cpp:403` / `:811`）与
`L"ASR failed: streaming fallback timeout"`（`:272`）核对一遍前缀，均已在白名单内，无需改。

**验收**：见 §6。

---

### Phase 2（必选）表驱动的分类回归测试

**新增**：`tests/asr_result_classification_test.cpp`
（只依赖 `src/asr/asr_result.cpp` + `src/asr/asr_result_policy.h`，属纯函数测试）

内容要求：

1. 把"每个后端的 `ProviderName()` ↔ 它的内建错误前缀"列成一张表，逐条断言
   `LooksLikeOperationalPrefix(prefix + L" ...") == true`。至少覆盖
   `Qwen Audio 3 ASR` / `Qwen ASR` / `MiMo ASR` / `Baidu Cloud` / `Local ASR` /
   `Microsoft MAI Transcribe 2` / `Doubao IME` / `Qwen IME (Free)` / `Volcano Engine`。
2. 断言**看门狗合成文案**（用 Phase 1 的构造方式 + 每个 `ProviderName()`）
   必然 `ClassifyAsrResult(...).kind == OperationalError`。这一条是本 bug 的直接回归护栏。
3. 断言 `volc_asr_debug.log`… 不涉及；但保留 `97c86c2` 原有的两条 qwen_free 断言
   （可迁移到新测试，或在新测试里重复一遍，不必删除旧文件里的）。

**接线**（两处都要改，缺一不可）：

- `CMakeLists.txt`：仿照 `qwen_free_protocol_test` 的写法新增 `add_executable`，
  至少声明 1 个源文件，`target_include_directories` 加 `src/asr` 与 `src/core`，
  并且**必须**显式 `target_compile_options(... /EHsc /utf-8)`（见 AGENTS.md 硬性要求），
  输出目录指向 `${CMAKE_BINARY_DIR}/../../artifacts/tests`，并 `add_test(...)`。
- `build.bat`：`--test` 分支的 `--build --target ...` 列表与运行段都要登记，
  否则新增测试不会被 `build.bat --test` 跑到。

**为什么必须表驱动**：`97c86c2`（v0.9.23）用"硬编码一条 qwen_free 文案"的方式打过一次同类补丁，
5 天后 `9887ab7` 换个后端又漏。单条文案断言不能防同类问题。

**风险**：低。纯新增测试 + 两处构建登记；若 `CMakeLists.txt` / `build.bat` 改错会构建失败，
不会污染运行载荷。

---

### Phase 3（必选）恢复 ASR 运行时日志（"下次还能查得到"的前提）

**现状**：`src/asr/asr_runtime_log.cpp:90/94/98` 的
`SetDebugModeEnabled` / `SetQwenFreeEnabled` / `SetDiagnosticAudioEnabled`
**全仓库只有定义与声明，零调用点**（`grep -rn` 全仓确认，非仅 `src/`）。
`Enabled()` 恒返回 false ⇒ 所有 `asr_runtime_log::Write(...)` 都是空操作：

- `%TEMP%\voxtype_asr_runtime.log` 最后写入停在 2026-09-19 09:44
- Settings「Open log」打开的 `%TEMP%\qwen_asr_debug.log` 走
  `WriteNamedV` → `ProviderDebugEnabled()`，同样恒 false ⇒ **该文件从未生成过**
- `event=fallback_start`、`event=fallback_suppressed`、`event=primary_final`
  这些排查本 bug 最需要的行，全部写不出来

**落点**（4 处，都不需要新增全局变量；`g_enableDebugMode` 已存在，见 `src/app/main.cpp:66`）：

| 位置 | 调用 |
|---|---|
| `src/app/main.cpp:65-66`（`LoadConfig(g_config);` 之后） | `asr_runtime_log::SetDebugModeEnabled(g_config.enableDebugMode);`<br>`asr_runtime_log::SetQwenFreeEnabled(g_config.qwenFreeDebugLog);`<br>`asr_runtime_log::SetDiagnosticAudioEnabled(g_config.diagnosticAudioMode != L"off");` |
| `src/app/main_window.cpp:222-223`（`kReloadMessage` 分支，Settings 保存后重载） | 同上三行 |
| `src/app/main_window.cpp:587-593`（托盘 `ID_TRAY_DEBUG_MODE`） | 只需 `SetDebugModeEnabled(g_config.enableDebugMode);` |
| `src/ui/tabs/` 中修改 `diagnostic_audio_mode` / `qwen_free_debug_log` 的控件 | 可只依赖的重载路径覆盖，不强制逐控件通知 |

**约束**：
- 不新增 JSON 键、不动 `config_registry.cpp` 的注册结构——三个开关只是把已有配置
  同步给日志模块，属于既有字段的传播，不是新配置项。
- 日志路径与 5 MB × (1+2) 轮转策略不变（`asr_runtime_log.cpp:13-14`）。
- 不改 `kMaxLogBytes`，也不把 provider 级 verbose 日志（`WriteNamedV`，
  含 trace id / 原始诊断）与结构化运行时日志混为一谈：前者继续只由 Debug Mode 打开。

**验收**：`build.bat` → 跑一次录音 → `%TEMP%\voxtype_asr_runtime.log` 出现新的
`event=attempt_start` / `event=primary_final` 行。

**风险**：低，但会**恢复日志写入**，意味着 `%TEMP%` 下开始生成/增长文件。
如果用户在意，可把 `SetDiagnosticAudioEnabled` 的开关放宽条件（仅 Debug Mode 生效）——
但这会削弱"诊断产物能自解释"的能力，建议保持。

---

### Phase 4（可选，需测试先行）保留最精确的错误原因

`src/asr/qwen_audio_streaming_session.cpp:386-391`：

```cpp
void DispatchAttempt(bool failed, const std::wstring& error, const std::wstring& text) {
    if (abort_.load()) return;                       // 用户取消 与 finalize 超时 被同等对待
    DispatchFinal(failed ? AudioErrorText(error) : text);
}
```

看门狗是**先** `session->Abort()` **再**发自己那条文案（`main_window.cpp:551-571`），
于是会话本可给出的精确文案
`Qwen Audio ASR error: timed out waiting for task-finished`
（`qwen_audio_streaming_session.cpp:684`）被这一行丢掉，最终只剩泛化的 `timeout`。

- **Phase 1 之后本项不是必需**：回退已能触发，本项只影响"错误原因能不能区分
  连接失败 / 服务端 task-failed / finalize 超时"。
- 若做：需要把 `abort_` 拆成"用户取消"与"finalize 超时"两种语义，或在超时路径
  先 dispatch 再 abort。
- **前置条件**：改动落在流式会话 worker 的生命周期上，属 AGENTS.md 踩坑规则【B】
  （无测试覆盖时不得改写）。必须先在 Phase 2 的测试骨架里补上
  "超时路径 dispatch 发生且文案正确"的用例，再动代码。

---

### Phase 5（可选，需测试先行）超时预算记账

**依据**（v0.9.5 设计文档 `.plan/feat/asr-fallback-backend.md`，当前工作区已不存在，
只能从历史取：`git show ceae849:.plan/feat/asr-fallback-backend.md`）：

> 重要约束：streaming provider 内部 retry/replay 不能在每次 retry 时重新获得一个完整
> `12s` 窗口。**主窗口 post-stop watchdog 是 primary 的总上限**；retry 只能在剩余时间内
> 完成。到点后 abort primary，生成 timeout result，再由 fallback handler 决定是否启动 fallback。
> —— §超时策略

同一文档还要求：

> Qwen/Doubao streaming `SendFinish` 后等 final：用同一个 post-stop final deadline，
> **不能比主窗口 watchdog 更长**。

**当前偏差**：`src/asr/qwen_audio_streaming_session.cpp:131-138` 的
`CurrentWatchdogMs()` 返回 `min(base + 9000, 45000)`，注释说明这 `+9000` 是
"Reserve a bounded connection/replay attempt after the primary task"。但：

- 外层看门狗在停录时被重置为**同一个** `CurrentWatchdogMs()`
  （`src/app/recording_session_controller.cpp:704-709`）；
- 内层 primary final 等待（`qwen_audio_streaming_session.cpp:671-672`）用的**也是**
  这个值，且从 `Finish()` 返回之后才开始计时。

两者相加的结果是：内层 primary 等待本身就吃掉了 `base + 9000` 全部预算，
`qwen_audio_streaming_session.cpp:770-773` 的 replay retry 在实际抢断前几乎跑不完——
与"retry 只能在剩余时间内完成"的记账意图不符。

**若做**：内层 primary final 等待改用基础的
`ComputeCloudAsrStreamingFinalWaitMs(recordingMs_, capturedBytes_)`，
把 `+9000` 真正留给 replay；`CurrentWatchdogMs()` 的返回值只服务外层看门狗。

**明确不做的两件事**：

- ❌ **不**把外层看门狗预算改成"严格大于内层"。那与设计文档"主窗口 post-stop watchdog
  是 primary 的总上限"直接冲突——设计上就是让外层到点抢占、生成 timeout、再交给
  fallback handler。抢占本身是对的，本 bug 的症结从来不是抢占。
- ❌ **不**放宽 `src/app/asr_attempt_manager.cpp:914` 的 `pcm->size() >= 8000` 门槛。
  录音 PCM 充足时**唯一**的阻断点就是分类，改门槛只会掩盖问题、引入新分支。

---

## 6. 验证清单

### 6.1 构建与守卫

```bash
build.bat                  # 必须过 tools/check_architecture.ps1（17 项检查）
build.bat --test           # 必须跑到 Phase 2 新增的分类测试
```

守卫关注项：`globals.h` 包含者 / `extern` 数 / 跨层越权 include / `main.cpp` 行数 /
`settings.cpp` 行数。Phase 1/2/3 都不触碰这些量；Phase 1 若使 `main_window.cpp` 行数变化，
不属守卫基线项，但需确认脚本未把该文件纳入统计。

### 6.2 静态验证（Phase 1 后立即做）

```bash
grep -rn 'L" error: timeout"' src/     # 期望：0 命中
grep -rn 'ASR failed: ' src/           # 期望：看门狗与 Volcengine 出口均走同一前缀
```

### 6.3 动态验证（Phase 1+3 后）

1. Settings 保持用户当前配置：`ASR Backend = Qwen ASR`、
   `Model = qwen-audio-3.0-asr-flash-streaming`、`Fallback = MiMo ASR`。
2. 打开 Debug Mode（托盘），确认 `%TEMP%\voxtype_asr_runtime.log` 开始增长。
3. 构造超时：把 `qwen_audio_streaming_base_url` 指向一个不可达/不回包的地址（或物理断网，
   但注意 `Connect` 阶段失败走的是另一条出口，**要的是"连上后 finalize 不回包"**），
   按住热键录 3–5 秒后松开。
4. 期望日志序列：

```
event=attempt_start attempt=N primary=qwen fallback=mimo
event=primary_final attempt=N backend=qwen kind=operational_error reason=timeout source=watchdog accepted=1
event=fallback_start attempt=N primary=qwen fallback=mimo reason=timeout pcm_bytes=...
event=fallback_final attempt=N backend=mimo kind=usable_text ...
```

5. 期望 UI：HUD 出现 `Fallback... MiMo ASR`，最终注入的是 MiMo 的识别文本，
   **而不是** `ASR failed: Qwen Audio 3 ASR timeout`。

### 6.4 反向验证（防回归）

- 把 `Fallback` 设为 `None`：超时后应正常显示错误文案、不进入回退（行为不得改变）。
- 把 `asr_backend` 切到 `doubao_ime` / `qwen_free` / `volcengine` 各跑一次超时：
  回退行为与改动前一致（这三者原本就正常，改动不得使其退化）。
- 把 `asr_backend` 切到 `qwen` + realtime 模型：`Qwen ASR error: timeout` 仍应分类为
  `operational_error`。

---

## 7. 边界与不做的事

- 不动 AGENTS.md 踩坑规则【B】排除清单里的协议层
  （`src/asr/volcengine_asr.h`、`src/asr/qwen_free_proto_*`、
  `src/asr/doubao_ime_asr.cpp` 的 protobuf/Opus 部分）。
- 不新增 JSON 配置键，不改 `config_registry.cpp` 的注册结构。
- 不动 `build/` 的目录白名单与产物边界；测试只落 `build/artifacts/tests/`。
- 不提高任何守卫基线（相对上调即 FAIL）。
- 不动 `sanitize` / `NormalizeAsrText` / LLM refine 的行为。
- 不把白名单扩成"看起来更全"的大列表而放弃 Phase 1。补白名单是止血，
  Phase 1 才是把"显示名即分类键"这个耦合切断。两者若同时做，补白名单仅作为
  Phase 1 未上前的临时手段，落地后应复核是否仍有必要保留
  （`Qwen Audio 3 ASR error:` 这类前缀在 Phase 1 之后不会再被生产者生成）。

---

## 8. 附录：证据命令

```bash
# 分类实证（工作区真实代码）
clang++ -std=c++23 -DUNICODE -D_UNICODE -I src/asr -I src/core \
  /tmp/vt_fallback_verify/verify.cpp src/asr/asr_result.cpp -o verify.exe && ./verify.exe

# v0.9.24 自带白名单的实证
git show 9887ab7:src/asr/asr_result_policy.h > policy_0924.h   # 再编译调用 LooksLikeOperationalPrefix

# 三要素同时出现
git grep -n 'error: timeout' 9887ab7 -- src/
git grep -n 'Qwen Audio 3 ASR' 9887ab7 -- src/
git show 9887ab7:src/asr/asr_result_policy.h | grep -n "Qwen Audio"

# 重构未改行为
git show 9fade0d^:src/app/main.cpp | grep -n -B4 -A12 'error: timeout'
git show 9fade0d:src/app/main_window.cpp | grep -n -B4 -A12 'error: timeout'

# 同类前例（v0.9.23）
git show 97c86c2:src/asr/asr_result_policy.h | grep -n "Qwen IME"
git log --all --oneline -S "Qwen Free watchdog failures"

# 设计文档（工作区已无此文件，需从历史取）
git show ceae849:.plan/feat/asr-fallback-backend.md

# 日志链路已死
grep -rn "asr_runtime_log::Set" src/
```

---

## 9. 实施记录（2026-09-23）

### 9.1 落地内容

| Phase | 状态 | 改动 |
|---|---|---|
| 1 | ✅ | 新增 `MakeAsrWatchdogTimeoutText()`（`src/asr/asr_result.h` / `.cpp`），看门狗改用它（`src/app/main_window.cpp`） |
| 2 | ✅ | 新增 `tests/asr_result_classification_test.cpp`；`CMakeLists.txt` 新增 `asr_result_classification_test` 目标；`build.bat --test` 登记编译与运行 |
| 3 | ✅ | 新增 `asr_runtime_log::ApplyRuntimeLogConfig(const Config&)`；三个同步点：`main_window.cpp` 的 `WM_CREATE`、`kReloadMessage`、托盘 `ID_TRAY_DEBUG_MODE` |
| 4 | ⛔ 未实施 | 见 §9.4 |
| 5 | ✅（收窄） | `src/asr/qwen_audio_streaming_session.cpp` 拆出 `PrimaryFinalWaitMs()`，内层 final 等待在**启用 fallback 时**只吃 primary 预算 |

`main.cpp` **未改动**（原因见 §9.3）。

### 9.2 与方案的偏差（以实施为准）

1. **Phase 1 保留 Volcano 特判，不删除。**
   方案原写"特判恒等冗余，可删"。核查 `.plan/complete/cloud-asr-architecture-refactor.md:72`
   后确认火山文案 `ASR failed: VolcEngine timeout` 是**既有决定，须保持不变**。
   实现改为：先走 `MakeAsrWatchdogTimeoutText()`，再对 Volcano Engine 覆写为原字符串，
   并在代码里注明该例外的出处。
   顺带修正了一处方案疏漏：Volcano 的 `ProviderName()` 是 `Volcano Engine`，与白名单里的
   `VolcEngine ...` 也不一致 —— 它本来也是同一个坑的受害者，这才是当年加特判的真实原因。

2. **Phase 2 用 `MakeAsrWatchdogTimeoutText()` 而非测试内硬编码模板。**
   `ProviderName()` 分布在各自 `.cpp` 的匿名命名空间里，测试无法直接取用。
   把"文案构造"提为 `asr_result.cpp` 的正式接口后，测试才能真正断言
   "生产者 → 分类器"的往返一致性，而不是复述一份模板。

3. **Phase 3 的启动接线放在 `main_window.cpp` 的 `WM_CREATE`，不放 `main.cpp`。**
   `main.cpp` 的行数棘轮基线是 **150 行且当前已用满**；在 `main.cpp` 加 2 行会正好顶到
   150（勉强通过但零余量，下一次任何改动都会 FAIL）。主窗口在 `LoadConfig()` 之后创建，
   语义等价，且把接线集中在同一文件里更好维护。

4. **Phase 5 收窄到"启用 fallback 时"。**
   方案原写"内层 primary final 等待改用基础值"。直接改会同时把**未启用 fallback** 的
   等待预算从 `legacy + 9000`（8~39s）砍到 `legacy`（8~30s），等于顺手改动一条与
   本 bug 无关的既有路径。实现改为：

   ```cpp
   const DWORD timeout = IsFallbackAsrEnabled(config_)
       ? PrimaryFinalWaitMs()      // 6~12s，把 CurrentWatchdogMs() 的 +9000 留给 replay
       : CurrentWatchdogMs();      // 未启用 fallback：保持旧预算，行为不变
   ```

   外层 `CurrentWatchdogMs()` 的返回值**未改**，总上限仍是 `min(base + 9000, 45000)`
   （第二轮实施后该表达式改由 `ComputeCloudAsrPostStopWatchdogMs()` 计算，
   对 Qwen Audio 3 而言**数值完全不变**；详见 §10.3）。
   注意一个连带效应：replay 重试的时长是 `base`，所以 `base + base` 仍可能超过外层上限；
   届时外层照旧抢占、生成看门狗文案、交给 fallback —— Phase 1 已保证该文案可被分类，
   所以这条链路是自洽的。

### 9.3 明确不做

- **Phase 4（保留最精确错误原因）不实施。** 理由：
  1. Phase 1 之后回退已能触发，Phase 4 只影响错误文案的精度，属体验级收益；
  2. 它要动 `qwen_audio_streaming_session.cpp` worker 里 `abort_` 的语义（区分"用户取消"
     与"finalize 超时"），属 AGENTS.md 踩坑规则【B】：无测试覆盖不得改写；
  3. 会议纪要式的"看起来更好"不值得拿一条录音主链路去换。将来若做，先补
     "超时路径 dispatch 发生且文案正确"的会话级用例。
- 不放宽 `asr_attempt_manager.cpp` 的 `pcm->size() >= 8000` 门槛。
- 不改 `asr_result_policy.h` 白名单（不补 `Qwen Audio 3 ASR error:`）。
  Phase 1 之后不再有生产者生成该形态，补进去只会让人误以为两处字符串需要同步。

### 9.4 验证证据

| 项 | 方法 | 结果 |
|---|---|---|
| 架构不变量 | `tools/check_architecture.ps1`（单独执行） | **17/17 PASS**；`main.cpp` 148/150（余量保住）、`settings.cpp` 377/400、跨层 include 1/2、新增 target 的 `/utf-8`、零源文件、输出目录、必测清单全部通过 |
| 改动文件语法 | clang++ `-fsyntax-only`，改动前后错误集合对比 | 5 个改动 TU 无新增错误；`main_window.cpp` 前后均为同 2 处**既有** clang/MSVC 差异（`text_injector.h` 的 `using` 冲突） |
| 检查覆盖范围 | 反证：在改动行注入 `@@@` 后重跑 clang | 报错定位到注入行 ⇒ 检查确实覆盖被改区域，不是"提前 bail" |
| 真实编译器 | 手工装配 MSVC 14.44.35207 + SDK 10.0.26100.0 环境，`cl.exe /c` 编译 5 个改动 TU | 全部通过（无 error） |
| 新测试可链接 | 同环境 `cl.exe` 链接 `tests/asr_result_classification_test.cpp` + `asr_result.cpp` + `path_service.cpp` | 链接并运行通过 ⇒ 该 target 的源文件集充分 |
| 新测试有效性 | MSVC 产物运行 | `asr_result_classification_test: PASS` |
| 新测试**保护力** | 把 `MakeAsrWatchdogTimeoutText()` 换回 `providerName + L" error: timeout"` 后重跑 | **29 条断言失败** ⇒ 测试对本次缺陷确实会红，不是空转 |
| 既有测试未受影响 | `cl /c` 编译 `tests/asr_json_protocol_test.cpp`、`tests/qwen_free_protocol_test.cpp` | 均通过 |
| 行尾约定 | 逐文件统计 CRLF/LF，与 `.bak` 快照对比 | 无变化；`build.bat` 保持 CRLF（270→278 行）；新增测试文件 LF，与 `tests/` 其余 6 个文件一致 |

### 9.5 未完成 / 待办

- **`build.bat --test` 全量构建未能在本机执行**（原因见 §9.6）。提交前必须由用户执行一次，
  以覆盖 CMake 配置、全部目标编译链接、runtime 安装、`validate_build_layout.ps1`、
  `validate_settings_layout.ps1` 与守卫在构建流程内的联动。
- **未做版本号 bump，未动 `CHANGELOG.md` / `doc/CHANGELOG_zh.md` / 两份 README。**
  `v0.10.9` 已打 tag 并发布（HEAD 即 `Release v0.10.9`），不应回填其小节；
  按 AGENTS.md，这批改动若要发布应整体升到下一版并**同步 4 份文档 + 打附注 tag**，
  属独立的发布动作，交由用户决定。

### 9.6 环境限制（不是代码问题）

本沙箱把 `reg.exe` 列入程序黑名单，`build.bat` 准备 MSVC 环境时依赖注册表查询，
导致 `vcvars`/SDK 路径未装配，全部 TU 报
`fatal error C1083: Cannot open include file: 'windows.h'`。
该拦截无法通过授权绕过（安全策略明示）。因此改用"手工导出 `INCLUDE`/`LIB`/`PATH`
后直接调用 `cl.exe`"的方式完成编译与链接验证（见 §9.4）。
**结论：本机跑不了 `build.bat` 是环境限制，与本次改动无关；`main` 之外的失败特征
（所有 TU 同时找不到 `windows.h`）也印证了这一点。**

---

## 10. 看门狗路径一致性评估（2026-09-23 复盘，未实施改动）

问题来源：本次只修了 Qwen Audio 3。其余 ASR 模块的看门狗路径是否统一、是否需要优化。

### 10.1 结论

**统一的是入口与判据，不是预算。** 五个流式会话共用唯一生产者
`kStreamingWatchdogTimer`（`main_window.cpp`）与唯一分发 `HandleAsrAttemptFinal`
→ `ShouldRunFallback`；Phase 1 之后连文案构造（`MakeAsrWatchdogTimeoutText`）也统一了。
但**停录后的预算有四种截然不同的形态**；第二轮之前其中只有 Qwen Audio 3 带 retry 预留，
第二轮把 `qwen` / `doubao_ime` / `volcengine` / `qwen_audio` 统一到共享函数（见 §10.3）。
`qwen_free` 是唯一保留自有实现的——它的外层本来就大于内层之和，是正确形态（见 §10.3 第 2 条）。

### 10.2 五个流式会话的实际形态（第二轮改动**前**的盘点）

> 下表记录的是第二轮统一之前的形态；改动内容与结果见 §10.3 / §10.4。

| 会话 | ProviderName | 录音期间隔 | 停录后外层预算 | 内部 final 等待 | 内部 retry 等待 | worker 自身 dispatch 在 abort 时 |
|---|---|---|---|---|---|---|
| qwen（realtime） | `Qwen ASR` | 18000 | `StreamingFinalWaitMs`(fallback 启用) / legacy | 同一公式 = base | 同一公式 = base | 不抑制，但被 attempt 级 `cancelled` 拒绝 |
| qwen_audio | `Qwen Audio 3 ASR` | 18000 | `min(base+9000, 45000)` | 本次修后 = base（仅启用 fallback）；未启用 = `CurrentWatchdogMs()` | base | `abort_` 早退（`qwen_audio_streaming_session.cpp:396`） |
| doubao_ime | `Doubao IME` | 18000 | `StreamingFinalWaitMs` / legacy | 同一公式 = base | 同一公式 = base | 不抑制（同 qwen） |
| qwen_free | `Qwen IME (Free)` | 18000 | `2*12000 + 20000 + (LLM?15000:0)` = **44000 / 59000** | `kQwenFreeFinalWaitMs` = 12000 | 同 | `abort_` 早退（多处） |
| volcengine | `Volcano Engine` | 18000 | `VolcFinalizeWaitMs`（= 同一 fallback 感知公式）+ opening 时 `max(…, 8000)` | 自有 | 自有 | 不抑制（同 qwen） |

补充事实（均已对照源码）：

- **"看门狗文案最终胜出"对五个会话都成立**，只是机制不同：qwen_audio / qwen_free 由
  会话内 `abort_` 早退抑制；qwen / doubao / volcengine 不抑制，但看门狗先调
  `CancelActiveAsrAttempt(attemptId, false)` 把 attempt 置为 `cancelled`，
  会话自己那条 final 会在 `PrepareAttemptFinal` 里被 `stale_or_duplicate` 拒绝。
  ⇒ **Phase 1 的修复覆盖全部流式后端**，不是只对 Qwen Audio 3 生效。
- **批处理后端（local / baidu / mimo / mai / qwen batch / qwen-audio-http）没有外层看门狗**：
  `kStreamingWatchdogTimer` 只在 `recording_session_controller.cpp:709` 的流式分支设置。
  它们靠各自客户端的请求超时：mimo / baidu / mai 均显式传
  `ComputeCloudAsrRecordedRequestTimeoutMs(...)`（8~30s，mimo 另 +12000），
  qwen-audio-http 亦显式传 `timeoutMs`；本地解码没有超时，这是设计文档明确要求的
  （"不给 local decode 强行加线程杀死式 timeout"）。⇒ **这是有意的不对称，不是缺陷**。

### 10.3 建议与处置（2026-09-23 第二轮实施）

1. **✅ 已做：qwen / doubao / volcengine 缺 retry 余量。**
   启用 fallback 时它们的外层预算就是 `base`，而内部 primary final 等待也是 `base`
   且从 `SendFinish` 之后才起算 —— 外层必然先到，`RetryRecognitionOnce` 那一轮
   replay 永远跑不完。这与 Qwen Audio 3 修前是同一类问题，只是少了那 9000ms 预留。
   处置：新增纯函数 `ComputeCloudAsrPostStopWatchdogMs(fallbackEnabled, recordingMs,
   pcmBytes, retryReserveMs)`（`src/asr/cloud_asr_common.{h,cpp}`），
   总上限 = primaryWait + 9000ms（上限 45000ms，两个常量沿用 Qwen Audio 3 既有值）；
   `qwen` / `doubao_ime` / `volcengine` / `qwen_audio` 四个会话统一改走它
   （volcengine 在其上叠加自己的 opening 守卫）。
   抽成纯函数的意义：它现在能被离线测试直接覆盖，从而解开 AGENTS.md 踩坑规则【B】
   "无测试覆盖不得改写"的约束。测试见 `tests/cloud_asr_timeout_test.cpp`。
   **只统一算术，不统一 send/drain/retry** —— AGENTS.md「不要轻易做的事」明确要求
   不要把各 provider 的 send loop、drain thread、retry 强行做成复杂模板。
   实现细节（自审修正）：不使用 `std::clamp(total, primaryWait, cap)`，
   因为 `clamp` 要求 `lo <= hi`，一旦将来 `primaryWait` 上限被抬到 cap 之上就是 UB；
   改为显式的"先封顶再取 max"。
2. **⛔ 不改（复核后修正定性）：`qwen_free` 的 44s / 59s 不是缺陷，是唯一把契约写对的实现。**
   `CurrentWatchdogMs()` 在 `stopped_` 分支返回
   `2 * 12000 + 20000 (+ 15000 若启用 bundled 后处理)`。源码注释写明意图：
   *"A stopped recording may first wait for the original final, then reconnect, replay
   and wait for a second final. Keep the main-window watchdog **outside** that whole
   bounded recovery window."* —— 即外层刻意大于内层之和，正是设计文档要求的形态。
   其余四个会话（修改前）恰恰是"外层 = 内层"，才是本次要修的问题。

   可达性与"用户到底等多久"（均对照源码）：

   - **定时器值确实是 44s / 59s**，不是纸面推演：`StopInput()` 会置 `stopped_ = true`
     （`qwen_free_streaming_session.cpp:164`），而 `recording_session_controller.cpp:704-705`
     正是**先 `StopInput()` 再取 `CurrentWatchdogMs()`**，必然命中 `stopped_` 分支。
   - **+15000 只在后处理开启时才加**：`QwenFreeNeedsPostProcess()` =
     rewrite ‖ polish ‖ punct ‖ correct。用户当前配置这四项全为 0 ⇒ 实际是 **44s**。
   - **44s 是兜底上限，不是常态等待**。worker 自身的两段死线是
     `kQwenFreeFinalWaitMs` = 12s：第一段"等原始 final"（`:590`），
     超时后走 `RecoverAndFinalize()`；replay 阶段再给一段 12s（`:750`）。
     也就是说 provider 卡住时，失败结果通常由这两段 12s 先产出（≈24s + 重连开销），
     44s 只在 worker 记账本身卡死时才成为用户等待。
   - `kQwenFreeReconnectBudgetMs = 20000` 全文件**只出现在 `CurrentWatchdogMs()` 里**
     （无独立死线），它是"重连 + replay 传输开销"的预留估计，属于外层预算的一部分。

   为什么仍然不改：缩短它只有两条路——(a) 收窄 recovery 预留 20s，
   但两段 12s 内层死线不动，对用户可见等待几乎无改善；
   (b) 收窄 `kQwenFreeFinalWaitMs`，那会同时降低"等原始 final"的容忍度，
   在健康但慢的服务端上误判 timeout、把已经拿到的好结果换成 fallback。
   收益有限而风险真实，且该后端不在当前配置中启用（`asr_backend=qwen` +
   audio streaming，fallback=mimo），优先级最低。**结论：保持现状。**
3. **✅ 已做（文档级）：批处理后端的超时是隐性契约。**
   已在 AGENTS.md「新增云端 ASR 接入规则」补两条：
   (a) streaming 停录预算统一走 `ComputeCloudAsrPostStopWatchdogMs()`，
   session 内部等待不得复用 `CurrentWatchdogMs()`；
   (b) 新增 batch 后端必须显式传入请求超时，否则该 attempt 会永久挂起、
   回退永不触发、且 `g_activeAttempt` 一直被占用（本地解码是唯一的有意例外）。
4. **⛔ 不做**：把五个会话的 `CurrentWatchdogMs()` 合并成一个实现。
   `qwen_free` 与 `volcengine` 的相位结构（ASR final + LLM 后处理 / opening 守卫）
   本质不同，强行统一会制造模板复杂度，违反上面那条项目规则。

### 10.4 第二轮验证证据

| 项 | 方法 | 结果 |
|---|---|---|
| 架构守卫 | `tools/check_architecture.ps1` | **17/17 PASS**；`main.cpp` 148/150（未增长）、`settings.cpp` 377/400、跨层 include 1/2、两个新增 target 的 `/utf-8` 与零源文件检查均通过 |
| 改动编译 | MSVC 14.44.35207 + SDK 10.0.26100.0，`cl /c` 编译 `cloud_asr_common.cpp` + 4 个会话 | 全部通过 |
| 新测试 | `cloud_asr_timeout_test`（MSVC 编译链接后运行） | `PASS` |
| 新测试保护力 | 去掉 `primaryWait + retryReserveMs` 里的预留后重跑 | **35 条断言失败** |
| 既有测试未受影响 | `cl /c` 编译 `qwen_audio_json_test.cpp`、`asr_json_protocol_test.cpp`、`qwen_free_protocol_test.cpp` | 均通过 |
| 版本一致性 | `resource.h` 0.11.0 ↔ 两 README ↔ 两 CHANGELOG；全仓检索残留 `0.10.9` | 仅剩历史 CHANGELOG 小节，无残留；打包版本由 CMake 从 `resource.h` 派生，单一改动点成立 |

---

## 12. 外部审查（GPT）后的复核与修复

审查结论：分类链修复、日志接线、保留火山文案均判定正确；预算改动提出两点，要求合并前处理。

### 12.1 两点均**成立**（已独立复核，含前提验证）

**P1 — 9 秒预留不足以完成长录音的 replay。**

- 前提验证（关键）：replay 确实是**实时节奏**重发，不是一次性灌完。
  `qwen_streaming_session.cpp` 的发送循环每块 `Sleep(clamp(chunkMs,20,1000))`；
  `qwen_audio_streaming_session.cpp` 处还有显式注释：
  *"Replay the same real-time cadence as the primary stream. A burst upload can trigger
  provider-side backpressure and create a misleading task failure even when the socket
  is healthy."* ⇒ 重发 N 秒录音≈N 秒。
- 30 秒录音、启用 fallback：外层 = `StreamingFinalWaitMs`(12s) + 9s = **21s**，
  primary final 可等 12s，留给重发约 9s，而重发本身需约 30s ⇒ **必被外层 Abort**。
  连"早期连接失败"这一剩余预算最充裕的时刻也装不下（21s < 30s+12s）。
- 我的**表述错误**：CHANGELOG / ARCHITECTURE 里"让内部 replay 真正跑得完
  / so the internal replay actually gets to run"是过头话。实际只有音频约 1.5 秒以内的
  录音才装得下。已全部改写。
- 更严重的连带影响（我在复核中补充的）：该改动会让**每次**超时都先启动一次注定失败的
  replay，把 fallback 推迟约 9 秒 —— 即净效果可能是负的。

**P2 — 未配置 fallback 时 qwen_audio 仍把预留全部用于首次 final 等待。**

- `const DWORD timeout = IsFallbackAsrEnabled(config_) ? PrimaryFinalWaitMs() : CurrentWatchdogMs();`
  在 fallback 关闭时取 `CurrentWatchdogMs()`，而这个值**就是**外层看门狗的总预算
  （30s 录音时两者都是 39000ms）；内层起算更晚 ⇒ 外层必先 Abort，预留作废。
- 属既有问题未修尽，不是本轮引入。同意改为**始终**使用 `PrimaryFinalWaitMs()`。

**另一点（P3，提示级）**：生产调用都传 9000，而 primary 上限 30000 ⇒ 45000 封顶分支在
生产中不可达。属实。保留为防御性上界（测试已显式覆盖它只在超大预留时触发），并在
`cloud_asr_common.h` 注明；不视作缺陷。

### 12.2 已实施

1. `cloud_asr_common.{h,cpp}` 新增纯函数
   `EstimateCloudAsrReplaySendMs(replayBytes)`（= bytes/32，即实时重发时长）与
   `CloudAsrReplayFitsInBudget(remainingBudgetMs, replayBytes, retryWaitMs, safetyMs)`，
   常量 `kCloudAsrReplayFitSafetyMs = 1500`（连接握手 / finish 发送 / 调度抖动）。
2. `StreamingAsrSessionBase` 新增共享门控
   `ShouldStartFailureReplay(backend, site, totalBudgetMs, stopTick, replayBytes, retryWaitMs)`
   与 `RemainingPostStopBudgetMs(totalBudgetMs, stopTick)`，并在不通过时写
   `event=replay_skipped ... reason=insufficient_budget`。集中一份实现，避免三个会话各写一份后漂移。
3. `qwen` / `qwen_audio` / `doubao_ime` 各自：`StopInput()` 记录 `stopTick_`、
   新增 `ReplayWaitMs(bytes)`、在**失败路径**接入门控。
   - `qwen_audio` 覆盖两处：连接失败 replay 与 final 失败 replay；
     门控不通过时自然落入既有 `else` 分支（下发连接错误）→ fallback。
   - 空结果路径一律**不门控**（理由见 §12.3）。
   - `volcengine` 只有空结果 replay 一个调用点 ⇒ 不接入，保持原状。
4. P2 修复：`qwen_audio` 内层"等 final"改为始终 `PrimaryFinalWaitMs()`。
5. 测试扩展：`tests/cloud_asr_timeout_test.cpp` 新增 4 组断言，含
   **"30 秒录音绝不可能在 9 秒预留内跑完一次 replay"** 这条回归钉、
   1ms 边界、以及 `EstimateCloudAsrReplaySendMs` 的换算。
   反证：把 `CloudAsrReplayFitsInBudget()` 改成恒真 ⇒ **5 条失败**。
6. 文档同步：两 CHANGELOG（改写过头表述 + 新增门控与 P2）、两 ARCHITECTURE
   （模块表 + 预算段落）、`AGENTS.md`（新增 replay 门控规则）。

### 12.3 残留的已知取舍（明确记录，不隐瞒）

- **空结果路径仍有约 9 秒的多等**：空 final（未失败）时 replay 不门控，长录音下它同样
  跑不完，最终靠外层看门狗抢占 → fallback。之所以不门控：跳过它会把一次可疑的空 final
  直接变成 `No speech detected` 且**不给 fallback 机会**；而将其改写成运维错误又会为
  真实静音场景白跑一次 fallback。这是有意选择的取舍，不是遗漏。
- **门控后的净效果**：失败路径从"启动必然被抢断的重试、晚 9 秒进 fallback"
  变为"立即进 fallback，且错误文案更精确（provider 自己的错误而非泛化 timeout）"；
  只有音频约 1.5 秒以内的录音还能真正跑完 replay。这与设计文档"fallback 启用时
  primary post-stop 最多等 6–12s，到点即走统一 timeout handler 再启动 fallback"
  的取向一致——**偏向快速 fallback，而非长时间同引擎重试**。
- 未做：动态预留（`reserve = replaySend + retryWait`）。它能让长录音的 replay 跑完，
  但会把 fallback 推迟到 25s+ 量级，与上面那条设计取向冲突，故不采纳。


---

## 11. main.cpp 行数棘轮：核对结论（2026-09-23）

- **事实核对**：`MainLines` 基线常量 = **150**，当前实测 = **148**，即**余 2 行**。
  本次实施过程中我曾一度报成"150 且已用满"—— 那是加上临时 2 行后的瞬时读数，
  已回退，**该说法作废**。基线自 P0（`847926b`）建立后从未调整过
  （`git log -S "MainLines" -- tools/check_architecture.ps1` 只有这一个提交）。
- **不建议上调。** 理由：
  1. 脚本注释写明 `Decrease-only`，且守卫有 "baselines were not raised vs HEAD"
     的防绕过检查 —— 上调正是它要拦的动作；
  2. `main.cpp` 是启动编排文件，其 DoD 一直是"只减不增"；新代码应进入对应模块
     （本次把日志接线放进 `main_window.cpp` 的 `WM_CREATE` 就是按这条做的）；
  3. 2 行余量足够容纳偶然的 include + 调用；更大的东西本就不该放在这里。
- **可选的反向调整**：把 `MainLines` 由 150 **收紧到 148**，让棘轮跟上已达成的下限
  （这才是 decrease-only 的常规做法）。代价是零余量 —— 之后往 `main.cpp` 加 1 行都会
  失败，必须先等量删减。**权衡结果：建议保留 150 作为 2 行缓冲**，不收紧也不上调。
- **顺手修正的文档不一致（已改）**：`AGENTS.md:34` 原写"当前最新基线 `0/0/0/148/400/2`
  … `settings.cpp` 降至 389 行"，与脚本不符（脚本是 `150 / 400 / 2`，实测
  `main.cpp` 148、`settings.cpp` 377、跨层 include 1）。已改为分别列出
  **基线常量**与**当前实测值**，并注明"只降不升"。


