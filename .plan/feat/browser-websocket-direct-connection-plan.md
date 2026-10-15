# 浏览器扩展 WebSocket 直连 VoxType 初步方案

状态：已实施并完成 Chrome 范围验收；其余实机项目按用户决定保留待验收，未公开发布。
日期：2026-10-14
当前实现：扩展 2.0 直接连接主程序内的本机 WebSocket 服务；Native Messaging 仅作显式短暂配对引导，1.x 转发仍保留兼容/回滚。
本轮交付：按用户 `/goal` 实施产品代码、配对/撤销 UI、构建载荷、协议/安全回归及 Chrome 实测；2026-10-14 用户确认“当前只验证 Chrome，其他项保留待验收”。初步研究正文保留为决策依据，实施结果以第 9 节为准。

## 1. 目标与决策

用户观察到打开浏览器后任务管理器出现两个 VoxType 进程，先讨论了单进程与折叠分组，最终初步选择 WebSocket 直连。

| 方案 | 常驻进程 | 收益与代价 | 当前决策 |
| --- | --- | --- | --- |
| 保持 Native Messaging | 主程序 + 每条连接对应的本地 host | 官方通信机制，现有链路可用 | 保留为迁移前基线 |
| 将 host 拆成独立名称的辅助程序 | 进程数基本不变 | 名称更清楚，但不能解决常驻进程开销 | 非本次方向 |
| 任务管理器折叠分组 | 进程数与资源开销基本不变 | 仅改善显示；系统是否分组需实测，不能通过统一 AppUserModelID 承诺 | 不作为本次目标 |
| 本机 WebSocket 直连 | 一个常驻 VoxType 主进程 | 省去本地 host，同时引入监听、鉴权、保活和连接管理 | **初步选择** |

“一个常驻进程”指 VoxType 自身；浏览器扩展仍在浏览器内部运行，浏览器的多进程架构不受影响。

## 2. 已确认的现状与证据

### 2.1 当前链路

```text
浏览器扩展 worker.js
        │ chrome.runtime.connectNative()
        ▼
浏览器启动的 VoxType.exe（受限 host 模式）
        │ 当前用户/Windows 会话命名管道
        ▼
VoxType.exe（托盘、录音、识别与上下文查询）
```

- `extensions/browser-bridge/worker.js` 的 `connect()` 调用 `chrome.runtime.connectNative(HOST_NAME)`，维护长连接与退避重连。
- `packaging/native-messaging/com.voxtype.browser_bridge.json` 的 `path` 是 `../VoxType.exe`，传输类型为 `stdio`，因此浏览器启动的是同一个 EXE 的另一个进程。
- `src/app/main.cpp` 在单实例互斥体之前调用 `browser_bridge::RunNativeHostIfInvoked()`；host 模式直接进入受限转发流程，不重复初始化录音应用。
- `src/core/browser_bridge.h` 的 `RunNativeHost()` 不创建窗口、托盘、模型或无障碍客户端，只校验并转发消息。
- 该模块还通过管道对端 PID、同用户会话、同一可执行文件、浏览器祖先 PID 与窗口几何匹配确定连接和焦点身份。这些保证不能因为换传输而丢失。
- `src/core/input_context.h` 调用公共 `Bridge::Query()`：当前字段查询预算为 150 ms，敏感焦点探测预算为 400 ms。新链路不得在录音开始时增加同步配对或等待连接。

2026-10-14 会话内进程快照确认存在两个 VoxType：一个普通启动，另一个启动参数包含固定扩展 origin 与 `--parent-window=0`，符合浏览器启动 host 的特征。Chrome 官方文档也明确：`connectNative()` 会启动独立 host 进程，并在端口存续期间保留它。

### 2.2 内存收益的边界

同次快照读取的工作集约为：主程序 **64.2 MB**，host **10.8 MB**。这是当时的观测值，尚非受控性能基线。

- 移除 host 有机会省去相应进程的工作集及管理开销。
- 主程序将新增服务端连接、协议解析、缓冲区和线程/事件等成本，浏览器端也有保活成本。
- 工作集含共享页面，不能将两个进程工作集简单相加当成独占物理内存；**不能承诺净省 10.8 MB，更不能承诺省掉第二份完整模型内存**。
- Chrome、Edge 和多个配置同时连接时，直连可避免各连接另起本地 host，但仍需要为各连接分配状态和缓冲区。
- 验收需比较 VoxType 全部进程的私有提交、工作集、CPU 与浏览器侧增量；进程数变少并不自动等于总资源更省。

## 3. 初步目标架构

```text
Chrome / Edge 扩展 service worker
        │ 本机 WebSocket；先认证，再接受有限的上下文消息
        ▼
VoxType.exe（唯一常驻主进程）
  ├─ 本机连接、认证、消息收发与窗口身份绑定
  ├─ 已有 Bridge 查询、超时与 fail-closed 结果
  └─ 托盘、录音、识别
```

尽量复用现有扩展按需读取字段、帧焦点选择、窗口几何校验和查询结果语义，仅替换通信和身份建立部分。不引入通用 RPC、远程控制接口、浏览器 UIA/MSAA 回退或页面持续扫描。

服务位于主进程工作线程/异步 I/O 中，成功取得单实例互斥体后才启动，退出时有界取消并释放连接。不要把实现塞入已达 150 行上限的 `main.cpp`；如需调整入口接线，先按现有规则瘦身。

## 4. 实施前必须明确的安全与生命周期设计

下列为设计门槛，尚未证明已有实现能够直接满足。

### 4.1 监听与协议

- 首版仅绑定 `127.0.0.1`，不绑定 `0.0.0.0` 或局域网地址，不申请管理员权限或新增入站防火墙放行。
- 端口选择、发现方式与冲突处理先做原型再定。不得偷偷扫描大量端口，也不得将端口冲突当成“已连接”。
- 校验握手目标和固定扩展 Origin，拒绝普通网页、未知扩展和错误协议版本；**Origin 不能代替密钥认证**，本机程序可以伪造握手。
- 设置握手/认证超时、连接数量、消息大小与嵌套深度上限，限制未认证连接资源消耗；复用现有受限消息集合，不提供任意脚本/文件/命令执行。
- 服务端 WebSocket 能力需单独确认：仓库已有 WinHTTP 客户端不能直接视为可复用的 WebSocket 服务端。先检查现有依赖和 Windows 能力，再决定最小实现，不预先添加新依赖或自写完整协议栈。

### 4.2 配对与身份

- 使用每安装实例/浏览器配置可区分的强随机配对凭据，双方建立信任后才交换上下文正文；凭据不得写入 URL、日志或网页 DOM。
- 扩展凭据只允许可信扩展上下文访问；桌面侧存储遵循当前用户保护策略。重新配对可撤销旧凭据，卸载、移动目录、多用户/多会话行为需要明确。
- 首次配对交互及端口发现尚未定稿。可以评估用户显式配对，或仅在迁移/首次配对短暂使用旧 host 建立信任；后者只能承诺**常驻单进程**，不能宣称生命周期内从不出现第二进程。
- 若原型保留短暂 host 引导，后续正常连接与重连不得依赖常驻 host，也不得循环拉起它来保活。
- 现有管道认证依赖 host PID 与浏览器祖先链，WebSocket 不会自动继承。必须验证本机 TCP 对端 PID/会话等可用证据与配对凭据的组合，继续将连接绑定到正确浏览器进程与窗口。不能直接信任消息里的 `browserPid`。
- 凭据认证不能替代实际焦点、窗口几何与密码属性判定；任何身份或匹配失败仍返回 Unknown/无上下文。

### 4.3 连接与空闲成本

- 覆盖程序先开/浏览器先开、双方重启、扩展重载、浏览器多配置，以及同时运行 Chrome/Edge。
- 程序未启动时采用有上限的退避重连，不快速轮询、不唤起完整录音应用、不反复弹窗。
- Chrome 官方说明 WebSocket 消息可以延长扩展 service worker 生命周期。保活间隔及是否维持常连需要测量，不能写成“零轮询/零空闲开销”。
- 不把可用性建立在“每次按录音键才连接并配对”上；当前 150/400 ms 查询预算必须覆盖冷态/恢复边界，未就绪则及时返回 Unknown。

## 5. 迁移涉及的模块与交付物

| 范围 | 拟调整内容 |
| --- | --- |
| `src/core/browser_bridge.h` 及同层实现 | 直连服务、连接状态、认证、浏览器身份绑定；复用可保留的查询/几何契约 |
| `src/app/main.cpp` 与应用生命周期模块 | 单实例之后启动、退出取消；入口不突破行数守卫 |
| `extensions/browser-bridge/worker.js` / `manifest.json` | WebSocket、配对、重连、必要权限与最低浏览器版本；保留按需字段读取 |
| `src/core/browser_bridge_registration.*` | 明确旧注册查询/撤销与新配对职责，避免注册成功被解释成直连已建立 |
| `src/ui/tabs/tab_general.cpp` / `src/ui/ui_types.h` | 将旧 Register Bridge 入口逐步改为连接/配对与重连状态，遵守既有 DPI 体系 |
| `tools/register_browser_bridge.ps1` | 迁移期保留原功能与共享记录，退出旧链路时提供有归属校验的清理路径 |
| `cmake/VoxTypeRuntime.cmake` / `CMakeLists.txt` | 维护新增源文件、构建标志与运行载荷；首个原型不删旧资产 |
| `packaging/windows/Package.wxs` 及实际生成 MSI 的脚本 | 核实旧 host 注册的实际拥有者与升级行为，直连正式切换时再决定调整；不能只改 GUI 就宣称迁移完成 |
| `tests/browser_bridge_test.cpp` / `tests/browser_bridge_worker_contract.cjs` 等 | 新增连接/认证/生命周期回归，保留字段、密码、frame 与窗口几何回归 |
| 架构、安装、升级与用户文档 | 同步新连接流程、旧版本兼容边界与回滚步骤 |

新持久配置如确有需要，遵循 Config/强类型注册表/UI 三步机制；不要在原型阶段预设一串端口、超时或实验开关。

安装器核查修正：当前 `Package.wxs` 及 `scripts/package_voxtype.ps1` **没有** Native Messaging 自动注册；MSI 仅拥有运行文件、安装目录项和快捷方式。配对引导注册仍由设置页/脚本拥有 HKCU 值。本次继续分发清单和脚本，保留归属记录，不新增 HKLM 写入。未来清理只能处理本产品拥有的注册，保留第三方值和用户原始记录。

## 6. 分阶段实施与退出条件

### P0：冻结当前基线

记录主程序版本、扩展版本、Chrome/Edge 版本、测试环境及现有启动顺序；测量单浏览器与双浏览器/多配置情况下的进程数、私有提交、工作集、空闲 CPU、查询时延及敏感探测结果。当前 64.2/10.8 MB 快照仅作为线索。

退出条件：有可复现实验步骤和对照数据，现有 Native Messaging 回归通过。

### P1：最小直连原型

在主程序内验证 WebSocket 服务、端口发现、首次配对和现有查询格式。先证明认证与浏览器/窗口身份绑定，随后验证多连接与退出取消；旧链路保留用于对照和回滚，不同时消费同一个查询结果。

退出条件：稳定运行仅一个常驻 VoxType；错误凭据/网页/错误窗口不能取得或提交可信上下文；断线不阻塞录音/UI；资源和时延有实测。

### P2：产品化与兼容迁移

接入设置页连接状态与显式配对/撤销流程，覆盖多配置、移动目录、重启、丢失凭据、旧扩展连接新程序和反向组合的行为。明确协议版本不兼容时提示与回滚，不静默落入不安全通道。

退出条件：用户可以完成首次连接并恢复断线，新增状态区分“已配对/已连接/未连接”，旧注册的恢复与清理行为可验证。

### P3：切换、清理与验收

直连签收后再移除常驻 host 路径、调整安装器及脚本交付。若实测不能满足身份保护、时延或资源目标，停止切换并保留旧方案，不以进程数掩盖性能退步。

退出条件：满足下列验收矩阵，安装/升级/回滚可恢复，文档与运行载荷一致。

## 7. 验收矩阵

- 进程：浏览器先开/程序先开、单浏览器、Chrome+Edge、多配置连接均只有一个常驻 VoxType；临时引导进程如存在，须单独记录并在完成后退出。
- 资源：同机同版本同场景多次对照，记录所有 VoxType 进程私有提交与工作集、浏览器增量、空闲 CPU/唤醒次数；报告净收益，不承诺固定节省 MB。
- 功能：普通字段、iframe、open/closed shadow root、密码框、切换窗口/配置，以及几何不匹配时行为保持；录音探测和查询期限不回退。
- 安全：错 Origin、错/过期凭据、未认证 flood、过大/畸形帧、端口占用/冒充服务、错误 PID/会话等受控失败；未知焦点仍不得写入可上传历史。
- 生命周期：程序退出、浏览器退出、崩溃/重启、扩展重载、睡眠恢复、会话切换均可退出或退避重连，无残留 host 与持续快速重试。
- 交付：MSI/Portable/开发运行目录、目录移动、升级/撤销与回滚均有明确步骤；旧注册只处理本产品拥有的项。
- 自动化：`build.bat --test`、架构守卫、`node tests/browser_bridge_worker_contract.cjs`；涉及 UI 时执行 96/144/192/288 DPI 静态布局校验，并单列实机验收。

## 8. 参考来源与当前未决项

2026-10-14 研究中已核对的官方文档：

- [Chrome Native messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging)：host 独立进程、stdio 协议、`connectNative()` 生命周期、扩展 origin 与注册机制。
- [Chrome 扩展 service worker 中的 WebSockets](https://developer.chrome.com/docs/extensions/how-to/web-platform/websockets)：Chrome 116 起的生命周期支持及周期消息保活示例；Edge 对应版本/策略仍需实测。
- [Microsoft AppUserModelIDs](https://learn.microsoft.com/en-us/windows/win32/shell/appids)：应用/窗口的任务栏分组身份，不能据此保证任务管理器分组。

未决项：服务端实现选型、端口发现/冲突策略、首次配对方式、会话与浏览器身份核对的可实施性、保活策略、旧版本兼容期限，以及实测净内存/CPU 收益。以上先由 P0/P1 取证决定，不在本文中冒充已解决。

## 9. 2026-10-14 实施与验收结果

### 9.1 已定的实现

- 服务端：Windows `websocket.dll` 协议组件处理握手、掩码、UTF-8、分片和控制帧，Winsock 事件驱动收发；没有新增第三方依赖或自写完整协议栈。仅 `127.0.0.1:19226`，固定 `/voxtype/browser/v2` + `voxtype-context-v2`；独占端口，冲突报 unavailable，不扫描/抢占。
- 认证：固定扩展 Origin、OS TCP 对端 PID、同用户/会话的 Chromium 根进程（父进程创建时间核对，持有进程句柄防复用）、每配置 256-bit 凭据与双向 HMAC-SHA256 随机挑战。先证明服务端，再证明客户端，认证前不发窗口清单/正文；凭据不经 WebSocket 发送。正文消息复用既有 v1 格式，连接认证/心跳是 v2。
- 配对：用户在扩展页点击按钮，短暂启动注册的可信 Native host，经同用户/会话、同 EXE 的管道换取凭据；完成后退出。无程序时有界失败；启动、连接失败与 alarm 不拉起 host。GUI/脚本注册契约与原记录不变。
- 持久化：路径哈希区分安装目录，浏览器配置各自随机 profile ID；桌面 owner-only ACL + 当前用户 DPAPI + 原子替换，扩展 storage 限制 `TRUSTED_CONTEXTS`。损坏存储不静默覆盖；显式 Unpair all 旋转身份并断开已有连接。移动目录需重新注册/配对；卸载保留用户数据，撤销须显式操作。
- 生命周期/预算：单实例成功后由原 `Bridge::Start` 接线；`main.cpp` 未增长（150 行）、`settings.cpp` 387 行。最多 8 peer，消息 64 KiB、JSON 深度 64、队列 8、配对条目 32；握手 2s、认证总预算 3s，事件取消中断读取。心跳 20s，空闲无页面读取；断线 250ms 快速重试一次，再 30s–5min 退避。字段/探测仍是 150/400ms，查询总预算现包含发送时间。
- UI/交付：General 的 Pair / Refresh、Unpair all 与每浏览器的认证连接/配对离线状态；扩展图标打开同一配对页面。新增控件尺寸集中 `UiStyle`，原 DPI 机制保留。MSI/Portable 分发 2.0 页面与 worker；凭据不打包。版本仅在 resource.h 升为 v0.11.10，两份 README/CHANGELOG 同步，标记为本地验证构建；未发布，因此不创建 release tag。

### 9.2 可复现的通过项

1. 修改前基线：Native worker contract、原 browser bridge test 与 23 项守卫通过；已有主程序 + host 两个进程。备份 `.bak/browser-websocket-20261014-092912/`。
2. `build.bat --test`：生产程序、ASR replay 目标构建与全部离线回归通过，新增真实 TCP/WebSocket 测试覆盖正确/错误/反射/重放凭据、版本/配置、Origin/子协议/重复 header、掩码/分片/二进制/超量/畸形/深度/UTF-8、端口冲突、DPAPI 重启/损坏/撤销和退出取消。
3. `node tests/browser_bridge_worker_contract.cjs`：原字段、iframe、shadow、焦点/过期应答回归保留；增加认证前不读页面/不发窗口、冒充服务、显式配对、可信存储、心跳及纯 WebSocket 重连证明。
4. `node tools/browser_bridge_live_contract.cjs`：复用当前 Chrome，不开浏览器实例；临时 loopback 合成页面测试普通字段、带读取陷阱的密码框、跨域 iframe、嵌套 iframe、open/closed shadow root 和 250% 网页缩放，7 项通过。以唯一自建测试窗口定位原生 HWND，PerMonitorV2 下的几何核验通过；DevTools 前台不会被误当作测试窗口。脚本结束关闭自己的页面/服务。
5. 实际扩展 2.0 首次配对：页面显示 Connected over WebSocket，短暂 host 退出，OS 只见一个 VoxType。扩展重载后直接重连；主程序停止/重启后恢复，未重新配对或常驻 host。
6. 守卫增加直连安全接线检查，当前 **24 项全部通过**；源码/文档欠账基线不提高。`validate_settings_layout.ps1` 的 96/144/192/288 DPI 静态布局校验通过。
7. `build.bat --test --package` 完成 v0.11.10 的验证包：`VoxType-v0.11.10-win-x64-portable-unsigned.7z`（9,252,354 bytes）和 `VoxType-v0.11.10-win-x64-unsigned.msi`（11,382,784 bytes），均在 `build/packages/`，带输入指纹与 SHA-256 sidecar；包内载荷/版本/提取验证通过。MSI authoring 编译 0 warning / 0 error。没有在用户工作站安装 MSI；VM 安装/升级仍待验收。

### 9.3 资源证据与边界

原始采样与总结在 [evidence/browser-websocket/](evidence/browser-websocket/)：每轮 7 个样本，间隔 5s。

| 指标 | Native 基线 | WebSocket Chrome | 解读 |
| --- | ---: | ---: | --- |
| VoxType 进程数 | 2 | 1 | 临时配对已退出 |
| 全部 VoxType 平均私有提交 | 38.024 MiB | 36.279 MiB | 该轮少 1.745 MiB，非承诺值 |
| 全部 VoxType 平均工作集 | 76.007 MiB | 62.585 MiB | 工作集包括共享页面，不能当独占物理内存 |
| 30s 累计 CPU 时间增量 | 0s | 0.203125s | 新程序刚启动且配对页打开，不能冒充严格空闲对照 |
| Chrome 平均私有提交 | 4533.411 MiB | 6631.150 MiB | 浏览器页面/工作负载变化，不能将增量归因于扩展 |

因此仅确认常驻进程减少及 VoxType 侧观测；**没有证明包含浏览器的净资源收益或真实唤醒次数**。正式资源结论需在同一负载下重复对照/ETW。不给出固定节省 MB、零空闲成本或全场景更快的承诺。

### 9.4 已接受的剩余边界

- 用户本轮只签收 Chrome 范围；Edge、多配置、真实多屏/混合 DPI、浏览器完整重启、睡眠恢复、会话切换，以及 MSI 的 VM 新装/升级/repair/uninstall/rollback 保留待验收。
- computer-use 技能已读取，但本会话未提供它需要的 `node_repl`/`@oai/sky` 运行工具；没有改用其它 Windows UI 自动化。Settings 实机视觉/交互检查未执行，静态四档 DPI 校验不替代它。
- 没有实跑云端 ASR 或读取用户真实字段；Chrome 用例只包含本地合成文本，录音/敏感历史的既有离线回归保留。
- 2.0 日常路径已切换直连，P3 的旧 1.x host/注册资产全面删除延期；继续保留兼容与明确回滚，旧扩展仍可能产生两个进程。没有擅自清理注册表、卸载旧产品或改 HKLM。v0.11.10 验证包不等于公开发布；正式发布须落在版本/CHANGELOG 提交并附注 tag。
