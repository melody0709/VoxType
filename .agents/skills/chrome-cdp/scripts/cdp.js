#!/usr/bin/env bun
/**
 * Chrome Quick Connector for Antigravity Agents
 * Dual-Mode:
 *  1. [Primary] Antigravity Browser Bridge (Extension Mode) - 0 popups, instant execution via Chrome Extension
 *  2. [Fallback] Direct CDP Mode via DevToolsActivePort
 */
const fs = require('fs');
const path = require('path');
const http = require('http');
const crypto = require('crypto');

const EXT_PORT = parseInt(process.env.BRIDGE_PORT || '9226', 10);

// --- 1. Zero-Dependency WebSocket Server for Extension Mode ---
function createSimpleWsServer(port, onConnection) {
  const server = http.createServer((req, res) => {
    res.writeHead(200);
    res.end('Antigravity Browser Bridge OK');
  });

  server.on('upgrade', (req, socket) => {
    const key = req.headers['sec-websocket-key'];
    if (!key) {
      socket.destroy();
      return;
    }
    const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write(
      'HTTP/1.1 101 Switching Protocols\r\n' +
      'Upgrade: websocket\r\n' +
      'Connection: Upgrade\r\n' +
      `Sec-WebSocket-Accept: ${accept}\r\n\r\n`
    );

    const client = {
      send(text) {
        const payload = Buffer.from(text);
        let header;
        if (payload.length < 126) {
          header = Buffer.from([0x81, payload.length]);
        } else if (payload.length <= 65535) {
          header = Buffer.alloc(4);
          header[0] = 0x81;
          header[1] = 126;
          header.writeUInt16BE(payload.length, 2);
        } else {
          header = Buffer.alloc(10);
          header[0] = 0x81;
          header[1] = 127;
          header.writeBigUInt64BE(BigInt(payload.length), 2);
        }
        socket.write(Buffer.concat([header, payload]));
      },
      close() {
        try { socket.destroy(); } catch (e) {}
      }
    };

    let buffer = Buffer.alloc(0);
    socket.on('data', (chunk) => {
      buffer = Buffer.concat([buffer, chunk]);
      while (buffer.length >= 2) {
        const opcode = buffer[0] & 0x0f;
        const isMasked = (buffer[1] & 0x80) !== 0;
        let payloadLen = buffer[1] & 0x7f;
        let offset = 2;

        if (payloadLen === 126) {
          if (buffer.length < 4) break;
          payloadLen = buffer.readUInt16BE(2);
          offset = 4;
        } else if (payloadLen === 127) {
          if (buffer.length < 10) break;
          payloadLen = Number(buffer.readBigUInt64BE(2));
          offset = 10;
        }

        let mask = null;
        if (isMasked) {
          if (buffer.length < offset + 4) break;
          mask = buffer.slice(offset, offset + 4);
          offset += 4;
        }

        if (buffer.length < offset + payloadLen) break;

        let payload = buffer.slice(offset, offset + payloadLen);
        if (isMasked && mask) {
          const unmasked = Buffer.alloc(payloadLen);
          for (let i = 0; i < payloadLen; i++) {
            unmasked[i] = payload[i] ^ mask[i % 4];
          }
          payload = unmasked;
        }

        buffer = buffer.slice(offset + payloadLen);

        if (opcode === 0x01) {
          if (client.onmessage) client.onmessage(payload.toString('utf8'));
        } else if (opcode === 0x08) {
          try { socket.destroy(); } catch (e) {}
        }
      }
    });

    socket.on('error', () => {});
    if (onConnection) onConnection(client);
  });

  return server;
}

async function callExtension(action, payload, timeoutMs = 2500) {
  return new Promise((resolve, reject) => {
    let timer = null;
    let server = null;
    let resolved = false;

    const cleanup = () => {
      if (timer) clearTimeout(timer);
      if (server) {
        try {
          if (server.closeAllConnections) server.closeAllConnections();
          server.close();
        } catch (e) {}
      }
    };

    server = createSimpleWsServer(EXT_PORT, (client) => {
      client.onmessage = (raw) => {
        try {
          const res = JSON.parse(raw);
          if (res.type === 'ready') {
            const req = { id: 1, action, ...payload };
            client.send(JSON.stringify(req));
            return;
          }
          if (res.id === 1) {
            resolved = true;
            client.close();
            cleanup();
            if (res.ok) {
              resolve(res.data);
            } else {
              reject(new Error(res.error || 'Extension error'));
            }
          }
        } catch (err) {
          client.close();
          cleanup();
          reject(err);
        }
      };
    });

    server.on('error', (err) => {
      cleanup();
      reject(err);
    });

    server.listen(EXT_PORT, '127.0.0.1', () => {
      timer = setTimeout(() => {
        if (!resolved) {
          cleanup();
          reject(new Error('Extension not connected'));
        }
      }, timeoutMs);
    });
  });
}

// --- 2. Fallback: Direct CDP Mode ---
function getDevToolsEndpoint() {
  const localAppData = process.env.LOCALAPPDATA || 'C:\\Users\\' + (process.env.USERNAME || 'kawae') + '\\AppData\\Local';
  const possiblePaths = [
    path.join(localAppData, 'Google', 'Chrome', 'User Data', 'DevToolsActivePort'),
    path.join('D:\\Chrome_MCP_Data', 'DevToolsActivePort'),
    path.join(localAppData, 'Microsoft', 'Edge', 'User Data', 'DevToolsActivePort')
  ].filter(p => fs.existsSync(p));

  if (possiblePaths.length === 0) {
    throw new Error('DevToolsActivePort not found. Make sure Chrome is running.');
  }

  possiblePaths.sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
  const portFile = possiblePaths[0];

  const lines = fs.readFileSync(portFile, 'utf8').trim().split(/\r?\n/);
  const port = lines[0].trim();
  const browserPath = lines[1].trim();
  return `ws://127.0.0.1:${port}${browserPath}`;
}

async function callDirectCdp(command, query, expr) {
  const wsUrl = getDevToolsEndpoint();
  const ws = new WebSocket(wsUrl);

  let msgId = 1;
  const pending = new Map();

  ws.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      if (data.id && pending.has(data.id)) {
        pending.get(data.id)(data);
        pending.delete(data.id);
      }
    } catch (e) {}
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

  try {
    if (command === 'list') {
      const res = await send('Target.getTargets');
      return (res.result.targetInfos || []).filter(t => t.type === 'page').map(p => ({
        id: p.targetId,
        title: p.title,
        url: p.url
      }));
    }

    if (command === 'open') {
      const res = await send('Target.createTarget', { url: query || 'about:blank' });
      return { targetId: res.result.targetId, url: query };
    }

    const resTargets = await send('Target.getTargets');
    const pages = (resTargets.result.targetInfos || []).filter(t => t.type === 'page');
    const target = pages.find(p => (p.url && p.url.includes(query)) || (p.title && p.title.includes(query)));
    if (!target) {
      const available = pages.map(p => ` - [${p.title}] (${p.url})`).join('\n');
      throw new Error(`No page found matching "${query}". Available pages:\n${available}`);
    }

    if (command === 'close') {
      await send('Target.closeTarget', { targetId: target.targetId });
      return { closed: true, title: target.title };
    }

    const resAttach = await send('Target.attachToTarget', { targetId: target.targetId, flatten: true });
    const sessionId = resAttach.result.sessionId;

    try {
      if (command === 'text') {
        const res = await send('Runtime.evaluate', { expression: 'document.body ? document.body.innerText : ""', returnByValue: true }, sessionId);
        return res.result ? res.result.result.value : '';
      }
      if (command === 'html') {
        const res = await send('Runtime.evaluate', { expression: 'document.documentElement.outerHTML', returnByValue: true }, sessionId);
        return res.result ? res.result.result.value : '';
      }
      if (command === 'eval') {
        const res = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true }, sessionId);
        if (res.error) throw new Error(res.error.message);
        return res.result ? res.result.result.value : null;
      }
      if (command === 'navigate') {
        await send('Page.navigate', { url: expr }, sessionId);
        return { navigated: true, url: expr };
      }
      if (command === 'click') {
        const js = `(() => { const el = document.querySelector(${JSON.stringify(expr)}); if (!el) return false; el.click(); return true; })()`;
        const res = await send('Runtime.evaluate', { expression: js, returnByValue: true }, sessionId);
        return { clicked: res.result ? res.result.result.value : false };
      }
      throw new Error(`Unknown command: ${command}`);
    } finally {
      try { await send('Target.detachFromTarget', { sessionId }); } catch (e) {}
    }
  } finally {
    try { ws.close(); } catch (e) {}
  }
}

// --- 3. Main Entry ---
async function main() {
  const args = process.argv.slice(2);
  const command = args[0] || 'list';
  const query = args[1];
  const expr = args.slice(2).join(' ');

  // Try Extension Mode first (0 prompts, ultra fast)
  try {
    let payload = {};
    if (command === 'open') payload = { url: query };
    else if (command === 'navigate') payload = { query, url: expr };
    else if (command === 'click') payload = { query, selector: expr };
    else if (command === 'eval') payload = { query, expr };
    else payload = { query };

    const result = await callExtension(command, payload, 1000);
    outputResult(command, result);
    return;
  } catch (extErr) {
    // If extension is not active, fallback to direct CDP
  }

  // Fallback: Direct CDP
  try {
    const result = await callDirectCdp(command, query, expr);
    outputResult(command, result);
  } catch (cdpErr) {
    console.error('Browser Control Error:', cdpErr.message || cdpErr);
    console.error('\n[Tip] Please load the Antigravity Browser Bridge extension from:\n  D:\\GITHUB_melody0709\\PC_modify\\Chrome extension\\antigravity-browser-bridge');
    process.exit(1);
  }
}

function outputResult(command, data) {
  if (command === 'list') {
    console.log(JSON.stringify(data, null, 2));
  } else if (command === 'text' || command === 'html') {
    console.log(data);
  } else {
    console.log(typeof data === 'object' ? JSON.stringify(data, null, 2) : data);
  }
}

main().catch(err => {
  console.error('Fatal Error:', err.message || err);
  process.exit(1);
});
