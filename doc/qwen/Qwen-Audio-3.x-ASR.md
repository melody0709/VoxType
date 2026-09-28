# 阿里通义千问 Qwen-Audio-3.x ASR 协议与技术规范 (3.0 & 3.1)

本文档是 VoxType 对阿里通义千问（Alibaba Qwen）大模型语音识别（Qwen-Audio 3.0 与 3.1 系列）的权威技术规范，整合了 WebSocket 实时流式传输、HTTP REST 批量识别、参数协议、热词与上下文增强等核心工程细节。

---

## 1. 模型矩阵与选型对比

通义千问语音大模型分为 **实时双向流式 (Streaming)** 与 **批量非实时 (Batch/REST)** 两种交互范式。VoxType 原生全面支持 3.0 与 3.1 系列模型。

| 模型标识 (Model ID) | 范式 | 协议 / 端点 | 适用场景 | VoxType 适配类 |
| :--- | :--- | :--- | :--- | :--- |
| `qwen-audio-3.1-asr-flash-streaming` | 实时流式 | WebSocket (`/api-ws/v1/inference`) | **新装默认与输入法首选**：超低延迟、原生润色，可用 `vad_model` 切换近/远场，`keep_dialect` 控制方言输出 | `CreateQwenAudioStreamingSession` → `qwen_audio_streaming::Client` |
| `qwen-audio-3.1-asr-flash-message` | 实时流式 | WebSocket (`/api-ws/v1/inference`，**与 streaming 同一端点**) | 官方定位"语音消息 / 输入法 / 企业业务"：润色可开关、强化多人与噪声场景；参数集更小（拒绝 `language_hints` 等）；**与 streaming 同价、RPM 更高（1200 vs 600）** | 同 `qwen_audio_streaming::Client`，按模型裁剪字段 |
| `qwen-audio-3.1-asr-flash` | 批量同步 | HTTP POST (`/api/v1/services/aigc/multimodal-generation/generation`) | 松开快捷键后整段识别、高并发稳定降级通道 | `QwenAudioAsrSession` → `qwen_audio_http::Client` |
| `qwen-audio-3.0-asr-flash-streaming` | 实时流式 | WebSocket (`/api-ws/v1/inference`) | 经典流式模型，向后兼容 | 同上（不发送 3.1 专属参数） |
| `qwen-audio-3.0-asr-flash` | 批量同步 | HTTP POST (`/api/v1/services/aigc/multimodal-generation/generation`) | 经典批量识别模型，向后兼容 | 同上（不发送 3.1 专属参数） |

> **VoxType 默认模型**：全新安装（无 `config.json`）取 `Config::qwenModel` 的默认值 `qwen-audio-3.1-asr-flash-streaming`；
> 已有配置文件**保持原模型不变**（`NormalizePersistedProfile()` 只在没有持久化 `qwen_model` 时回落到 legacy，未知模型名也回落 legacy）。

### 1.1 3.1 对比 3.0 核心演进

1. **原生转写润色 (Native Polishing)**：
   - Qwen-Audio-3.1-ASR-Flash **原生支持文本润色**：自动清理无意义语气词与口吃重复、处理说话过程中的自我纠正、理顺口语表达并规范标点（官方《非实时语音识别 HTTP API》原文；3.0-ASR-Flash 与 Fun-ASR-Flash 的润色顺滑默认关闭且暂未开放）。
   - `disfluency_removal_enabled`（`boolean`，默认 `false`）**只属于 `qwen-audio-3.1-asr-flash-message`**。3.1 streaming 与 3.1 HTTP 的参数表中都不存在该字段，不得向其发送。
2. **多方言与文言古诗词增强**：
   - 支持多语种与多地区中文方言识别；`language_hints` 提供 30 个语言代码（3.1 与 3.0 属同一分组，最多 4 个生效）。
   - 新增 `parameters.keep_dialect`（**3.1 全代次**，streaming 与 HTTP 均支持，默认 `false`）：`false` 将方言转写为普通话文本，`true` 保留方言表达。
   - 针对古诗词的韵律、节奏与文言表达做了专门调优，显著降低断句错位率。
3. **近/远场声学 VAD 选型 (`parameters.vad_model`)**：
   - **仅 3.1 的两种双工模型支持**：`qwen-audio-3.1-asr-flash-streaming` 与 `qwen-audio-3.1-asr-flash-message`；3.0 streaming 不接受该字段。
   - `near_meeting_16k`：近场场景（近距离耳麦、手持麦克风）。
   - `far_field_meeting_16k`（默认）：远场场景（笔记本内置麦克风、会议桌面拾音）。
4. **3.1 message 形态（`qwen-audio-3.1-asr-flash-message`）**：
   - 与 streaming **共用同一 WebSocket 端点与 `run-task` / `continue-task` / `finish-task` 事件协议**，但参数集不同（见 §1.3）。
   - 官方定位为"面向语音消息、输入法和企业业务场景"，强化了多人声、背景对话、远场与环境噪声下的稳定性。
   - 资费与 3.1 streaming **完全相同**（北京：输入 6 元 / 输出 4.5 元每百万 Token），但 **RPM 为 1200**（streaming 为 600）。
     它不作为 VoxType 默认档位的原因**不是价格**，而是参数集更窄（拒绝 `language_hints` / `special_word_filter` 等），且定位更偏多人与噪声场景。

### 1.2 定价与限流指标 (Pricing & Quotas)

- **计费机制**：按 Token 计费（音频 1 秒 ≈ 25 tokens）。
- **官方标准资费**（元 / 每百万 Token，模型调用原价、不含限时优惠）：

| 模型 | 输入（北京） | 输出（北京） | 输入（新加坡） | 输出（新加坡） | RPM |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `qwen-audio-3.1-asr-flash`（HTTP 批量） | **0.8** | **2.7** | 1.094 | 3.427 | 600 |
| `qwen-audio-3.1-asr-flash-streaming` | 6 | 4.5 | 6.781 | 5.104 | 600 |
| `qwen-audio-3.1-asr-flash-message` | 6 | 4.5 | 6.781 | 5.104 | **1200** |

- **选型含义**：两种 3.1 双工形态（streaming / message）**同价**，差别只在参数集与 RPM。HTTP 批量相对双工档位的差价**在输入与输出上并不一致**：
  **输入** 0.8 vs 6（双工是它的 7.5 倍），**输出** 2.7 vs 4.5（双工只有它的约 1.67 倍，即 HTTP 输出更便宜）。按整段录音估算成本时必须分开算两项，不要用单一倍数概括。
  HTTP 批量另有一个体验差异：只在松开按键后提交整段音频、只返回 final。
- **上下文长度上限**：streaming 8192 tokens（最大输入 8192 / 最大输出 1024）；message 与 HTTP flash 8192 tokens（最大输入 7168 / 最大输出 1024）。

> 历史更正：`0.8 / 2.7` 元**只是 HTTP flash 的价**，不得用它概括 3.1 系列。折算到双工档位时，
> **输入**差价是 7.5 倍（6 / 0.8），**输出**只有约 1.67 倍（4.5 / 2.7）——不要用"单价 7.5 倍"这类笼统说法。

### 1.3 3.1 message 与 3.1 streaming 的字段差异

两者端点、事件时序一致，**只有 `run-task.payload.parameters` 的参数集不同**：

| 参数 | 3.1 streaming | 3.1 message | 说明 |
| :--- | :--- | :--- | :--- |
| `format` / `sample_rate` | 必填 | 必填（`sample_rate` **仅支持 16000**） | message 更严格 |
| `language_hints` | 支持（≤4 个） | **不支持** | 发送即被判为非法参数 |
| `semantic_punctuation_enabled` | 支持（默认 `false`） | **不支持** | - |
| `multi_threshold_mode_enabled` | 支持（仅在语义断句关闭时生效） | **不支持** | - |
| `special_word_filter` | 支持 | **不支持** | 敏感词过滤仅 streaming 有 |
| `max_sentence_silence` | 支持（默认 1300，[200,6000]） | 支持（同默认与范围） | - |
| `heartbeat` / `speech_noise_threshold` | 支持 | 支持 | - |
| `vocabulary_id` / `vocabulary` | 支持 | 支持 | 即时热词权重 [1,5] 或 50 |
| `keep_dialect` | 支持（默认 `false`） | 支持（默认 `false`） | 3.1 全代次 |
| `vad_model` | 支持（默认 `far_field_meeting_16k`） | 支持（同默认） | 3.1 双工代次 |
| `disfluency_removal_enabled` | **不支持** | **支持**（默认 `false`，过滤语气词并润色） | message 独有 |
| `intermediate_result_enabled` | **不支持**（总是返回中间结果） | **支持**（默认 `false`） | message 默认只回 `sentence_end=true` |

服务端事件与取值路径两者完全一致（`payload.output.sentence.text`、`sentence_end`、`sentence_id`、`heartbeat`、`words[]`）；
message 的 `result-generated` 示例额外出现 `sentence_begin` 与 `stash` 字段（官方字段表未定义 `stash`，不得用它取值）。

> **VoxType 对 message 的处理**：按 `qwen_audio_profile` 的谓词自动裁剪上表"不支持"的字段，
> 并**固定发送 `intermediate_result_enabled: true`**（录音期间 HUD 需要 partial 上屏，官方默认只回句末结果）。
> `disfluency_removal_enabled` 默认关闭，由用户在 Qwen 高级设置中显式开启——避免模型在用户未授权时改写措辞。

---

## 2. WebSocket 实时双向流式协议 (Streaming)

流式接口是 VoxType 默认的核心通道，实现按住说话实时上屏、松开即可出最终文本的极速输入体验。

### 2.1 端点与鉴权

- **WebSocket URL 格式**（以北京地域为例，新加坡及其他地域前缀不同）：
  ```text
  wss://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference
  ```
- **鉴权 Header**：
  ```http
  Authorization: Bearer <DASHSCOPE_API_KEY>
  ```

### 2.2 任务交互生命周期

```
Client                                                  Server (DashScope)
  │                                                            │
  │─── 1. 发送 run-task 指令 (JSON 文本帧) ───────────────────>│
  │<── 2. 返回 task-started 事件 (JSON 文本帧) ────────────────│
  │                                                            │
  │─── 3. 持续发送 PCM 音频切片 (二进制帧，100ms/帧) ──────────>│
  │<── 4. 实时返回 result-generated (中间结果/整句结果) ───────│
  │                                                            │
  │─── 5. 录音结束，发送 finish-task 指令 (JSON 文本帧) ───────>│
  │<── 6. 返回最后一段 result-generated ───────────────────────│
  │<── 7. 返回 task-finished 事件 ─────────────────────────────│
  │                                                            │
  │    (任何步骤出现异常，Server 发送 task-failed 并断开连接)  │
```

#### Step 1: `run-task` 报文结构

```json
{
  "header": {
    "action": "run-task",
    "task_id": "32位随机小写十六进制UUID",
    "streaming": "duplex"
  },
  "payload": {
    "task_group": "audio",
    "task": "asr",
    "function": "recognition",
    "model": "qwen-audio-3.1-asr-flash-streaming",
    "parameters": {
      "format": "pcm",
      "sample_rate": 16000,
      "max_sentence_silence": 1500,
      "vad_model": "far_field_meeting_16k",
      "keep_dialect": false,
      "vocabulary": {
        "VoxType": 50,
        "音素": 4
      }
    },
    "input": {
      "context": [
        {
          "role": "user",
          "content": [
            {
              "type": "input_text",
              "text": "用户当前输入框前文或提示语"
            }
          ]
        }
      ]
    }
  }
}
```

#### Step 2: 二进制音频帧发送

- 格式：16kHz, 16-bit, 单声道 (Mono), Little-Endian 裸 PCM。
- 发送节奏：建议每 100ms 发送一个 Chunk（即 1600 samples = 3200 bytes）。严禁无节奏地突发发送，以免触发服务端流控背压。

#### Step 3: `continue-task`（可选的录音中上下文热注入）

在流式录音长连接进行中，若需要动态更新上下文（例如用户切换了窗口或输入了前置提示），可发送 `continue-task` 指令动态追加 context：
```json
{
  "header": {
    "action": "continue-task",
    "task_id": "与run-task相同的task_id",
    "streaming": "duplex"
  },
  "payload": {
    "input": {
      "context": [ ... ]
    }
  }
}
```

#### Step 4: `finish-task` 报文结构

```json
{
  "header": {
    "action": "finish-task",
    "task_id": "与run-task相同的task_id",
    "streaming": "duplex"
  },
  "payload": {
    "input": {}
  }
}
```

#### Step 5: 服务端下发事件处理

- **`task-started`**：标志任务建连成功，客户端开始进入 PCM 推流循环。
- **`result-generated`**：
  ```json
  {
    "header": {
      "event": "result-generated",
      "task_id": "..."
    },
    "payload": {
      "output": {
        "sentence": {
          "text": "识别出的文本",
          "begin_time": 0,
          "end_time": 1280
        }
      }
    }
  }
  ```
- **`task-finished`**：标志任务正常结算，可安全关闭 WebSocket。
- **`task-failed`**：包含 `error_code` 和 `error_message`。

---

## 3. HTTP REST 批量语音识别协议 (Batch)

用于整段录音完成后一次性提交并等待结果的同步/异步模式。在 VoxType 中，`QwenAudioClient` 负责通过该协议作为备选模式或流式异常时的回退通道。

### 3.1 同步识别 (Short-speech)

- **URL**：
  ```text
  https://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation
  ```
- **Header**：
  ```http
  Authorization: Bearer <DASHSCOPE_API_KEY>
  Content-Type: application/json
  X-DashScope-SSE: disable
  ```
- **Body**：
  ```json
  {
    "model": "qwen-audio-3.1-asr-flash",
    "input": {
      "messages": [
        {
          "role": "user",
          "content": [
            {
              "type": "input_audio",
              "input_audio": { "data": "data:audio/wav;base64,UklGRi..." }
            }
          ]
        }
      ]
    },
    "parameters": {
      "format": "wav",
      "sample_rate": "16000",
      "keep_dialect": false,
      "vocabulary": {
        "VoxType": 50
      }
    }
  }
  ```

> **字段形态（易错点）**：音频必须写成 `content[].input_audio.data`（对象内 `data` 字段），
> **不是** `content[].audio`。`data` 同时支持公网 URL 与 `data:audio/wav;base64,<...>` Data URI；
> Base64 后体积必须仍在 10 MB 音频输入上限内。
> 携带上下文时，`input_text` / `text` 消息必须按轮次排列且 `user` 在前，
> **包含 `input_audio` 的 user 消息必须位于 `messages` 数组最后**。
> `keep_dialect` / `speaker_diarization_enabled` 为 3.1 独有；开启说话人分离后
> `keep_dialect` 与 `language_hints` 不再生效。

---

## 4. 识别准确率进阶调优（热词与上下文增强）

详细指南请参考专项文档：[提升识别准确率.md](提升识别准确率.md)。以下为工程实现的要点提炼：

> **适用模型**：预编译热词（`vocabulary_id`）、即时热词（`vocabulary`）与上下文增强（`input.context`）对本仓支持的全部 Audio 3 模型生效，
> **包含 `qwen-audio-3.1-asr-flash-message`**（其客户端事件文档明确列出这三项能力；官方《提升识别准确率》页的清单尚未收录它）。
> 一个例外：message **不接受** `special_word_filter`（敏感词过滤），该能力仅 3.x streaming 系列与 Fun-ASR-Realtime 具备。
> 另外，预编译热词列表的 `target_model` 必须与实际调用的模型完全一致，否则热词不生效。

### 4.1 即时热词 (`parameters.vocabulary`)

- **权重机制**：
  - **常规热词（1 ~ 5）**：推荐权重为 `4`。权重越高，倾向性越强。
  - **超级热词（50）**：强制提升命中概率，但**限制单个请求最多包含 50 个超级热词**。若超过 50 个，服务端将降级或报错。
- **词数上限**：单个请求支持最多传入 2000 个即时热词。
- **本仓库实现（排序）**：下发的即时热词一律按**权重降序**排列（同权重保持文件顺序），再套用"最多 50 个超级热词 / 共 2000 条"的名额——因此被降级或截掉的总是权重最低的词，而不是写在文件末尾的词；`vocabulary.json` 默认权重为 `4`（非 50），模板里的 `50` 只是示例。
- **词长规范**：
  - 中文热词：长度应不超过 15 个汉字。
  - 英文热词：长度应不超过 7 个以空格分隔的单词。
- **词表匹配原理**：基于发音相近性纠偏。如果预设热词发音与用户发音完全无关，模型不会凭空生成。

### 4.2 上下文增强 (`payload.input.context`)

- **结构规范**：传入多轮对话结构数组，角色包含 `user`、`assistant`。
- **轮数上限**：最多支持 **5 轮**对话。
- **字符长度限制**：每轮文本最多 **400 字符**。若超出 400 字符，官方系统会自动截断尾部超出部分，不会直接返回错误。
- **生效机制**：上下文增强不仅提供语义主题背景，同时也会自动提取前文中的关键词作为软性热词加权。
- **本仓库实现**：`input.context` / `input.messages` 由 `src/asr/asr_context.*` 装配为 `[最近 N 轮识别结果…, 焦点输入框文本]`（旧→新，每轮按尾部截到 400 字符），N 由 `qwen_history_context_rounds`（1–5，默认 3）控制、总开关为 `qwen_history_context`（默认关闭）。**注意 5 条消息的上限**：焦点字段非空时它自己占掉一条，实际发出的历史轮最多 4 条（`asr_context::kMaxContextTurns` 保证"字段轮优先 + 只保留最近 5 条"）。输入框与历史都没有文本时改用词表作为领域词表兜底。历史轮来自 `src/core/asr_history.*` 的全局环形缓冲，每条最终转写在 `DispatchAsrFinalText()` 里只记录一次；`continue-task` 刷新只替换字段轮，保留历史轮。

---

## 5. VoxType 中的工程架构与最佳实践

### 5.1 双模式自愈与降级机制

```
[录音开始] ───────────► 判定 Qwen Model 种类
                              │
            ┌─────────────────┴─────────────────┐
            ▼                                   ▼
    [Streaming 模型]                     [Batch/HTTP 模型]
`qwen-audio-3.1-asr-flash-streaming`   `qwen-audio-3.1-asr-flash`
`qwen-audio-3.0-asr-flash-streaming`   `qwen-audio-3.0-asr-flash`
            │                                   │
   CreateQwenAudioStreamingSession        QwenAudioAsrSession
            │                                   │
   [双向 WebSocket 实时推流]            [录音结束一次性 HTTP POST]
            │                                   │
      遇到断连 / 报错                           │
            │                                   │
            ▼                                   ▼
   触发统一 ASR Fallback 机制 ◄─────────────────┘
   (回退至 SenseVoice / MiMo / 火山等)
```

### 5.2 停录看门狗预算控制 (`ComputeCloudAsrPostStopWatchdogMs`)

流式会话中，松开录音键后并不立即掐断连接，而是发出 `finish-task` 并等待最终文本。
- 必须严格使用 `ComputeCloudAsrPostStopWatchdogMs()` 分配后置等待预算。
- 失败路径的 Replay 机制必须遵循 `ShouldStartFailureReplay()` 门控校验，防止重传长音频超出看门狗总上限导致被粗暴 Abort。

### 5.3 隔离的连通性探测 (Test Connection)

在设置界面点击 "Test Connection" 时：
- 严格构建独立的短连接或轻量探针，**严禁污染或关闭生产语音输入的长连接**。
- 严格根据生产分类器（如 `ClassifyAsrResult`）核对响应，避免将空结果或超时误判为“测试通过”。

### 5.4 代次与形态专属参数的发送门控

下列字段只对特定代次或形态有效，向错误的模型发送会被服务端拒绝，因此**全部集中**在 `src/core/qwen_audio_profile.h`，由请求构造处按模型裁剪：

| 参数 | 作用域 | 判定函数 | 配置项 / 默认 |
| :--- | :--- | :--- | :--- |
| `vad_model` | 3.1 streaming 与 3.1 message 的 run-task | `SupportsVadModel()` | `qwen_vad_model`（默认 `far_field_meeting_16k`） |
| `keep_dialect` | 3.1 streaming / 3.1 message / 3.1 HTTP | `SupportsKeepDialect()` | `qwen_keep_dialect`（默认 `false`） |
| `language_hints` | 除 message 外的全部模型 | `SupportsLanguageHints()` | 面板 "Hints" 输入框 |
| `semantic_punctuation_enabled`、`multi_threshold_mode_enabled` | 除 message 外的全部流式模型 | `SupportsSemanticPunctuation()` | `qwen_semantic_punctuation` / `qwen_multi_threshold` |
| `special_word_filter` | 除 message 外的全部流式模型 | `SupportsSpecialWordFilter()` | `qwen_special_word_replace` 等 |
| `disfluency_removal_enabled` | **仅 message** | `SupportsDisfluencyRemoval()` | `qwen_disfluency_removal`（默认 `false`） |
| `intermediate_result_enabled` | **仅 message** | `SupportsIntermediateResult()` | 无配置项，固定发送 `true` |

- 所有字段都在 `qwen_audio_streaming.cpp` / `qwen_audio_http.cpp` 的请求构造处按模型追加；Test Connection 探针复用同一构造函数，因此探针与生产请求的字段集永远一致。
- **字段顺序对 3.0 / 3.1 streaming 逐字节不变**：message 专属字段只在命中 message 时追加（`qwen_audio_json_test` 有断言兜底）。
- `config_store.cpp` 加载时把非法 `qwen_vad_model` 收敛为官方默认值，并把 `qwen_keep_dialect`、`qwen_disfluency_removal` 与最终模型求交，防止手工编辑 config.json 后把 3.1 / message 字段泄漏进 3.0 请求。
- UI 全部位于 Settings → Qwen → Advanced：`VAD model` + `Keep dialect (3.1)` 一行、`Filler-word removal / polish (3.1 message)` 一行；不匹配的模型上对应控件自动禁用（语义断句、多阈值、敏感词同理）。
- 面板上的 `Language` / `Hints` 在 message 模型下会被**同时禁用**并改写提示文案——`language_hints` 对 message 无效，不能让输入框假装生效。

### 5.5 Manual 模式的 60 秒提示（仅 `qwen3-asr-flash-realtime`）

官方《实时语音识别》指南在 **Qwen3-ASR-Flash-Realtime** 的 `turn_detection: null`（Manual，客户端 commit 控制断句）示例旁提示：
**"使用非 VAD（Manual）模式时，建议单次会话持续发送的音频时长累加不超过 60 秒"**。
该提示所属示例使用 `input_audio_buffer.append` / `input_audio_buffer.commit`，属于 Qwen3-Realtime 的 OpenAI 风格协议，**与 Audio 3 的 `run-task` + 二进制 PCM 协议不是同一套**。

- **对 VoxType 的影响**：legacy 档位 `qwen3-asr-flash-realtime` 恰好固定在 Manual 模式——`qwen_asr.cpp` 的 `ManualTurnDetectionJson()` 在非 `server_vad` 时发送 `"turn_detection": null`。
  因此**该档位下单次按住超过约 60 秒存在被服务端提前结束的风险**；长文听写建议改用 Audio 3 的 streaming / message 档位。
- **不要把它套到 Audio 3**：`semantic_punctuation_enabled` / `max_sentence_silence` 是 Audio 3 的断句开关，官方**未**对 Audio 3 streaming / message 给出同类时长上限。
- 官方另说明：Audio 3 的 `max_sentence_silence` 在语义断句开启时**不作为 `sentence_end` 的返回依据**，设置过小仍可能影响识别效果。

### 5.6 `qwen-audio-3.1-asr-flash-message` 的上架状态与复验

- 该模型走**独立文档集**：`qwen-asr-message-websocket-api`（端点 `/api-ws/v1/inference`）、`qwen-asr-message-client-events`、`qwen-asr-message-server-events`、模型页 `qwen-audio-3-1-asr-flash-message`。
- 截至 2026-09-28，**《实时语音识别》主指南的「支持的模型与地域」清单尚未收录它**（北京/新加坡两地的实时清单里仍只有 3.0/3.1 streaming、Fun-ASR-Realtime、Qwen3-Realtime、Paraformer）。
- 但百炼控制台**已上架**该模型（显示 100 万免费 token、输入 6 元 / 输出 4.5 元每百万 token、RPM 1200），且其独立 WebSocket 指南给出的端点与 streaming 完全相同。
- **因此：接入按独立文档集实现，但首次使用前必须用 Settings → Qwen → Test Connection 实测一次**；若握手或 `run-task` 被拒，说明该模型尚未在对应地域开放，应回退到 streaming 档位。
