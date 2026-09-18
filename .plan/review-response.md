# 对审查意见的回应（供转交）

> 日期：2026-09-18
> 关于：`.plan/fix-test-connection-probe-plan.md`（已更新为 v4）

---

## 一、总体结论

**6 点意见中，我采纳 5 点（3 点完全成立、2 点部分成立），1 点不认同其推论。**

| # | 意见 | 我的裁决 | 处置 |
| :-- | :--- | :--- | :--- |
| ① | `AsrSessionResult` 无 `ok` 字段 | **完全成立** | 已改：会话族判据换成 `ClassifyAsrResult(r.text)` |
| ② | 配置对象错位 | **类型冲突成立，但"死锁"推论不成立** | 已改：明确用 `g_config` 派生 + 覆盖字段 |
| ③ | 循环包含 | **包含方向读错，但担忧有一半合理** | 已改：只走方案 X，明确禁环 |
| ④ | 全局长连接被污染 | **完全成立，且我此前判错** | 已改：测试改用局部 `VolcSession` |
| ⑤ | 立论前提被击穿 | **完全成立** | 已改：撤回"批式入口"规律 |
| ⑥ | 真正病因与正解 | **病因诊断成立；"正解"部分保留意见** | 见下第四节 |

---

## 二、完全认同的三点（已改）

### ① `AsrSessionResult` 无 `ok` 字段

**你说的对，这是我的实质性错误。**

我核对：

- `src/asr/asr_session.h:27-45` 的 `AsrSessionResult` 确实**没有 `ok` 成员**，
  只有 `text` / `providerName` / `backend` / `isStreaming` / `cloudApiMs` / `pcmBytes` …；
- 我 v3 里的 `result.ok = r.ok`，那个 `r.ok` 来自 **HTTP REST 族的 `Result`**
  （`mai_transcribe.h:28`、`qwen_audio_http.h:32` 都有 `bool ok = false;`）。

**我把两个不同的结构体混为一谈，导致计划里写了编译不过的代码。已全部撤回。**

v4 的会话族判据改为复用生产路径既有的分类器：

```cpp
const AsrResultKind kind = ClassifyAsrResult(r.text);   // src/asr/asr_result.cpp:59
result.ok = (kind == AsrResultKind::UsableText ||
             kind == AsrResultKind::NoSpeech);
result.message = result.ok ? L"Connection OK. ..." : r.text;
```

### ④ 全局长连接被污染 —— 我此前判错了

**你指出这一点是对的，而且我 v3 自审时问错了问题。**

我当时的推理是"`VolcengineResetForNewSession()` 只有两行，无害"：

```cpp
s_volcSession.forceAbort = false;
s_volcSession.lastError.clear();
```

**我问的是"这两行有没有副作用"，而正确的问题是"测试凭什么该碰生产的长连接"。**

从源码上看，这条链路是真实存在的（不是理论风险）：

```
settings.cpp:3036   构造 vcfg
        ↓
volc_asr::TestConnection(vcfg)          volcengine_asr.h:1121
        ↓
OpenSession(verifySess, cfg, 15000)     volcengine_asr.h:1273
```

而 `VolcengineForceAbortAndCloseAll()` 的**唯一调用点是 `main_window.cpp:616`（`WM_DESTROY`）**
——这反过来说明 `s_volcSession` 是**跨录音会话复用**的全局对象。
任何测试路径接到它上面，就是在动生产的连接。

**v4 已封堵**：火山测试迁到 `volcengine_streaming_session.cpp`，使用**栈上局部 `VolcSession`**，
并在 DoD 里加了可证伪的验收项（走查 + 日志双证据）。

### ⑤ 立论前提被击穿

**你举的两个反例我逐条核对，都成立：**

| Provider | 我原以为 | 实际 |
| :--- | :--- | :--- |
| 豆包 IME | 有批式接口 | **没有**。`doubao_ime_asr.cpp:1748` 用的是 `RealtimeClient` + `SendPcmFrame` + `Finish`，帧式 API |
| 千问 legacy | 有批式接口 | **没有**。`qwen_asr.cpp:869` **只调 `Connect()`，一个音频字节都不发** |

所以我 v3 那个"有批式 `Recognize` 入口的通过、没有的失败"的规律**站不住**。
已撤回，改为：**分野在于"测试是否复用生产路径的握手/会话实现"**。

---

## 三、部分认同的两点

### ② 配置对象错位 —— 类型冲突成立，"死锁"不成立

**类型冲突部分：你说的对，我采纳。**

`settings.cpp:3036` 构造的是 `volc_asr::VolcConfig`（从输入框逐字段 `GetWindowTextW` 拼装），
而 `RunStreamingOnce(const Config&, ...)` 要的是 `Config`。**这个冲突是真的。**
v4 已明确处置：不再手工拼装 provider 结构体，改为

```cpp
Config effective = g_config;              // 全量配置，含路由字段
effective.volcApiKey = <输入框值>;         // 仅覆盖待测字段
```

**但"路由死锁"这个推论我不认同，理由三条：**

1. **本轮方案中 `TestConnection` 的唯一调用点就在 settings 的测试按钮里**
   （`settings.cpp:2974 / 3034 / 3056 / 3082 / 3108 / 3140 / 3160`，
   全部位于 `WndProc` 的 `WM_COMMAND` 分支）。
   **不存在"在非 settings 场景下调用它"的路径**，所以"路由前提不成立"这个场景构造不出来。

2. 即便按你的构造，`CreateStreamingSessionForOneShot` 返回 `nullptr` 时，
   `RunStreamingAsrOnce:214-274` 会设
   `result.text = L"ASR failed: streaming backend is not available"`，
   `ClassifyAsrResult` 识别为 `OperationalError` → **测试报失败并给出可读文案**，
   不是死锁，也不是空指针崩溃。

3. 测试用输入框当前值覆盖字段，本就是"测我正在配置的那个 provider"的语义，
   与 `asrBackend` 是否已保存无关。

**不过你这个质疑有价值**：它暴露了我原文没写清"测试入参怎么来"。
v4 已把这条写成显式改动项（§3.1），并额外加一条防御：
`RunStreamingOnce` 在 `session == nullptr` 时返回带 provider 名的明确文案。

### ③ 循环包含 —— 包含方向读错，但担忧有一半合理

**你给的环是：**

```
volcengine_asr.h → streaming_oneshot.h → volcengine_streaming_session.h → volcengine_asr.h
```

**这个链路在源码上不成立。** 核对：

- `volcengine_streaming_session.h`（第 1-15 行）只 include
  `asr_dispatcher.h` + `asr_streaming_session.h`，**没有** include `volcengine_asr.h`；
- 是 **`volcengine_streaming_session.cpp`**（第 17 行）include 了 `volcengine_asr.h`。
  **`.cpp` 不参与头文件包含图，不构成环。**

所以我推荐的**方案 X（测试入口放 `volcengine_streaming_session.cpp` 一侧）
不会形成环**——事实上它恰恰是打破你担忧的那种结构。

**但你的担忧有一半得算数**：我 v3 在"方案 Y"里确实写了
"在 `qwen_audio_streaming.cpp` / `volcengine_asr.h` 内各自写一个小包装"。
**如果真按方案 Y 在 `volcengine_asr.h` 里 include `streaming_oneshot.h`，就会成环。**

v4 已把**方案 Y 彻底删除**，并加机械防线：
`volcengine_asr.h` 禁止 include `streaming_oneshot.h`，且列入 DoD 第 9 条。
这一条算你帮我把一个隐患提前掐掉了。

---

## 四、不认同的部分（第六点的"正解"）

第六点把"真正病因"和"正解"合在一起讲。**病因诊断我认同，正解我有保留。**

### 4.1 病因诊断：认同

**千问 streaming**：`qwen_audio_streaming.cpp:896` 的
`r.ok = finished && !receiveTimedOut && error.empty()`
——三个条件都与"网络是否连通"无关，任一不满足就误报失败。**这个诊断成立。**

**火山**：`volcengine_asr.h` 里测试先 GET 探 101、再 `OpenSession` 的双握手。
**"多余的第二次握手"这个方向我也认同。**

### 4.2 正解一：千问 streaming 改成"握手即通过"—— 不认同

你的建议是学豆包 IME / 千问 legacy，"收到 task-started 就足以证明连通"。

**我不认同，因为它退回到"弱探测"，而且与我提出的统一架构相冲突。**

理由三条：

1. **用户明确的指示是统一架构，不是"每个 provider 各自最省事"。**
   您的建议会导致：
   - 火山 = 握手即通过
   - 千问 streaming = 握手即通过
   - 千问 legacy = 握手即通过
   - MiMo / MAI / qwen HTTP = 完整识别
   这是**四种不同的探测强度**，正是用户要消除的那种分裂。

2. **握手成功不等于识别链路可用。** 千问 streaming 的生产路径里，
   鉴权通过之后还有 `run-task` 受理、音频接收、服务端收尾。
   只测握手，会漏掉"Resource ID / model 配置错"这类**只有在发音频后才会暴露**的问题。
   反过来，火山测试当初加第二次 `OpenSession` 的注释写得很清楚：
   > "HTTP 101 only proves the upgrade succeeded; invalid resource/mode values are
   > 升级之后经 error frame 返回。"

   也就是说——**这个"多余的第二跳"是为了抓配置错误而故意加的。**
   直接删掉它会丢掉一个真实的检测能力。

3. **我认同的是"统一"，不是"都降级到握手"。**
   我 v4 走的是反向：**把两个失败的提到和其他具备完整测试的 provider 同级**
   ——发一段静音、走完一次真实会话、用生产分类器判定。
   这样 7 个 provider 的探测强度**全部一致**（都走完整链路），
   而 legacy 的"只握手"保留为**已知的、需记录的例外**（§2.3）。

   > 补充：legacy 保留"只握手"不是因为它是正解，而是因为它**已经实测通过**，
   > 用户要求"只搞简单的握手连接"、不要越改越复杂。这是**尊重既有可用状态**，
   > 不是把它当作设计范式去推广。

### 4.3 正解二：火山删掉第二次 `OpenSession` —— 部分认同，但不建议先删

**问题不在"要不要删"，而在"删之前得先确定失败发生在哪一跳"。**

我核对 `OpenSession` 内部的 init 帧判定（`volcengine_asr.h:944-967`）：

```cpp
const bool initServerError =
    initResp.text.find(L"[VolcEngine error:") != std::wstring::npos;
if (!initResp.receivedServerResponse || initServerError) {
    sess.lastError = initServerError ? initResp.text
        : L"VolcEngine init response timed out or was invalid";
    ...
}
```

**这一层判定本身是合理且与生产共用的**——它区分了"服务端明确报错"和"超时无响应"。
所以失败原因可能在三处，删第二跳只解决其中一种：

| 可能原因 | 是否靠删第二跳解决 |
| :--- | :--- |
| (a) 第二次握手多余、触发限流 | ✓ |
| (b) `/api/v3/sauc/{mode}` 的 URL 构造与生产不一致（`volcengine_asr.h:1148`） | ✗ 反而会掩盖 |
| (c) `hardTimeoutMs = 15000` 与生产用的 3000/3000/5000/6000 序列不一致 | ✗ |

**v4 的处置**：火山测试改为**走一次完整的真实会话**（形态 B，与千问 streaming 相同），
而不是"删掉一跳"。这样不论失败原因是 (a)(b)(c)，**新的实现都只有一条路径**，
不存在的路径自然不会再成为故障点——比"删一跳"更彻底。

> 注：(a)(b)(c) 中哪一个是真因，**我无法从静态源码确定**，需要用户实测的报文才能定位。
> 我不打算在报告里把它写成已确认结论。

### 4.4 一处需要澄清的事实（避免双方各自"翻烧饼"）

你的第六点提到千问 streaming 由 `qwen_audio_streaming.cpp:880` 判定。
我在核对时发现一个关键细节，**会影响"静音是否可行"的判断**：

```cpp
// qwen_audio_streaming.cpp:230-236
event.noSpeech = message.find("ASR_RESPONSE_HAVE_NO_WORDS") != std::string::npos;

// qwen_audio_streaming.cpp:880
if (ev.noSpeech || ev.taskFinished) { finished = true; break; }
```

**`noSpeech` 会让接收线程正常收尾**（`finished = true`）。
所以"静音导致服务端不回终帧"这个推论不成立——
**问题不在静音，而在 `:896` 那个三条件 `r.ok` 判定太严**
（例如 `receiveTimedOut` 由 7 秒窗口过紧触发）。

这一点对双方都重要：**不要为了解决"静音"而去换音频**，
**真正要改的是判据**。我 v4 的形态 B 正是按这个结论设计的。

---

## 五、总结成三句话

1. **①②③④⑤ 我采纳（②③的推论部分除外），计划已改到 v4**，主要变化是：
   判据换成 `ClassifyAsrResult`、测试改用 `g_config` 派生、方案 Y 删除、
   火山测试迁出 `volcengine_asr.h` 且不碰 `s_volcSession`。
2. **不认同的是"都退回握手即通过"这个方向**：那会造出四种探测强度，
   与用户要求的统一架构冲突；火山那第二次握手是为了抓配置错误故意加的，
   删掉会丢检测能力。
3. **火山真因（限流 / URL 构造 / 超时）我无法静态确定**，
   v4 用"统一走完整会话"绕过这个问题，并把三项都列入用户实测清单。
