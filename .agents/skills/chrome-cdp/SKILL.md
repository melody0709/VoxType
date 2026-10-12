---
name: chrome-cdp
description: Connects to local Chrome or Edge browser via Chrome DevTools Protocol (CDP) WebSocket without external MCP server dependencies. Use when you need to inspect live open browser tabs, extract page text/HTML, evaluate JavaScript expressions, open new tabs, navigate, or control the user's active browser.
---

# Chrome CDP Quick Connector Skill (Daemon-Backed)

This skill allows Antigravity agents to instantly connect to and manipulate the user's running Chrome or Edge browser via Chrome DevTools Protocol (CDP) over WebSocket.

It features a **background daemon (长连接守护进程)** that maintains a persistent connection with Chrome, eliminating repeated "Allow remote debugging?" permission prompts.

## Prerequisites

The user's browser must be running with remote debugging enabled:
```powershell
chrome.exe --remote-debugging-port=9222
```
(Chrome automatically creates `DevToolsActivePort` under `%LOCALAPPDATA%\Google\Chrome\User Data\DevToolsActivePort` or custom `--user-data-dir`).

## Tool Usage

The skill script is located at:
`C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js` (can be run with `rtk bun run` or `rtk node`).

### 1. List All Open Browser Tabs
```powershell
rtk bun run "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" list
```
Returns a JSON array of all open tabs with `id`, `title`, and `url`.

### 2. Open a New Tab
```powershell
rtk bun run "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" open "https://example.com"
```

### 3. Navigate an Existing Tab
```powershell
rtk bun run "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" navigate "example.com" "https://news.ycombinator.com"
```

### 4. Extract Text Content from a Tab
Matches by title or URL substring:
```powershell
rtk bun run "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" text "zhihu"
```

### 5. Evaluate JavaScript in a Tab
```powershell
rtk bun run "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" eval "zhihu" "document.title"
```

### 6. Extract Full HTML
```powershell
rtk bun run "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" html "zhihu"
```

### 7. Click an Element
```powershell
rtk bun run "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" click "query" ".my-button-selector"
```

### 8. Close a Tab
```powershell
rtk bun run "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" close "example.com"
```

### 9. Check Status or Stop Daemon
```powershell
rtk bun run "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" status
rtk bun run "C:\Users\kawae\.gemini\config\skills\chrome-cdp\scripts\cdp.js" stop
```

## Lifecycle & Clean Exit Policy

To avoid lingering background tasks and unnecessary memory usage:
1. **Auto-Exit on Idle**: The daemon automatically shuts down and exits after **5 minutes** of inactivity (`CDP_IDLE_TIMEOUT_MS=300000`).
2. **Auto-Exit on Browser Close**: If the user closes Chrome, the daemon immediately terminates.
3. **Task-Completion Clean-up**: Agents should invoke `cdp.js stop` at the end of a user request if no further browser interactions are expected, ensuring the workspace remains clean without active background tasks.

