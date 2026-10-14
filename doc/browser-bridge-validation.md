# 浏览器上下文桥验证记录

日期：2026-10-13。修改保留在工作区，未提交、未发布。

## 结论与根因

本机 Chrome 的启动参数带 `--force-device-scale-factor=1.25`，窗口实际 DPI 为
144（Windows 150%）。CDP 读到 Chrome 外框 `1540×1672`，Win32 物理外框
`1925×2089`；Google 页面缩放为 250%，页面 DPR 为 3.125。
旧实现拿 Windows 1.5 倍缩放转换 Chrome 1.25 倍坐标，稳定产生
`no_window_match`。页面 DPR 也不能直接当作窗口缩放。

配套扩展 1.0.1 按需上报 `DPR / chrome.tabs.getZoom()`（本机为 1.25）、
窗口矩形与 `screen.availLeft/availTop`。桌面端先验证 native host 的浏览器祖先
PID 与最后聚焦窗口，再按显示器工作区原点换算矩形；缺字段/不匹配一律 Unknown。
不从待匹配窗口的宽高比反推缩放，不增加页面常驻脚本/观察者/轮询。

用户重载扩展后确认实际口述已可读取上下文；运行日志出现
`event=input_context_snapshot captured=1 chars=1 layer=6 timed_out=0 password=0 reason=none`。
此前重启后的 `not_connected peers=0` 是另一类连接空窗，不是矩形不匹配。

## 全部工作区变动审查后的修复

- host 检测仍在单实例互斥体之前，桌面监听移到获取互斥体成功之后，拒绝的重复启动
  不再建立另一个监听器或复位 readiness。删除 main.cpp 未使用的 include 后仍为 150 行。
- 首次断线 250ms 后重试一次；连续失败保留指数退避，收到桌面消息后恢复快速重连资格。
- 窗口快照用序号丢弃逆序应答；启动/重连时恢复最后聚焦窗口；实际焦点与保留的
  最后焦点分开，避免多个 Chrome profile 共用 PID 时永远歧义。
  异步 query 绑定发起时的 native port，重连后不得把旧结果发给新桌面连接的重用 ID。
- 非浏览器查询统一走 `accessibility_focus`：从捕获 HWND 取元素，只在其后代查焦点。
  删除全局 GetFocusedElement/ElementFromPoint，避免后台查询期间切入 Chrome。
  MSAA 使用同根、非浏览器的捕获 HWND。两项架构守卫同时检查禁止的全局调用。
- 密码布尔值缺失/类型错误保持 Unknown；小数/指数不能充当协议整数；窗口 ID
  范围与窗口清单上限明确校验。无焦点帧不读取字段值；密码字段从不读取值。
- 同步 I/O 超时重复取消以覆盖下一次读写的竞争；host 的无限等待可由结束事件取消；
  桌面退出先断开服务端管道，避免取消发生在下一次读取开始之前。
- 注册卸载先验证两处注册再开始恢复，且允许已经恢复的键继续重试。
  独立临时注册表测试覆盖外部改动与半完成卸载，不触碰正式 host 注册。

## 已通过的验证

| 验证 | 结果与范围 |
| --- | --- |
| `build.bat --test` | 产品构建、规范运行载荷安装、全部离线回归通过 |
| 架构守卫 | 23 项通过；基线未上调；文档欠账仍为 27/34 |
| UI 静态布局 | 96/144/192/288 DPI 通过 |
| 扩展 worker 契约 | iframe、开放/闭合 Shadow DOM、密码、无焦点帧、DPR/zoom、焦点丢失/重连、快速重连/退避、乱序 hello 通过 |
| C++ 桥回归 | 分类/隐私、负坐标与混合 DPI 算术、强制缩放、坏类型/越界值、同步超时和 overlapped 取消通过 |
| 注册脚本回归 | 外部改动时不做半卸载、已半完成的卸载可恢复；已接入 `build.bat --test` |
| Chrome 实机扩展查询 | 普通输入框、多行文本、contenteditable、同源 iframe、开放 Shadow DOM、密码框 × 80/100/125/250/500% 页面缩放，共 30 用例通过 |
| Chrome 重复查询 | 再做 20 次密码敏感探测，返回值始终为空；50 次查询合计 p50≈0.4ms、p95≈1.5ms（首轮） |
| 用户真实口述 | Google 网页输入框已确认上下文读取恢复；生产日志 `layer=6` |

查询延迟仅覆盖 worker 内部的 tabs/window API 与 executeScript，不包括桌面管道、
录音、ASR 和上屏；不能作为完整性能签收。实机诊断只读 DOM/Runtime，不开
Accessibility 域。测试页使用合成文本和合成密码。

实机矩阵汇总证据存于 `build/artifacts/diagnostics/browser-bridge-chrome-matrix-summary.json`；
临时 CDP/本地测试页脚本保留在 `.bak/`，不会随产品打包。

最后一次成功矩阵仍使用缩放修复后的 worker；其后另一次 CDP 检查已确认最终
worker 的 helloRevision/快速重连/focused 标志均已加载，但重跑矩阵受浏览器许可
阻塞，未取得结果。Computer Use 因无法可靠确定当前 URL 而停止，未点击 Allow。
最终生命周期与焦点修复由离线回归验证；不把此前实机矩阵算作这部分的实机验收。
最后追加的 native port 代际保护已写入扩展源码与运行载荷并通过契约测试；当前浏览器
仍需下次重载扩展后应用该最后追加项。

## 尚未验证与边界

- Edge 实机、物理双屏/负坐标/跨屏混合 DPI、多 profile 实机、完整普通应用矩阵。
- 干净浏览器与旧 UIA 路径的持续性能对照、长期 CPU/内存增长、完整录音延迟。
- 浏览器选区读取仍明确跳过；本轮恢复的是焦点字段上下文。闭合 Shadow DOM、
  受限页面和地址栏拿不到字段时保持 Unknown，不回退 UIA/MSAA。
- 扩展安装仍为手动加载；商店、企业分发与设置页连接状态行不在本轮范围。

参考坐标模型：[Chromium Blink Coordinate Spaces](https://www.chromium.org/developers/design-documents/blink-coordinate-spaces/)。
