# Test Connection 统一架构 · 实施计划

> 状态：**v4 —— 已按外部审查意见修正，待用户确认**
> 日期：2026-09-18
> 修订：v4 —— 采纳外部审查 6 点中的 5 点（3 成立 + 2 部分成立），撤回 v3 的错误判据
> 实测：**由用户本人执行**，AI 不代为实测

---

## 0. 重新校准的立论基础

### 0.1 v3 的错误（已撤回）

v3 提出了一个"规律"：**有批式 `Recognize` 入口的 provider 测试通过，没有的失败。**

**这个规律不成立。** 外部审查提供了反例，我逐条核对源码后**确认反例成立**：

| Provider | 是否有批式 `Recognize` | 实测结果 | 复核 |
| :--- | :--- | :--- | :--- |
| **豆包 IME** | **✗ 无** | **通过** | `doubao_ime_asr.cpp:1748` 用的是 `RealtimeClient` + `SendPcmFrame` + `Finish`，**帧式 API，不是批式** |
| **qwen legacy** | **✗ 无** | **通过** | `qwen_asr.cpp:869` **只调 `Connect()`，一个音频字节都不发** |
| MiMo / MAI / qwen HTTP | ✓ 有 | 通过 | 确实是 `silence → Recognize → ok` |
| qwen streaming | ✗ 无 | **失败** | 自建 Client + 自定义终帧判定 |
| 火山 | ✗ 无 | **失败** | HTTP 探针 + `OpenSession` 双握手 |

**正确的表述**：通过与否**与有没有批式接口无关**。
真正的分野是——**测试代码是否直接复用了生产路径的握手/会话实现**。

- 通过的 5 个：都走的是**生产用的那条连接**（`Connect()` 或 `Recognize()`）；
- 失败的 2 个：都**另外发明了一套连接判定**（自建 client、自定义终帧语义、双握手）。

### 0.2 统一目标形态的修正

v3 写的三行模式：

```cpp
std::vector<BYTE> silence(16000 * sizeof(int16_t), 0);
Result r = Recognize(silence, cfg, 30000);
result.ok = r.ok;                                        // ← v3 写法
```

**这行 `result.ok = r.ok` 在会话族里编译不过。** 外部审查指出这点，我核对确认：

- `AsrSessionResult`（`src/asr/asr_session.h:27-45`）**没有 `ok` 成员**。
  它只有 `text` / `providerName` / `backend` / `isStreaming` / `cloudApiMs` … 等字段。
- 我 v3 里看到的 `r.ok` 来自 **HTTP REST 族的 `Result`**
  （`mai_transcribe.h:28`、`qwen_audio_http.h:32`，两者都有 `bool ok = false;`）。
- 两者被我混为一谈。**这是我的实质性错误，已撤回。**

**修正后的判据**：会话族没有 `ok`，成败必须用**生产路径已有的分类器**判定。

---

## 1. 真正的病因（采纳外部审查第 6 点）

对照生产路径的成败分类器：

```
ClassifyAsrResult(text)            src/asr/asr_result.cpp:59
asr_result_policy::LooksLikeOperationalPrefix(text)   src/asr/asr_result_policy.h:11
    → AsrResultKind::{UsableText, NoSpeech, TooShort, OperationalError, Cancelled}
```

`AsrSessionResult` 的失败是通过 **`text` 前缀**表达的
（`L"ASR failed: ..."`，见 `asr_attempt_manager.cpp:214-274` 的多种文案：
`streaming backend is not available` / `session start failed` /
`streaming fallback audio enqueue failed` / `streaming fallback timeout`）。

**这是生产路径唯一的成败判据，测试路径应当直接复用它。**

| Provider | 失败原因（核对后） |
| :--- | :--- |
| **qwen streaming** | `qwen_audio_streaming.cpp:896` 自建 `r.ok = finished && !receiveTimedOut && error.empty()`。三条件与网络连通性无关，任一不满足即误报失败 |
| **火山** | `volcengine_asr.h:1270` 握手两次：先 GET 探 101，**再 `OpenSession(verifySess, cfg, 15000)`**。第二次失败直接归因为"测试失败"，但生产录音路径并不走这条双握手 |

> 补充证据（火山）：`OpenSession` 内部（`volcengine_asr.h:944-967`）的 init 帧判定用的
> `initResp.receivedServerResponse` + `InitServerError`，这一层判定本身是合理且
> 与生产共用的。问题**不在这一层**，而在于**测试额外套了一层 GET 探针**，
> 以及 `/api/v3/sauc/{mode}` 这个 URL 的构造（`volcengine_asr.h:1148`）与生产握手
> 可能不一致。**具体是探针多余还是 URL 构造有误，需用户实测给出报文后再定位。**

---

## 2. 统一后的目标形态

**原则：测试不发明新判据，只调用生产路径的入口，然后用生产的分类器读结果。**

### 2.1 形态 A —— 批式族（已有 `Recognize`，保持不动）

```cpp
TestResult TestConnection(const Config& cfg) {
    TestResult result;
    const std::vector<BYTE> silence(16000u * sizeof(int16_t), 0);
    const Result r = Recognize(silence, cfg, 30000);
    result.ok = r.ok;                       // ← HTTP 族 Result 有 ok，合法
    result.message = r.ok ? L"Connection OK. ..." : r.error;
    return result;
}
```

现状已是如此（MiMo / MAI / qwen HTTP），**不改**。

### 2.2 形态 B —— 会话族（qwen streaming / 火山）

```cpp
TestResult TestConnection(const Config& cfg) {
    TestResult result;
    // 1. 用当前输入框内容覆盖到一份完整 Config（见 §3.1）
    Config effective = cfg;  effective.<字段> = ...;
    // 2. 1 秒静音走完一次完整会话（复用生产编排）
    const std::vector<BYTE> silence(16000u * sizeof(int16_t), 0);
    const AsrSessionResult r = RunStreamingOnce(effective, silence);
    // 3. 用生产分类器判定，而不是自建 r.ok
    const AsrResultKind kind = ClassifyAsrResult(r.text);
    result.ok = (kind == AsrResultKind::UsableText ||
                 kind == AsrResultKind::NoSpeech);
    result.message = result.ok ? L"Connection OK. ..."
                               : (r.text.empty() ? L"connection test failed" : r.text);
    return result;
}
```

**关键修正点**：

| v3 写法 | v4 写法 | 原因 |
| :--- | :--- | :--- |
| `result.ok = r.ok` | `ClassifyAsrResult(r.text)` | `AsrSessionResult` 无 `ok` 字段（§0.2） |
| `r.error` | `r.text` | `AsrSessionResult` 也无 `error` 字段 |
| 不看 `NoSpeech` | **`NoSpeech` 判为通过** | 静音输入**必然**产生 no-speech；若要求 `UsableText`，静音测试将永远失败 |

> `NoSpeech` 判通过这一点是**本方案成立的前提**。它的合理性在于：
> 能返回 `NoSpeech` 说明 **HTTP 升级成功 + 鉴权通过 + run-task 受理 + 音频被接收 +
> 服务端正常收尾**，即链路完全打通——这正是"测试连接"要证明的东西。

### 2.3 qwen legacy —— 保持"只握手"是有意为之

`qwen_asr.cpp:869` 只调 `Connect()` 即返回 `ok = true`。v3 曾计划"给它补上 `Recognize`"，
**现撤回该改动**：

- 外部审查第 6 点指出这是**可接受的轻量探测**，且用户已实测通过；
- 强行改成完整会话，是把一个**已验证可用**的路径改复杂，违反"只搞简单的握手连接"的用户要求；
- 但需在 `AGENTS.md` 记一条说明：**legacy 的探测强度弱于其他 provider**（只证明握手，不证明识别链路）。

---

## 3. 需要解决的结构问题（采纳外部审查第 2、3、4 点）

### 3.1 配置对象错位（第 2 点 · 接口类型冲突，成立）

外部审查指出：`settings.cpp:3036` 构造的是 `volc_asr::VolcConfig`，不是全局 `Config`；
而 `RunStreamingOnce(const Config&, ...)` 要的是 `Config`。**这个类型冲突是真实的。**

核对结果：

| 位置 | 类型 |
| :--- | :--- |
| `settings.cpp:3036` | `volc_asr::VolcConfig vcfg;`（从输入框逐字段 `GetWindowTextW` 拼装） |
| `RunStreamingAsrOnce` 形参 | `const Config&`（`core/config_store.h` 的全量配置） |

**处置（明确写进改动清单，不留模糊）**：测试时**不再手工拼装 provider 结构体**，
而是：

```cpp
Config effective = g_config;              // 全量配置（含 asrBackend 等路由字段）
effective.volcApiKey    = <输入框值>;      // 仅用输入框值覆盖待测字段
effective.volcResourceId = <输入框值>;
effective.volcMode      = <输入框值>;
// ... 其余 provider 同理
RunStreamingOnce(effective, silence);
```

这样同时解决了路由问题：`CreateStreamingSessionForOneShot` 依赖
`config.asrBackend` 派发（`asr_attempt_manager.cpp:192`，不匹配则 `return nullptr`），
用 `g_config` 派生即可保证路由字段有效。

### 3.2 关于"路由死锁"的反驳（不认同的部分）

外部审查进一步推论：若在**非 settings 场景**下 `g_config.asrBackend` 不是对应值，
`CreateStreamingSessionForOneShot` 会 `return nullptr` → 死锁/空指针。

**这个推论我不认同**，理由：

1. 本方案中 `TestConnection` 的**唯一调用点就在 settings 的测试按钮处理里**
   （`settings.cpp:2974 / 3034 / 3056 / 3082 / 3108 / 3140 / 3160`，
   全部位于 `WndProc` 的 `WM_COMMAND` 分支）。
   不存在"非 settings 场景下调用它"的路径。
2. 测试用的是**输入框当前值**，本就是"用户正在配置的那个 provider"，
   与 `asrBackend` 是否已保存无关——覆盖字段本身就是测试语义。
3. 即便如此，`nullptr` 也有兜底：`RunStreamingAsrOnce:214-274` 在
   `session == nullptr` 时设 `result.text = L"ASR failed: streaming backend is not available"`，
   `ClassifyAsrResult` 会识别为 `OperationalError` → 测试报失败**并给出可读文案**。

**结论**：接口类型冲突要修（3.1），但"死锁"不成立。
不过我会在实现时**加一条防御**：`RunStreamingOnce` 在 `session == nullptr` 时
返回明确的 provider 名 + 错误文案，避免用户看到空洞的失败。

### 3.3 循环包含（第 3 点 · 部分成立）

外部审查称：

> `volcengine_asr.h` → `streaming_oneshot.h` → `volcengine_streaming_session.h` → `volcengine_asr.h`

**这个包含方向读错了。** 核对：

- `volcengine_streaming_session.h`（第 1-15 行）只 include
  `asr_dispatcher.h` + `asr_streaming_session.h`，**没有** include `volcengine_asr.h`；
- 是 **`volcengine_streaming_session.cpp`**（第 17 行）include 了 `volcengine_asr.h`。
  `.cpp` 不参与头文件包含图，**不构成环**。

所以 v3 推荐的**方案 X（`volcengine_asr.h` 不含 `streaming_oneshot.h`，
而由 `volcengine_streaming_session.cpp` 这一侧调用）不会形成环**。

**但外部审查的担忧有一半是合理的**：v3 在"方案 Y"里确实写了
"在 `qwen_audio_streaming.cpp` / `volcengine_asr.h` 内各自写一个小包装"——
若照此在 `volcengine_asr.h` 里 include `streaming_oneshot.h`，**就会成环**。
所以：

- **v4 明确：只走方案 X，彻底删除方案 Y。**
- 并新增机械防线：`volcengine_asr.h` **禁止** include `streaming_oneshot.h`，
  测试入口由 `volcengine_streaming_session.cpp` 提供（它已 include `volcengine_asr.h`，
  方向天然单向）。

### 3.4 全局火山长连接被污染（第 4 点 · 成立，且我此前判错）

外部审查指出：`volcengine_streaming_session.cpp:29` 的
`static volc_asr::VolcSession s_volcSession;` 是**生产语音输入用的后台长连接**，
测试不应触碰它。

我在 v3 的自审里判过"`VolcengineResetForNewSession()` 无害"，依据是它只有两行：

```cpp
s_volcSession.forceAbort = false;
s_volcSession.lastError.clear();
```

**这个判断是错的——错在我问错了问题。** 我问的是"这两行有没有副作用"，
而正确的问题是"**测试凭什么该碰生产的长连接**"。

我从源码上**证实**了这条链路确实存在（不是理论风险）：

```
settings.cpp:3036  构造 vcfg
        ↓
volc_asr::TestConnection(vcfg)              volcengine_asr.h:1121
        ↓  (若按 v3 方案走 OpenSession)
OpenSession(verifySess, cfg, 15000)         volcengine_asr.h:1273
```

而 `ForceAbortAndCloseAll` 的唯一调用点是 `main_window.cpp:616`（`WM_DESTROY`），
**说明 `s_volcSession` 的生命周期是"跨录音会话复用"的全局对象**——
任何测试路径接到它上面，都是在**动生产的连接**。

**处置**：**测试绝不接入 `s_volcSession`。** 火山测试改为使用
**栈上的局部 `VolcSession`**（`VolcSession verifySess;` 已经是局部变量，
但要确保它不通过任何路径引用到 `s_volcSession`）。

**实测验证方法（用户执行）**：测试火山连接前后，
若 `volc_asr_debug.log` 里出现生产会话的 `forceAbort` / 重连记录，即为污染。

---

## 4. 改动清单

| # | 文件 | 改动 | 风险 |
| :-- | :--- | :--- | :--- |
| 1 | `src/asr/streaming_oneshot.h/.cpp` | **新建**：`RunStreamingOnce(const Config&, const std::vector<BYTE>&)`，从 `app` 层精确搬运 | 中（进 CMake + 分层矩阵） |
| 2 | `src/app/asr_attempt_manager.cpp` | 4 个 `static` 符号搬迁至 #1；原函数改为转调 | 中（须行为等价） |
| 3 | `src/asr/qwen_audio_streaming.cpp` | `TestConnection` 改为形态 B（`RunStreamingOnce` + `ClassifyAsrResult`） | 低 |
| 4 | `src/asr/volcengine_streaming_session.cpp` | **新增**火山测试入口（形态 B），不触碰 `s_volcSession` | 中（规则【B】） |
| 5 | `src/asr/volcengine_asr.h` | **删除** `TestConnection`（1121-1318 的探针 + 双握手）；`OpenSession` 等协议逻辑**不动** | 中（规则【B】） |
| 6 | `src/ui/settings.cpp` | 火山/qwen-streaming 测试分支：拼装 `Config`（非 provider 结构体）→ 调新入口 | 低（应净减行数） |
| 7 | `src/asr/qwen_asr.cpp` | **不改**（§2.3，撤回 v3 的改动） | — |
| 8 | `src/asr/asr_runtime_log.cpp` 等 | 日志门控修复 | 低 |
| 9 | `AGENTS.md` | 记 legacy 探测强度说明 + 防环规则 | 低 |

### 明确不做的事（回应"搞得越来越麻烦"）

- ✗ 不新建 core 层 bridge（v2 方案）
- ✗ 不合成扫频音频（1 秒静音即可）
- ✗ 不改 qwen legacy
- ✗ 不动 `volcengine_asr.h` 的协议层（只删 `TestConnection`）
- ✗ 不新建第四种 JSON 取值函数

---

## 5. 风险与前置核实

### 5.1 已完成的前置核实

| # | 项目 | 结论 |
| :-- | :--- | :--- |
| a | `tests/` 是否依赖待删的 `TestConnection` | **无依赖**（grep `TestConnection\|TestResult` 零命中） |
| b | `streaming_oneshot.h` 是否重名 | **无重名**（全库 Glob 零命中） |
| c | `RunStreamingAsrOnce` 依赖闭合性 | **闭合**，全部落在 asr + core（§5.2） |
| d | 探针与录音并发 | **不存在**：`ShowSettingsWindow`（`settings.cpp:3410`）首行 `UninstallKeyboardHook()` |
| e | 诊断副作用 | **不存在**：`CompleteStageIfMissing`（`audio_diagnostics.cpp:1253`）按 `attemptId` 索引，未命中即 `return` |
| f | 搬迁边界 | 4 个符号均为文件作用域 `static`，互不纠缠；`ApplyAsrSessionSideEffects` 为导出函数，`RunStreamingAsrOnce` 不调用它 |
| g | 火山长连接污染 | **确认存在接线风险**，见 §3.4，v4 已封堵 |

### 5.2 不能整文件下沉，只能精确抽取

`asr_attempt_manager.cpp` 整体 include 了 ui/app 层头文件：

| 头文件 | 层 | asr 可否 include |
| :--- | :--- | :--- |
| `hud.h` | **ui** | ✗ |
| `recording_session_controller.h` | **app** | ✗ |
| `asr_attempt_manager.h` | **app** | ✗ |
| `app_messages.h` / `app_state.h` / `selection_context.h` | core | ✓ |

**但 `RunStreamingAsrOnce` 函数本身**的依赖全部落在 asr + core：

| 符号 | 所属层 |
| :--- | :--- |
| `AsrSessionResult`、`IStreamingAsrSession` | asr |
| `CreateQwen/QwenAudio/QwenFree/DoubaoIme/VolcengineStreamingSession` | asr |
| `VolcengineResetForNewSession` | asr |
| `asr_diagnostics::CompleteIfMissingFromText` | asr |
| `g_mainWindow` | **core**（`app_state.h`）✓ |
| `CaptureOneShotStreamingFinal` | 同文件 `static`，随之搬迁 |

**处置**：新建 `src/asr/streaming_oneshot.{h,cpp}`，精确搬运
`OneShotStreamingResult` / `CaptureOneShotStreamingFinal` /
`CreateStreamingSessionForOneShot` / `RunStreamingAsrOnce` 四个符号，
**不得带入任何 ui/app 依赖**。`app` 层原函数改为转调。

### 5.3 剩余真实风险

| 风险 | 评估 | 处置 |
| :--- | :--- | :--- |
| 规则【B】：`volcengine_asr.h` | 中 | 仅**删除** `TestConnection`，不触碰 `OpenSession` 等。实施前已核实 `tests/` 无依赖 |
| `RunStreamingAsrOnce` 行为等价性 | 中 | 下沉后逐行比对；`app` 侧转调，避免逻辑漂移 |
| 基线反弹 | 低 | `settings.cpp` 应为净减；新增 2 文件不得引入 `globals` 相关项 |
| **`NoSpeech` 判通过是否恰当** | **中** | 见 §2.2；若某 provider 在静音下返回其他 kind，需按实测调整 |

---

## 6. 实施顺序

1. 新建 `src/asr/streaming_oneshot.{h,cpp}`，精确搬运 4 个符号
2. `asr_attempt_manager.cpp` 改为转调
3. **`build.bat` + 17 项守卫**（检查点 1：行为等价、基线不升、无越权）
4. qwen streaming `TestConnection` → 形态 B
5. `volcengine_streaming_session.cpp` 新增火山测试入口（局部 `VolcSession`，不碰 `s_volcSession`）
6. `volcengine_asr.h` 删除 `TestConnection`
7. `settings.cpp` 两个分支改为拼装 `Config`
8. **`build.bat` + 守卫**（检查点 2：`SettingsLines` 应下降）
9. 日志门控修复
10. **`build.bat` + 守卫**（检查点 3）
11. 更新 `AGENTS.md`
12. **自审查**

---

## 7. 验收判据（DoD）

1. 千问 streaming 测试通过
2. 千问 legacy 测试仍然通过（且**代码未改动**）
3. 火山引擎测试通过
4. qwen HTTP / MiMo / MAI / 豆包 IME 行为不变（回归）
5. `build.bat` 全量构建通过；17 项守卫全绿；基线不上调
6. 所有 provider 的 `TestConnection` **判据来源统一**（要么批式 `Result.ok`，
   要么 `ClassifyAsrResult`），**无自建判定**
7. 测试与生产**共用同一份识别实现**
8. **火山测试不触碰 `s_volcSession`**（走查可证 + 日志可证）
9. **`volcengine_asr.h` 不含 `streaming_oneshot.h`**（禁环）
10. Debug Mode 开启后 provider 日志有真实内容
11. `AGENTS.md` / `ARCHITECTURE.md` 同步更新

---

## 8. 待用户实测确认（不由 AI 执行）

| 待验证项 | 若不成立的后果 | 备选处置 |
| :--- | :--- | :--- |
| 千问 streaming + 1 秒静音 → `ClassifyAsrResult` 返回 `UsableText` 或 `NoSpeech` | 测试仍失败 | 改用合成音频 / 延长静音 / 加长超时 |
| 火山 + 1 秒静音走完完整会话 | 同上 | 同上 |
| 火山测试前后 `volc_asr_debug.log` 无生产会话污染 | 需重设计隔离方式 | 为测试起独立进程 |
| 其余 4 个 provider 改造后仍通过 | 回归 | 回滚对应改动 |

> **归纳依据**：MiMo（`mimo_asr.cpp:319`）、MAI（`mai_transcribe.cpp:428`）、
> qwen HTTP（`qwen_audio_http.cpp:305`）已用 1 秒静音并通过，用户截图确认 MAI 通过；
> qwen legacy 与豆包 IME 则证明**不用音频也能通过**。
> 但以上均非对 qwen streaming / 火山这两个端点的直接验证。

---

## 9. 与 v3 的差异摘要

| | v3 | v4 |
| :--- | :--- | :--- |
| 立论 | "有批式入口的通过" | **该规律被反例击穿**，改为"是否复用生产路径" |
| 判据（会话族） | `result.ok = r.ok` ❌ 编译不过 | **`ClassifyAsrResult(r.text)`** |
| `NoSpeech` | 砍掉不看 | **判为通过**（静音测试的必要前提） |
| qwen legacy | 给它补 `Recognize` | **不改** |
| 火山测试位置 | `volcengine_asr.h` 内（有成环风险） | **移到 `volcengine_streaming_session.cpp`**，明确禁环 |
| 全局长连接 | 判"无害" | **确认为接线风险**，测试改用局部 `VolcSession` |
| 测试入参 | 未明确 | **明确用 `g_config` 派生 + 覆盖字段** |
| 方案 Y | 保留（"不推荐"） | **彻底删除** |
