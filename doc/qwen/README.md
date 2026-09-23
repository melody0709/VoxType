# 阿里通义千问 (Alibaba Qwen) 接入与技术文档

本文档为 VoxType 接入阿里通义千问系列（Qwen）模型的架构总览、协议实现分类与模型跟踪维护指南。

---

## 1. 架构总览与在 VoxType 中的形态

通义千问在 VoxType 中支持三类技术形态：

1. **官方 DashScope 云端 ASR（语音识别）**：
   - **流式识别（首选）**：`qwen-audio-3.0-asr-flash-streaming`（基于 WebSocket 双向协议，毫秒级流式出字）
   - **非流式批量识别**：`qwen-audio-3.0-asr-flash`（基于 HTTP REST POST，整段识别）
   - **实时会话识别**：`qwen-audio-realtime`
2. **Qwen IME (Free) 本地逆向独立运行时**：
   - 逆向分析官方千问输入法桌面版（`QianwenIME`），通过抽取并挂载其核心动态库（`unet.dll` / `UTDID.dll`）实现免 API Key 语音识别与后处理标点、润色。
3. **Qwen LLM 文本纠错与语义重写**：
   - 支持通过官方 DashScope、SiliconFlow（硅基流动）或 OpenRouter 调用通义千问旗舰指令模型（如 `Qwen/Qwen3.6-35B-A3B`、`qwen/qwen3.5-9b`）进行语音纠错。

---

## 2. 核心技术避坑与规则【A 级约束】

### 2.1 针对 LLM 纠错的思考模式关闭规则

各平台托管的 Qwen 思考模型必须关闭推理模式以确保极低时延与温度生效：
- **SiliconFlow**：在 `extraParams` 中传入 `"enable_thinking": false`
- **OpenRouter**：在 `extraParams` 中传入 `"reasoning": {"effort": "none"}`
- 默认 `temperature` 设定为 0.1，防模型过度改写或把输入当提示词执行。

### 2.2 UTDID 唯一性约束（千问输入法逆向运行时）

- 每台电脑必须生成并保存专属的设备指纹（UTDID），严禁直接拷贝其他主机的 `config.json` 或 `UTDID.dll` 注册项，否则会导致云端校验拦截。

---

## 3. 目录与参考文档索引

- [Qwen-Audio-3.0-ASR-Flash-Streaming.md](Qwen-Audio-3.0-ASR-Flash-Streaming.md)：流式语音识别协议规范、WebSocket 鉴权、心跳机制与实时出字解析。
- [Qwen-Audio-3.0-ASR-Flash.md](Qwen-Audio-3.0-ASR-Flash.md)：批量非实时语音识别 REST 接口规范与参数说明。
- [提升识别准确率.md](提升识别准确率.md)：热词词表（`vocabulary_id` / `vocabulary`）、多重阈值模式（`multi_threshold_mode`）、信噪比过滤与声学提示调优。
- [qwen_free_standalone_runtime_guide_zh.md](qwen_free_standalone_runtime_guide_zh.md)：千问输入法逆向工程、DLL 挂载与独立运行时部署指南。

---

## 4. 模型更新追踪清单

- [ ] **ASR 新版本发布**：跟进阿里通义实验室是否推出 `Qwen-Audio-3.5` 或 `Qwen-Audio-4.0`。
- [ ] **端侧小模型 Qwen-Audio 开源**：关注 HuggingFace / ModelScope 上的端侧量化版本，评估本地端侧 ONNX / GGUF 推理可行性。
- [ ] **LLM 纠错推荐模型升级**：随通义开源模型演进，评估更适合输入法低时延场景的 MoE 或轻量蒸馏版模型。
