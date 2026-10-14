---
name: chrome-cdp
description: 连接用户自己正在运行的 Chrome/Edge（Chrome 扩展通道为主，CDP 仅兜底）来查资料、读网页、检测 DOM、点击元素，全程零「允许远程调试」弹窗。当用户说「连接我的浏览器 / 打开这个网页查资料 / 帮我查一下这个页面 / 读一下这个链接 / 看看我浏览器里这个页面 / 用我登录的浏览器查 / 检测浏览器里的元素 / 抓一下这个页面的内容」，或英文 connect to my browser / open this page and read it / look something up in my Chrome / read this page / inspect this page / browse with my Chrome 时，必须使用本 skill；入口是 node "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" <命令>，不要改用其它浏览器自动化工具。
---

# Chrome CDP Quick Connector（守护进程版）

通过**常驻守护进程**连接用户正在运行的 Chrome / Edge，支持列出标签页、取文本 / HTML、执行 JS、打开 / 关闭 / 跳转标签页。

## 触发语（何时必须用我）

中文：**连接我的浏览器 / 打开这个网页查资料 / 帮我查一下这个页面 / 读一下这个链接 / 看看我浏览器里这个页面 /
用我登录的浏览器查 / 检测浏览器里的元素 / 抓一下这个页面的内容**
英文：**connect to my browser / open this page and read it / look something up in my Chrome /
read this page / inspect this page / browse with my Chrome**

命中任一句即执行本 skill 的命令，**不要反问、不要改用 puppeteer / playwright / chrome-devtools-mcp 或 harness 自带浏览器工具（Codex browser-use / computer-use 等——走 CDP 会弹「允许远程调试」）**，
也不要新开 Chrome 实例或调试端口。

## 为什么是守护进程

Chrome 对**每一条新建的 CDP 连接**都会弹一次「要允许远程调试吗？ / Allow remote debugging?」。
早期的实现是「每条命令新建一条连接」，所以操作过程中用户要反复点允许。现在改为：

1. **扩展通道（首选，零弹窗）**：CLI → 守护进程 → Chrome 扩展（`chrome.tabs` / `chrome.scripting`）。
   完全不经过 CDP，Chrome 永远不会弹窗。
2. **直连 CDP（兜底）**：守护进程持有**一条长期连接**（读 `DevToolsActivePort`），
   仅在扩展离线、或 `eval`（扩展侧被 MV3 的 CSP 禁止）时使用。授权因此**每个守护进程生命周期最多一次**。
   注意 Chrome 对**每条新建的 CDP 连接**都要授权一次——所以任何"每条命令新建连接"的写法都会反复弹窗，
   必须复用这条常驻连接。

守护进程只要扩展还连在线就常驻；扩展掉线后空闲 `CDP_IDLE_TIMEOUT_MS`（默认 5 分钟）自动退出，
也可以用 `stop` 立刻结束。

## 一次性准备（只做一次）

1. **启用配套扩展**（必须）：
   `chrome://extensions` → 开发者模式 → 加载已解压的扩展程序 / 启用
   **Antigravity Browser Bridge**（目录：`D:\GITHUB_melody0709\PC_modify\Chrome extension\antigravity-browser-bridge`）。
   只要它启用，整条链路就**不会再有任何「允许」弹窗**。
2. **可选的 CDP 兜底**：`chrome://inspect/#remote-debugging` → 勾选 *Enable remote debugging*。
   仅在扩展被禁用时才用得上；真用到时点一次「允许」即可（守护进程会复用那条连接）。

## 用法

```powershell
node "<skill>\scripts\cdp.js" list
node "<skill>\scripts\cdp.js" open "https://example.com"
node "<skill>\scripts\cdp.js" navigate "example" "https://news.ycombinator.com"
node "<skill>\scripts\cdp.js" text "zhihu"
node "<skill>\scripts\cdp.js" html "zhihu"
node "<skill>\scripts\cdp.js" eval "zhihu" "document.title"
node "<skill>\scripts\cdp.js" click "query" ".my-button-selector"
node "<skill>\scripts\cdp.js" close "example.com"
node "<skill>\scripts\cdp.js" status      # 守护进程 / 扩展 / CDP 连接状态
node "<skill>\scripts\cdp.js" stop        # 结束守护进程
```

- 标签页匹配按标题或 URL 的子串。
- CLI 发现守护进程没在跑会**自动拉起**它（隐藏窗口，日志见下），无需手工启动。
- 也可用 `start_daemon.vbs`（隐藏启动）或把它放进 `shell:startup` 开机常驻。

## 环境变量

| 变量 | 默认 | 说明 |
| :--- | :--- | :--- |
| `BRIDGE_PORT` | `9226` | 扩展连接的端口 |
| `CDP_CTL_PORT` | `9227` | CLI → 守护进程的控制端口（仅 127.0.0.1，带令牌） |
| `CDP_IDLE_TIMEOUT_MS` | `300000` | 扩展离线后的空闲退出时间（扩展在线时不退出） |
| `CDP_CONNECT_TIMEOUT_MS` | `60000` | 建立 CDP 连接的等待上限（要留出用户点「允许」的时间） |
| `CDP_WS_URL` | — | 直接指定 CDP WebSocket 地址，跳过 `DevToolsActivePort` |
| `CDP_VERBOSE` | — | 同时把日志打到 stderr |

日志：`%TEMP%\chrome-cdp-daemon.log`；状态文件（端口 + 令牌 + pid）：`%TEMP%\chrome-cdp-daemon.json`。

## 排障

- **还在弹「允许」**：说明扩展没在线，命令走了 CDP 兜底。`status` 看 `extension.connected`，
  到 `chrome://extensions` 确认扩展已启用；扩展的控制台（service worker）应显示它连上了 `ws://127.0.0.1:9226`。
- **扩展控制台刷 `ERR_CONNECTION_REFUSED`**：守护进程不在。执行任意一条命令（会自动拉起它）或跑 `start_daemon.vbs`。
- **`eval` 报 `unsafe-eval` / CSP**：这是**扩展侧**的限制（MV3 的隔离世界与主世界都禁止 `eval`，与页面 CSP 无关，
  已用知乎 / B 站 / `about:blank` 三个页面验证过同一条 CSP 报错）。守护进程会自动改用 CDP 执行该条表达式，
  而 CDP 连接是**守护进程级复用的**，因此 `eval` 最多消耗一次授权（实测 Chrome 常常直接放行，不弹窗）。
  其余命令（`list` / `text` / `html` / `open` / `close` / `navigate` / `click`）**始终零弹窗**。
- **`chrome://`、扩展商店、PDF 阅读器、`file://`、`about:blank`**：扩展注入不到，属已知限制。

## 应急

`cdp.js --no-daemon <command> ...`：不起守护进程，一次性直连 CDP。会按次弹「允许」，仅在排查时使用。
