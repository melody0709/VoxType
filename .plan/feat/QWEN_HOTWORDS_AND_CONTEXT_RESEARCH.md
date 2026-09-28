# 热词管理与上下文增强：跨后端调研、差距分析与实施记录

本文档对 VoxType 的**热词管理（Vocabulary）**与**上下文增强（Context Enhancement）**做一次跨后端审计。
与初版相比，本版把结论全部锚定到**可复核的证据**（官方页面抓取 + 仓库代码行号），并修正初版的两个方向性错误：

1. 初版把热词问题写成了「Qwen 一家的事」——实际 `vocabulary.json` 是**单一事实来源**，同一份词表同时下发到
   Qwen（即时热词）、火山引擎（`corpus.context` 内联热词）、LLM 纠错（`【用户词表】`），
   改默认权重/改映射会**同时改变四个后端的行为**。
2. 初版把火山的权重映射当成已落地能力——官方文档里**内联热词只文档化了 `word` 字段，没有任何 `scale`**。
   拿它当"已验证"的前提去设计权重语义，会把一个未经证实的字段当成约束。

---

## 1. 调研方法与证据来源

| 证据 | 来源 | 抓取/复核时间 |
| :--- | :--- | :--- |
| 百炼《提升识别准确率》 | `https://docs.bailian.console.aliyun.com/zh/model-studio/improve-asr-accuracy`（用户浏览器 CDP 实时读取） | 2026-09-28 |
| 百炼《非实时语音识别》 | `https://docs.bailian.console.aliyun.com/zh/model-studio/non-realtime-speech-recognition-user-guide` | 2026-09-28 |
| 百炼模型页 `qwen-audio-3.1-asr-flash-message` | 控制台模型广场（用户浏览器 CDP） | 2026-09-28 |
| 火山引擎《大模型流式语音识别 API》 | `https://www.volcengine.com/docs/6561/1354869`（页面自带"最近更新时间：2026.08.06"） | 2026-09-28 |
| 火山引擎《热词》（自学习平台） | `https://www.volcengine.com/docs/6561/155739`（"最近更新时间：2026.05.11"） | 2026-09-28 |
| 代码现状 | 本仓库工作区（行号在文中逐条给出） | 2026-09-28 |

火山侧两条关键事实是**逐字节核验**的：`https://www.volcengine.com/docs/6561/1354869` 的页面 HTML 中
**不存在子串 `scale`**（全文检索命中 0 次），热词管理页同样为 0 次。

---

## 2. 官方规范（核对结论）

### 2.1 Qwen-Audio 3.x（百炼）

**即时热词（`parameters.vocabulary`）**

| 项目 | 规范 |
| :--- | :--- |
| 形态 | 单次请求内联的 JSON 对象 `{"词": 权重}` |
| 权重取值 | **`[1,5]` 或 `50`**（预览/正式文档口径一致） |
| 超级热词 | `weight=50`，单请求**最多 50 个** |
| 词数上限 | 单请求最多 **2000** 个；若同时配置预编译热词，服务端合并后**随机选 2000** |
| 推荐权重 | **从 `4` 起测**（1~2 轻微偏好 / 3~4 明显偏好 / 5 强制偏好） |
| 词长 | 含非 ASCII ≤15 字符；纯 ASCII ≤7 个空格分段 |
| `language_hints` 交互 | 一旦设置，只匹配该语种的热词 |

**上下文增强（`payload.input.context` / `input.messages`）**

| 项目 | 规范 |
| :--- | :--- |
| 轮数 | 最多保留**最近 5 轮**，超出丢弃早期（不报错） |
| 每轮长度 | **≤400 字符**（同一轮所有 user/assistant 的 text 之和），超出**从末尾截断** |
| 角色 | `user`（`input_text`，放前几轮识别结果或词表）+ `assistant`（大模型回复，可选） |
| 生效机理 | **词表匹配**：`text` 里必须出现待识别的**原词**；纯语义背景效果有限 |
| 动态刷新 | 实时流式用 `continue-task` 追加/替换 |
| 顺序 | 非实时须置于音频消息**之前** |

### 2.2 火山引擎（豆包大模型流式识别）

**`request.corpus.context`（字符串，内层引号转义）** —— 官方原文：

> 热词或者上下文；**热词直传（优先级高于传热词表）**，双向流式支持 100 tokens，流式输入 nostream 支持 5000 个词
>
> `"context":"{"hotwords":[{"word":"热词1号"}, {"word":"热词2号"}]}"`
>
> 上下文，限制 800 tokens 及 20 轮（含）内……`context_data` 字段**按照从新到旧的顺序排列**

结论：
- 内联热词的**官方字段只有 `word`**，没有 `scale`、没有权重。
- `context` 字段里**两类内容的预算不同，不要混为一谈**：
  - **热词直传（`hotwords`）**：双向流式（`bigmodel` / `bigmodel_async`）100 tokens；`bigmodel_nostream` 5000 个词。
  - **对话上下文（`context_data`）**：800 tokens / 20 轮；官方速查表还把"双向流式的上下文容量"标为 **N/A**（见开放问题 Q6）。
- `context_data` 必须**从新到旧**排列。

**热词表（自学习平台，`boosting_table_id`/`boosting_table_name`）**
- 每个应用最多 500 张表；每张表**最多 5000 个热词**（仓库旧文档写的 2000 已过期）；
  每行一个词，**权重用 `|` 分隔、范围 1–10、不填默认 4**（如 `火山语音|8`）；
  每个词 <10 字；一个请求只生效一张表；不支持标点；阿拉伯数字要改汉字。

---

## 3. VoxType 现状审计（**改动前**快照，2026-09-28）

> 本节整体描述**本次改动之前**的工作区状态，用于解释 §4 的差距来源；符号名与行号都会随改动过期。
> **现行实现以 §5 为准**，不要把本节当作当前代码描述。

### 3.1 数据流：一份词表，四条下发路径

```
%APPDATA%\VoxType\vocabulary.json   （唯一事实来源，回退 cfg.qwenVocabulary）
        │
        ├─ GetEffectiveQwenVocabulary() ─ TranspileToQwenJson()
        │     └─ Qwen 流式 qwen_audio_streaming_session.cpp:48
        │        Qwen 批量 asr_session.cpp:424
        │        Qwen 探针 asr_probe_service_impl.cpp:87
        │        → 参数 parameters.vocabulary，且必须过 qwen_audio_json::IsValidVocabulary
        │          （qwen_audio_json.h:391-459：**只接受整数 1..5 或 50，>2000 条或 >50 个 super 直接拒**）
        │
        ├─ GetEffectiveVocabularyEntries() ─ BuildVolcengineContextJson()
        │     └─ 火山会话 volcengine_streaming_session.cpp:429-440（开关 volcEnableReuseVocabulary，默认 true）
        │        火山探针 asr_probe_service_impl.cpp:68
        │        → corpus.context 字符串，内含 {"hotwords":[{"word":…,"scale":WeightToVolcengineScale(w)}]}
        │          （**改动前**：`scale` 与 `WeightToVolcengineScale()` 现已删除，见 §5.8）
        │
        ├─ BuildLlmVocabularySection()
        │     └─ asr_attempt_manager.cpp:101-104（开关 llmVocabularyInjection，默认 true）
        │        → 系统提示【用户词表】，权重只用于排序，不下发数值
        │
        └─ TranspileToSherpaHotwords()   ← 无任何生产调用方（仅"预接线"）
```

### 3.2 现状实现细节

> 本节是**改动前**的审计快照（行号对应改动前的工作区），用于解释 §4 的差距来源；
> 现行实现见 §5。不要把本表当成当前代码描述。

| 位置 | 现状 |
| :--- | :--- |
| `vocabulary_manager.h:17` | `VocabularyEntry::weight = 50`（默认值） |
| `vocabulary_manager.cpp:334` | JSON 解析缺权重时不允许（必须显式） |
| `vocabulary_manager.cpp:415` | **行格式裸词默认 `50`** |
| `vocabulary_manager.cpp:417-428` | 引号词无权重时默认 `50` |
| `vocabulary_manager.cpp:250-253` | `WeightToQwen`: `w>=10 → 50`，否则 `clamp(w,1,5)` |
| `vocabulary_manager.cpp:523-544` | `TranspileToQwenJson`: **按词表顺序**遍历，前 50 个 super，第 51 个起降为 5，第 2000 个后 `break` |
| `vocabulary_manager.cpp:245-248` | `WeightToVolcengineScale`: `clamp(1+w/50*2, 1, 3)`（同一公式也是 `WeightToSherpaScore`） |
| `vocabulary_manager.cpp:562-609` | `BuildVolcengineContextJson`: `hotwords` + `context_type=dialog_ctx`，`context_data` = **[输入框文本, 历史（旧→新）]** |
| `vocabulary_manager.cpp:711-730` | `BuildLlmVocabularySection`: `stable_sort` 权重降序 + 200 条上限（**唯一做了排序的地方**） |
| `vocabulary_manager.cpp:636-642` | 模板文件注释写"权重可选 1-5 或 50"，示例 `"VoxType": 50` |
| `tab_vocabulary.cpp:35-37` | 状态栏按 `weight >= 10` 计"高优先级" |
| `tab_vocabulary.cpp:93` | 提示语 `Weights: 1–5 or 50 (default: 50)` |
| `volcengine_streaming_session.cpp:42-43,221-231,826` | 火山私有历史 `g_volcRecognitionHistory`（deque+mutex），仅火山记录/消费 |
| `input_context.h:341,476-508` | 读焦点文本，默认上限 200 字符，`TakeLastN` 保尾部；超时 200ms |
| `input_context.h:172,256` | TextPattern 路径先 `Move(-100)` 再读，**有效前文窗口 ≈ 100 字符** |
| `qwen_context.h:14-26` | `SanitizeText` 用 `TakeFirstN`，并注释"provider 从末尾截断，所以保留前 N" |
| `qwen_audio_streaming.cpp:206-216` | 单条 user 消息，超 400 再 `TakeFirstN` |
| `qwen_audio_http.cpp:163-172` | 单条 `input_text` 消息置于音频消息前 |
| `qwen_audio_streaming_session.cpp:659-679` | `continue-task` 仅在松键后发一次（`qwenEnableContinueContext` 默认 false） |
| `config_store.h:112,126` | `qwenEnableContinueContext` / `qwenEnableInputContext` 默认 false；`volcEnableContext` 默认 false |

### 3.3 已核实为「死代码」的部分

| 符号 | 现状 |
| :--- | :--- |
| `WeightToScale10` | 仅测试引用，**无生产调用方** |
| `TranspileToVolcengineHotwordsJson` | 仅测试引用；生产走 `BuildVolcengineContextJson`，两者**逻辑重复** |
| `TranspileToSherpaHotwords` | 无任何调用方（含测试） |
| `VolcengineRecognitionHistorySize` | 仅 `main_window.cpp:124` 的调试打印用 |

---

## 4. 差距与缺陷（修订版）

| # | 领域 | 差距 | 证据 | 影响 |
| :-- | :--- | :--- | :--- | :--- |
| D1 | 权重语义 | `w>=10` 即超级热词，且**裸词默认 50**，导致「随手加词」直接吃满 50 个 super 配额；而官方推荐普通词从 **4** 起 | `vocabulary_manager.cpp:250-253,415` | 🔴 发音近邻误纠偏 + 配额耗尽 |
| D2 | 跨后端耦合 | 词表是四路共用；改默认值 50→4 会让**火山 scale 从 3.0 掉到 1.2**、Sherpa score 同步下降（该映射公式两者共用） | `vocabulary_manager.cpp:245-248,255-258` | 🔴 单点改动静默改变其它后端行为（初版计划完全漏掉） |
| D3 | 火山内联热词 | 下发 `scale` 字段，官方内联格式**只有 `word`**，页面全文无 `scale` | §2.2；`vocabulary_manager.cpp:582-586` | 🟠 该字段很可能被忽略（权重对火山实际无效），属未文档化依赖；需在文档中标注为待验证 |
| D4 | 火山上下文顺序 | 官方要求 `context_data` **从新到旧**；代码把历史按**旧→新**追加 | §2.2；`vocabulary_manager.cpp:599-603` + `volcengine_streaming_session.cpp:436-437` | 🟠 与规范不符（当前轮数少，影响有限） |
| D5 | 火山预算 | `context` 在 `bigmodel`/`bigmodel_async` 只有 **100 tokens**，而代码会把整份词表（≤2000 词）+ 上下文塞进去；默认模式 `bigmodel_nostream` 才是 5000 词 | §2.2；`config_store.h:76` | 🟠 双向流式下大概率被服务端截断/丢弃，且**无任何日志** |
| D6 | Qwen 截断顺序 | `TranspileToQwenJson` 不排序即截断：用户写在词表末尾的高权重词会被降级为 5 或被丢弃 | `vocabulary_manager.cpp:523-544` | 🔴 明确的实现缺陷（排序修复成本极低） |
| D7 | Qwen 多轮上下文 | 只发**当前焦点输入框**的单轮 `user`，没有历史轮次；输入框为空时 `input:{}`，上下文彻底失效 | `qwen_audio_streaming.cpp:206-216`、`qwen_audio_http.cpp:163-172` | 🟠 连续语音输入时上下文断裂（终端/游戏/Canvas 等取不到文本的窗口更明显） |
| D8 | 截断层次不一致 | 读层 `TakeLastN`（保光标前）+ 发送层 `TakeFirstN`（保头部），两层语义相反；仅因两侧同为 400 才恰好是空操作 | `input_context.h:341` vs `qwen_context.h:25` | 🟡 语义矛盾；一旦预算分层不同就会静默丢关键上下文 |
| D9 | 取词窗口 | TextPattern 路径硬编码 `Move(-100)`，**实际前文只有约 100 字符**，远小于文档 400 | `input_context.h:172,256` | 🟡 400 字符预算名不副实（本次只记录，不改 UIA 取词策略） |
| D10 | 历史抽象 | 历史只存在于火山会话内部，Qwen 无法复用；`IsUsableAsrTextForContext` 过滤也只在火山侧调用 | `volcengine_streaming_session.cpp:42-43,221-231` | 🟠 重复实现风险 |
| D11 | 死代码 | `WeightToScale10` / `TranspileToVolcengineHotwordsJson` 重复且未被生产使用，却容易让维护者以为"火山权重已按 1–10 生效" | §3.3 | 🟡 认知负债 |
| D12 | 预编译+即时合并 | 同时配置 `vocabulary_id` 与内联词表时，服务端**合并后随机选 2000**，我们排序好的 super 优先级可能被随机丢弃 | §2.1 | 🟡 建议在 UI 提示（本次只记录，不改行为） |
| D13 | Sherpa 热词 | `TranspileToSherpaHotwords` 未接线到本地引擎 | §3.3 | 🟡 本地模式其实拿不到用户词表（本次不改） |

---

## 5. 本次实施的优化

### Phase 1：热词权重语义与排序精准化

1. **单一阈值**：新增 `krSuperHotwordWeight = 50` 与 `IsSuperHotword(int)`，作为"超级热词"的唯一判据。
2. **`WeightToQwen` 改为契约内映射**：`w>=50 → 50`（超级）；`1..5 → 原值`；`6..49 → 5`；`<=0 → 1`。
   输出恒为 `{1..5, 50}`，与 `qwen_audio_json::IsValidVocabulary` 的硬校验保持一致
   （该校验失败会让 `Connect()` 直接返回 `invalid_vocabulary`，属**不可重试**的硬失败）。
3. **`TranspileToQwenJson` 先稳定排序再截断**：权重降序（同权保持词表顺序），super 只取前 50，总数只留前 2000。
4. **默认权重改为 4**：`VocabularyEntry::weight`、行格式裸词、模板文件同步；`50` 只代表"超级热词"。
5. **UI 同步**：`tab_vocabulary.cpp` 状态栏与提示语改用 `IsSuperHotword`，文案改为"1–5（推荐 4）或 50（超级，最多 50 项）"。
6. **收敛死代码**：删除 `WeightToScale10` 与 `TranspileToVolcengineHotwordsJson`（生产只有一条火山构造路径），
   相关测试改为直接覆盖 `BuildVolcengineContextJson`。`TranspileToSherpaHotwords` 保留并在文档中标注"未接线"。

### Phase 2：上下文增强（Qwen 多轮 + 全局历史）

1. **`src/core/asr_history.{h,cpp}`（新）**：全局识别历史环形缓冲（线程安全，保留最近 20 轮，`Add/Snapshot/Size/Clear`），
   Core 层不做文本过滤（过滤留在 asr 层）。
2. **中心化记录点**：`DispatchAsrFinalText()`（所有后端最终文本的唯一出口）在 `NormalizeAsrText` 之后
   用 `IsUsableAsrTextForContext` 过滤后写入历史。火山会话内的私有 deque 与重复记录随即删除。
3. **`src/asr/asr_context.{h,cpp}`（新）**：`BuildHistoryTurns()` 取最近 N 轮、逐轮按**尾部**截到 400 字符并返回旧→新排列；
   `ClampTurns()` 把报文收敛到 provider 的"最近 5 条消息"窗口（`kMaxContextTurns`）；`NormalizeTurn()` 是唯一的 Trim + 尾部截断实现。
   "输入框与历史都没有文本"时的词表兜底由调用方用 `vocabulary_manager::BuildVocabularyWordList()` 组装。
   （早期实现里的 `ReplaceFieldTurn()` **已删除**：生产 `continue-task` 由报文构造器直接拼装"历史轮 + 新字段轮"，
   该 helper 当时只有测试调用。）
4. **Qwen 多轮上下文**：流式 `input.context` 与非实时 `input.messages` 现在可以是
   `[历史轮…, 当前焦点文本轮]`；`continue-task` 刷新时**保留历史轮**，只替换当前文本轮。
5. **配置项（强类型注册表三步）**：`qwen_history_context`（bool，默认 false）、
   `qwen_history_context_rounds`（int，默认 3，钳制 1–5），并在 Qwen 面板新增一行控件。
6. **截断语义统一**：所有出站上下文的截断一律**保尾部**（近端优先），`qwen_context::SanitizeText` 从
   `TakeFirstN` 改为 `TakeLastN`，消除 D8 的矛盾。
7. **火山上下文规范对齐**：`context_data` 改为**从新到旧**；内联热词按权重降序后再截断。
8. **删除无效的 `scale` 字段（Q1 已有定论）**：官方《热词与上下文》（`6561/2604976`，2026-08-25 更新）明文
   **"大模型没有类似小模型的权重概念"**，内联格式只有 `{"word": "..."}`；火山链路的权重**只用于排序**，
   `WeightToVolcengineScale()` 与其输出一并删除（`doc/volcengine` §3.8 同步更正）。
9. **不做客户端条数裁剪（审查后修正）**：官方 Q4 给出"双向流式建议控制在 30–50 个短词以内"，且截断规则是
   **从前往后保留、末尾截断**。一度按"50 词"实现客户端裁剪，但 **token ≠ 词数**：50 个长词可能远超 100 tokens，
   51 个短词也可能装得下——固定条数既不能保证装得下，又会提前丢弃本可保留的词。
   最终只保留**权重降序排序**（让服务端按真实 token 限额裁掉末尾的低权重词），并写
   `event=volc_inline_hotwords_prepared` 日志记录每次发送（含模式、是否双向流式、以及**实际排在第一位的词**）。
  > 该日志曾在建请求阶段记录却命名为 "sent"（审查 P3：连接失败也会留下"已发送"记录）；同时它依赖
  > `VOLC_DEBUG_LOG` 宏，而该宏在全仓 CMake 里**从未定义**，等于日志永不落盘。现改为走
  > `asr_runtime_log::Write`（由 Debug Mode 门控），实跑时可真正观测。

> **实施状态（2026-09-28）**：Phase 1 与 Phase 2 均已落地，`build.bat --test` 全绿（17 项架构守卫 + DPI 布局校验 +
  全部离线测试）。新增文件 `src/core/asr_history.{h,cpp}`、`src/asr/asr_context.{h,cpp}` 需要一并 `git add`。实跑结论见 §8。

### 明确不做（本次范围外）

- 不新增/不重写火山二进制帧协议与三模式 send/drain（踩坑规则【B】）。
- 不改 UIA 取词策略（D9 的 `Move(-100)`、D13 Sherpa 接线、D12 随机合并提示）。
- 不为 `scale` 编造取值范围：官方无文档且明文否认大模型有"权重概念"，因此直接**不再发送**该字段（见 §5 第 8 条）。

---

## 6. 开放问题（需要服务端实测或用户决策）

| 编号 | 问题 | 验证建议 |
| :--- | :--- | :--- |
| ~~Q1~~ | ~~火山内联 `scale` 是否被服务端接受/生效？~~ **已结案（2026-09-28）**：《热词与上下文》`6561/2604976` 明文"大模型没有类似小模型的权重概念"，内联格式只有 `{"word": "..."}` → 已删除 `scale`，权重仅用于排序 | 无需实测；若未来官方新增权重字段，按新文档再接 |
| ~~Q2~~ | ~~双向流式 100 tokens 预算下转发大词表会怎样？~~ **已实测（见 §8）**：301 条词表在 `bigmodel` 下**被接受、不报错**（请求完成并返回文本）。服务端是否截断**不可观测**（该模式下热词效果本就接近 0，无判别信号） | 如需确证截断，只能抓包/服务端日志；实践中按"大词表用 nostream、双向流式改用 `boosting_table_id`"处理 |
| ~~Q6~~ | ~~双向流式下发送 `context_data` 是被忽略、被拒还是生效？~~ **已实测（见 §8）**：`bigmodel` + dialog_ctx 请求**被接受**（无错误码），本样本**未观察到效果**（与无上下文基线逐字相同）——与速查表 "N/A" 一致，但"该模式一定忽略 dialog_ctx"属于超出证据的推断。对照组的 `bigmodel_nostream` 无法判别（该音频在 nostream 下无任何配置就已识别正确） | 实践口径：双向流式不要指望 dialog_ctx；需要上下文就用 `bigmodel_nostream` 或开二遍的 `bigmodel_async` |
| Q3 | Qwen 同时配置 `vocabulary_id` 与内联词表时，服务端随机取 2000 是否会丢掉我们的 super 优先级 | 用 >2000 词 + 预编译词表实测 |
| Q4 | 是否要把「输入框为空时用词表兜底」也应用到火山（当前只在 Qwen 侧） | 由用户决定；实现成本低 |
| Q5 | 多轮历史是否默认开启 | 现状默认关闭（隐私优先，与 `volcEnableContext` 一致） |
| ~~Q7~~ | ~~隐私边界：`asr_history` 会收录可用转写，密码框口述内容可能被后续云端请求带出~~ **已按方案②实现（2026-09-28，经两轮审查后定为"写入时排除 + fail-closed"）**：<br>• 探测**每次录音**执行、且**排在焦点字段读取之前**（Win32 `ES_PASSWORD` 同步判定；UIA `IsPassword` 异步，不占用录音启动延迟）；<br>• 判定 = `input_context::MustSkipHistoryForFocus()`（**只有明确 Safe 才允许记录**，Unknown 一律不记）+ `asr_context::ShouldRecordHistory()`，作用点是**写入历史**而非准备请求——"先在密码框口述、之后才开历史开关"与"火山读同一份历史"两条路径都被堵住；<br>• 探测同步捕获录音开始时的焦点（`CaptureSensitiveFocusAnchor`：前台窗口 + 其焦点控件），工作线程核对 UIA 元素所属窗口；`Safe` 只能由纯判定函数产出，且需同时满足"**录音开始时有焦点控件**（否则 `unknown_no_start_focus`）+ **UIA 密码属性读到明确布尔 false**（`PasswordQueryState` 三态，失败/非布尔 → `unknown_password_query`）+ **元素窗口句柄存在且落在锚点窗口内**"；焦点已移走 / 无句柄 / UIA 不可用 → 各自 `unknown_*` 并拒绝写入。它是**核对**，不是"录音开始瞬间的焦点快照"：**保证边界是 HWND 级**——同一窗口内两个元素之间切换（浏览器密码框→普通框共用渲染窗口句柄）不可区分，只靠"探测启动足够早"来压窗口期；<br>• `input_context` 读取链发现密码控件时**立即终止**（不再走父元素/坐标点/MSAA）并清空文本，火山发送层再查一次 `isPassword`；<br>• 单飞守卫改由工作线程释放（超时返回不再释放），字段读取器与探测各持一个守卫，不再互相阻塞。 | 跳过历史**只影响后续上下文增强**，本次识别与上屏不受影响；跳过时记 `reason=sensitive_focus`（命中密码）或 `reason=focus_unknown`（含 `answer=unknown_no_handle` / `unknown_focus_moved` / `unknown_uia_unavailable` 等），终端等窗口的历史缺失可据此诊断。若某类窗口出现大面积 `focus_unknown`，优先怀疑 UIA 窗口句柄不可得 |

### 6.1 外部审查轮次记录（2026-09-28）

第一轮实现后经外部审查（GPT），确认并修正下列问题；本节保留结论以便后续维护者理解取舍：

| 编号 | 发现 | 处置 |
| :--- | :--- | :--- |
| R1（P1） | "History ctx" 单独开启无效：`BeginAsrAttempt` 先要求 `qwenEnableInputContext`，HTTP / 流式发送路径又各重复了一道门控 | **已修**：历史装配与传输只受历史开关控制；焦点字段读取仍只受原开关控制（`asr_attempt_manager.cpp`、`asr_session.cpp`、`qwen_audio_streaming_session.cpp`） |
| R2（P2） | 火山固定裁到 50 词无法实现 100-token 预算（token≠词数），且写成"确定保留"是伪保证 | **已撤回裁剪**，只保留权重降序排序 + 发送日志；Q2 恢复为开放问题 |
| R3（P2） | Qwen 作为 fallback 时丢失新增历史（装配只在主后端为 Qwen 时执行） | **已修**：历史装配不再受 `asrBackend` 限制（只受历史开关），fallback 到 Qwen 同样携带 |
| R4（P2） | 配置 5 轮 + 字段非空 = 6 条消息，官方只保留最近 5 条，最旧一轮被静默丢弃 | **已修**：字段轮有文本时预留一个名额（`kMaxContextTurns`），两个报文构造器用 `ClampTurns` 收敛到 5 条，并补边界测试 |
| R5（P2） | 新提示按单行高度绘制但文案过长（且描述的条件不准确），四档 DPI 下都会被裁 | **已修**：改为 43 字符的准确单行文案（"Sends recent recognition results as context."） |
| R6 | 火山日志把预算值当成 kept 输出、且只在双向流式路径打印；多轮断言缺 `npos` 检查；`ReplaceFieldTurn` 仅被测试调用（生产不使用）；"逐字节一致"没有 golden 断言；长文本样本用重复字符区分不了保头/保尾 | **已修**：日志改为每次发送都记录真实条目数；断言补 `npos`；删除无生产调用方的 `ReplaceFieldTurn`；新增精确单轮片段断言与"保头/保尾"判别样本 |
| R7 | "删除 `scale`"被写成"服务端一定忽略过它" | **已修**：CHANGELOG / 文档改为纯文档论证（官方未定义该字段且否认权重概念），不声称实测结论 |

### 6.2 第二轮外部审查记录（2026-09-28，实跑之后）

| 编号 | 发现 | 处置 |
| :--- | :--- | :--- |
| R8（P1） | 敏感判定发生在"准备 Qwen 请求"而不是"写入历史"：开关关闭时在密码框口述 → 未标记 → 之后开历史即被追溯发送；火山也读同一份历史 | **已修**：探测改为**每次录音无条件执行**（异步，不增加启动延迟），`asr_context::ShouldRecordHistory(usableText, sensitiveFocus)` 在**写入时**排除；新增 `Config::asrSensitiveProbe`（runtime-only，值拷贝共享同一答案） |
| R9（P1） | 火山焦点文本路径不守密码标记：UIA 识别到 IsPassword 后仍继续走父元素/坐标点/MSAA，可能留下 `isPassword=true` 且文本非空；火山发送层不检查该标记 | **已修**：`TryReadFromElement` 在读任何 pattern **之前**判密码并清空文本；父元素遍历遇到密码即停止；`ReadInputFieldTextUIA` 在焦点元素块后与坐标点块后**提前返回**；`GetInputFieldContext` 末尾兜底清空；火山发送层再查一次 `isPassword` 并写 `event=volc_input_context_skipped reason=password` |
| R10（P2） | "单飞"守卫不成立：超时即清除运行标记，但分离线程可能仍在跑，连续超时可造成重叠 UIA 线程（字段读取器同样） | **已修**：运行标记改由**工作线程结束时**释放（成功与超时路径都不再释放）；字段读取器同步修正 |
| R11（P3） | Q6 结论超出实验证据（"不报错 + 本样本文本未变" ≠ "bigmodel 一定忽略 dialog_ctx"） | **已修**：§8 V5 与 Q6 行改为"请求被接受，当前样本未观察到效果" |
| R12（P3） | Q7 行仍写着"未实现密码标记" | **已修**：Q7 行同步为最终实现状态（见上表 Q7 行） |

### 6.3 第三~五轮（产品决定 → 焦点时序 → 三态收口，2026-09-28）

| 编号 | 发现/决定 | 处置 |
| :--- | :--- | :--- |
| R13 | 产品口径决定：**Unknown 按敏感处理**（不写入历史）。理由：焦点无法确认时，把转写留在可上传的全局历史里风险更高；代价只是后续上下文增强缺一轮，本次识别与上屏不受影响 | **已实现**：`input_context::MustSkipHistoryForFocus()` = "只有明确 Safe 才允许记录"；跳过记 `reason=sensitive_focus` / `reason=focus_unknown`（附 `answer=`）以便诊断终端等窗口的历史缺失。空探测句柄按 `unknown_no_probe` 处理，同样不记录 |
| R14（P1） | **焦点时序**：原实现先等焦点字段读取（最长 200 ms）再启动探测，探测于是读的是 200 ms 后的焦点；若期间从密码框切到普通字段，可能返回 Safe——即使 Unknown 改为拒绝也挡不住 | **已修**：探测移到字段读取**之前**，并同步捕获录音开始时的焦点（`CaptureSensitiveFocusAnchor` = 前台窗口 + `hwndFocus`）；工作线程核对 UIA 元素的窗口句柄（同一顶层窗口内才接受），移走/无句柄/UIA 不可用一律记 `unknown_*` 并拒绝写入。字段读取器与探测拆成两个单飞守卫，避免探测抢先占用守卫导致字段读取拿 `UIA_BUSY`。**不把异步查询描述成"录音开始瞬间的焦点快照"**：同一窗口内元素级切换在 HWND 层不可区分，只能靠"探测启动足够早"压缩窗口期，这一点在注释、AGENTS.md 与本节写明 |
| R15（P2） | 四象限测试只覆盖 `ShouldRecordHistory(bool, bool)`，没有覆盖真正决定去留的 `Unknown`／空句柄映射 | **已修**：`asr_json_protocol_test` 新增断言——只有 `kSensitiveProbeSafe` 允许记录；`sensitive` 与全部 9 种 `unknown_*`（含空句柄）都必须拒绝写入；空探测句柄归类为 `unknown_no_probe` 而非 `sensitive`（便于诊断）。守卫新增"探测必须早于字段读取"的接线检查 |
| R16（P1） | **`Safe` 可能是假的**：`IsPasswordElement()` 把"查询失败/返回值不是布尔型/明确 false"合并为 false，探测随即记 `kSensitiveProbeSafe` → 允许写入。另外 `VARIANT var` 未初始化就在失败路径 `VariantClear` | **已修**：拆出三态纯函数 `PasswordQueryState(hr, VARIANT)`（`kPasswordQueryFailed` / `NotPassword` / `Password`）与 `ReadPasswordQueryState()`（`VariantInit` + 全路径 `VariantClear`）；`Safe` 只允许由 `SensitiveProbeAnswerFromUia()` 在**明确 NotPassword + 句柄存在 + 锚点匹配**时产出，读取失败 → `kSensitiveProbeUnknownPasswordQuery`（拒绝写入）。读取链仍按"明确 true 才终止"（未知不阻断普通字段取字，不回归） |
| R17（P2） | **锚点不完整仍可能 Safe**：`CaptureSensitiveFocusAnchor()` 允许 `focus == nullptr`，窗口核对只看顶层窗口，于是没有起始控件可核对时也可能判 Safe | **已修**：新增纯函数 `InitialSensitiveProbeAnswer(hasForeground, hasFocus, win32Password)`——无前台窗口 → `unknown_no_focused_element`；有窗口但**无焦点控件** → `unknown_no_start_focus`（不启动 UIA 工作线程即拒绝）；`MatchesSensitiveFocusAnchor()` 同时要求 `anchor.focus` 非空（纵深防御）。文档中"绝不会进入历史"的表述改为与保证边界一致：**HWND 级核对**，同一窗口内元素级切换不可区分 |
| R18（复审收口） | 第四轮把 `hr != S_OK` 一律判 Failed 后，`IsPasswordElement()`（读取链）也随之变成"`S_FALSE` + 载荷为 TRUE"时**继续读取**——与"读取链只在明确识别到密码控件时停止"的口径不一致，且比改动前更宽松 | **已修**：拆成两条规则，各自纯函数化——**读取链**用 `PasswordPayloadIsTrue()`（只看载荷：明确 TRUE 即停止，忽略 HRESULT，矛盾状态不得把密码信号变回"继续读"）；**探测**用 `PasswordQueryState()`（仅 `S_OK` + 规范值可判定，其余 Unknown）。两条规则不得互相套用，已在 `AGENTS.md` 与代码注释写明，并补 4 条载荷回归断言 |

### 6.4 交接边界（给下一位审查者，2026-09-28 定稿）

**结论口径**：本版对**已确认**与**未知**焦点采取"当前约定的过滤策略"（历史写入 fail-closed），**不能**称为绝对防泄漏。

**保证**（可核对）：
1. 历史写入点唯一（`DispatchAsrFinalText`），且必须先过 `asr_context::ShouldRecordHistory(usableText, sensitiveFocus)`；`sensitiveFocus` 由 `input_context::MustSkipHistoryForFocus()` 给出——**只有 `kSensitiveProbeSafe` 放行**，9 种 `unknown_*` 与 `sensitive` 全部拒绝写入，并分别记 `reason=sensitive_focus` / `reason=focus_unknown answer=<原因>`。
2. 探测**每次录音**执行、**排在焦点字段读取之前**，且不占用录音启动延迟（答案在 transcript 时刻读取，经 `Config::asrSensitiveProbe` 传递）。
3. `Safe` 只能由纯判定函数产出，需同时满足：录音开始时有焦点控件（`InitialSensitiveProbeAnswer`）、UIA 密码属性为**明确布尔 false**（`PasswordQueryState` 三态）、元素窗口句柄存在且落在锚点窗口内（`SensitiveProbeAnswerFromUia`）。
4. 取字链发现明确密码信号即终止整条链路（`TryReadFromElement` 前置判定、父元素遍历停止、坐标点/MSAA 前提前返回、结果兜底清空），火山发送层再自查 `isPassword`；Qwen 侧经 `qwen_context::SanitizeText` 与流式诊断检查同一标记。

**不保证**（已知边界，属设计取舍）：
1. 焦点核对是 **HWND 级**：同一窗口内两个元素之间的快速切换（浏览器密码框↔普通框共用渲染窗口句柄）不可区分；探测的异步性由"尽早启动"压缩窗口期，**探测不是"录音开始瞬间的焦点快照"**。
2. 探测结果 `Unknown` 时**不写历史**（fail-closed 的直接代价）：终端/游戏/无焦点子窗口等可能因此长期缺失历史，靠日志可诊断，未做豁免清单。
3. 取字链**刻意**只在明确密码信号时停止：属性"未知"不影响普通字段取字（若改成"未知即停"，会丢掉这些窗口的字段上下文）。
4. 未重跑云端 API：本轮改动只涉及本地判定，请求体形状与第二轮实跑（§8）一致。

**已验证命令**：`build.bat --test`（8 个测试程序 + 18 项守卫 + 96/144/192/288 DPI + 回放工具构建）、`git diff --check`；历史判定的回归断言在 `tests/asr_json_protocol_test.cpp`（三态、载荷、9 种 unknown、空句柄、锚点缺失）。

---

## 7. 验证方式

1. `build.bat`（含 17 项架构守卫 + `validate_settings_layout.ps1`）。
2. `build.bat --test`：`asr_json_protocol_test` 覆盖权重映射、排序截断、默认值、火山顺序、
   历史轮构造与 5 条消息窗口；`qwen_audio_json_test` 覆盖多轮 `input.context` / `input.messages`、
   5 条窗口、`continue-task` 保留历史，以及单轮报文的**精确片段**断言。
3. 行为回归：历史/上下文开关关闭时，Qwen 单轮报文形状由精确片段断言锁定（不是"逐字节 golden 对比"；
   完整字段顺序仍由 `qwen_audio_json_test` 既有断言覆盖）。

---

## 8. 实跑记录（2026-09-28，真实 API + `tools\asr_audio_replay.bat`）

**方法**：`asr_audio_replay --wav test_wavs\0.wav --backend volcengine|qwen --show-text --fast`（10 秒中英混说样本；
本地 sherpa 参考转写 `昨天是 MONDAY TODAY IS礼拜二 THE DAY AFTER TOMORROW是星期三`）。词表与配置按实验临时改写，
跑完已从 `.bak\20260928-live-tests\` 还原；证据存 `build\artifacts\live-tests\*.txt`。

| 实验 | 配置 | 观测 | 结论 |
| :--- | :--- | :--- | :--- |
| V1 基线 | `bigmodel`，无热词无上下文 | `昨天是 Monday，today is 礼拜二，today after tomorrow 是星期三。` | 该模式下 "the day after tomorrow" **稳定误识别** |
| V2 nostream 裸基线 | `bigmodel_nostream`，无热词无上下文 | `…the day after tomorrow 是星期三。` | **同一音频在 nostream 本来就正确** → 这个样本无法用来判别 nostream 侧的热词/上下文增益（一度据此误判"热词生效"，已由本对照纠正） |
| V3 大词表 | `bigmodel` + 301 条（目标词写在文件**末尾**、权重 50） | 请求成功、无 `45000001`；日志 `entries=301 first_word=the day after tomorrow` | 大词表在双向流式**被接受**；权重排序在真实请求里生效（末尾的高权重词被排到首位） |
| V4 负对照 | 同 V3，但目标词权重 4 | 日志 `first_word=填充词001` | 排序行为与权重严格对应 |
| V5 上下文 | `bigmodel` + dialog_ctx（3 轮合成历史，含目标词） | 请求**被接受**（无错误码）；转写与无上下文基线逐字相同 | **Q6：该请求被接受，本样本未观察到任何效果**（"bigmodel 一定忽略 dialog_ctx"是超出实验证据的推断，不写进结论） |
| V6 Qwen 批量 | `qwen_http` + **仅历史开关**（焦点字段关闭）+ 3 轮历史 | 请求成功：`昨天是 Monday， Today is 礼拜二， The day after tomorrow 是星期三。` | **R1 修复实跑通过**：只开历史也能发出多轮 `input.messages` |
| V7 Qwen 流式 | `qwen-audio-3.0-asr-flash-streaming` + 3 轮历史 | 请求成功；会话日志 `event=input_context using_snapshot=0 captured=0 chars=0 history_turns=3` | 多轮 `input.context` 在 `/api-ws/v1/inference` 上被接受 |

**明确不可观测 / 未验证**：
- 服务端对超出 100 tokens 的词表**是否截断**（该模式热词效果本身接近 0，没有判别信号）。
- 热词/上下文在 **nostream** 下的准确率增益（样本本来就对；需要一段"未被正确识别"的音频）。
- 旧版本发的 `"scale"` 是否曾被服务端消费（Q1 仍是文档论证，不是实测）。

**顺带发现（可操作）**：同一段音频 `bigmodel` 比 `bigmodel_nostream` **更差也更慢**（误识别 + 约 7.5s vs 2.0s；
`--fast` 下计时仅供量级参考）。仓库默认模式已是 `bigmodel_nostream`，建议保持；需要热词/上下文时更应避开 `bigmodel`。

### 8.1 实跑对工具的连带修复

`tools\asr_audio_replay.bat` 是本仓库唯一的云端后端实跑工具，但它在架构重构后**已经无法链接**（`EXCLUDE_FROM_ALL`，
不在任何守卫覆盖范围内，因此静默腐烂至今）。本次修复：补 `d2d1.h`/`dwrite.h`；删除它与已编译模块（`app_state` /
`audio_capture` / `streaming_vad_trimmer` / `engine_local` / `asr_metrics`）重复定义的全局量；把 `app_state.cpp` /
`asr_metrics.cpp` 加入其源列表；新增 `--context-rounds <n>` 合成上下文注入；启动时按配置生效 Debug Mode 日志。
`build.bat --test` 现在会**构建**该工具（不运行）以防再次静默腐烂。
