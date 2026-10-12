# 火山引擎豆包语音大模型 (Volcengine Doubao) 技术文档

本文档为 VoxType 接入字节跳动火山引擎豆包大模型语音识别（ASR）与实验性豆包输入法逆向工程的技术矩阵与维护指南。

---

## 1. 架构总览与能力矩阵

火山引擎语音识别在 VoxType 中是功能最丰富、控制选项最细致的云端后端之一：

```
+-----------------------------------------------------------------------------+
|                                 VoxType Core                                |
+--------------------------------------+--------------------------------------+
                                       |
                   +-------------------+-------------------+
                   |                                       |
                   v                                       v
    +------------------------------+       +------------------------------+
    | 火山引擎大模型 WebSocket ASR  |       |  实验性豆包输入法 (逆向工程)  |
    | (SeedASR / BigASR)           |       |  (doubao_ime_asr)            |
    +--------------+---------------+       +--------------+---------------+
                   |                                       |
                   v                                       v
       私有二进制帧协议 + JSON 载荷             Protobuf 编码 + Opus 压缩音频
       (支持二遍识别、热词、对话上下文)          (基于设备 CDID 与专属 Token)
```

### 1.1 大模型流式语音识别服务

- **协议架构**：基于 WebSocket 的私有二进制分帧协议（Header 4 字节 + Payload 长度 + JSON 元数据 + 原始 PCM 音频）。
- **主要模型版本**：
  - `SeedASR`（极速、高准确率，当前主推大模型方案）
  - `BigASR`（经典大模型语音识别）
- **计费模式**：
  - 按时长计费（Duration）
  - 按并发路数计费（Concurrent）
- **关键特性**：
  - **二遍识别（Nonstream Pass）**：边录音边流式返回中间结果；松开按键后，服务端大模型触发二次精修与语义整理，大幅降低首字延迟与最终错字率。
  - **热词增强（Boosting Table）**：支持绑定云端热词词表与动态词表权重。
  - **纠错表（Correct Table）**：支持服务端级别的专有名词自动替换。
  - **上下文注入（Context）**：可将光标前文本或历史转写以 JSON 转义字符串形式注入，提升指代和专业术语命中率。

### 1.2 实验性豆包输入法逆向协议 (Doubao IME)

- **实现文件**：`src/asr/doubao_ime_asr.cpp` / `src/asr/doubao_ime_streaming_session.cpp` / `src/asr/doubao_ime_payload.h` / `src/asr/doubao_ime_context.cpp` / `tools/doubao_ime_probe.cpp`
- **协议特征**：采用 Protobuf 序列化协议，音频采用 Opus 格式高压缩率编码上传，使用 `DeviceId`、`Cdid` 和专用动态 Token 鉴权。
- **会话上下文（P1，默认关闭）**：客户端 `cfg.extra` 会被 SAMICore 原样并入线级 StartSession JSON，因此 AsrContext（chat 轮次 + hotwords，base64）放在 **`extra["context"]`**；开关为 `doubao_context_enabled`。实机 A/B（E-018）显示服务端**接受**该载体但未观察到转写效果变化，故出厂关闭；启动载荷由 `doubao_ime_payload.h` 逐字节冻结（`tests/asr_json_protocol_test.cpp` 第 30 组）。
- **逆向依据**：`reverse/work/20261012-doubao-ime-extraction/evidence/E-015.md`（AsrContext schema）、`E-016.md` / `E-017.md`（SAMICore cfg 载体、字段语义修正）、`E-018.md`（载体实测与生产化结论）。
- **A/B 已跑（2026-10-12）：服务端无条件加标点，`enable_punctuation` 不被此端点消费。** 客户端 `enable_punctuation` 硬编码 `true`（`src/asr/doubao_ime_config.h`）；用 `VOXTYPE_DOUBAO_PUNCT=0`（诊断环境变量，未设置时载荷逐字节不变，激活时写 `event=doubao_punct_experiment` 日志）对同一条 WAV 做差分，两次返回**完全一致**（`开放时间，早上 9 点至下午 5 点。`，全角逗号与句号俱在；运行日志确认 `enable_punctuation=0` 确实上线）。结论：
  - 标点由服务端**无条件附加**，`enable_punctuation` 被接受但不生效（本样本：streaming、16k、`input_mode=tool`；样本有限，换多条 WAV 可复测）；
  - 因此本地 `voice_punct_mode` 的非默认模式必然作用在**已标点文本**上：`remove_trailing` = "服务端先加、本地再去掉最后一个"，`space_replace` 同理——这是**组合语义**，不是缺陷；
  - 想要无标点输出的用户只能用本地 `remove_trailing` / `space_replace`，`keep_all`（默认）保留服务端标点。
  复现：
  ```powershell
  $wav = "models\sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17\test_wavs\zh.wav"
  & .\tools\asr_audio_replay.bat --wav $wav --backend doubao_ime --show-text     # enable_punctuation=true
  $env:VOXTYPE_DOUBAO_PUNCT = "0"
  & .\tools\asr_audio_replay.bat --wav $wav --backend doubao_ime --show-text     # false
  Remove-Item Env:VOXTYPE_DOUBAO_PUNCT
  ```
  注意：回放工具**不走** `DispatchAsrFinalText`（callback 收文本），本地 `voice_punct_mode` 在这条路径不生效——差分测的是**服务端行为**。`VOXTYPE_DOUBAO_PUNCT` 只接受 `0`/`1`。

---

## 2. 核心工程不变量与踩坑规则【A 级与 B 级约束】

在维护火山引擎模块时，必须严格遵守以下红线：

1. **【A】`connected` 必须为原子类型**：WebSocket `connected` 必须使用 `std::atomic<bool>`，禁止使用 `volatile bool`，防多线程重连竞态。
2. **【A】`context` 字段必须是 JSON 字符串**：火山引擎服务端要求 `context` 字段的值是**已转义为字符串的 JSON**，而不是原始 JSON 对象（嵌套引号必须转义为 `\"`）。
3. **【A】连接探测不得触碰生产长连接**：
   - `src/asr/volcengine_streaming_session.cpp` 中的 `s_volcSession` 是生产语音输入的全局长连接。
   - 测试连接时必须直接调用 `volc_asr::OpenSession(localSession, cfg, timeout)`，**绝对禁止**经由 `VolcengineStreamingSession` 派发，避免破坏用户正在进行的录音或污染生产状态。
4. **【B】协议层重构前提条件约束**：
   - 在未建立完整离线单元测试覆盖前，**严禁**重写 `src/asr/volcengine_asr.h` / `src/asr/volcengine_streaming_session.cpp` 中的 frame 编解码、send/drain 循环与长连接管理机制。

---

## 3. 详细文档索引

- [火山引擎设置完整指南 (中文版)](volcengine_asr_guide_zh.md) / [English Guide (根目录)](../../volcengine_asr_guide.md)：
  - 详细的 API Key 与 Resource ID 获取指引
  - `end_window_size` 与 `force_to_speech_time` 调节
  - DDC 语义顺滑、POI/Music 语义识别增强
  - 客户端与服务端二进制数据帧拆包组包逻辑

---

## 4. 模型演化与追踪清单

- [ ] **SeedASR 2.0 / 豆包新版语音大模型**：跟进字节跳动 AI Lab 发布的最新大模型版本名称与 API 变更。
- [ ] **端到端语音转文本大模型**：评估火山引擎实时语音对话大模型在听写模式下的延迟与抗噪表现。
- [ ] **热词动态绑定 API 优化**：关注是否支持每次连接动态下发临时词表，减少预建词表维护成本。
