#!/usr/bin/env node
/**
 * chrome-cdp —— 常驻守护进程版 Chrome/Edge 连接器
 *
 * 两条通道（守护进程按优先级自动选）：
 *   1) 扩展通道（零弹窗）：CLI --HTTP 127.0.0.1:9227--> 守护进程 --WS 9226--> Chrome 扩展
 *      扩展用 chrome.scripting / chrome.tabs 执行，**不经过 CDP**，Chrome 永远不会弹
 *      「要允许远程调试吗？」。
 *   2) 直连 CDP（兜底）：守护进程 --WS 9222--> Chrome DevTools（读 DevToolsActivePort）。
 *      Chrome 对**每一条新建的 CDP 连接**都要点一次「允许」，所以这里必须复用同一条连接：
 *      守护进程常驻 —— 每条命令都新建连接的老实现在操作过程中会反复弹窗。
 *
 * 生命周期：只要扩展还连在线就常驻；扩展掉线（Chrome 退出 / 扩展被禁用）后空闲
 * CDP_IDLE_TIMEOUT_MS（默认 5 分钟）自动退出。`stop` 可立即关闭。
 *
 * 用法：
 *   cdp.js list | open <url> | navigate <query> <url> | text <query> | html <query>
 *          | eval <query> <expr> | click <query> <selector> | close <query>
 *   cdp.js status            # 守护进程 / 扩展 / CDP 连接状态
 *   cdp.js stop              # 结束守护进程
 *   cdp.js --no-daemon ...   # 应急：不起守护进程，一次性直连 CDP（会按次弹窗）
 *   cdp.js __daemon__        # 守护进程本体（由 CLI 自动拉起，或 start_daemon.vbs 隐藏启动）
 */
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const crypto = require('crypto');
const { spawn } = require('child_process');

const EXT_PORT = parseInt(process.env.BRIDGE_PORT || '9226', 10);
const CTL_PORT = parseInt(process.env.CDP_CTL_PORT || '9227', 10);
const IDLE_TIMEOUT_MS = parseInt(process.env.CDP_IDLE_TIMEOUT_MS || '300000', 10);
const EXT_WAIT_MS = parseInt(process.env.CDP_EXT_WAIT_MS || '2500', 10);
const CMD_TIMEOUT_MS = parseInt(process.env.CDP_CMD_TIMEOUT_MS || '20000', 10);
const CDP_CONNECT_TIMEOUT_MS = parseInt(process.env.CDP_CONNECT_TIMEOUT_MS || '60000', 10);
const EXT_PING_MS = 20000;
const EXTENSION_ID = process.env.BRIDGE_EXTENSION_ID || 'dfhibbeifejiggbnkmijoiedldgiijih';
const CLIENT_TIMEOUT_MS = CDP_CONNECT_TIMEOUT_MS + 2 * CMD_TIMEOUT_MS + EXT_WAIT_MS + 8000;
const MAX_FRAME_BYTES = 32 * 1024 * 1024;

const STATE_FILE = path.join(os.tmpdir(), 'chrome-cdp-daemon.json');
const LOG_FILE = path.join(os.tmpdir(), 'chrome-cdp-daemon.log');

function log(msg) {
  const line = `${new Date().toISOString()} ${msg}\n`;
  try {
    fs.appendFileSync(LOG_FILE, line);
  } catch (e) {}
  if (process.env.CDP_VERBOSE) process.stderr.write(line);
}

// ---------------------------------------------------------------- WebSocket 服务端

function writeFrame(socket, opcode, payload) {
  const body = Buffer.isBuffer(payload) ? payload : Buffer.from(String(payload), 'utf8');
  let header;
  if (body.length < 126) {
    header = Buffer.from([0x80 | opcode, body.length]);
  } else if (body.length <= 65535) {
    header = Buffer.alloc(4);
    header[0] = 0x80 | opcode;
    header[1] = 126;
    header.writeUInt16BE(body.length, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | opcode;
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(body.length), 2);
  }
  try {
    socket.write(Buffer.concat([header, body]));
  } catch (e) {}
}

/**
 * 极简 WebSocket 服务端：只服务浏览器扩展（客户端必然带掩码）。
 * 支持文本帧、close、ping（回 pong）、pong（忽略）。
 */
function createWsServer(onClient) {
  const server = http.createServer((req, res) => {
    res.writeHead(200, { 'content-type': 'text/plain; charset=utf-8' });
    res.end(JSON.stringify({ service: 'chrome-cdp', protocol: 2 }));
  });

  server.on('upgrade', (req, socket) => {
    const key = req.headers['sec-websocket-key'];
    if (req.headers.origin !== `chrome-extension://${EXTENSION_ID}` || !key) {
      socket.destroy();
      return;
    }
    const accept = crypto
      .createHash('sha1')
      .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
      .digest('base64');
    socket.write(
      'HTTP/1.1 101 Switching Protocols\r\n' +
        'Upgrade: websocket\r\n' +
        'Connection: Upgrade\r\n' +
        `Sec-WebSocket-Accept: ${accept}\r\n\r\n`
    );

    let closed = false;
    const client = {
      onmessage: null,
      onclose: null,
      send(payload) {
        if (closed) return;
        writeFrame(socket, 0x1, payload);
      },
      close() {
        if (closed) return;
        closed = true;
        writeFrame(socket, 0x8, Buffer.alloc(0));
        try {
          socket.destroy();
        } catch (e) {}
      },
      get closed() {
        return closed;
      }
    };

    let buffer = Buffer.alloc(0);
    let fragments = [];
    let fragmentOpcode = 0;
    let fragmentBytes = 0;
    socket.on('data', (chunk) => {
      buffer = Buffer.concat([buffer, chunk]);
      while (buffer.length >= 2) {
        const fin = (buffer[0] & 0x80) !== 0;
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
        if (payloadLen > MAX_FRAME_BYTES) {
          client.close();
          return;
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
          for (let i = 0; i < payloadLen; i++) unmasked[i] = payload[i] ^ mask[i % 4];
          payload = unmasked;
        }
        buffer = buffer.slice(offset + payloadLen);

        if (opcode === 0x00 || opcode === 0x01 || opcode === 0x02) {
          // FIN=0 的连续帧必须攒齐再交给上层：Chrome 会把大消息拆成多个帧
          if (opcode !== 0x00) {
            fragments = [];
            fragmentOpcode = opcode;
            fragmentBytes = 0;
          }
          if (payload.length) {
            fragments.push(payload);
            fragmentBytes += payload.length;
          }
          if (fragmentBytes > MAX_FRAME_BYTES) {
            client.close();
            return;
          }
          if (fin && fragmentOpcode === 0x01 && client.onmessage) {
            const full = fragments.length === 1 ? fragments[0] : Buffer.concat(fragments);
            fragments = [];
            fragmentBytes = 0;
            try {
              client.onmessage(full.toString('utf8'));
            } catch (e) {
              log(`client message handler error: ${e.message}`);
            }
          }
        } else if (opcode === 0x08) {
          closed = true;
          try {
            socket.destroy();
          } catch (e) {}
          if (client.onclose) client.onclose();
          return;
        } else if (opcode === 0x09) {
          writeFrame(socket, 0xa, payload);
        }
      }
    });

    const finish = () => {
      if (closed) return;
      closed = true;
      if (client.onclose) client.onclose();
    };
    socket.on('close', finish);
    socket.on('error', finish);

    if (onClient) onClient(client);
  });

  return server;
}

// ---------------------------------------------------------------- 直连 CDP（兜底通道）

function getDevToolsEndpoint() {
  if (process.env.CDP_WS_URL) return process.env.CDP_WS_URL;

  const localAppData = process.env.LOCALAPPDATA || 'C:\\Users\\' + (process.env.USERNAME || '') + '\\AppData\\Local';
  const candidates = [
    path.join(localAppData, 'Google', 'Chrome', 'User Data', 'DevToolsActivePort'),
    path.join(localAppData, 'Microsoft', 'Edge', 'User Data', 'DevToolsActivePort'),
    path.join('D:\\Chrome_MCP_Data', 'DevToolsActivePort')
  ].filter((p) => fs.existsSync(p));

  if (candidates.length === 0) {
    throw new Error(
      'DevToolsActivePort not found —— 没有可直连的调试端点。\n' +
        '请先在 Chrome 打开 chrome://inspect/#remote-debugging 并勾选 Enable remote debugging，' +
        '或让扩展保持启用（扩展通道不需要 CDP）。'
    );
  }

  candidates.sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
  const lines = fs.readFileSync(candidates[0], 'utf8').trim().split(/\r?\n/);
  const port = lines[0].trim();
  const browserPath = (lines[1] || '').trim();
  return `ws://127.0.0.1:${port}${browserPath}`;
}

/** 连接由守护进程复用；浏览器或连接重建后可能需要重新授权。 */
function connectCdp() {
  return new Promise((resolve, reject) => {
    let wsUrl;
    try {
      wsUrl = getDevToolsEndpoint();
    } catch (err) {
      reject(err);
      return;
    }

    log(`connecting CDP ${wsUrl}`);
    const ws = new WebSocket(wsUrl);
    let settled = false;
    const pending = new Map();
    let seq = 0;

    const conn = {
      ws,
      closed: false,
      send(method, params = {}, sessionId = undefined) {
        const id = ++seq;
        return new Promise((res, rej) => {
          if (conn.closed) return rej(new Error('CDP connection closed'));
          const timer = setTimeout(() => {
            pending.delete(id);
            rej(new Error(`CDP command timeout: ${method}`));
          }, CMD_TIMEOUT_MS);
          pending.set(id, { res, rej, timer });
          const msg = { id, method, params };
          if (sessionId) msg.sessionId = sessionId;
          try {
            ws.send(JSON.stringify(msg));
          } catch (err) {
            pending.delete(id);
            clearTimeout(timer);
            rej(err);
          }
        });
      },
      close() {
        conn.closed = true;
        try {
          ws.close();
        } catch (e) {}
      }
    };

    ws.onmessage = (event) => {
      let data;
      try {
        data = JSON.parse(event.data);
      } catch (e) {
        return;
      }
      if (data.id && pending.has(data.id)) {
        const { res, rej, timer } = pending.get(data.id);
        clearTimeout(timer);
        pending.delete(data.id);
        if (data.error) rej(new Error(data.error.message || JSON.stringify(data.error)));
        else res(data.result === undefined ? null : data.result);
      }
    };
    ws.onclose = () => {
      conn.closed = true;
      for (const { rej, timer } of pending.values()) {
        clearTimeout(timer);
        rej(new Error('CDP connection closed'));
      }
      pending.clear();
    };
    ws.onerror = () => {
      if (!settled) {
        settled = true;
        reject(new Error(`CDP connect failed: ${wsUrl}`));
      }
    };
    ws.onopen = () => {
      if (settled) return;
      settled = true;
      log('CDP connected');
      resolve(conn);
    };

    // 连接停在「等用户点允许」时给出明确提示，而不是静默挂死
    const hint = setTimeout(() => {
      if (!settled) {
        // 守护进程是隐藏窗口运行，stderr 看不到；真正的用户提示由 CLI 在等待时打印
        log('waiting for CDP connect —— Chrome 可能正在等用户点「允许远程调试」');
      }
    }, 2500);

    const deadline = setTimeout(() => {
      clearTimeout(hint);
      if (!settled) {
        settled = true;
        try {
          ws.close();
        } catch (e) {}
        reject(new Error('CDP connect timeout'));
      }
    }, CDP_CONNECT_TIMEOUT_MS);

    const done = () => {
      clearTimeout(hint);
      clearTimeout(deadline);
    };
    ws.addEventListener?.('open', done, { once: true });
    ws.addEventListener?.('error', done, { once: true });
    ws.addEventListener?.('close', () => {
      done();
      if (!settled) {
        settled = true;
        reject(new Error('CDP closed before connection completed'));
      }
    }, { once: true });
  });
}

async function cdpExecute(conn, action, payload) {
  const query = payload.query;
  const url = payload.url;
  const expr = payload.expr;

  if (action === 'list') {
    const res = await conn.send('Target.getTargets');
    return (res.targetInfos || [])
      .filter((t) => t.type === 'page')
      .map((t) => ({ id: t.targetId, title: t.title, url: t.url }));
  }

  if (action === 'open') {
    const res = await conn.send('Target.createTarget', { url: url || 'about:blank' });
    return { targetId: res.targetId, url };
  }

  const resTargets = await conn.send('Target.getTargets');
  const pages = (resTargets.targetInfos || []).filter((t) => t.type === 'page');
  const matches = pages.filter((p) =>
    !query || [p.url, p.title, p.targetId].some((v) => String(v || '').toLowerCase().includes(String(query).toLowerCase())));
  if (matches.length > 1) throw new Error(`Ambiguous page query "${query}"; use a unique URL or target ID`);
  const target = matches[0];
  if (!target) {
    const available = pages.map((p) => ` - [${p.title}] (${p.url})`).join('\n');
    throw new Error(`No page found matching "${query}". Available pages:\n${available}`);
  }

  if (action === 'close') {
    await conn.send('Target.closeTarget', { targetId: target.targetId });
    return { closed: true, title: target.title };
  }

  const attached = await conn.send('Target.attachToTarget', { targetId: target.targetId, flatten: true });
  const sessionId = attached.sessionId;
  const evaluate = async (expression) => {
    const result = await conn.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }, sessionId);
    if (result.exceptionDetails) {
      const detail = result.exceptionDetails;
      throw new Error(detail.exception?.description || detail.text || 'JavaScript evaluation failed');
    }
    if (result.result?.type === 'undefined') return null;
    if (result.result?.unserializableValue !== undefined) return result.result.unserializableValue;
    return result.result?.value ?? null;
  };
  try {
    if (action === 'text') {
      return await evaluate('document.body ? document.body.innerText : ""');
    }
    if (action === 'html') {
      return await evaluate('document.documentElement.outerHTML');
    }
    if (action === 'eval') {
      return await evaluate(expr);
    }
    if (action === 'navigate') {
      await conn.send('Page.navigate', { url: url || expr }, sessionId);
      return { navigated: true, url: url || expr };
    }
    if (action === 'click') {
      const sel = payload.selector || expr;
      const js = `(() => { const el = document.querySelector(${JSON.stringify(sel)}); if (!el) return false; el.scrollIntoView({block:'center'}); el.click(); return true; })()`;
      return { clicked: !!(await evaluate(js)) };
    }
    throw new Error(`Unknown command: ${action}`);
  } finally {
    try {
      await conn.send('Target.detachFromTarget', { sessionId });
    } catch (e) {}
  }
}

// ---------------------------------------------------------------- 守护进程

function waitForExtension(hub, timeoutMs) {
  return new Promise((resolve) => {
    const deadline = Date.now() + timeoutMs;
    const tick = () => {
      if (hub.client || Date.now() >= deadline) resolve(!!hub.client);
      else setTimeout(tick, 100);
    };
    tick();
  });
}

function callExtension(hub, action, payload, timeoutMs) {
  return new Promise((resolve, reject) => {
    const client = hub.client;
    if (!client)
      return reject(Object.assign(new Error('extension not connected'), { code: 'no_extension' }));

    const id = ++hub.seq;
    const timer = setTimeout(() => {
      hub.pending.delete(id);
      hub.chunks.delete(id);
      reject(Object.assign(new Error(`extension timeout after ${timeoutMs}ms`), { code: 'ext_timeout' }));
    }, timeoutMs);

    hub.pending.set(id, {
      resolve: (data) => {
        clearTimeout(timer);
        resolve(data);
      },
      client,
      reject: (err) => {
        clearTimeout(timer);
        hub.chunks.delete(id);
        reject(err);
      }
    });

    try {
      client.send(JSON.stringify({ id, action, ...payload }));
    } catch (err) {
      hub.pending.delete(id);
      clearTimeout(timer);
      reject(Object.assign(err, { code: 'ext_send_failed' }));
    }
  });
}

function isEvalCspError(action, err) {
  if (action !== 'eval') return false;
  if (err?.code === 'script_error') return false;
  return err?.code === 'eval_unavailable' || /unsafe-eval|Refused to evaluate|Content Security Policy/i.test(String(err && err.message));
}

function jsonResponse(res, code, body) {
  const text = JSON.stringify(body);
  res.writeHead(code, { 'content-type': 'application/json; charset=utf-8' });
  res.end(text);
}

function readJsonBody(req, limit = MAX_FRAME_BYTES) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    req.on('data', (c) => {
      size += c.length;
      if (size > limit) {
        req.destroy();
        reject(new Error('body too large'));
        return;
      }
      chunks.push(c);
    });
    req.on('end', () => {
      try {
        resolve(chunks.length ? JSON.parse(Buffer.concat(chunks).toString('utf8')) : {});
      } catch (err) {
        reject(err);
      }
    });
    req.on('error', reject);
  });
}

function runDaemon() {
  const startedAt = Date.now();
  const token = crypto.randomBytes(16).toString('hex');
  const hub = { client: null, ready: new Set(), connectedSince: 0, seq: 0, pending: new Map(), chunks: new Map(), caps: null };
  let cdpConn = null;
  let cdpConnecting = null;
  let lastActivity = Date.now();
  const touch = () => {
    lastActivity = Date.now();
  };

  // --- 扩展通道（零弹窗）
  const extServer = createWsServer((client) => {
    client.onmessage = (raw) => {
      let msg;
      try {
        msg = JSON.parse(raw);
      } catch (e) {
        return;
      }
      if (!msg) return;
      touch();

      // 扩展对大回复分片发送：攒齐后当成一条正常回复
      if (msg.chunk && msg.id) {
        if (hub.pending.get(msg.id)?.client !== client) return;
        const c = msg.chunk;
        if (!Number.isInteger(c.total) || c.total < 1 || c.total > 1024 ||
            !Number.isInteger(c.i) || c.i < 0 || c.i >= c.total || typeof c.part !== 'string' || c.part.length > 40000) {
          const pending = hub.pending.get(msg.id);
          hub.pending.delete(msg.id);
          pending.reject(new Error('Invalid chunked reply'));
          return;
        }
        const entry = hub.chunks.get(msg.id) || { parts: new Array(c.total), received: 0, total: c.total, bytes: 0 };
        entry.bytes += Buffer.byteLength(c.part);
        if (entry.total !== c.total || entry.bytes > MAX_FRAME_BYTES) {
          const pending = hub.pending.get(msg.id);
          hub.pending.delete(msg.id);
          pending.reject(new Error('Chunked reply too large or inconsistent'));
          return;
        }
        if (msg.chunk.i >= 0 && msg.chunk.i < entry.total) {
          if (entry.parts[msg.chunk.i] === undefined) entry.received++;
          entry.parts[msg.chunk.i] = msg.chunk.part;
        }
        hub.chunks.set(msg.id, entry);
        if (entry.received < entry.total) return;
        hub.chunks.delete(msg.id);
        let assembled;
        try {
          assembled = JSON.parse(entry.parts.join(''));
        } catch (e) {
          const bad = hub.pending.get(msg.id);
          if (bad) {
            hub.pending.delete(msg.id);
            bad.reject(new Error(`chunked reply assembly failed: ${e.message}`));
          }
          return;
        }
        msg = assembled;
      }

      if (msg.type === 'ready' || msg.type === 'hello') {
        const first = !hub.ready.has(client);
        hub.ready.add(client);
        hub.client = client;
        if (first || hub.connectedSince === 0) hub.connectedSince = Date.now();
        hub.caps = msg.caps || null;
        log(`extension connected (executor)`);
        try {
          client.send(JSON.stringify({ type: 'ping' })); // 立刻取一次能力探测
        } catch (e) {}
        return;
      }
      if (msg.type === 'pong' || msg.type === 'ping') {
        if (msg.caps) {
          const prev = hub.caps;
          hub.caps = msg.caps;
          if (!prev || prev.userScripts !== msg.caps.userScripts) {
            log(`extension caps: userScripts=${msg.caps.userScripts} offscreen=${msg.caps.offscreen} v${msg.caps.version}`);
          }
        }
        return;
      }
      if (!msg.id) return;

      const entry = hub.pending.get(msg.id);
      if (!entry || entry.client !== client) return;
      hub.pending.delete(msg.id);
      if (msg.ok) entry.resolve(msg.data);
      else entry.reject(Object.assign(new Error(msg.error || 'extension error'), { code: msg.code }));
    };
    client.onclose = () => {
      hub.ready.delete(client);
      for (const [id, entry] of hub.pending) {
        if (entry.client !== client) continue;
        hub.pending.delete(id);
        entry.reject(new Error('Extension disconnected; command was not retried'));
      }
      if (hub.client !== client) return;
      // 一条连接断开不代表扩展离线（可能还有别的窗口/实例连着）：改认领仍然存活的那条
      let next = null;
      for (const c of hub.ready) if (!c.closed) next = c;
      hub.client = next;
      hub.caps = null;
      if (next) next.send(JSON.stringify({ type: 'ping' }));
      hub.connectedSince = next ? Date.now() : 0;
      log(next ? 'executor connection closed —— switched to a live one' : 'extension disconnected');
    };
  });

  extServer.on('error', (err) => {
    if (err.code === 'EADDRINUSE') {
      log(`port ${EXT_PORT} busy —— 已有守护进程，退出`);
      process.exit(0);
    }
    log(`extension server error: ${err.message}`);
  });

  // --- 控制通道（CLI 使用）
  const ctlServer = http.createServer(async (req, res) => {
    if (req.headers['x-cdp-token'] !== token) {
      jsonResponse(res, 403, { ok: false, error: 'bad token' });
      return;
    }
    const url = (req.url || '/').split('?')[0];

    if (req.method === 'GET' && url === '/health') {
      jsonResponse(res, 200, {
        ok: true,
        pid: process.pid,
        startedAt,
        uptimeMs: Date.now() - startedAt,
        extension: {
          connected: !!hub.client,
          sinceMs: hub.connectedSince ? Date.now() - hub.connectedSince : 0,
          caps: hub.caps
        },
        cdp: { connected: !!cdpConn && !cdpConn.closed, connecting: !!cdpConnecting },
        protocol: 2,
        script: __filename,
        expectedExtensionId: EXTENSION_ID,
        logFile: LOG_FILE
      });
      return;
    }

    if (req.method === 'POST' && url === '/stop') {
      jsonResponse(res, 200, { ok: true, stopping: true });
      setTimeout(() => shutdown(0), 50);
      return;
    }

    if (req.method === 'POST' && url === '/cmd') {
      let body;
      try {
        body = await readJsonBody(req);
      } catch (err) {
        jsonResponse(res, 400, { ok: false, error: `bad request: ${err.message}` });
        return;
      }
      touch();
      try {
        const result = await handleCommand(hub, body);
        touch();
        jsonResponse(res, result.ok ? 200 : 500, result);
      } catch (err) {
        touch();
        jsonResponse(res, 500, { ok: false, via: 'none', error: err.message || String(err) });
      }
      return;
    }

    jsonResponse(res, 404, { ok: false, error: 'not found' });
  });

  ctlServer.on('error', (err) => {
    if (err.code === 'EADDRINUSE') {
      log(`control port ${CTL_PORT} busy —— 已有守护进程，退出`);
      process.exit(0);
    }
    log(`control server error: ${err.message}`);
  });

  async function getCdp() {
    if (cdpConn && !cdpConn.closed) return cdpConn;
    if (!cdpConnecting) {
      cdpConnecting = connectCdp().then((conn) => { cdpConn = conn; return conn; })
        .finally(() => { cdpConnecting = null; });
    }
    return cdpConnecting;
  }

  async function handleCommand(hubRef, body) {
    const action = body.action || 'list';
    if (!['list', 'open', 'navigate', 'text', 'html', 'eval', 'click', 'close'].includes(action)) {
      throw new Error(`Unknown command: ${action}`);
    }
    let fallbackReason = body.transport === 'cdp' ? 'requested' : 'extension_offline';
    const payload = {
      query: body.query,
      url: body.url,
      expr: body.expr,
      selector: body.selector || (action === 'click' ? body.expr : undefined),
      tabId: body.tabId
    };

    if (body.transport !== 'cdp' && !hubRef.client) await waitForExtension(hubRef, EXT_WAIT_MS);

    if (body.transport !== 'cdp' && hubRef.client) {
      try {
        const data = await callExtension(hubRef, action, payload, CMD_TIMEOUT_MS);
        return { ok: true, via: 'extension', data };
      } catch (err) {
        if (!isEvalCspError(action, err)) {
          return {
            ok: false,
            via: 'extension',
            error: err.message,
            hint: err.code === 'ext_timeout' ? '扩展执行超时（页面可能未加载完或标签页不可注入）' : undefined
          };
        }
        fallbackReason = 'extension_eval_unavailable';
        log(`extension eval unavailable (${err.message}) —— reuse CDP`);
      }
    }

    try {
      const data = await cdpExecute(await getCdp(), action, payload);
      return { ok: true, via: 'cdp', fallbackReason, data };
    } catch (err) {
      return {
        ok: false,
        via: 'cdp',
        error: err.message,
        hint: fallbackReason === 'extension_offline'
          ? '扩展离线；检查扩展是否启用与本机服务是否在线'
          : '扩展在线但无法执行此脚本；检查扩展详情中的“允许运行用户脚本”，或允许本次 CDP 连接'
      };
    }
  }

  function shutdown(code) {
    try {
      fs.unlinkSync(STATE_FILE);
    } catch (e) {}
    if (cdpConn) cdpConn.close();
    try {
      extServer.close();
    } catch (e) {}
    try {
      ctlServer.close();
    } catch (e) {}
    log(`daemon exit code=${code}`);
    setTimeout(() => process.exit(code), 30);
  }

  extServer.listen(EXT_PORT, '127.0.0.1', () => {
    log(`daemon up pid=${process.pid} ext=${EXT_PORT} ctl=${CTL_PORT} idle=${IDLE_TIMEOUT_MS}ms`);
  });

  ctlServer.listen(CTL_PORT, '127.0.0.1', () => {
    try {
      fs.writeFileSync(
        STATE_FILE,
        JSON.stringify({ pid: process.pid, extPort: EXT_PORT, ctlPort: CTL_PORT, token, startedAt }, null, 2),
        'utf8'
      );
    } catch (e) {
      log(`state file write failed: ${e.message}`);
    }
  });

  // 扩展在线时保持守护进程存活；同时用 ping 让 MV3 service worker 不被回收
  setInterval(() => {
    if (hub.client) {
      try {
        hub.client.send(JSON.stringify({ type: 'ping' }));
      } catch (e) {}
    }
  }, EXT_PING_MS);

  setInterval(() => {
    if (hub.client) {
      lastActivity = Date.now();
      return;
    }
    if (Date.now() - lastActivity > IDLE_TIMEOUT_MS) {
      log('idle timeout —— 扩展离线且无命令，退出');
      shutdown(0);
    }
  }, 30000);
}

// ---------------------------------------------------------------- CLI

function readState() {
  try {
    return JSON.parse(fs.readFileSync(STATE_FILE, 'utf8'));
  } catch (e) {
    return null;
  }
}

function ctlRequest(method, pathname, body, timeoutMs) {
  return new Promise((resolve, reject) => {
    const state = readState();
    const req = http.request(
      {
        host: '127.0.0.1',
        port: state ? state.ctlPort : CTL_PORT,
        path: pathname,
        method,
        headers: Object.assign(
          { 'content-type': 'application/json' },
          state ? { 'x-cdp-token': state.token } : {}
        )
      },
      (res) => {
        const chunks = [];
        res.on('data', (c) => chunks.push(c));
        res.on('end', () => {
          let parsed = null;
          try {
            parsed = JSON.parse(Buffer.concat(chunks).toString('utf8'));
          } catch (e) {}
          if (res.statusCode >= 400 && !parsed) {
            reject(new Error(`daemon HTTP ${res.statusCode}`));
            return;
          }
          resolve(parsed);
        });
      }
    );
    req.setTimeout(timeoutMs, () => req.destroy(new Error('daemon request timeout')));
    req.on('error', reject);
    if (body) req.write(JSON.stringify(body));
    req.end();
  });
}

function isConnRefused(err) {
  return err && (err.code === 'ECONNREFUSED' || /ECONNREFUSED|socket hang up/i.test(err.message || ''));
}

function spawnDaemon() {
  log(`spawning daemon via ${process.execPath}`);
  const child = spawn(process.execPath, [__filename, '__daemon__'], {
    detached: true,
    stdio: 'ignore',
    windowsHide: true,
    env: process.env
  });
  child.unref();
}

async function waitForHealth(timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const health = await ctlRequest('GET', '/health', null, 1500);
      if (health && health.ok) return health;
    } catch (e) {}
    await new Promise((r) => setTimeout(r, 250));
  }
  return null;
}

async function sendViaDaemon(payload) {
  // 慢命令（尤其 eval 首次触发 CDP 连接）要有人告诉你为什么在等
  const hint = setTimeout(() => {
    process.stderr.write(
      '[chrome-cdp] 仍在等待……若 Chrome 弹出「要允许远程调试吗？ / Allow remote debugging?」，' +
        '本次连接点一次「允许」即可；连接存续期间后续命令共用它。\n'
    );
  }, 3000);
  try {
    try {
      return await ctlRequest('POST', '/cmd', payload, CLIENT_TIMEOUT_MS);
    } catch (err) {
      if (!isConnRefused(err)) throw err;
    }
    spawnDaemon();
    const health = await waitForHealth(8000);
    if (!health) throw new Error('守护进程未能启动，请看日志：' + LOG_FILE);
    return await ctlRequest('POST', '/cmd', payload, CLIENT_TIMEOUT_MS);
  } finally {
    clearTimeout(hint);
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

async function main() {
  const argv = process.argv.slice(2);
  const noDaemon = argv[0] === '--no-daemon';
  const forceCdp = argv[0] === '--cdp';
  const rest = noDaemon || forceCdp ? argv.slice(1) : argv;
  const command = rest[0] || 'list';
  const query = rest[1];
  const expr = rest.slice(2).join(' ');

  if (command === '__daemon__') {
    runDaemon();
    return;
  }

  if (command === 'status') {
    try {
      const health = await ctlRequest('GET', '/health', null, 2500);
      console.log(JSON.stringify(health, null, 2));
    } catch (err) {
      console.log(JSON.stringify({ ok: false, running: false, state: readState(), error: err.message }, null, 2));
    }
    return;
  }

  if (command === 'stop') {
    try {
      const res = await ctlRequest('POST', '/stop', {}, 2500);
      console.log(JSON.stringify(res));
    } catch (err) {
      console.log(JSON.stringify({ ok: false, running: false, error: err.message }));
    }
    return;
  }

  const payload = { action: command, ...(forceCdp ? { transport: 'cdp' } : {}) };
  if (command === 'open') {
    payload.url = query;
  } else if (command === 'navigate') {
    payload.query = query;
    payload.url = expr;
  } else if (command === 'click') {
    payload.query = query;
    payload.selector = expr;
    payload.expr = expr; // CDP 兜底需要同一份表达式
  } else if (command === 'eval') {
    payload.query = query;
    payload.expr = expr;
  } else {
    payload.query = query;
  }

  if (noDaemon) {
    const conn = await connectCdp();
    try {
      outputResult(command, await cdpExecute(conn, command, payload));
    } finally {
      conn.close();
    }
    return;
  }

  const res = await sendViaDaemon(payload);
  if (!res) throw new Error('守护进程无响应');
  if (!res.ok) {
    throw new Error(`${res.error}${res.hint ? `\n[提示] ${res.hint}` : ''}`);
  }
  if (res.via === 'cdp') {
    const reason = res.fallbackReason === 'requested' ? '按要求使用 CDP'
      : res.fallbackReason === 'extension_eval_unavailable' ? '扩展在线，脚本执行能力不可用' : '扩展离线';
    process.stderr.write(`[chrome-cdp] 本次复用 CDP（${reason}）；连接重建时可能需要一次 Allow。\n`);
  }
  outputResult(command, res.data);
}

module.exports = { createWsServer, connectCdp, cdpExecute, callExtension, isEvalCspError };

if (require.main === module) main().catch((err) => {
  console.error('Browser Control Error:', err.message || err);
  console.error(
    '\n[Tip] 两条通道：\n' +
      '  1) 扩展：chrome://extensions 启用 "Antigravity Browser Bridge"（常驻、零弹窗）\n' +
      '  2) 直连：Chrome → chrome://inspect/#remote-debugging → Enable remote debugging\n' +
      `日志：${LOG_FILE}`
  );
  process.exit(1);
});
