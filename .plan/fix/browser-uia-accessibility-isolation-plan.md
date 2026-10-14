# 浏览器 UIA 无障碍触发的持续性能下降：VoxType 影响核实与修复方案

> 日期：2026-10-13
>
> 状态：浏览器隔离与配套扩展已实现；2026-10-13 已做 Chrome CDP 联调并获用户实际口述确认。完整性能对照、Edge/真实混合 DPI 双屏与商店分发仍未签收。当前验证以 `doc/browser-bridge-validation.md` 为准。
>
> ## 实施状态（v0.11.9，2026-10-13 补记）
>
> **已交付**：P0 根层隔离（`src/core/browser_target.h` + `input_context.h` / `selection_context.h` 门控）；P1 配套扩展（`extensions/browser-bridge/`）、Native Messaging host 模式（复用 `VoxType.exe`，在单实例互斥体之前进入）、桌面桥（`src/core/browser_bridge.h`）、fail-closed 三态映射、扩展载荷 CMake install、开发注册脚本；守卫 20 → 23 项（含两条 "browser targets never reach accessibility" 与一条 "companion never yields a password value"）；离线回归 `tests/browser_bridge_test.cpp`。
>
> **与本文原文的偏差（均已在下文标注）**：
> 1. **选区/文档文本未走扩展桥**（§5.1.2 原列在范围内）。浏览器目标在 `selection_context` 上直接报告 `browser_skipped`（无选区），未实现扩展侧 `selection` 字段。影响面：`qwen_free` 改写、Undo 复核、学习反射在浏览器内不可用——三者默认均关闭。理由是先把"焦点字段 + 敏感判定"这条主链路做扎实，选区属于低频路径。
> 2. **设置页状态行推迟**（§5.1.5）：理由见该节；当前用日志 + 扩展控制台观测。
> 3. **`main.cpp` 顶到 150 行上限**（余 0 行）。host 模式入口只用了 1 行 include + 1 行调用，entry 说明写在 `browser_bridge.h`；下次动 `main.cpp` 必须先瘦身（AGENTS.md 已记）。
> 4. **TSF 路线未评估**（§4.9 结论不变）。
>
> **仍未签收**：本机浏览器性能对照（§5.0 判据 1、2）、§8.2 的完整手工矩阵、§8.3 性能矩阵、Edge 联调、商店/企业分发。Chrome 焦点字段端到端已由用户确认，并有扩展侧 30 用例/50 查询实测；不等同于完整性能签收。
>
> **本次已实测的部分**（无浏览器条件下可验证的）：`build.bat --test` 全绿（23 项守卫 + 13 个离线测试）；正常启动后 `\\.\pipe\VoxType.BrowserBridge.<SID>.<session>` 管道实例确实建立；以 `chrome-extension://<id>/` 启动且 stdio 为重定向管道时进程以退出码 **2** 立即退出（未变成托盘程序）——退出码 2 来自"祖先链里没有浏览器"，即 host 模式的门禁真的生效；正常启动不会被误判成 host。
> **无法在无浏览器条件下验证**：扩展与 host 的真实握手、`executeScript` 取到焦点字段、窗口矩形匹配与 `extensionId` 校验（后者位于祖先检查之后，必须先有浏览器父进程才能到达）。
>
> **首轮实机测试（2026-10-13）**：用户在 Chrome 中口述仍无上下文。诊断顺序与结论——
> 1. 查注册表发现 **Native Messaging host 从未注册**（HKCU 两个键均缺失），已补注册（安装步骤漏做的成本被 README 显式写出）；
> 2. 注册后桌面日志给出 `no_window_match peers=1 matched=0 advertised=[0,0,1540x1672] anchor=[0,0,1925x2089]` —— `peers=1` 证明扩展/管道已通，比值恰为 **1.25**，即**窗口矩形比较没做 DIP→物理换算**（`SetThreadDpiAwarenessContext(UNAWARE)` 对 PerMonitorV2 进程不虚拟化）。已改为显式换算的**纯函数** `BoundsMatchPhysical`，并把这对真实矩形写进离线回归。
> 3. 本轮同时把桥的失败原因拆开（`no_window_match` / `window_ambiguous` / `no_anchor`）并把证据（peer 数、广告矩形、锚定矩形、DPI）写进 reason —— **正是因为上一版把四种失败合并成 `not_connected`，第一轮才无法远程判断根因**。
>
> 参照对象：`D:\GITHUB_melody0709\zencrop_ocr_pxipin`
> - 研究：`.plan/fix/browser-accessibility-dom-disconnect-research.md`
> - 实施：`.plan/fix/browser-dom-companion-implementation-plan.md`
> - 验证：`doc/browser-companion-validation.md`
> - 提交：`2982cca`（v3.1.10，扩展化落地）、`db00c82`（v3.2.0 加固）、`fb056ce`（设置页提示与安装引导）

---

## 1. 结论

**是，VoxType 存在同一类问题；而且触发面比 ZenCrop 更宽、频率更高。**

| 维度 | ZenCrop（已完成修复） | VoxType（现状） |
| :--- | :--- | :--- |
| 触发时机 | 截图选框会话内 | **每次录音**（含默认 `local` 后端） |
| 触发原语 | MSAA 预热 + UIA 点查询 | `WM_GETOBJECT(UiaRootObjectId)` + `GetFocusedElement` ×2 + `ElementFromPoint` + MSAA `get_accValue` |
| 触发频率 | 每次截图若干次 | 每次录音 1~3 轮（敏感探测**无条件**；字段读取受开关控制，qwen 音频族**默认开启**） |
| 浏览器识别 / 白名单 | 有 BrowserWhitelist，现已用于拦截 | **完全没有**（全 `src/` 无 chrome/msedge/browser 判定） |
| 失败降级路径 | 修复后硬边界：不再回退 UIA | 现状即 UIA，无任何降级 |
| 与用户的关系 | 截图工具，按需使用 | **语音输入法**：一次对话可能连续录音十几次 |

三点关键差异，决定了我们的方案不能照搬：

1. **我们是高频触发**：录一句话就查一次。用户"对着浏览器说话半小时"= 几十轮 UIA 会话，而 ZenCrop 只在那一次截图里触发。
2. **我们要的东西不同**：我们要的是**焦点字段文本 + 密码判定 + 选区文本**，不是元素矩形。扩展通道因此可以做得比 ZenCrop 更窄（无候选链、无滚轮父级导航、无几何矩阵），但必须额外处理**值语义与隐私边界**（见 §6.4）。
3. **我们有更强的前置约束**：`asrSensitiveProbe` 的 fail-closed 三态、历史写入门控、以及 `tools/check_architecture.ps1` 的隐私接线检查都是机械守卫；扩展答案必须映射进**同一组答案码**，不允许引入第二个 "Safe" 来源。

**关于策略本身**：单看"能不能不碰无障碍又拿回焦点字段"，扩展是**唯一现实的通道**（§4.4 逐条排除了 MSAA、CDP、键盘重建、OCR、内部抑制开关等路线；TSF/真输入法是另一条正道，但等于改产品形态）。但**"不做扩展"是一个同等正当的终局选择**——硬约束是"不拖慢浏览器"，而"浏览器内有字段上下文"只是质量项。是否值得承担扩展的装机与维护成本，必须先用数据回答（§4.5 的价值量化 + §4.8 的分发调研 + §9 的 P1-g 闸门），而不是由"对标 ZenCrop"决定。

---

## 2. 外部事实（由 ZenCrop 核实，本次直接引用，不重复论证）

以下条目来自 §"参照对象"的研究文档，本次已在 ZenCrop 仓库源码中二次确认存在对应实现：

| 事实 | 含义 | 对我们的后果 |
| :--- | :--- | :--- |
| Chromium 用 `EnableAXModeFromPlatform` 把新请求与既有模式做 **OR**，存进**进程级** `platform_ax_mode_` | "客户端对象已释放" ≠ "浏览器网页无障碍已关闭" | 退出 VoxType 也不会恢复，只能重启浏览器 |
| Chrome 138+ 在 Windows 上默认提供原生 UIA；Chrome 154 的 `RecordUiaClientConnection` 在断连分支**只记统计、不关闭网页模式** | 没有"客户端退出即撤销"的租约 | 不能指望"少查询几次"或"及时释放"解决 |
| 旧的自动关闭 flag 与 `OnAccessibilityApiUsage` 路径在当前版本**已不存在** | 网上常见的 `enable-auto-disable-accessibility` 方案无效 | 不要把旧教程写进文档 |
| `chrome://accessibility` 的 "Suppress automatic accessibility enablement from platform API consumers" 能**免重启**过滤来源模式 | 它是全浏览器级的诊断开关，会同时影响 NVDA/Narrator | **只作诊断与临时恢复**，不得让 VoxType 自动操作它 |
| ZenCrop 改为"MV3 扩展直读 DOM + Native Messaging"后，用户复核"选框正常、无明显性能影响" | 该路线在同类工程上已跑通 | 我们的 P1 沿用同一路线 |

**不在本文承诺的范围**：我们不主张"UIA 一次查询必然开启完整读屏模式"，也不主张"修复后一定回到某个分数"。这些需要我们自己实测（§5.0 与 §8.3）。

---

## 3. VoxType 现状证据

### 3.1 UIA / MSAA 的全部落点（穷举）

| 位置 | 调用 | 浏览器相关性 |
| :--- | :--- | :--- |
| `src/core/input_context.h:98-106` `EnsureAccessibilityTree` | `SendMessageW(hwnd, WM_GETOBJECT, 0, 0xFFFFFFFC)`（`UiaRootObjectId`） | **最直接的触发点**：主动要求前台窗口提供 UIA 根 provider |
| `src/core/input_context.h:459` | `ReadInputFieldTextUIA` 在 Win32 `Edit` 快路径之后调用上面的函数 | 浏览器 `Chrome_RenderWidgetHostHWND` 必然走到这里 |
| `src/core/input_context.h:462-464` | `CoCreateInstance(CLSID_CUIAutomation)` | 每次字段读取新建客户端，无实例缓存 |
| `src/core/input_context.h:479` / `:511` | `GetFocusedElement` / `ElementFromPoint` | 跨进程 UIA 查询 |
| `src/core/input_context.h:492` / `:524` | `TryWalkParentsForText`（父链最多 8 层） | 放大浏览器侧节点遍历成本 |
| `src/core/input_context.h:539-562` | MSAA 回退：`AccessibleObjectFromWindow(hwndFocus, OBJID_WINDOW, IID_IAccessible)` + `get_accValue` | MSAA 仍接入同一套浏览器无障碍体系 |
| `src/core/input_context.h:683-740` `BeginSensitiveFocusProbe` | 独立 MTA 线程：`CoCreateInstance` + `GetFocusedElement` + `GetFocusedElement` 上的 `UIA_IsPasswordPropertyId` | **每次录音无条件执行** |
| `src/core/selection_context.h`（`co-creates` 于 UIA 选区/文档读取路径） | `GetFocusedElement` + 选区/文档读取 | `qwen_free` 改写（默认关闭）与学习反射读回（默认关闭） |

项目内**没有**任何浏览器/Chromium 识别、白名单或降级分支：`src/` 下唯一按进程名分流的是 `text_injector.cpp` 的微信特判（`QueryFullProcessImageNameW`），与 UIA 无关。

### 3.2 触发条件与频率（默认配置）

| 链路 | 守卫条件 | 默认值 | 频率 |
| :--- | :--- | :--- | :--- |
| **敏感焦点探测** | **无任何开关**（`asr_attempt_manager.cpp:475-489` 注释明确"runs for EVERY recording"） | 恒定执行 | **每次录音 1 轮 UIA** |
| 焦点字段快照 | `asr_attempt_manager.cpp:493-499`：`asrBackend == L"qwen"` && `qwenEnableInputContext` | `qwenEnableInputContext = true`（`config_store.h:150`）、`qwenModel` 默认 `kMessageModel31`（`:121`）→ qwen 后端下**默认开启** | 每次录音 1 轮完整读取链（UI 线程阻塞 ≤200 ms，`input_context.h:801`） |
| 火山流式字段读取 | `volcEnableInputContext` | `false`（`config_store.h:105`） | 默认不发生 |
| continue-task 刷新 | `qwenEnableContinueContext`（默认 `true`，`config_store.h:135`）× 输入上下文开关 | 默认成立 | 每场录音**再一次**完整读取链 |
| 选区/文档读取 | `qwenFreeRewriteEnabled` | `false`（`config_store.h:208`） | 默认不发生；`asrUndoEnabled`（`:192`）、`asrLearningEnabled`（`:182`）同为 `false` |
| 学习反射读回 | `text_injector.cpp:247-254` 用 sensitive 标记短路 | 关闭时不读 | — |

> **默认后端是 `local`（`config_store.h:84`）**，所以字段读取在默认配置下不发生；但**敏感探测不区分后端**，因此**即使用本地模型，每次对着浏览器说话都会触发一轮 UIA**。这是本次最主要的发现。

### 3.3 现有"减速阀"都不解决根因

- `s_uiaProbeRunning` / `s_uiaReadRunning` 单飞（`input_context.h:573-574`）：只防并发，不防跨录音累积。
- `s_triggeredWindows` 按 HWND 去重（`:95-106`）：**集合永不清理**，因此"每个窗口只 Nudge 一次"——但副作用是浏览器侧模式一旦被这唯一一次 Nudge 打开就已尘埃落定；反之 HWND 复用会让 Nudge 静默失效（顺手要修，见 §5.0）。
- 200 ms 超时（`:801`）：只保护调用方延迟，不取消底层 UIA 调用（worker 已 `detach`）。
- **完全不存在**基于时间窗的限流 / 退避 / 缓存。连续按 20 次热键 = 20 轮真实 UIA。

### 3.4 现状的功能耦合（改动的代价必须写清）

跳过浏览器 UIA 后，浏览器目标上会失去：

| 能力 | 现状实现 | 跳过后的行为 | 影响面 |
| :--- | :--- | :--- | :--- |
| 敏感判定 | `SensitiveProbeAnswerFromUia`（`input_context.h:666-673`） | 恒为 Unknown → `MustSkipHistoryForFocus` 判真（`:771-773`） | **fail-closed，安全**；代价是浏览器内转写不进历史、注入被标 `sensitive`（`main_window.cpp:416-421`、`:515-521`）且不参与反射学习（`text_injector.cpp:247-254`） |
| 焦点字段上下文 | `qwen_context::CaptureInputFieldText`（`qwen_context.h:30-34`） | 空字符串 | qwen 音频族的输入上下文与 continue-task 在浏览器内失效（P1 由扩展恢复） |
| 选区 / 文档读回 | `selection_context` | 不可用 | `qwen_free` 改写、Undo 复核、学习反射在浏览器内失效（三者默认均关闭） |

> 这是**刻意的、有意的降级**：把"浏览器被持续拖慢"换成"浏览器内没有上下文增强"。后者用户可感知、可解释、可由扩展恢复；前者不可逆且要重启浏览器。

---

## 4. 目标与非目标

### 4.1 目标

1. **不再让任何 Chrome/Edge/Firefox 进程因为我们而进入平台无障碍模式。** 这是硬边界，任何失败路径都不得回退到 UIA/MSAA。
2. **用配套扩展把浏览器内的能力恢复得比 UIA 更好**：密码判定从"三态猜测"变成 DOM 权威事实；字段文本从"插入符/父链启发式"变成 `activeElement` 直读。
3. 归档可复核的证据，让"是否修好"能被测量，而不是被声明。

### 4.2 非目标

- 不改动 ZenCrop，不复制其几何/候选/滚轮相关代码（我们不需要元素矩形）。
- 不动任何浏览器的启动参数、flags、profile，或自动操作 `chrome://accessibility`。
- 不扩展为通用"网页文本抓取"能力：首版只读**焦点控件**的值与选区，不读正文、不读 Cookie、不读密码框的值。
- 不在本次触碰 P6 语法收敛排除清单（火山协议层、qwen_free_proto、doubao protobuf/Opus）。

### 4.3 硬边界（写入 AGENTS.md 的不变量）

> **浏览器目标的录音不得通过 UIA/MSAA 读取焦点、字段或选区信息。** 覆盖：扩展未安装、未授权、页面受限（`chrome://`、PDF、扩展商店）、断连、冷启动、查询超时、多窗口歧义、非 http(s) 页面。以上任一情形一律返回"无上下文 + 敏感 Unknown"，并记录 reason。**不允许用"减少频次"、"仅去掉预热"或"临时回落一次"来绕过本规则。**

### 4.4 路线比选：为什么是扩展，以及哪些路线被排除

我们的需求只有两条：**①不碰浏览器无障碍；②尽量保住"焦点字段"这项能力。** 把可行路线列全再裁决：

| 路线 | 拿到焦点字段事实 | 不碰无障碍 | 装机 / 维护成本 | 浏览器侧持续成本 | 判定 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **A. MV3 扩展 + Native Messaging** | 是（DOM 权威事实） | 是 | 高（每浏览器一套 + host 注册 + 分发） | 低但**非零**（SW 被长连接保活 + host 进程 + 单次注入查询） | 唯一能同时满足 ①② 的可产品化路线；P1 候选 |
| **B. 只隔离，不恢复** | 否 | 是 | **零** | **零** | **B 不是"没方案"，是正当的终局选择**。是否可接受取决于字段上下文在浏览器内的实际价值（§4.5） |
| **C. 注册为 TSF 文本服务（真输入法）** | 是（这是 OS 官方给输入法的通道，还能顺带拿到输入范围/密码标记） | 是 | 极高（in-proc COM 文本服务 + 安装注册 + 32/64 位；DLL 会被载入大量宿主进程） | 未知（进入每个 TSF 宿主进程） | 只有把产品形态改为"真输入法"时才成立；**不是本次修复路径**，详评见 §4.9 |
| D. 仅用 MSAA（只删 UIA 预热） | 是 | **否**（MSAA 接入同一套浏览器无障碍体系） | 低 | 持续 | 排除（ZenCrop §5.1 已裁决） |
| E. CDP（`DOM.getNodeForLocation` / Runtime 类接口） | 是 | 是 | 低 | 低 | 排除：Chrome 136+ 要求非默认 `--user-data-dir` + 调试端口；改在扩展里挂 `debugger` 权限会带来常驻调试提示与更强权限，比 A 更重 |
| F. 用键盘钩子 / 剪贴板重建字段内容 | 否（不可靠） | 是 | 低 | 低 | 排除：鼠标改插入符、自动填充、撤销、录音前已有内容都无法重建；不能作为隐私判定依据 |
| G. 截图 + OCR 读字段 | 部分 | 是 | 中 | 中高 | 排除：为一个"上下文增强"引入屏幕采集与 OCR，代价与收益不成比例 |
| H. 操作 `chrome://accessibility` 的抑制开关 | 是（沿用现有 UIA） | 否（只是被过滤） | 低 | 持续，且影响全浏览器其他辅助工具 | 排除：普通扩展无权访问 `chrome://`；模拟点击内部 UI 不可维护（ZenCrop §4.3） |

**结论：只有 A 与 C 能同时满足 ①②；C 的代价是产品形态改变，因此 A 是唯一现实的"恢复"路线。但 B 必须被当成同等正当的备选**——因为 ② 是硬约束，① 只是质量项。

**本文的推荐（分两件事，不要混为一谈）：**

| | 定位 | 推荐强度 |
| :--- | :--- | :--- |
| **P0（根层隔离）** | **问题的修复本身**：消除不可撤销的浏览器降速 | **无条件做，且不依赖任何数据** |
| **P1（配套扩展）** | **能力的恢复与增强**：把字段上下文与敏感判定拿回来 | **推荐做，但先过 §4.5 价值量化 + §4.8 分发调研两道闸门**；未过闸门就停在 B，**这不算"问题没修"** |
| C（TSF） | 产品转型，不是修复 | 不推荐（§4.9） |

三条放弃 P1 的明确触发条件（满足任一即停在 B，无需反复权衡）：① §4.5 显示浏览器内上下文命中率与长度都很低；② 分发无解（拿不到零摩擦安装路径）；③ 目标用户主力浏览器不在可覆盖范围内。**因为 P0 已把降级路径定义完整（无上下文 + fail-closed 隐私），"不做扩展"不会留下任何未定义行为。**

### 4.5 决策前置：先量化"浏览器内字段上下文"的真实价值

在写 P1 任何一行代码之前，先用零风险的方式回答问题：**浏览器里的字段上下文到底被用到几次、有多长？**

- 做法：在 P0 落地**之前**先加一条**只统计不改行为**的日志（沿用现有 `event=input_context_snapshot chars=... layer=...`，补上目标进程名 / 窗口类名 / 是否浏览器），跑一段真实使用。
- 判据：若浏览器内录音占比低，或命中上下文的比例与平均字符数都低，则**直接选 B，P1 整体取消**，把预算放在别处。
- 若占比高，则 P1 的价值成立，再进入 §4.6 的轻量化设计。
- 注意：这条统计必须在 P0 之前或与之同批落地；P0 落地后浏览器路径不再产生上下文，数据就消失了。

### 4.6 若选 A：我们要比 ZenCrop 更轻的地方

ZenCrop 的扩展是"选框会话内、鼠标驱动、需要元素几何与层级"的形态，因此它有常驻 content script、会话状态机、观察者与租约。**我们的访问模式完全不同（低频、非鼠标驱动、只关心焦点控件），因此不应该照抄它的形态：**

| 项 | ZenCrop | 我们（目标形态） |
| :--- | :--- | :--- |
| 页面驻留 | 常驻 content script（`all_frames`、`document_start`，全站生效） | **不驻留**：按需 `chrome.scripting.executeScript`（每次录音 1~3 次） |
| 观察者 | MutationObserver + 滚动/尺寸监听（活动期） | **无**：不观察 DOM，不监听滚动 |
| 定时器 | 租约续期定时器 | **无**：无心跳、无轮询、无 alarms 保活 |
| 协议 | 几何、候选链、滚轮层级、失效代际 | 单请求单应答，字段更少（§5.1.3） |
| 会话状态 | Idle→Active→Ended + 租约兜底 | 无会话；`requestId` + `document.hasFocus()` + 文档代际即可 |

**必须实测并如实披露的残留成本**（不得写成"零损耗"）：service worker 因长连接被保活、host 进程常驻（参考量级：ZenCrop 实测 host ~8.45 MiB Working Set）、首次 `executeScript` 注入延迟、每次查询的往返 p50/p95。

### 4.7 关于"不能拖慢浏览器"的准确说法

三件事必须分清，否则会因为"扩展也是常驻"而误判：

1. **拖慢浏览器的不是"有人读 DOM"，而是"浏览器以为有读屏工具在盯着页面"。** 一旦进入无障碍模式，此后每一次 DOM 变更与布局都要额外维护网页无障碍树，代价是**持续的**。
2. **无障碍模式的成本不可撤销**：退出应用、关闭页面、刷新都不恢复，只能重启浏览器（§2）。这也意味着"少查几次"没有意义——一次就够。
3. **扩展通道的成本是离散、可测、可归零的**：查询是毫秒级一次性动作；停用扩展即全部归零；非录音期页面零工作。

所以两者的差别不只是量级，而是**性质**：一个是"不可逆的持续税"，一个是"可关闭的按次成本"。这才是选 A 的决定性理由，而不是"ZenCrop 这么做所以我们也这么做"。

### 4.8 若选 A：分发是最大的非技术风险（**必须先回答再写代码**）

扩展要真正落地，必须先解决"用户怎么装上"：

- **浏览器覆盖面**：Chrome、Edge 各需一套注册；360/QQ/搜狗等国产壳浏览器是否接受同一 MV3 扩展与 Native Messaging 注册键**未知**；Firefox 需要另一套 manifest 与注册方式（`allowed_extensions`）。覆盖面越宽，装机与维护成本越高。
- **零摩擦安装**：手动"加载已解压扩展"需要开发者模式，对普通用户不可接受；其余路径要么上架扩展商店，要么用企业策略强制安装（通常需要管理员权限 / MSI）。
- 这两点必须在 P1 的 spike 阶段先用一次调研回答，**不允许先写协议与桥再接分发**。

### 4.9 路线 C（TSF / 注册为输入法）详评：**本次不做**

**先纠正一个容易得出的错误结论**：TSF 不是"更好的选择"，它只是"另一条同时满足 ①② 的路线"。它满足 ① 的方式是把整个产品换成另一种东西，因此不是性能修复的候选实现，而是**产品转型**的候选。

#### 4.9.1 决定性约束：上下文只给"当前活动的那个输入法"

TSF 的模型是：每个线程同一时刻只有一个**活动的文本服务**（active language profile），文档焦点事件（`ITfThreadMgrEventSink::OnSetFocus`）与 `ITfContext` 只交给它。非活动的文本服务拿不到文档上下文——这也是为什么输入法必须"被选中"才能工作。

推论：

- 用户在用微软拼音 / 搜狗时，**我们拿不到任何上下文**。"用 TSF 读浏览器字段"实际上等价于"**让用户把我们设成当前输入法**"。
- 一旦我们成了当前输入法，用户就**在我们这里打不出中文**（除非我们自带拼音/形码引擎）——等于把用户原有输入法挤掉了。
- 所以 TSF 路线的可行性不取决于"能不能写出来"，而取决于"我们愿不愿意取代用户的输入法"。这是**产品决策**，不是技术选型。

> ⚠️ 上述是权威架构约束，但**必须在动手前用一个 spike 实证**：TSF 是否允许一个"非键盘型"文本服务在用户保持原输入法时被激活并拿到 `ITfContext`。若不成立（预期如此），结论即"TSF 路线 = 取代用户输入法"。

#### 4.9.2 "网上有公开输入法，直接搬过来"为什么行不通

| 障碍 | 具体内容 |
| :--- | :--- |
| **许可证** | 主流 Windows 开源输入法并非宽松许可：Weasel（小狼毫，Rime for Windows）为 **GPL-3.0**（已核实，多个镜像仓库的 LICENSE 一致）。**本仓库根目录没有 LICENSE 文件**（`git ls-files` 只找到 `third_party/opus/COPYING`），因此"能否吸收 GPL 代码"本身就是未定的合规问题；在定下来之前，GPL 实现一行都不能进。注意 librime 引擎与 Windows 前端是两套不同许可，不能混为一谈（须逐个核实，不得凭印象）。 |
| **抄的东西对不上** | 开源输入法的主体是"按键处理 + 拼音引擎 + 候选窗 + 用户词典 + 配置体系"。我们要的只是"读焦点上下文 + 判定敏感 + 不改行为"。搬过来约 90% 是净负担，而真正缺的那部分（与现有托盘/热键/录音/注入/ASR 链路的整合）没有任何开源实现可以抄。 |
| **风险面反而扩大** | TSF 文本服务是 in-proc COM，DLL 会被载入**每一个 TSF 宿主进程**（Office、浏览器、记事本…）。它一旦崩溃或卡住，受影响的是**全系统的文字输入**，而不是一个录音功能。这与"不能拖慢浏览器"的初衷是**反方向**的：我们把风险从一个应用的性能扩大到系统输入链路。 |
| **分发与安装更重** | 需要注册 COM CLSID + 语言配置文件、处理语言栏、32/64 位、升级与卸载、用户切换输入法的可见行为；与现有"便携运行目录、不要求管理员权限"的约束冲突（TSF 注册的 HKCU 路径能力需实测）。相比之下"装一个扩展"是轻的。 |
| **覆盖面未必更广** | TSF 只对 TSF 宿主有效。老式 IMM32 应用与自绘控件拿不到上下文；这一项**不一定优于**现状的 UIA 覆盖面。 |

#### 4.9.3 必须承认的优点（避免被读成一味反对）

- 它是 OS **官方**给输入法读上下文的通道，**不触发浏览器无障碍**，并能拿到光标附近文本以及输入范围/敏感字段相关的属性。
- Chromium 确实把编辑框内容、选区与光标同步到 `TsfTextStore`，因此 VoxType 目前最想要的那几个场景（浏览器输入框、聊天框）在技术上成立。
- 一旦成为输入法，**注入**也可以改走 `ITfRange`，能顺带解决现在剪贴板注入、`WM_CHAR` 与微信特判这些历史包袱。

**但这些收益只在"产品转型为真语音输入法"（对标讯飞/搜狗）时才兑现。** 若将来真做这个决策，应以本节与 §4.4 为评估起点，并先完成 4.9.1 的 spike 与 4.9.2 的合规前置。

#### 4.9.4 裁决

**路线维持 A / B（§4.4）。TSF 记入"产品转型备选"，不作为本次性能问题的修复路径；P0 与 P1 的排期不因本节改变。**

---

## 5. 方案

### 5.0 P0：根层隔离（止血，小改动）

**唯一目标：让浏览器进程不再被我们召唤。** 不做任何"优化查询"，因为查询次数与副作用大小无关（§2）。

#### 新增：`src/core/browser_target.h` / `browser_target.cpp`（core 层）

放在 `core` 是硬约束：`tools/check_architecture.ps1:83` 规定 `core = @()`（core 不得包含其它层的项目头文件），而 `input_context.h` / `selection_context.h` 都在 core。

```cpp
namespace browser_target {
enum class Kind { NotBrowser, Chromium, Gecko };
struct Target { Kind kind = Kind::NotBrowser; std::wstring exe; std::wstring windowClass; };

// 纯函数，可离线单测（不碰 HWND / 不碰 UIA）
bool IsBrowserImageName(std::wstring_view exeImageName);
Kind Classify(std::wstring_view exeImageName, std::wstring_view windowClass);

// 薄探测：顶层窗口 HWND -> Target（GetWindowThreadProcessId + OpenProcess
// PROCESS_QUERY_LIMITED_INFORMATION + QueryFullProcessImageNameW + GetClassNameW）
// 复用 src/platform/text_injector.cpp:424-433 已有的微信判定写法。
Target IdentifyTopLevel(HWND topLevel);
}
```

- **不含任何 UIA/MSAA 调用**，只做进程名与窗口类名匹配，成本为一次 `OpenProcess` + 一次 `GetClassNameW`。
- **（实施后裁决）不实现 TTL 缓存**：实测调用次数是每次录音 2~3 次（探测 1 + 字段读取 1 + 选区 1，且都在 worker 线程上，不在音频回调或 UI 热路径），按 `(HWND, pid)` 缓存必须处理 HWND 复用失效才能保持正确，为省两三次 `OpenProcess` 引入这个风险不划算。取而代之只做无风险的优化：`ProcessImageName` 先用 512 字符栈缓冲，只有真正超长路径才分配堆缓冲（原来是无条件 `std::wstring(32768)` 清零）。
- 首版名单（**显式写死在代码常量里并在文档中登记**，不做成配置项）：`chrome.exe` / `msedge.exe` / `brave.exe` / `vivaldi.exe` / `opera.exe` / `opera_gx.exe` / `chromium.exe` / `thorium.exe` / `arc.exe` / `firefox.exe` / `waterfox.exe` / `librewolf.exe` / `floorp.exe` / `zen.exe` / `360se.exe` / `360chrome.exe` / `qqbrowser.exe` / `sogouexplorer.exe` / `maxthon.exe`。
- **明确排除**：`msedgewebview2.exe`（宿主窗口属于宿主进程）、Electron 应用（`Code.exe`、`Slack.exe`…）。理由与 ZenCrop §2.2 相同：它们不是浏览器，按"其他应用"处理。**这个边界必须写进文档**，因为它意味着 Electron 应用仍会触发同类开销（§7 未决项 2）。

#### 修改：`src/core/input_context.h`

1. `BeginSensitiveFocusProbe()`（`:683-740`）：在 `:686` 取得 anchor 之后、`:694` 抢单飞之前插入

   ```cpp
   if (browser_target::IdentifyTopLevel(anchor.foreground).kind != browser_target::Kind::NotBrowser) {
       state->store(kSensitiveProbeUnknownBrowserTarget, std::memory_order_relaxed);
       return state;   // 不建线程、不 CoCreateInstance
   }
   ```

   新增答案码 `kSensitiveProbeUnknownBrowserTarget = 18`（`:593-603` 同族），并在 `SensitiveFocusAnswerName()`（`:748`）登记名字。**不得**把它并进已有的任何一个 Unknown 码——诊断需要区分"探测失败"与"按设计跳过"。

2. `ReadInputFieldTextUIA()`（`:402-567`）：在 Win32 `Edit` 快路径（`:431-457`）之后、`EnsureAccessibilityTree(fgWnd)`（`:459`）**之前**插入浏览器短路，返回 `failReason = "BROWSER_SKIPPED"`（空文本、`isPassword=false`）。

   **必须放在这里而不是 `GetInputFieldContext()` 入口**：`volcengine_streaming_session.cpp`、`qwen_audio_streaming_session.cpp`、`asr_session.cpp` 各自有入口，只堵 UI 层会被绕过。这就是 ZenCrop §4.1 的"根层防线"。

3. 顺手修 `s_triggeredWindows` 永不清理（`:95-106`）：改成 `(HWND, pid)` 键或加上限的 LRU，避免 HWND 复用后 Nudge 静默失效（**非浏览器路径**的行为修复，与性能问题无关但同处一地）。

#### 修改：`src/core/selection_context.h`

- `TryReadSelectionWithUiAutomation` / `TryReadFocusedDocumentTextWithUiAutomation` / `VerifyCurrentUiAutomationSelection` 三条纯 UIA 路径前加同样的浏览器短路。
- `Capture()` / `TryReadFocusedDocumentText()` 在浏览器目标上直接返回不可用（`reason = "browser_skipped"`），**不落到** `EM_GETSEL` / `WM_GETTEXT` / `WM_GETTEXTLENGTH`：对 `Chrome_RenderWidgetHostHWND` 本就无意义，且会额外阻塞最多 3×500 ms。
- 这条同时覆盖 `text_injector.cpp:258` 的学习反射读回（默认关闭，但打开后同样会触发浏览器无障碍）。

#### 可观测性

- 新增日志：录音开始时若判定为浏览器目标，写一条 `event=browser_ax_isolated exe=<name> class=<class>`（**每次录音 1 条**，用户回报"浏览器里没上下文"时可直接定位）。
- `SensitiveFocusAnswerName()` 与 `failReason` 的新值必须能被 `asr_runtime_log` 打出来（沿用现有格式）。

#### 机械守卫（`tools/check_architecture.ps1`）

新增两条检查（现 **20 项 → 22 项**）：

| 检查 | 判定 |
| :--- | :--- |
| `Contract: browser UIA isolation in input_context.h` | 文件内必须同时出现浏览器门控与 `BROWSER_SKIPPED`，且 `EnsureAccessibilityTree(` 的调用位置**晚于**该门控的出现位置（用 `IndexOf` 比较，同现有 "probe 必须在字段读取之前" 的写法，`check_architecture.ps1:563-570`） |
| `Contract: browser UIA isolation in selection_context.h` | 文件内必须出现浏览器短路与 `browser_skipped` |

**不得提高任何基线**（`GlobalsIncluders` / `GlobalsExterns` / `GlobalsCrossLayerHeader` / `MainLines` / `SettingsLines` / `LayerViolations` / `DocEntriesMissingEn` / `DocEntriesMissingZh` 全部只降不升）。

#### 契约同步（P0 的 DoD，缺一不可）

- `ARCHITECTURE.md` 与 `doc/ARCHITECTURE_zh.md`：登记新模块 `browser_target`；顺带补 `selection_context`（当前两处均**未登记**）。
  ⚠️ `DocEntriesMissingEn/Zh` 是 `-le` 棘轮（当前 28 / 35，`check_architecture.ps1` 的 `$BASELINES`）：**新增模块不登记会让计数 +1 并可能直接 FAIL**。
- `AGENTS.md`：新增一条【A】不变量（§4.3 的硬边界）与一条"不要轻易做的事"（不得为恢复上下文而重新启用浏览器 UIA）。
- `CHANGELOG.md` + `doc/CHANGELOG_zh.md` + `README.md` + `doc/README_zh.md` 四件套与版本号（改 `src/app/resource.h` 的 `APP_VERSION_*`，其余宏派生）。

#### P0 退出判据（**必须实测，未测不得写"已修复"**）

1. 浏览器侧观测：在 Chrome/Edge 中录音 N 次（N≥10）前后，`chrome://accessibility` 显示的目标页面无障碍状态**不变**；对照实验为改动前的版本（同机器、同浏览器、同页面）。
2. 性能对照：本地固定基准页（纯 JS/DOM 计算 + 布局压力用例，仓库内提供，替身用于 Speedometer 类长任务）三组各 ≥5 次取中位数 —— ①干净会话；②旧版本录音 1 次后；③新版本录音 10 次后。③ 落在 ① 的波动区间内，且 ② 显著劣于 ①（若 ② 不劣于 ①，说明本机复现不出该现象，须如实记录，不得把"没复现"写成"修好了"）。
3. 非浏览器回归：记事本 / Word / VS Code / 微信，字段读取 `successLayer`、`failReason`、敏感探测答案与改动前逐项一致（对照日志）。
4. `build.bat` 全绿（含 22 项守卫），`MainLines`/`SettingsLines` 无增长。
5. 未安装扩展时浏览器内的行为可解释：日志给出 `reason=browser_target`，历史被跳过（`asr_dispatcher` 打出答案码 18）。

#### 回退

P0 是纯隔离改动，回退 = 撤销 `input_context.h` / `selection_context.h` 的门控与新增文件。**但注意**：回退会把浏览器持续降速的问题带回来，因此回退必须同时回退 `AGENTS.md` 的不变量条目与守卫检查，不能只回退代码。

---

### 5.1 P1：配套扩展恢复浏览器内能力

**目标：把 §3.4 里被刻意放弃的三项能力，用不经操作系统无障碍的通道拿回来。**

#### 5.1.1 与 ZenCrop 的差异（决定我们不照搬的部分）

| 项 | ZenCrop | VoxType |
| :--- | :--- | :--- |
| 取什么 | 元素矩形 + 祖先候选 + 滚轮层级 | **焦点控件：是否密码框 / 是否可编辑文本 / 值（尾部 N 字符）/ 选区文本** |
| 何时取 | 选框会话内，鼠标移动驱动（高频、需合并） | 录音开始与结束（每次录音 1~3 次，**低频**） |
| 页面驻留 | 常驻静态 content script（`all_frames`、`document_start`） | **首版不驻留**：按需 `chrome.scripting.executeScript` |
| 身份校验 | 窗口矩形匹配 + 前台 HWND + 候选视口 | 焦点帧 `document.hasFocus()` + 窗口矩形匹配 + 文档代际 |
| 状态机 | Idle→Active→Ended + 租约 + MutationObserver 失效 | 无会话、无观察者；单次请求/应答 + 过期丢弃 |

**为什么不驻留 content script**：录音是高频动作，但"每页都注入一个脚本"是持续成本；我们每次只需要一次同步查询，按需注入更窄。代价是每次录音多一次 `executeScript` 往返（需实测延迟，见 §8.3）。

#### 5.1.2 扩展侧

- Manifest V3，先支持 Chrome 与 Edge；`permissions: ["nativeMessaging", "scripting", "alarms"]`，`host_permissions: ["http://*/*", "https://*/*"]`。
  - **不用 `activeTab`**：桌面全局热键不是用户手势，拿不到 `activeTab`（与 ZenCrop §3.3 同一结论）。
  - 不使用 `tabs` 权限去读 URL；只读窗口/标签 id 与焦点状态。
- service worker：启动 / 安装 / `onStartup` 时 `chrome.runtime.connectNative(...)` 建立长连接，维持一条被动 PORT（原生连接可延长 SW 生命周期）；重连使用**有界退避**，绝不紧密重试。
- 查询实现（单次，无观察者）：

  ```js
  // 伪代码：在目标窗口的 active tab、allFrames 注入
  (needValue, needSelection) => {
    const el = document.activeElement;
    const hasFocus = document.hasFocus();
    const tag = el && el.tagName;
    const isTextInput = !!(el && (
      tag === 'INPUT' && /^(text|search|url|tel|email|number|password)$/i.test(el.type) ||
      tag === 'TEXTAREA' || el.isContentEditable));
    const isPassword = !!(el && (
      (tag === 'INPUT' && el.type === 'password') ||
      el.getAttribute?.('autocomplete') === 'current-password'));   // 保守扩展见 §6.4
    // 密码控件的值绝不读取、绝不返回
    const value = (!isPassword && needValue) ? readTail(el) : null;
    const selection = (!isPassword && needSelection) ? readSelection(el) : null;
    return { hasFocus, isTextInput, isPassword, value, selection, url: null };
  }
  ```

  - 多帧：`allFrames: true`，只接受 `hasFocus === true` 的那一帧（跨域 iframe 由该帧自己回答，天然隔离）。
  - **密码判定必须在读值之前**，且 `readTail` 对密码控件不可达（代码结构保证，而非注释保证）。

- 路由与授权：只查询**当前处于焦点的浏览器窗口的 active tab**；`windowId`/`tabId` 取自扩展 API，不接受页面自述。页面为 `chrome://`、扩展商店、PDF 查看器、`file://` 时直接 `unavailable`。

#### 5.1.3 传输：Native Messaging（与 ZenCrop 同构）

```
VoxType.exe（托盘常驻） ── 当前用户命名管道 ──> VoxType.exe --native-host（受限模式）
                                                      ↕ 官方长度前缀 JSON（stdio）
                                            扩展 service worker
                                                      ↕ chrome.scripting.executeScript
                                               目标页焦点控件
```

- `VoxType.exe` 复用为 Native Messaging host：**入口分支必须在单实例 mutex 与托盘/窗口/热键初始化之前**（否则桌面版已运行时 host 会被单实例检查直接拒绝）；只走受限协议，不创建托盘、不加载模型、不建 UIA 客户端；stdio 用二进制 `ReadFile`/`WriteFile`，`stdout` 不写日志。
- host manifest `allowed_origins` **固定到登记的扩展 ID**（开发 ID 与正式 ID 分开登记）；注册键只写本功能自己的项（HKCU，Chrome 与 Edge 位置不同），记录原值、不覆盖未知配置。
- 协议（单条 ≤ 64 KiB，UTF-8 JSON 严格校验，长度前缀分段读写）：

  | 方向 | 消息 | 字段 |
  | :--- | :--- | :--- |
  | 扩展 → host | `hello` | 协议版本、扩展版本、扩展 ID |
  | host → 扩展 | `query` | `requestId`、`needValue`、`needSelection`、`deadlineMs` |
  | 扩展 → host | `result` | `requestId`、`hasFocus`、`isTextInput`、`isPassword`、`value`、`selection`、`identity{windowId, tabId, frame}` |
  | 扩展 → host | `unavailable` | `requestId`、`reason`（`no_focus` / `restricted_page` / `no_target` / `timeout` / `internal`） |

  过期/未知 `requestId` 一律丢弃；`deadlineMs` 由桌面侧给出，扩展超时回 `unavailable` 而不是沉默。

- **不采用**本地 WebSocket + 监听端口作为首版方案：需要开监听 + 令牌认证 + 处理防火墙/杀软误报，而 Native Messaging 已被同构工程验证、无监听端口、可把来源钉死在扩展 ID。仅在"GUI EXE 继承管道实测不可靠"时才重新裁决（ZenCrop §3.2 的同款退出条件）。

#### 5.1.4 桌面侧映射（**不得引入第二个 Safe 来源**）

扩展答案经**纯三态函数**映射，与 `SensitiveProbeAnswerFromUia`（`input_context.h:666-673`）并列：

```cpp
// 纯函数，可离线单测
inline int SensitiveProbeAnswerFromBrowser(bool passwordHit,
                                           bool passwordKnown,
                                           bool hasFocusInFrame,
                                           bool identityMatched) {
    if (passwordHit) return kSensitiveProbeSensitive;          // 明确密码框
    if (!passwordKnown) return kSensitiveProbeUnknownBrowserQuery;   // 扩展没答 / 答不上
    if (!hasFocusInFrame) return kSensitiveProbeUnknownBrowserFocusMismatch;
    if (!identityMatched) return kSensitiveProbeUnknownBrowserIdentityMismatch;
    return kSensitiveProbeSafe;                                 // 只有四者齐备才 Safe
}
```

- `identityMatched`：扩展报告的窗口矩形与录音开始时锚定的顶层 HWND 矩形在 1 DIP 容差内一致（最大化/全屏单独处理）。**匹配失败一律 Unknown**，并在日志里累计误判以便评估（fail-closed）。
- 字段文本仍走 `qwen_context::SanitizeText`（`qwen_context.h:14-28`）尾部截断与密码清洗，语义不变；快照仍发生在**录音开始**（与现状一致），异步化改造不在本次范围。
- 延迟预算：沿用 `GetInputFieldContext` 的 200 ms 上限；超时即视为无上下文（不阻塞录音）。扩展侧热态往返目标见 §8.3。

#### 5.1.5 设置页（**本版未实施，已推迟**）

**推迟理由（实施后补记）**：设置页受 `UiStyle` 常量集中化 + `validate_settings_layout.ps1` 静态校验 + 96/144/192/288 四档 DPI 验收的硬约束，而本机无法完成四档 DPI 的目视验收；新增一行状态需要动 `ui_types.h` 常量并复核布局校验器，属于一轮独立的 UI 工作。因此 v0.11.9 **只交付功能与扩展**，状态行留待单独一轮。

**当前可用的替代观测手段**：桌面端日志（`event=input_context_snapshot ... layer=6` 表示走了扩展通道；`failReason=browser_not_connected` / 敏感答案码 18~22 表示没有扩展）与扩展自身的 service worker 控制台（`chrome://extensions` → 该扩展 → "检查视图 service worker"）。两者都写进了 `extensions/browser-bridge/README.md`。

---

#### 5.1.5-orig 设置页（原始设计，仍待实施）

- 新增状态行：`未安装扩展 / 已安装未连接 / 已连接`，附一段简短的安装与授权说明（"读取当前焦点输入框的内容以提升识别准确度；密码框内容不会被读取"）。
- 遵循 §"开发约定"：新增配置项走强类型注册表三步规范；UI 走 `UiStyle::*` + `S()` + `UpdateUiScale`，不得私设局部 `k*` 常量。
- 失败不弹窗、不阻塞录音，只在状态行与日志中体现。

---

### 5.2 P2：交付与安装

- CMake install 把扩展资产与 host manifest 装进唯一运行目录（`build/run/x64-release/...`）；**不得**手工向运行目录复制文件。
- 开发用注册/取消脚本只操作本功能自己的 HKCU 键，记录并保留未知值。
- 便携目录移动后注册路径失效 → 提供显式重新注册，不擅自创建 `portable.flag`。
- 发布收尾按 `AGENTS.md`：版本号 → 两份 README、两份 CHANGELOG → `git tag -a`。
- 商店发布与正式 MSI 升级矩阵**单独排期**，不在本次签收范围内。

---

### 5.3 P3：验收

见 §8。**P0 与 P1 的实现完成与性能签收必须分开记录**（ZenCrop 在 `doc/browser-companion-validation.md` 里明确区分"实现完成 / 开发验收 / 发布验收"，我们要沿用这种诚实的分档）。

---

## 6. 隐私与信任边界（本次最需要事先说清的部分）

### 6.1 与现状的语义对齐

扩展提供的 `isPassword` 与今日 UIA 的判定**语义一致**：密码控件 → 敏感；明确的可编辑文本控件 → 可 Safe；答不上来 → Unknown。**不扩大也不缩小**敏感范围，避免"顺手"改变隐私行为。

### 6.2 密码值永不离开页面

- 扩展侧：`isPassword` 判定在读取 `value` 之前；密码控件返回 `value: null`，`selection: null`。这是结构保证，必须有单测（DOM 夹具断言"密码框查询结果的 JSON 中不含任何输入字符"）。
- 桌面侧：`SensitiveProbeAnswerFromBrowser` 命中 `passwordHit` 时立即 `Sensitive`；字段读取路径复用 `ReadInputFieldTextUIA` 的密码终止语义（读到密码即清空文本、不再走父链/坐标/MSAA）。

### 6.3 数据最小化

- 协议不传 URL、不传正文、不传 Cookie、不传页面标题；只传焦点控件的必要事实。
- 诊断默认不记录字段文本内容，只记长度/层/状态（与现有 `event=input_context_snapshot ... chars=` 一致）。
- 命名管道：当前用户 SID + 会话隔离、拒绝远端连接、核对连接进程；不引入管理员权限。

### 6.4 未决（P1 开工前必须裁决）

1. **`autocomplete` 保守扩展**：是否把 `cc-number` / `one-time-code` / `new-password` 等一并视为敏感？现状 UIA 不做此判断，扩了会改变隐私行为、缩了没意义。倾向：**首版与现状对齐**（只认 `type=password`），把扩展列为后续可选加固项，且改动必须单独评审。
2. `contenteditable` 富文本编辑器（飞书文档、Notion、Gmail）的"值"语义：整段文本还是光标附近文本？是否只在可判定时返回。
3. 输入法自身窗口/自绘控件（Electron 之外的 WebView2 宿主）是否要单独登记。

---

## 7. 风险与未决项

| # | 风险 / 未决 | 处置 |
| :--- | :--- | :--- |
| 1 | **本机尚未实测**：浏览器是否真的被我们拖慢、症状是否与 ZenCrop 一致 | §5.0 退出判据 1、2 就是裁决实验；**先做对照再写结论**。若复现不出，如实记录并重新评估 P1 优先级 |
| 2 | Electron / WebView2 宿主（VS Code、Slack、微信内嵌）不在浏览器名单内 → 仍会触发 Chromium 无障碍 | 明确写入文档作为已知边界；若收到同类反馈，扩展名单或改用"窗口类名 + 进程名白名单"扩展，**不擅自扩大** |
| 3 | Firefox 非 Chromium，触发机制与开销不同；扩展不覆盖 | 首版一并跳过（`Gecko` 类型），浏览器内上下文在 Firefox 上保持不可用 |
| 4 | 多 profile / 同标题多窗口 / 并排窗口的身份歧义 | 匹配失败即 Unknown（fail-closed），UI 无提示但有日志；不做"任选一个" |
| 5 | MV3 service worker 生命周期与"桌面端凭空唤醒" | 依赖长连接 + 有界退避重连；实测待机成本（CPU/内存），**不承诺零损耗或完全休眠** |
| 6 | 每次录音多一次 `executeScript` 的往返延迟 | 计入 §8.3 的 p50/p95 测量；超过 200 ms 预算则退化为"本次无上下文"，不阻塞录音 |
| 7 | 守卫/文档棘轮：新模块未登记会使 `DocEntriesMissing*` 计数上升 | P0 的 DoD 含契约同步；提交前跑 `build.bat` 全绿 |
| 8 | P0 门控被后续改动"顺手绕过" | 已加两条机械守卫（§5.0）；守卫检查名与判定以脚本内清单为唯一事实来源 |

---

## 8. 验证清单

### 8.1 自动化

- 纯函数单测（离线、链接现有测试目标风格）：`browser_target::IsBrowserImageName` / `Classify`（正反例：`chrome.exe`、`msedge.exe`、`firefox.exe`、`msedgewebview2.exe`、`Code.exe`、空串、大小写与路径）；`SensitiveProbeAnswerFromBrowser` 的四元素真值表（尤其"密码但未匹配锚点 → 仍 Sensitive"、"缺任一要素不得 Safe"）。
- 导航到 `tests/` 现有目标（`CMakeLists.txt` 里按 `add_executable + add_test` 风格注册；**不新增 test-only 生产 API**，include 目录已含 `src/core`）。
- 机械守卫 22 项全绿；`GlobalsIncluders/Externs/CrossLayerHeader`、`MainLines`、`SettingsLines`、`LayerViolations`、`DocEntriesMissing*` **一律不得上调**。
- 扩展侧（P1）：DOM 夹具页 + 断言脚本，覆盖"密码框查询结果不含输入字符"、"无焦点帧 → `no_focus`"、"受限页 → `restricted_page`"、"过期 requestId 被丢弃"。

### 8.2 手工矩阵

| 场景 | 期望 |
| :--- | :--- |
| Chrome / Edge 普通输入框口述 | 录音正常；P0：无上下文 + 历史跳过（日志有 reason）；P1：上下文恢复、历史恢复 |
| 浏览器密码框口述 | 永不取到文本、永不进历史、注入标 sensitive（两个阶段都必须成立） |
| 浏览器地址栏 / 页面空白处 / chrome:// / PDF | 无上下文、无崩溃、无额外阻塞 |
| 未安装扩展 / 扩展禁用 / host 未注册 | 同上；**绝不回退到 UIA** |
| 记事本 / Word / VS Code / 微信 | 与改动前逐项一致（对照日志） |
| 连续录音 20 次（浏览器内） | 无句柄/内存增长、无累积延迟、浏览器侧 AX 状态不变 |
| 多显示器 + 混合 DPI + 浏览器最大化 | 身份匹配不误判（误判必须表现为 Unknown 而非错判 Safe） |

### 8.3 性能验收（诚实版）

- 分组：① 干净浏览器基线；② **旧版本**录音 1 次后；③ 新版本（P0）录音 10 次后；④ 扩展已连接但未工作；⑤ 扩展工作期间；⑥ 扩展结束后同一会话。
- 每组 ≥5 次取中位数并记录离散范围；调查阈值暂用 5% 相对差，**不硬要求某个具体分数**。
- 同时观察浏览器进程与 VoxType 的 CPU；热态扩展往返记 p50/p95（ZenCrop 实测 0.6/1.2 ms，可作为参考量级，不作为我们的目标值）。
- 测量期间不得使用会自行触发无障碍的工具（Inspect、Accessibility Insights、DevTools 无障碍树、`chrome://accessibility` 的 Show/Refresh tree）。自动化跑分若用 CDP，只操作 DOM/Runtime，不开 Accessibility 域。
- **被旧版本开启过 AX 的会话不能用来验收新版本**（新路径只防新增，不承诺撤销历史状态）。

---

## 9. 实施顺序与工作量估计

| 阶段 | 内容 | 交付物 | 阻塞关系 |
| :--- | :--- | :--- | :--- |
| **P-1** | 价值量化：只统计不改行为地记录"浏览器内录音次数 / 是否有字段上下文 / 平均字符数"（§4.5） | 统计日志与汇总 | 无。**必须在 P0 之前或同批落地**，否则数据消失 |
| **P0-a** | 对照实验：旧版本在浏览器内是否真的降速（§5.0 判据 2 的 ①②） | 诊断记录进 `build/artifacts/diagnostics/` | 无。**先做这个再动手写代码** |
| **P0-b** | `browser_target` + 两处门控 + 日志 + 单测 | 生产代码与测试 | P0-a 复现即可开工；即复现不出也可先落地（改动本身是纯隔离） |
| **P0-c** | 两条守卫检查 + 契约同步（ARCHITECTURE 中英、AGENTS、CHANGELOG 四件套） | 文档与守卫 | P0-b |
| **P0-d** | 验收（§5.0 判据 1~5） | 证据归档 | P0-c |
| **P1-g** | **Go/No-Go 闸门**：按 §4.5 的统计决定"做扩展"还是"停在 B 方案"；若做，先完成 §4.8 的分发调研 | 一页决策记录 | P0-d + 足够样本的 P-1 数据 |
| **P1** | 扩展 + Native Messaging host + 桌面桥 + 映射函数 + 设置页 | 扩展源码、host 注册资产、协议契约测试 | P1-g 判为 Go 且分发路径可行 |
| **P2** | CMake install / 注册脚本 / 发布四件套 | 安装说明与脚本 | P1 |
| **P3** | 实机与性能签收 | 验证记录（新增 `doc/` 记录，参照 ZenCrop 的分档写法） | P2 |

---

## 10. 参照证据索引（ZenCrop 侧）

- 触发链与"客户端释放 ≠ 浏览器状态关闭"：`browser-accessibility-dom-disconnect-research.md` §2.1、§2.2、§3.1
- 新 UIA 断连通知存在但 Chrome 未用于撤销模式：同上 §3.3
- 内部抑制开关（仅诊断/临时恢复）：同上 §4.1、§4.3
- 扩展 + Native Messaging 路线与"平时被动、会话内有限查询"：同上 §6.1、§6.3
- 硬边界（失败不得回退无障碍）与根层防线：`browser-dom-companion-implementation-plan.md` §1、§4.1
- 会话身份/代际与过期结果：同上 §4.2
- 权限与 `activeTab` 不适用桌面热键：同上 §3.3
- 实测参考量级（往返 0.6/1.2 ms，host 待机 ~8 MiB）：`doc/browser-companion-validation.md` §"Initial real browser development matrix"、"Idle and performance limits"
- "实现完成 ≠ 性能已签收"的分档写法：同上 §"Still required for sign-off"

---

## 10.5 外部审查回应（2026-10-13）

一份第三方审查提出 5 条意见。逐条核实后的裁决（**接受 3 条并已修，1 条部分接受，1 条为对比评价**）：

| 意见 | 裁决 | 处置 |
| :--- | :--- | :--- |
| ① iframe 场景 `hasFocus()` 使每层祖先文档都报焦点 → `focus_ambiguous` | **成立，且是本轮最严重的缺陷** | 已修，但**未采纳其修复方案**：审查建议按 `textInput === true` 过滤，这会漏掉"焦点在 iframe 内的非可编辑元素（按钮/链接）"——那种情况应当得到"非密码"的 Safe 答案，按 `textInput` 过滤会把它误判为歧义。改用真正的判据：`activeElement` 是 frame 宿主元素的文档只是**透传**，丢弃后必须恰好剩一个；剩下 0 个（焦点在无法注入的 frame 里）返回独立原因，保持 fail-closed。见 `tests/browser_bridge_worker_contract.cjs` |
| ② `onFocusChanged` 被 200 ms 防抖 → 切窗口后立刻口述会脱靶 | **成立** | 已改为焦点变化立即发送一次 + 防抖后再补发一次（防浏览器自身状态未更新）。窗口缩放仍防抖 |
| ③ 同步模式 `timeoutMs` 是死参数，host 的"10 s 等 hello"是假的 | **成立** | 已修：同步句柄无法带 `OVERLAPPED`，改用**从另一线程 `CancelSynchronousIo`**（MS 文档记载的做法，被取消的调用以 `ERROR_OPERATION_ABORTED` 结束）实现真实 deadline |
| ④ `ProcessImageName` 每次分配 64 KiB 且无 TTL 缓存，"违背方案自身约定" | **部分接受** | 缓存缺失属实（已在 §5.0 记录本次裁决：**刻意不做**，理由见该节）；但"GC/内存抖动"的定性不成立——C++ 无 GC，且该函数每次录音只被调用 2~3 次。已做无风险的栈缓冲优化 |
| ⑤ 与 ZenCrop 的架构对比表 | **基本同意，但不是裁决依据** | "无常驻脚本、复用 EXE 做 host"确实是我们更轻的选择，理由是访问模式不同（离散事件 vs 鼠标驱动）；这不改变 §4.4 的路线结论 |
| ⑥（第二轮）Shadow DOM 盲区：`activeElement` 重定向到宿主 → 封装密码框被误判 Safe | **成立，隐私级 fail-open** | 双层修复：①页面函数（限深）穿过**开放** shadow root 取真实控件；②`ClassifyFocus` 新增 `textInputIdentified`——只有"确实识别到可编辑控件"才允许 Safe。**闭合** shadow root 从外面看与普通元素无法区分（`host.shadowRoot === null`），所以第②层是隐私保证，第①层只是功能恢复。代价：焦点在普通元素（body/按钮）上时不再记录历史——fail-closed 的正确方向。契约测试新增 5 条 shadow 用例，并用"去掉穿透循环"的变体验证能捕获回归 |

**同时指出审查遗漏的一点**：它没有提到 `extensionId` 此前只在注释里"钉住"、代码里无人校验——本轮已补上 host 侧校验与守卫侧的 `key` 现算比对（见 §5.1.3）。

**后续验证**：Chrome 端到端已恢复；强制界面缩放/网页缩放分离、窗口内 UIA、重连及线程退出加固见 `doc/browser-bridge-validation.md`。Edge 联调与完整性能对照仍未做。

---

## 11. 本文未做的事（避免被当作既成事实）

- 已实现并运行构建、离线回归与 Chrome 联调；修改保留在工作区，未提交、未发布。
- 未在本机做浏览器性能对照（`§5.0 判据 2` 的①②尚未执行），因此**本文不主张"VoxType 已被证明拖慢了本机浏览器"**，只主张"VoxType 具备与 ZenCrop 同类的触发原语，且触发频率更高、无任何识别与降级"。
- 已测量扩展内部查询耗时，未测完整录音链路和普通应用 UIA 耗时分布。
- 未裁决 §6.4 的三个未决项。
- 用户已选择配套扩展方案，P0/P1 已实施；设置页状态行、浏览器选区读取和商店分发不在本轮范围内。
- 未核实国产壳浏览器（360 / QQ / 搜狗）对 MV3 扩展与 Native Messaging 注册的兼容性，也未核实零摩擦分发路径是否可行。
- 未实证 §4.9.1 的 TSF 关键问题（非活动文本服务能否拿到 `ITfContext`）；该 spike 只在"评估产品转型"时才需要执行。
- **未确定本仓库的许可证**（根目录无 `LICENSE`）。在任何代码吸收发生之前必须先定这件事；在此之前 GPL-3.0 实现（如 Weasel/小狼毫）一律不得引用。本节引用的许可信息来自公开仓库，采纳前需逐项复核。
