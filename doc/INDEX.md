# VoxType 技术文档导航与模型演进索引 (Documentation Hub)

本文档是 VoxType 所有核心技术文档、模型协议规范与持续演进更新的权威导航中心。随着各大 AI 厂商模型的快速迭代，我们通过此索引统一管理研究记录、避坑规则与实现参考。

---

## 1. 模型提供商与文档分类导航

| 厂商 / 生态 | 核心能力 | 接入模型 | 协议类型 | 文档专区 |
| :--- | :--- | :--- | :--- | :--- |
| **小米 MiMo** | ASR + LLM 纠错 | ASR: `mimo-v2.5-asr`<br>LLM: `mimo-v2.6-flash` | HTTP Multipart / OpenAI Compatible | [📂 doc/mimo/](mimo/README.md)<br>• [ASR 详细指南](mimo/mimo_asr_guide.md)<br>• [LLM 纠错指南](mimo/mimo_llm_guide.md) |
| **阿里通义千问** | ASR + 逆向运行时 + LLM 纠错 | ASR: `qwen-audio-3.0-asr-flash-streaming`<br>IME: `QianwenIME` 独立运行时<br>LLM: `Qwen/Qwen3.6-35B-A3B` 等 | WebSocket / HTTP / 私有 DLL 挂载 | [📂 doc/qwen/](qwen/README.md)<br>• [流式 ASR 规范](qwen/Qwen-Audio-3.0-ASR-Flash-Streaming.md)<br>• [批量 ASR 规范](qwen/Qwen-Audio-3.0-ASR-Flash.md)<br>• [识别率优化与热词](qwen/提升识别准确率.md)<br>• [千问输入法逆向运行时](qwen/qwen_free_standalone_runtime_guide_zh.md) |
| **字节火山引擎** | 大模型 ASR + 逆向工程 | ASR: `SeedASR` / `BigASR`<br>IME: `doubao_ime` | 私有二进制帧 WebSocket / Protobuf + Opus | [📂 doc/volcengine/](volcengine/README.md)<br>• [火山 ASR 完整设置指南](volcengine/volcengine_asr_guide_zh.md) / [English](../volcengine_asr_guide.md) |
| **百度智能云** | 多语种/方言 ASR | ASR: 普通话(1537)、英语(1737)、粤语(1637)、四川话(1837) | HTTP POST REST API | [📂 doc/baidu/](baidu/README.md) |
| **本地离线引擎** | 本地 ASR + 双 VAD | ASR: SenseVoice / FireRedASR<br>VAD: Silero VAD / FireRedVAD | C++ direct ONNX runtime (sherpa-onnx) | [架构文档 (中文版)](ARCHITECTURE_zh.md) / [English](../ARCHITECTURE.md) |

---

## 2. 模型演进与研发工作流规范

当面对新模型发布、协议升级或新 Provider 接入时，团队必须遵循以下标准工程流程进行研究、参考与修改：

### 阶段一：协议调研与逆向/抓包取证
1. **接口规范确立**：明确是流式（Streaming WebSocket）还是非流式（Batch HTTP Multipart / JSON Base64）。
2. **鉴权与防刷策略**：确认鉴权头（Bearer Token / Header 签名 / Cookie 设备指纹），验证是否有设备绑定逻辑（如 UTDID / CDID）。
3. **特异性参数发掘**：
   - **典型教训**：LLM 纠错必须检查思考模式（如 MiMo 的 `thinking: {"type": "disabled"}`、SiliconFlow 的 `enable_thinking: false`），严禁携带数秒的思考链用于即时输入法。

### 阶段二：建立 C++23 最小可执行原型与测试保护
1. **不破坏既有生命周期**：
   - Batch Provider 派生自 `IAsrSession` / `BatchAsrSessionBase`。
   - Streaming Provider 派生自 `IStreamingAsrSession` / `StreamingAsrSessionBase`。
2. **测试先行（踩坑规则【B】）**：
   - 在动核心逻辑之前，在 `tests/` 下建立协议解析测试或离线回归测试（例如 `tests/llm_refine_test.cpp`）。
3. **健康探测绝不污染生产状态（踩坑规则【A】）**：
   - 测试连接必须是纯净、独立的轻量探针，**严禁**复用或关闭正在录音的生产长连接（如火山的 `s_volcSession`）。

### 阶段三：强类型注册表与 DPI 自适应 UI 落地
1. **强类型注册表三步走**：
   - 在 `src/core/config_store.h` 声明字段与默认值；
   - 在 `src/core/config_registry.cpp` 的 `InitializeRegistry()` 绑定键名、类型与 DPAPI 策略；
   - 在对应 Tab/Provider（`src/ui/providers/`）中创建控件与绑定。
2. **DPI 自适应设计基准**：
   - 物理像素转换统一使用 `S(UiStyle::Constant)`（144 DPI 基准）。
   - 控件尺寸必须在 `src/ui/ui_types.h` 的 `UiStyle` 统一维护，严禁私自声明局部常数。
   - 输入框宽度必须考虑各节点 URL 长度，避免文字被截断。

### 阶段四：沉淀技术文档与更新归档
- 在 `doc/<provider>/` 建立或更新技术文档，详述本次模型升级的核心变动、参数取值、踩坑教训与未来版本追踪计划。

---

## 3. 全局核心开发与架构文档

- [系统核心架构设计 (中文版)](ARCHITECTURE_zh.md) / [English](../ARCHITECTURE.md)
- [版本历史与变更日志 (中文版)](CHANGELOG_zh.md) / [English](../CHANGELOG.md)
- [开发者与智能体行为准则 (AGENTS.md)](../AGENTS.md)
