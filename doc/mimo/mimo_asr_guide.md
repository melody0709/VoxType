# 小米 MiMo 语音识别 (ASR) 技术文档

本文档详细记录 VoxType 接入小米 MiMo 语音识别服务（`mimo-v2.5-asr`）的技术细节、网络协议、音频规范、错误诊断及未来模型升级维护方案。

---

## 1. 协议规范

### 1.1 HTTP 接口定义

小米 MiMo 语音识别遵循类似 OpenAI Whisper 的 REST API 设计，采用 HTTP POST `multipart/form-data` 格式上传音频文件并进行批量识别。

- **请求方法**：`POST`
- **请求地址**：`${mimoBaseUrl}/audio/transcriptions`
  - 默认按量端点：`https://api.xiaomimimo.com/v1/audio/transcriptions`
  - 资源包国内端点：`https://token-plan-cn.xiaomimimo.com/v1/audio/transcriptions`
  - 资源包海外端点：`https://token-plan-ams.xiaomimimo.com/v1/audio/transcriptions`
- **鉴权请求头**：
  ```http
  Authorization: Bearer <API_KEY>
  Content-Type: multipart/form-data; boundary=----VoxTypeBoundary...
  ```

### 1.2 表单字段 (Form Fields)

| 字段名 | 类型 | 必填 | 默认值 / 允许值 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| `file` | 二进制文件 | 是 | 单声道 16kHz WAV | 上传包含 RIFF 头的文件流，建议命名为 `audio.wav`，MIME 类型为 `audio/wav` |
| `model` | 字符串 | 是 | `mimo-v2.5-asr` | 当前小米官方唯一的 ASR 模型标识 |
| `language` | 字符串 | 否 | `auto` / `zh` / `en` | 语言提示。`auto` 为自动检测，`zh` 为中文，`en` 为英文 |

---

## 2. 音频格式与封装实现

### 2.1 基础音频要求

- **采样率**：固定 16,000 Hz（若输入设备采样率为 44.1kHz 或 48kHz，VoxType 会在采集层由重采样算法转换为 16kHz）
- **采样位数**：16-bit 线性 PCM（有符号整数，Little-Endian）
- **通道数**：单声道（Mono）

### 2.2 RIFF WAV 封装规范

小米 MiMo 不支持直接发送裸 PCM 数据，必须包装标准的 44 字节 WAV 容器头。`src/asr/mimo_asr.cpp` 内置生成标准标头：

```cpp
// 44-byte standard RIFF WAV Header
uint32_t dataBytes = static_cast<uint32_t>(pcmData.size() * sizeof(int16_t));
uint32_t totalBytes = 36 + dataBytes;
// 写入 "RIFF", totalBytes, "WAVE", "fmt ", 16 (chunk size), 1 (PCM), 1 (mono), 16000 (sample rate), 32000 (byte rate), 2 (block align), 16 (bits/sample), "data", dataBytes
```

---

## 3. 响应格式与解析

### 3.1 正常响应 (HTTP 200)

```json
{
  "text": "今天天气真不错，我们去散步吧。"
}
```

- VoxType 通过内置的转义安全 JSON 解析器读取 `text` 字段，并清除首尾空格后返回给 ASR 会话管理器。

### 3.2 异常响应 (HTTP 4xx / 5xx)

```json
{
  "error": {
    "message": "Invalid API key provided",
    "type": "invalid_request_error",
    "code": "invalid_api_key"
  }
}
```

- VoxType 会提取 `message` 字段并格式化为 `MiMo ASR error: <message>`，通知界面并在诊断日志中保留。

---

## 4. 源码实现映射

| 功能模块 | 文件路径 | 职责说明 |
| :--- | :--- | :--- |
| **ASR 客户端** | `src/asr/mimo_asr.h`<br>`src/asr/mimo_asr.cpp` | 封装 `MimoAsrClient`，实现 `Transcribe()`（音频组包发送）与 `TestConnection()`（健康探测） |
| **会话编排层** | `src/asr/cloud_asr_common.h` | 派生自 `BatchAsrSessionBase`，接入统一录音收集与 VAD 头尾裁切管线 |
| **配置注册表** | `src/core/config_store.h`<br>`src/core/config_store.cpp` | 维护 `mimoApiKey`（DPAPI 加密）、`mimoBaseUrl`、`mimoModel`、`mimoLanguage` |
| **UI 控制面板** | `src/ui/providers/provider_mimo.h`<br>`src/ui/providers/provider_mimo.cpp` | Win32 DPI 自适应布局、下拉预设与无截断编辑框联动、测试连接按钮 |

---

## 5. 测试与探测规范（踩坑规则【A】）

在设置面板中点击「测试连接」时，必须严格遵守以下测试规范：

1. **深度探测与零状态污染**：
   - 测试连接发送一段微小且合法的静音 WAV，探测端点网络可达性与 API Key 有效性。
   - 绝不使用未经验证的假假连通探测，也不污染生产环境的长连接和全局识别状态。
2. **防假阳性判据**：
   - 依赖 HTTP 状态码 200 与返回值合法性，不将服务端返回的空文本或连接中断误判为成功。

---

## 6. 未来模型追踪与升级清单

当小米官方更新 ASR 服务时，按以下步骤跟进：

- [ ] **新模型发布核验**：若发布 `mimo-v2.6-asr` 或 `mimo-v3.0-asr`，检查其音频格式与语言参数是否发生变更。
- [ ] **流式协议评估**：若官方推出 WebSocket 全双工流式接口，评估接入 `IStreamingAsrSession`，以实现打字机般边说边出字的效果。
- [ ] **词表与热词增强**：关注官方接口是否开放热词或提示词上下文参数（类似 Qwen 的 `vocabulary_id` 或火山的 `boosting_table`）。
- [ ] **默认值更新**：同步更新 `kDefaultModel` 与 `Config::mimoModel`，并在 `LoadConfig` 中提供无痛平滑升级。
