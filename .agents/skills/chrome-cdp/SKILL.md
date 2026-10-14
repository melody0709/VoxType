---
name: chrome-cdp
description: 连接用户自己正在运行的 Chrome/Edge（Chrome 扩展通道为主，CDP 仅兜底）来查资料、读网页、检测 DOM、点击元素，扩展优先免弹窗，必要时复用 CDP 连接，避免重复授权。当用户说「连接我的浏览器 / 打开这个网页查资料 / 帮我查一下这个页面 / 读一下这个链接 / 看看我浏览器里这个页面 / 用我登录的浏览器查 / 检测浏览器里的元素 / 抓一下这个页面的内容」，或英文 connect to my browser / open this page and read it / look something up in my Chrome / read this page / inspect this page / browse with my Chrome 时，必须使用本 skill；入口是 node "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" <命令>，不要改用其它浏览器自动化工具。
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

扩展优先，不为每条命令建立 CDP 连接。

1. **扩展通道**：CLI → 守护进程 → Chrome 扩展。`tabs` / `scripting` 以及启用后的 `userScripts` 均不需要 CDP 授权。
2. **CDP 兜底**：扩展离线或脚本执行能力不可用时，守护进程建立并复用一条连接；并发首次调用共用同一个连接任务。当前连接存续期间通常只需一次 Allow；浏览器、守护进程重启或连接断开后，可能需要重新授权，不能保证永久只点一次。

扩展在线时守护进程常驻；扩展离线且空闲 5 分钟后退出。`stop` 会主动释放连接，下次 CDP 使用可能重新授权。不要为每次操作执行 stop 或 --no-daemon。

## 一次性准备

1. 在 Chrome 扩展管理页加载并启用 **Antigravity Browser Bridge**：
   `D:\GITHUB_melody0709\PC_modify\Chrome extension\antigravity-browser-bridge`。
2. 在该扩展详情页开启“允许运行用户脚本”（旧版 Chrome 可能要求开发者模式）；`status` 应显示版本 `1.1.0`、`extension.caps.userScripts=true`。更新磁盘代码后必须重新加载扩展；`caps:null` 表示旧版未上报能力。
3. 需要 CDP 兜底时，在 `chrome://inspect/#remote-debugging` 启用远程调试。沿用当前浏览器，不新开实例或调试端口。

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
node "<skill>\scripts\cdp.js" stop        # 主动结束守护进程并释放 CDP 连接
node "<skill>\scripts\cdp.js" --cdp eval "query" "document.title" # 显式复用 CDP，用于扩展不能访问的页面/页面 JS 全局量
```

- 标签页按标题/URL 子串或 list 返回的精确 ID 匹配；多个匹配会报错，不会默默选择第一项。
- open/navigate 返回 loading 与请求 URL；读取前等待加载，再核对 URL / document.readyState。
- eval 的扩展路径在 USER_SCRIPT 隔离世界运行，可访问 DOM，不能直接读取页面自有的 JS 全局量；确需页面主世界时用 --cdp。Promise 会等待完成，脚本异常会报失败，不自动重放产生副作用的脚本。
- 默认只读；页面点击、填写、跳转等变更须有用户授权。不得自动确认浏览器的 Allow 授权或代为登录。
- CLI 发现守护进程没在跑会**自动拉起**它（隐藏窗口，日志见下），无需手工启动。
- 也可用 `start_daemon.vbs`（隐藏启动）或把它放进 `shell:startup` 开机常驻。

## 环境变量

| 变量 | 默认 | 说明 |
| :--- | :--- | :--- |
| `BRIDGE_PORT` | `9226` | 守护进程端口；改动时须同步 background.js 的 BRIDGE_PORT |
| `BRIDGE_EXTENSION_ID` | `dfhibbeifejiggbnkmijoiedldgiijih` | 允许连接的扩展 ID；换安装路径/浏览器后须与扩展详情一致 |
| `CDP_CTL_PORT` | `9227` | CLI → 守护进程的控制端口（仅 127.0.0.1，带令牌） |
| `CDP_IDLE_TIMEOUT_MS` | `300000` | 扩展离线后的空闲退出时间（扩展在线时不退出） |
| `CDP_CONNECT_TIMEOUT_MS` | `60000` | 建立 CDP 连接的等待上限（要留出用户点「允许」的时间） |
| `CDP_WS_URL` | — | 直接指定 CDP WebSocket 地址，跳过 `DevToolsActivePort` |
| `CDP_VERBOSE` | — | 同时把日志打到 stderr |

日志：`%TEMP%\chrome-cdp-daemon.log`；状态文件（端口 + 令牌 + pid）：`%TEMP%\chrome-cdp-daemon.json`。

## 排障

- **OFF 标记 / ERR_CONNECTION_REFUSED**：当时本机服务没有监听，不代表现在仍断线。执行 list 会自动启动服务；status 本身只检查，不启动。新版先做 HTTP 探测，服务不可用时退避等待，不反复创建失败的 WebSocket；网络检查仍可能出现在开发者工具网络记录中。
- **扩展在线但 eval 使用 CDP**：检查 caps.userScripts、扩展版本和“允许运行用户脚本”开关。提示会区分“扩展离线”和“扩展在线但脚本能力不可用”。
- **版本/路径不符**：status 包含守护进程脚本路径、协议版本、预期扩展 ID，以及扩展上报的 ID/版本。核对扩展管理页“加载来源”，重新加载该扩展；守护进程脚本更新后需重启一次服务。
- **chrome:// / 扩展商店 / PDF 等页面**：扩展的注入限制仍存在；必要且已授权时尝试 --cdp，失败时如实报告。当前只处理主框架，不宣称已支持跨域 iframe、原生文件选择器或系统弹窗。
- **本机连接安全**：控制接口要求令牌；扩展 WebSocket 检查预期扩展 Origin。不会接纳普通网页的连接。状态文件中包含令牌，不要把其内容贴到聊天或日志。

## 检查与应急

`node "<skill>\scripts\cdp.test.cjs"` 运行无浏览器回归检查。
`cdp.js --no-daemon <command> ...` 每次另建连接，仅用于应急；日常需要强制 CDP 时用 --cdp，继续复用守护进程连接。
