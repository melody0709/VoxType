#!/usr/bin/env bun
/**
 * Chrome CDP Quick Connector for Antigravity Agents
 * Zero-dependency: Uses native WebSocket in Bun/Node.js.
 * Automatically resolves DevToolsActivePort from Chrome/Edge User Data.
 */
const fs = require('fs');
const path = require('path');

function getDevToolsEndpoint() {
    // Resolve %LOCALAPPDATA% without baking a username into the repository.
    // Inventing a fallback user name would silently probe the wrong profile.
    const localAppData = process.env.LOCALAPPDATA ||
        (process.env.USERPROFILE ? path.join(process.env.USERPROFILE, 'AppData', 'Local') : '');
    if (!localAppData) {
        throw new Error('LOCALAPPDATA/USERPROFILE is not set; cannot locate the Chrome or Edge user data directory.');
    }
    const chromePortFile = path.join(localAppData, 'Google', 'Chrome', 'User Data', 'DevToolsActivePort');
    const edgePortFile = path.join(localAppData, 'Microsoft', 'Edge', 'User Data', 'DevToolsActivePort');

    let portFile = null;
    if (fs.existsSync(chromePortFile)) {
        portFile = chromePortFile;
    } else if (fs.existsSync(edgePortFile)) {
        portFile = edgePortFile;
    }

    if (!portFile) {
        throw new Error('DevToolsActivePort not found. Make sure Chrome/Edge is launched with --remote-debugging-port=9222');
    }

    const lines = fs.readFileSync(portFile, 'utf8').trim().split(/\r?\n/);
    const port = lines[0].trim();
    const browserPath = lines[1].trim();
    return `ws://127.0.0.1:${port}${browserPath}`;
}

async function createCdpClient() {
    const wsUrl = getDevToolsEndpoint();
    const ws = new WebSocket(wsUrl);

    let msgId = 1;
    const pending = new Map();

    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        if (data.id && pending.has(data.id)) {
            pending.get(data.id)(data);
            pending.delete(data.id);
        }
    };

    await new Promise((resolve, reject) => {
        ws.onopen = resolve;
        ws.onerror = reject;
    });

    function send(method, params = {}, sessionId = undefined) {
        const id = msgId++;
        return new Promise((resolve) => {
            pending.set(id, resolve);
            const msg = { id, method, params };
            if (sessionId) msg.sessionId = sessionId;
            ws.send(JSON.stringify(msg));
        });
    }

    async function listTargets() {
        const res = await send('Target.getTargets');
        return res.result.targetInfos.filter(t => t.type === 'page');
    }

    async function attach(targetId) {
        const res = await send('Target.attachToTarget', { targetId, flatten: true });
        return res.result.sessionId;
    }

    async function evaluate(sessionId, expression) {
        const res = await send('Runtime.evaluate', {
            expression,
            returnByValue: true,
            awaitPromise: true
        }, sessionId);
        if (res.error) throw new Error(res.error.message);
        return res.result ? res.result.result.value : null;
    }

    return {
        ws,
        listTargets,
        attach,
        evaluate,
        close: () => ws.close()
    };
}

async function main() {
    const args = process.argv.slice(2);
    const command = args[0] || 'list';

    const client = await createCdpClient();

    try {
        if (command === 'list') {
            const pages = await client.listTargets();
            console.log(JSON.stringify(pages.map(p => ({
                id: p.targetId,
                title: p.title,
                url: p.url
            })), null, 2));
            return;
        }

        const query = args[1];
        if (!query) {
            console.error('Usage: cdp.js <list | text | html | eval> [title_or_url_filter] [eval_expression]');
            process.exit(1);
        }

        const pages = await client.listTargets();
        const target = pages.find(p => p.url.includes(query) || p.title.includes(query));
        if (!target) {
            console.error(`No page found matching "${query}". Available pages:`);
            pages.forEach(p => console.error(` - [${p.title}] (${p.url})`));
            process.exit(1);
        }

        const sessionId = await client.attach(target.targetId);

        if (command === 'text') {
            const text = await client.evaluate(sessionId, 'document.body ? document.body.innerText : ""');
            console.log(text);
        } else if (command === 'html') {
            const html = await client.evaluate(sessionId, 'document.documentElement.outerHTML');
            console.log(html);
        } else if (command === 'eval') {
            const expr = args.slice(2).join(' ');
            const result = await client.evaluate(sessionId, expr);
            console.log(typeof result === 'object' ? JSON.stringify(result, null, 2) : result);
        } else {
            console.error('Unknown command:', command);
        }
    } finally {
        client.close();
    }
}

main().catch(err => {
    console.error('CDP Error:', err.message || err);
    process.exit(1);
});
