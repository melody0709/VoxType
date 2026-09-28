# Qwen Audio 3.1 ASR 集成与调研方案

> 状态：已完成（v0.11.3：3.1 三个模型全部接入，含 `vad_model` / `keep_dialect` / `disfluency_removal_enabled` 门控，新装默认切至 3.1 streaming）
> 创建日期：2026-09-28
> 调研方式：Chrome DevTools Protocol (CDP) 直连本地浏览器读取阿里云百炼控制台与官方文档

---

## 1. 调研背景与取证结论

阿里云通义实验室于近期（2026年9月）上线了 **Qwen-Audio-3.1** 语音识别大模型系列。
本次调研通过本地 CDP 调试通道直接提取用户浏览器当前打开的两个官方页面，获取一手协议与控制台数据：

1. **控制台详情页**：`https://bailian.console.aliyun.com/cn-beijing/model/market/detail/qwen-audio-3.1-asr-flash`
2. **实时语音识别官方指南**：`https://docs.bailian.console.aliyun.com/zh/model-studio/real-time-speech-recognition-user-guide`

### 1.1 核心特性与技术规格
* **模型型号定位**：
  * **非流式/文件批量转写**：`qwen-audio-3.1-asr-flash`
  * **实时双向流式转写**：`qwen-audio-3.1-asr-flash-streaming`
* **能力升级**：
  * **原生转写润色**：具备自动去除口语化词汇（如语气助词、重复词）并规范标点的能力。
  * **多语种与方言**：支持 16 种以上方言 ASR/AST 可控输出。
  * **文化与古诗词优化**：针对文言文、古诗词韵律节奏深度调优。
  * **工业级分角色**：支持多人会议及对话的说话人角色识别（Diarization）。
* **VAD 断句机制**：
  * 3.1 流式新增支持通过 `vad_model` 指定近场/远场模型：
    * `near_meeting_16k`（近场麦克风模式）
    * `far_field_meeting_16k`（远场会议模式，默认值）
  * 依然兼容 3.0 的 `max_sentence_silence` 断句静音阈值配置。
* **计费标准与限流**（2026-09-28 按官方模型页逐档复核，此前误把 HTTP flash 的价格当作整个 3.1 系列）：
  * `qwen-audio-3.1-asr-flash`（HTTP 批量）：输入 0.8 元 / 输出 2.7 元 每百万 tokens（以 25 tokens/s 计，1 小时音频约 0.072 元）。
  * `qwen-audio-3.1-asr-flash-streaming`：输入 6 元 / 输出 4.5 元 每百万 tokens。
  * `qwen-audio-3.1-asr-flash-message`：输入 6 元 / 输出 4.5 元 每百万 tokens（**与 streaming 同价**）。
  * RPM：HTTP flash 600、streaming 600、message **1200**。
  * 免费额度：新用户含 1,000,000 tokens 体验额度。

### 1.2 服务端协议兼容性分析
经现场抓取百炼控制台给出的 cURL 与 SDK 代码样例：
1. **HTTP Batch 调用（`qwen-audio-3.1-asr-flash`）**：
   * 端点路径：`/api/v1/services/aigc/multimodal-generation/generation`
   * 请求体格式：
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
                 "input_audio": { "data": "data:audio/wav;base64,..." }
               }
             ]
           }
         ]
       },
       "parameters": {
         "format": "wav",
         "sample_rate": "16000"
       }
     }
     ```
   * **结论**：与 VoxType 现有的 `qwen_audio_http.cpp` 完全一致，仅需传入模型名称为 `qwen-audio-3.1-asr-flash`。
2. **WebSocket 实时流式调用（`qwen-audio-3.1-asr-flash-streaming`）**：
   * 端点路径：`/api-ws/v1/inference`
   * 首包握手：`{"header":{"action":"run-task", ...}, "payload":{"model":"qwen-audio-3.1-asr-flash-streaming", ...}}`
   * 下发响应包：`payload.output.sentence` 携带 `text`, `begin_time`, `end_time`, `words`。
   * **结论**：与 VoxType 现有的 `qwen_audio_streaming.cpp` 完全一致。
3. **3.1 代次独有的请求字段（本次补充接入）**：
   * `vad_model`（`near_meeting_16k` / `far_field_meeting_16k`，默认远场）：**仅** `qwen-audio-3.1-asr-flash-streaming` 的 run-task 支持，3.0 streaming 不接受该字段。
   * `keep_dialect`（默认 `false`）：**整个 3.1 代次**都支持（3.1 streaming 的 run-task 与 3.1 HTTP 的 parameters）；`false` 把方言转写为普通话，`true` 保留方言表达。
   * HTTP 侧 3.1 另有 `speaker_diarization_enabled`（默认 `false`，输入法场景不使用）：开启说话人分离后 `keep_dialect` 与 `language_hints` 不再生效。
   * **纠错**：`disfluency_removal_enabled` 属于 `qwen-audio-3.1-asr-flash-message`（另一种 message 形态），3.1 streaming 与 3.1 HTTP 的参数表中都不存在，不得发送。
4. **`qwen-audio-3.1-asr-flash-message` 形态（本次接入）**：
   * 端点与事件协议：**与 3.1 streaming 完全相同**——`wss://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference`，`run-task` → `task-started` → 二进制音频 + `result-generated` → `finish-task` → `task-finished`；连接可复用（`task-finished` 之后）。因此直接复用 `qwen_audio_streaming::Client`，不新增会话类。
   * 参数差异（官方《Qwen-Audio-ASR-Message 客户端事件》）：必填 `format` / `sample_rate`（**仅 16000**）；**拒绝** `language_hints`、`semantic_punctuation_enabled`、`multi_threshold_mode_enabled`、`special_word_filter`；新增 `disfluency_removal_enabled`（默认 `false`）与 `intermediate_result_enabled`（默认 `false`）；支持 `vad_model` / `keep_dialect` / `vocabulary(_id)` / `max_sentence_silence` / `heartbeat` / `speech_noise_threshold` / `input.context`。
   * 服务端结果结构与 streaming 一致（`payload.output.sentence.text` / `sentence_end` / `sentence_id` / `heartbeat`），示例中额外出现 `sentence_begin` 与未定义字段 `stash`（不得用 `stash` 取值）。
   * 采用决策：固定发送 `intermediate_result_enabled: true`（HUD 需要 partial）；`disfluency_removal_enabled` 暴露为用户开关且默认关闭；资费与 3.1 streaming **相同**（6 / 4.5 元每百万 token），不作为默认档位的原因是参数集更窄（拒绝 `language_hints` / `special_word_filter`）而非价格。

---

## 2. VoxType 客户端架构适配方案

### 2.1 现状与拦截点
在 VoxType 现状代码中，此前将 Audio 3.0 的模型名做了单一字符串的硬编码比对：
1. `src/core/qwen_audio_profile.h`：`IsSupportedModel` 仅校验 3.0 与 legacy；未知模型会被 `NormalizePersistedProfile` 强制降级并重置回 `qwen3-asr-flash-realtime`。
2. `src/app/asr_attempt_manager.cpp`、`src/app/recording_session_controller.cpp`、`src/asr/asr_session.cpp`、`src/app/asr_probe_service_impl.cpp`：
   直接使用 `config.qwenModel == L"qwen-audio-3.0-asr-flash-streaming"` 或 `config.qwenModel == L"qwen-audio-3.0-asr-flash"`。
3. `src/ui/providers/provider_qwen.cpp`：设置界面的下拉 ComboBox 仅包含 3.0 与 legacy 选项。

### 2.2 重构与支持策略（同时保留 3.0 与引入 3.1）
1. **模型常量与族系判定（Family Predicates）**：
   在 `src/core/qwen_audio_profile.h` 中定义常量：
   * `kHttpModel = L"qwen-audio-3.0-asr-flash"`
   * `kStreamingModel = L"qwen-audio-3.0-asr-flash-streaming"`
   * `kHttpModel31 = L"qwen-audio-3.1-asr-flash"`
   * `kStreamingModel31 = L"qwen-audio-3.1-asr-flash-streaming"`
   并提供内联判定函数：
   * `IsHttpModel(model)`: 匹配 3.0 或 3.1 HTTP 批量模型；
   * `IsStreamingModel(model)`: 匹配 3.0 或 3.1 流式 WebSocket 模型；
   * `IsSupportedModel(model)`: 覆盖全部已支持版本。
2. **规范化与传输映射**：
   在 `NormalizePersistedProfile` 中：
   * `IsHttpModel(model)` 统一绑定到 `kHttpTransport`；
   * `IsStreamingModel(model)` 统一绑定到 `kStreamingTransport`；
   * 确保 3.0 和 3.1 用户均能正确持久化与迁移配置。
3. **消除硬编码分散判断**：
   将 `asr_attempt_manager.cpp`、`recording_session_controller.cpp`、`asr_session.cpp`、`asr_probe_service_impl.cpp`、`asr_diagnostics.cpp` 中的 `== L"qwen-audio-3.0-asr-flash*"` 全部替换为 `qwen_audio_profile::IsStreamingModel` 和 `qwen_audio_profile::IsHttpModel`。
4. **UI 下拉列表增强**：
   在 `provider_qwen.cpp` 中将模型列表更新为：
   * `qwen-audio-3.1-asr-flash-streaming`
   * `qwen-audio-3.1-asr-flash`
   * `qwen-audio-3.0-asr-flash-streaming`
   * `qwen-audio-3.0-asr-flash`
   * `qwen3-asr-flash-realtime`
5. **文档同步更新**：
   * 更新 `doc/qwen/README.md`
   * 新建整合规范 `doc/qwen/Qwen-Audio-3.x-ASR.md`；3.0 的两份官方逐字归档（`Qwen-Audio-3.0-ASR-Flash-Streaming.md` / `Qwen-Audio-3.0-ASR-Flash.md`）**予以保留**，继续作为版本档案供回溯取证
   * 同步 `doc/INDEX.md`
6. **3.1 专属参数接入**：
   * `src/core/qwen_audio_profile.h` 增加代次门控 `SupportsVadModel()`（仅 3.1 streaming）与 `SupportsKeepDialect()`（整个 3.1 代次）。
   * `qwen_audio_streaming.cpp` / `qwen_audio_http.cpp` 在请求构造处按模型追加 `vad_model` / `keep_dialect`，3.0 请求保持逐字节不变；Test Connection 探针复用同一构造路径。
   * 新配置项 `qwen_vad_model` / `qwen_keep_dialect` 走强类型注册表（`config_store.h` → `config_registry.cpp` → Qwen 高级设置对话框）。
   * `config_store.cpp` 在模型归一化之后收敛取值，防止手工编辑 config.json 把 3.1 字段泄漏进 3.0 请求。
7. **message 形态接入（复用而非新建）**：
   * `qwen_audio_profile.h` 增加 `kMessageModel31` / `IsMessageModel()`，把 message 纳入 `IsStreamingModel()` 家族（同一端点与协议类），并新增 `SupportsLanguageHints` / `SupportsSemanticPunctuation` / `SupportsSpecialWordFilter` / `SupportsDisfluencyRemoval` / `SupportsIntermediateResult` 五个字段谓词。
   * `qwen_audio_streaming.cpp` 的 `BuildRunTaskMessageImpl` 按谓词裁剪字段；**3.0 / 3.1 streaming 的字段顺序逐字节不变**（新增字段只在命中 message 时追加）。
   * 新配置项 `qwen_disfluency_removal`（默认 `false`）；`intermediate_result_enabled` 无配置项，固定发 `true`。
   * UI：模型下拉新增 `qwen-audio-3.1-asr-flash-message`；Advanced 对话框新增 `Filler-word removal / polish (3.1 message)` 行，并在 message 模型下自动禁用语义断句 / 多阈值 / 敏感词 / 语言提示等它不接受的控件。

---

## 3. 验收标准
1. **编译与架构守卫**：运行 `build.bat` 通过，未破坏守卫基线（17 项检查全绿）。
2. **测试用例**：
   * `qwen_audio_json_test.exe` 测试通过，包含 3.1 模型规范化及有效性断言。
   * `asr_json_protocol_test.exe` 测试通过，包含 3.1 显示名称断言。
3. **UI 验证**：打开 Settings -> Qwen 能够正常展示 3.1 模型并切换保存。
4. **3.1 专属参数验证**：`qwen_audio_json_test` 断言 3.1 streaming run-task 携带 `vad_model` 与 `keep_dialect`、3.1 HTTP 请求携带 `keep_dialect`，且 3.0 两代协议均不含这两个字段；`build.bat` 的 DPI 布局校验覆盖新增的 Advanced 行。
5. **message 形态验证**：`qwen_audio_json_test` 断言 message 的 run-task 含 `disfluency_removal_enabled` / `intermediate_result_enabled` / `vad_model` / `keep_dialect` / `max_sentence_silence`，且**不含** `language_hints` / `semantic_punctuation_enabled` / `multi_threshold_mode_enabled` / `special_word_filter`；`asr_json_protocol_test` 断言 message 的显示名与新装默认 3.1 streaming。
