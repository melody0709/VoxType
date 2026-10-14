'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const http = require('node:http');
const { createWsServer, cdpExecute, callExtension, isEvalCspError } = require('./cdp.js');
const extensionFile = process.env.BRIDGE_SOURCE || 'D:/GITHUB_melody0709/PC_modify/Chrome extension/antigravity-browser-bridge/background.js';
const daemonSource = fs.readFileSync(__dirname + '/cdp.js', 'utf8');
const extensionSource = fs.readFileSync(extensionFile, 'utf8');

function extensionContext(overrides = {}) {
  const noop = () => {};
  const chrome = {
    runtime: { id: 'test', getManifest: () => ({ version: '1.1.0' }), onStartup: { addListener: noop }, onInstalled: { addListener: noop }, onMessage: { addListener: noop } },
    alarms: { create: noop, onAlarm: { addListener: noop } },
    action: { setBadgeText: async () => {}, setBadgeBackgroundColor: async () => {}, setTitle: async () => {} },
    tabs: { query: async () => [{ id: 10, url: 'https://test.invalid/', title: 'test' }] },
    ...overrides
  };
  const code = extensionSource.replace(/^startBridge\(\);$/m, '');
  const context = { chrome, console, WebSocket: { OPEN: 1, CONNECTING: 0 }, setTimeout, clearTimeout, AbortSignal };
  vm.createContext(context); vm.runInContext(code, context);
  return context;
}

test('parallel first calls share one CDP connection, and failure permits a retry', async () => {
  const match = daemonSource.match(/  async function getCdp\(\) \{[\s\S]*?\n  \}/);
  let calls = 0, fail = false;
  const context = { cdpConn: null, cdpConnecting: null, connectCdp: async () => { calls++; await new Promise(r => setImmediate(r)); if (fail) throw Error('offline'); return { closed: false }; } };
  vm.createContext(context); vm.runInContext(match[0], context);
  const results = await Promise.all(Array.from({ length: 10 }, () => context.getCdp()));
  assert.equal(calls, 1); assert.ok(results.every(r => r === results[0]));
  context.cdpConn.closed = true; fail = true;
  await assert.rejects(context.getCdp(), /offline/);
  fail = false; await context.getCdp(); assert.equal(calls, 3);
});

function fakeCdp(evaluation) {
  const methods = [];
  return { methods, send: async (method) => { methods.push(method);
    if (method === 'Target.getTargets') return { targetInfos: [{ type: 'page', targetId: 'a', url: 'https://test.invalid/', title: 'test' }] };
    if (method === 'Target.attachToTarget') return { sessionId: 'session' };
    if (method === 'Runtime.evaluate') return evaluation;
    return {};
  } };
}

test('CDP script errors fail and detach the target', async () => {
  const conn = fakeCdp({ exceptionDetails: { exception: { description: 'Error: intentional' } } });
  await assert.rejects(cdpExecute(conn, 'eval', { query: 'test.invalid', expr: 'throw Error()' }), /intentional/);
  assert.equal(conn.methods.at(-1), 'Target.detachFromTarget');
});

test('CDP preserves false, zero and undefined, and awaits expressions', async () => {
  for (const value of [false, 0, '', null]) {
    assert.equal(await cdpExecute(fakeCdp({ result: { value } }), 'eval', { query: 'test.invalid', expr: 'x' }), value);
  }
  assert.equal(await cdpExecute(fakeCdp({ result: { type: 'undefined' } }), 'eval', { query: 'test.invalid', expr: 'x' }), null);
});

test('extension command timeout drops partial chunks', async () => {
  const hub = { client: { send() {} }, seq: 0, pending: new Map(), chunks: new Map() };
  const result = callExtension(hub, 'html', {}, 5); hub.chunks.set(1, { incomplete: true });
  await assert.rejects(result, /timeout/); assert.equal(hub.pending.size, 0); assert.equal(hub.chunks.size, 0);
});

test('page errors never cause a retry through another execution world', () => {
  assert.equal(isEvalCspError('eval', Object.assign(Error('unsafe-eval'), { code: 'script_error' })), false);
  assert.equal(isEvalCspError('eval', { code: 'eval_unavailable' }), true);
  assert.equal(isEvalCspError('click', Error('unsafe-eval')), false);
});

test('userScripts awaits promises, handles errors and preserves __error as data', async () => {
  const c = extensionContext({ userScripts: { configureWorld: async () => {}, execute: async ({ js }) => [{ result: await vm.runInNewContext(js[0].code) }] } });
  for (const expr of ['Promise.resolve(42)', 'undefined', '({__error:"ordinary data"})']) {
    const out = await c.handleAction({ action: 'eval', query: 'test.invalid', expr });
    assert.equal(out.ok, true);
    if (expr.includes('42')) assert.equal(out.data, 42);
    if (expr === 'undefined') assert.equal(out.data, null);
    if (expr.includes('__error')) assert.equal(out.data.__error, 'ordinary data');
  }
  await assert.rejects(c.handleAction({ action: 'eval', query: 'test.invalid', expr: 'Promise.reject(Error("intentional"))' }), e => e.code === 'script_error' && /intentional/.test(e.message));
});

test('unavailable userScripts reports capability failure', async () => {
  const c = extensionContext();
  await assert.rejects(c.handleAction({ action: 'eval', query: 'test.invalid', expr: '1' }), e => e.code === 'eval_unavailable');
});

test('ambiguous queries and missing tab IDs never select another tab', async () => {
  const c = extensionContext({ tabs: { query: async () => [{ id: 1, url: 'https://same.invalid/a' }, { id: 2, url: 'https://same.invalid/b' }] } });
  await assert.rejects(c.findTargetTab('same.invalid'), /Ambiguous/);
  await assert.rejects(c.findTargetTab('', 99), /No tab/);
  assert.equal((await c.findTargetTab('2')).id, 2);
});

test('offline health probe does not create a WebSocket, concurrent probes are shared', async () => {
  const c = extensionContext(); let probes = 0, sockets = 0;
  c.fetch = async () => { probes++; await new Promise(r => setImmediate(r)); throw Error('offline'); };
  c.WebSocket = class { static OPEN = 1; static CONNECTING = 0; constructor() { sockets++; } };
  c.setTimeout = () => 1;
  await Promise.all([c.connectBridge(), c.connectBridge()]);
  assert.equal(probes, 1); assert.equal(sockets, 0);
});

test('bridge rejects webpage WebSocket origins', async () => {
  const server = createWsServer(() => { throw Error('untrusted origin accepted'); });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  try {
    await new Promise((resolve, reject) => {
      const req = http.request({ host: '127.0.0.1', port: server.address().port, headers: { Connection: 'Upgrade', Upgrade: 'websocket', Origin: 'https://evil.invalid', 'Sec-WebSocket-Key': 'dGhlIHNhbXBsZSBub25jZQ==', 'Sec-WebSocket-Version': '13' } });
      req.on('upgrade', () => reject(Error('unexpected upgrade'))); req.on('error', () => resolve()); req.end();
    });
  } finally { await new Promise(r => server.close(r)); }
});
