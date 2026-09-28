---
name: chrome-cdp
description: Connects to local Chrome or Edge browser via Chrome DevTools Protocol (CDP) WebSocket without external MCP server dependencies. Use when you need to inspect live open browser tabs, extract page text/HTML, or evaluate JavaScript expressions in user's active browser.
---

# Chrome CDP Quick Connector Skill

This skill allows Antigravity agents to instantly connect to the user's running Chrome or Edge browser via Chrome DevTools Protocol (CDP) over WebSocket, bypassing modern Chrome's HTTP discovery restrictions (`404 on /json/list`).

## Prerequisites

The user's browser must be running with remote debugging enabled:
```powershell
chrome.exe --remote-debugging-port=9222
```
(Chrome creates `DevToolsActivePort` automatically under `%LOCALAPPDATA%\Google\Chrome\User Data\DevToolsActivePort`).

## Tool Usage

The skill script is located at:
`scripts/cdp.js` (can be run with `bun` or `node`).

### 1. List All Open Browser Tabs
```powershell
rtk bun run ".agents/skills/chrome-cdp/scripts/cdp.js" list
```
Returns a JSON array of all open tabs with `id`, `title`, and `url`.

### 2. Extract Text Content from a Tab
Matches by title or URL substring:
```powershell
rtk bun run ".agents/skills/chrome-cdp/scripts/cdp.js" text "bailian"
```

### 3. Evaluate JavaScript in a Tab
```powershell
rtk bun run ".agents/skills/chrome-cdp/scripts/cdp.js" eval "bailian" "document.title"
```

### 4. Extract Full HTML
```powershell
rtk bun run ".agents/skills/chrome-cdp/scripts/cdp.js" html "bailian"
```
