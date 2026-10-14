# chrome-cdp 触发规则（唯一来源，改动请只改这里再分发）

> 分发目标与各自的 SKILL.md 引用路径（全部是 junction，指向同一个真身
> `C:\Users\kawae\.gemini\config\skills\chrome-cdp`）：
>
> | Harness | 指令文件 | SKILL.md 引用路径 |
> | :--- | :--- | :--- |
> | Codex CLI | `~/.codex/AGENTS.md` | `C:\Users\kawae\.codex\skills\chrome-cdp\SKILL.md` |
> | Claude Code | `~/.claude/CLAUDE.md` | `C:\Users\kawae\.claude\skills\chrome-cdp\SKILL.md` |
> | Gemini / Antigravity | `~/.gemini/GEMINI.md` | `C:\Users\kawae\.gemini\config\skills\chrome-cdp\SKILL.md` |
> | WorkBuddy | `~/.workbuddy/AGENTS.md` | `C:\Users\kawae\.workbuddy\skills\chrome-cdp\SKILL.md` |
> | DeepSeek harness | `~/.deepseek/AGENTS.md` | `C:\Users\kawae\.deepseek\skills\chrome-cdp\SKILL.md` |
> | CodeBuddy（用户级） | `~/.agents/rules/chrome-cdp.md` + 全局记忆库 | `C:\Users\kawae\.agents\skills\chrome-cdp\SKILL.md` |
> | Cursor | `AGENTS.md` / `.cursor/rules/chrome-cdp.mdc`（工作区层） | `C:\Users\kawae\.cursor\skills\chrome-cdp\SKILL.md` |

---

## 浏览器连接

- 用户要求连接自己的浏览器、读取链接/网页、用已登录的 Chrome/Edge 查资料或检查页面时，必须使用 chrome-cdp skill；先读 `<该 harness 的 skills 目录>\chrome-cdp\SKILL.md`，操作与排障细节以该文件为准。
- 唯一入口：`node "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" <命令>`。不得改用其它浏览器自动化工具，也不得新开浏览器实例或调试端口。
- 扩展优先；必要时复用常驻 CDP，避免逐命令重连和重复 Allow。浏览器/服务重启或断线后可能重新授权，不承诺永久只点一次。
- 默认只读；页面点击或修改须有用户授权。不得代为登录、填写账号密码或自动确认 Allow。
