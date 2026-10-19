# chrome-cdp 触发规则（唯一来源，改动请只改这里再分发）

> 这份文本会被复制进各 harness 的指令文件，用来保证"用户说『连接我的浏览器查资料』时必定调用本 skill"。
> 分发目标：
> - 工作区：`AGENTS.md`、`GEMINI.md`、`.cursor/rules/chrome-cdp.mdc`
> - 全局：`C:\Users\kawae\.claude\CLAUDE.md`、`C:\Users\kawae\.codex\AGENTS.md`、`C:\Users\kawae\.gemini\GEMINI.md`
> - 技能安装（自动触发用）：`C:\Users\kawae\.gemini\config\skills\chrome-cdp`（真身），
>   以及 `~/.claude/skills`、`~/.codex/skills`、`~/.cursor/skills` 下的 junction。

---

## 浏览器连接（chrome-cdp）——强制触发规则

**触发语（命中任一句就必须执行，不要反问、不要改用其它浏览器工具）**

中文：连接我的浏览器 / 打开这个网页查资料 / 帮我查一下这个页面 / 读一下这个链接 / 看看我浏览器里这个页面 /
用我登录的浏览器查 / 检测浏览器里的元素 / 抓一下这个页面的内容
英文：connect to my browser / open this page and read it / look something up in my Chrome /
read this page / browse with my Chrome / inspect this page

**唯一入口**（绝对路径，Windows）：

```powershell
node "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" <命令>
```

`status` | `list` | `open <url>` | `navigate <query> <url>` | `text <query>` | `html <query>` | `eval <query> <expr>` | `click <query> <selector>` | `close <query>` | `stop`

- `<query>` = 标签页标题/URL 子串或精确标签页 ID；多项匹配会报错，不确定先 `list`。
- 标准查资料流程：`open "<url>"` → 等 2~3 秒（不会自动等加载）→ `text "<query>"`；不够完整再用 `eval` 做结构化提取。
- 通道说明：优先走已登录 Profile 的扩展；eval 免 CDP 需要在扩展详情启用“允许运行用户脚本”。扩展离线或脚本能力不可用时可复用常驻 CDP；必要时用 `--cdp <命令>`。并发调用共用连接，当前连接通常只需一次 Allow；浏览器/服务重启或断线重连可能重新授权，禁止承诺永久只点一次。
- 限制：当前只处理主框架；扩展在浏览器内部页、扩展商店、PDF 等页面有注入限制，必要且已授权时尝试 `--cdp`。扩展 eval 是隔离世界，读取页面 JS 全局量需 `--cdp`。
- 详细行为与排障：`C:\Users\kawae\.gemini\config\skills\chrome-cdp\SKILL.md`（仓库镜像 `.agents/skills/chrome-cdp/SKILL.md`）。

**禁止**：不要改用 puppeteer / playwright / chrome-devtools-mcp 等其它浏览器自动化；不要新开 Chrome 实例或调试端口；
不要为了取内容让用户反复点「允许」；**默认只读**，点击/改动页面必须先得到用户同意；不要替用户登录或输入账号密码。

**故障**：`status` 显示 `extension.connected=false` → 提醒用户在 `chrome://extensions` 启用 "Antigravity Browser Bridge"
（或执行 `list` 自动启动本机服务，`status` 只检查）；`ERR_CONNECTION_REFUSED` 说明当时本机服务未监听。更新后核对 `caps.version=1.1.0` / `caps.userScripts`，旧版需重新加载扩展；日志：`%TEMP%\chrome-cdp-daemon.log`。
