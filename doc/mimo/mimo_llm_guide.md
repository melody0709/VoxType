# 小米 MiMo LLM 语音文本纠错技术指南

本文档记录在 VoxType 中使用小米 MiMo 大语言模型（`mimo-v2.6-flash`）进行语音识别文本后处理（纠错、标点恢复、语义顺滑、润色）的架构设计、关键参数控制与技术实践。

---

## 1. 为什么选择 mimo-v2.6-flash

在语音输入法的场景下，LLM 文本纠错对模型的性能要求与常规对话 Chatbot 存在显著差异：

1. **时延极度敏感**：用户说完话松开快捷键后，等待上屏的时间应尽可能在 500ms~1000ms 内完成。任何超过 2 秒的停顿都会严重打断输入心流。
2. **极高遵循度与低幻觉**：模型必须严格克制，仅对语音识别过程中的同音错字、语病、缺失标点进行微调修复，严禁过度重写或自作主张回复用户的输入内容。
3. **高吞吐与经济性**：输入法高频触发，Token 计费成本必须可控。

`mimo-v2.6-flash` 具备毫秒级首字响应能力，且在中文语境的语义消歧上表现优异，是目前 MiMo 平台中最适合担任输入法纠错引擎的模型。

---

## 2. 关键发现与避坑不变量：深度思考模式必须关闭

### 2.1 问题溯源

小米 MiMo 平台对 v2.6 系列默认启用了深度思考（Thinking/Reasoning）机制：
```json
"thinking": {
  "type": "enabled"
}
```
在该默认设置下，服务端会产生两大致命问题：
1. **产生 3~10 秒的思考耗时**：模型会先在内部生成长篇思维链，导致整体响应时间拖长到 4~12 秒，完全破坏实时语音输入的体验。
2. **强制重置 Temperature = 1.0**：MiMo 官方规范规定，一旦启用 Thinking，接口会自动忽略客户端传入的 `temperature` 并强制固定为 1.0。高随机度会导致模型在做语音纠错时频繁出现幻觉、把用户输入当指令回答、或过度改写文本。

### 2.2 解决方案与参数固化

在 VoxType 的 Provider 预设中，显式指定 `extraParams`：
```json
"thinking": {
  "type": "disabled"
}
```
- **测试对比**：

| 参数配置 | 平均首字时延 (TTFT) | 整体纠错耗时 | Temperature 控制 | 纠错效果表现 |
| :--- | :--- | :--- | :--- | :--- |
| 默认（Thinking 开启） | 3200ms ~ 5800ms | 4500ms ~ 9200ms | 强制 1.0 (不可控) | 容易答非所问、过度发挥、时延严重卡顿 |
| **关闭（thinking: disabled）** | **280ms ~ 650ms** | **450ms ~ 1100ms** | **严格生效 (0.1)** | **纯粹文本纠正、准确率极高、近乎即时上屏** |

该参数在 `src/core/llm_refine.h` 的 `kProviderPresets` 中硬编码固化，并在 `tests/llm_refine_test.cpp` 中设立回归测试保护。

---

## 3. 请求规范与端点

### 3.1 HTTP 请求示例

```http
POST /v1/chat/completions HTTP/1.1
Host: api.xiaomimimo.com
Authorization: Bearer <API_KEY>
Content-Type: application/json

{
  "model": "mimo-v2.6-flash",
  "temperature": 0.1,
  "thinking": {
    "type": "disabled"
  },
  "messages": [
    {
      "role": "system",
      "content": "你是语音输入法的文本后处理助手。请对用户提供的语音识别初稿进行纠错、恢复标点、去除重复口吃词。直接输出纠错后的最终文本，严禁添加任何解释或前后缀。"
    },
    {
      "role": "user",
      "content": "【待处理语音文本开始】今天晚饭吃了北京烤鸭味道挺好但是有点油【待处理语音文本结束】"
    }
  ]
}
```

### 3.2 官方端点列表

- **按量付费（默认推荐）**：`https://api.xiaomimimo.com/v1`
- **国内资源包抵扣**：`https://token-plan-cn.xiaomimimo.com/v1`
- **海外资源包抵扣**：`https://token-plan-ams.xiaomimimo.com/v1`

---

## 4. 历史版本自动平滑迁移机制

VoxType 在 `src/core/llm_refine.h` 中实现了 `MigrateLegacyProviderConfig` 逻辑：
- 若用户此前手动配置过 `mimo-v2.5` 或 `mimo-v2.5-pro`，VoxType 在启动加载配置时会自动升级为 `mimo-v2.6-flash`。
- 若用户的 `extraParams` 字段为空，自动填充补齐 `"thinking":{"type":"disabled"}`。
- 用户无需手动重置设置即可直接获得升级后的低延迟纠错体验。

---

## 5. 后续模型演进追踪清单

- [ ] **Flash 系列小版本迭代**：关注是否有更低延迟的 `mimo-v2.6-flash-speed` 或类似专门微调版本。
- [ ] **下一代核心大模型**：跟进 `mimo-v2.7` / `mimo-v3.0` 发布动态，重点测试其在关闭思考模式下的遵循度与延迟指标。
- [ ] **端侧 / 本地量化可能**：关注小米移动端与端侧开源模型在 ONNX 或 GGUF 架构下的移植可行性。
