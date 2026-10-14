"use strict";
// Offline contract test for extensions/browser-bridge/worker.js.
//
// Run: node tests/browser_bridge_worker_contract.cjs
//
// It loads the real service worker into a fresh V8 context with a stubbed
// `chrome` API, so the routing rules that decide "which frame owns the focused
// control" are pinned without a browser. Those rules are the part that silently
// breaks:
//
//   - Document.hasFocus() is true for a document when it OR any element inside it
//     has focus, so focusing a control in a nested frame makes every ancestor
//     document report focus as well. Treating that as a conflict (the first
//     version of this worker did) turns every page with an iframe - embedded
//     editors, third-party login and payment widgets - into "no answer", which
//     the desktop side then fails closed on, dropping the dictation out of the
//     shared recognition history.
//   - A document whose activeElement is a frame owner only passed focus through,
//     so exactly one non-delegating frame must remain.
//
// This test is intentionally NOT part of build.bat: the product build has no Node
// dependency. Run it whenever extensions/browser-bridge changes.

const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

// An explicit path may be passed (node <this> <worker.js>) so a pre-fix copy can be
// run through the same assertions; that is how the iframe regression was proven
// to be caught rather than assumed.
const WORKER = process.argv[2]
  ? path.resolve(process.argv[2])
  : path.join(__dirname, "..", "extensions", "browser-bridge", "worker.js");
const EXTENSION_ID = "cjmhlnmnolcbkjpfdbnpejfnigacbpai";

let failures = 0;
function expect(condition, description) {
  if (condition) return;
  console.error("FAIL: " + description);
  failures++;
}

function makeEvent() {
  const listeners = [];
  return {
    addListener: (fn) => listeners.push(fn),
    listeners,
    fire: (...args) => listeners.forEach((fn) => fn(...args)),
  };
}

// A harness that loads the worker and lets the test drive it like the browser and
// the desktop side would.
function loadWorker(options) {
  const fixtures = options || {};
  const port = { sent: [], onMessage: makeEvent(), onDisconnect: makeEvent() };
  port.postMessage = (message) => port.sent.push(message);

  const windows = {
    getAll: async () => fixtures.windows || [],
    get: async (id) => (fixtures.windowById || {})[id],
    getLastFocused: async () => fixtures.lastFocused || {id: -1},
    onFocusChanged: makeEvent(),
    onCreated: makeEvent(),
    onRemoved: makeEvent(),
    onBoundsChanged: makeEvent(),
  };
  let connectionCount = 0;
  const alarms = [];
  const chrome = {
    runtime: {
      id: EXTENSION_ID,
      connectNative: (name) => {
        connectionCount++;
        chrome.__hostName = name;
        return port;
      },
      onInstalled: makeEvent(),
      onStartup: makeEvent(),
    },
    action: { onClicked: makeEvent() },
    alarms: { create: (name, args) => alarms.push({name, ...args}), onAlarm: makeEvent() },
    windows,
    tabs: {getZoom: async () => fixtures.zoom ?? 1},
    scripting: { executeScript: async ({ target, args }) => fixtures.inject(target, args) },
  };

  // The worker logs its connection/query trace for humans; the assertions below are
  // the contract, so keep that trace out of the test output.
  const quiet = { log() {}, warn() {}, error() {} };
  const context = vm.createContext({ chrome, setTimeout, clearTimeout, console: quiet,
    devicePixelRatio: fixtures.dpr ?? 1,
    screen: {availLeft: 0, availTop: 0} });
  vm.runInContext(fs.readFileSync(WORKER, "utf8"), context, { filename: WORKER });
  // `context.readFocusedField` is the injected page function; it is reachable here
  // because a top-level function declaration in the worker script becomes a global.
  return { port, chrome, windows, context, alarms, connections: () => connectionCount };
}

async function testReconnectOncePromptlyThenBackOff() {
  const harness = loadWorker({});
  expect(harness.connections() === 1, "initial native connection opens once");
  harness.port.onDisconnect.fire();
  await new Promise((r) => setTimeout(r, 300));
  expect(harness.connections() === 2, "desktop disconnect retries within 300 ms");
  harness.context.disconnect();
  await new Promise((r) => setTimeout(r, 300));
  expect(harness.connections() === 2, "repeated failed connection does not spin");
  expect(harness.alarms.at(-1)?.delayInMinutes === 1, "repeated failure retains alarm backoff");
  harness.chrome.alarms.onAlarm.fire({name: "voxtype-bridge-reconnect"});
  harness.port.onMessage.fire({version: 1, type: "ack"});
  harness.context.disconnect();
  await new Promise((r) => setTimeout(r, 300));
  expect(harness.connections() === 4, "confirmed desktop traffic rearms the single fast reconnect");
}

// A synthetic page element for driving readFocusedField directly.
function element(tagName, options) {
  const o = options || {};
  return {
    tagName,
    shadowRoot: o.shadowRoot || null,
    isContentEditable: o.isContentEditable === true,
    value: o.value,
    getAttribute: (name) => (name === "type" ? o.type || null : null),
  };
}

async function waitForResult(port, id, timeoutMs = 3000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const found = port.sent.find((m) => m.type === "result" && m.id === id);
    if (found) return found;
    await new Promise((r) => setTimeout(r, 5));
  }
  return null;
}

async function query(harness, message) {
  harness.port.onMessage.fire({ version: 1, ...message });
  return waitForResult(harness.port, message.id);
}

const FIELD_TARGET = { tabId: 11 };
const ACTIVE_TAB = { id: 11, active: true, url: "https://example.test/" };
const WINDOW_BY_ID = { 41: { id: 41, left: 0, top: 0, width: 1600, height: 900, tabs: [ACTIVE_TAB] } };

// A frame result as readFocusedField() would produce it.
function frame(frameId, { focus, delegated = false, password = false, textInput = false, value = null }) {
  return { frameId, documentId: "doc" + frameId,
    result: { focus, delegated, password, textInput, value, dpr: 1, screenLeft: 0, screenTop: 0 } };
}

async function testForcedScaleWithPageZoom() {
  const top = frame(0, {focus: true, textInput: true, value: "test"});
  top.result.dpr = 3.125;
  const harness = loadWorker({windowById: WINDOW_BY_ID, zoom: 2.5, inject: () => [top]});
  const result = await query(harness, {type: "query", id: 20, need: "field", windowId: 41, valueChars: 400});
  expect(result?.ok === true && result.scaleMicros === 1250000,
         "250% page zoom is removed from DPR to recover the forced 125% browser UI scale");
  expect(result?.windowWidth === 1600 && result.windowHeight === 900,
         "each answer carries current browser window geometry");
}

async function testUnfocusedFieldIsNotRead() {
  const harness = loadWorker({});
  const input = element("INPUT", {value: "private"});
  Object.defineProperty(input, "value", {get() { throw new Error("unfocused field read"); }});
  harness.context.document = {activeElement: input, hasFocus: () => false};
  expect(harness.context.readFocusedField(400).value === null,
         "allFrames injection does not read values in unfocused documents");
}

async function testLastFocusSurvivesLossAndReconnect() {
  const harness = loadWorker({
    windows: [{id: 41, type: "normal", focused: true, left: 0, top: 0, width: 1600, height: 900}],
  });
  await new Promise((r) => setTimeout(r, 20));
  harness.windows.getAll = async () => [
    {id: 41, type: "normal", focused: false, left: 0, top: 0, width: 1600, height: 900}];
  harness.windows.onFocusChanged.fire(-1);
  await new Promise((r) => setTimeout(r, 20));
  const hello = harness.port.sent.filter((m) => m.type === "hello").at(-1);
  expect(hello?.windows[0].lastFocused === true, "initial focused window survives focus loss");
  const reconnected = loadWorker({
    windows: [{id: 41, type: "normal", focused: false, left: 0, top: 0, width: 1600, height: 900}],
    lastFocused: {id: 41},
  });
  await new Promise((r) => setTimeout(r, 20));
  expect(reconnected.port.sent.find((m) => m.type === "hello")?.windows[0].lastFocused === true,
         "reconnect while browser is unfocused recovers the last focused window");
}

async function testHelloOnConnect() {
  const harness = loadWorker({
    windows: [
      { id: 41, type: "normal", focused: true, left: 0, top: 0, width: 1600, height: 900 },
      { id: 42, type: "normal", focused: false, left: 1600, top: 0, width: 1600, height: 900 },
    ],
  });
  expect(harness.chrome.__hostName === "com.voxtype.browser_bridge",
         "the worker connects to the pinned native messaging host");

  const deadline = Date.now() + 1000;
  while (Date.now() < deadline && !harness.port.sent.some((m) => m.type === "hello")) {
    await new Promise((r) => setTimeout(r, 5));
  }
  const hello = harness.port.sent.find((m) => m.type === "hello");
  expect(!!hello, "a hello frame is sent on connect");
  if (!hello) return;
  expect(hello.version === 1, "hello carries the protocol version");
  expect(hello.ext === EXTENSION_ID, "hello carries the extension id the host verifies");
  const flagged = (hello.windows || []).filter((w) => w.lastFocused);
  expect(flagged.length === 1, "exactly one window is flagged lastFocused");
  expect(flagged.length === 1 && flagged[0].id === 41, "the focused window is the flagged one");
  expect(flagged[0]?.focused === true, "actual focus is distinct from retained last-focus across profiles");
}

async function testStaleHelloCannotOverwriteNewFocus() {
  const harness = loadWorker({});
  await new Promise((r) => setTimeout(r, 20));
  let resolveOld;
  harness.windows.getAll = () => new Promise((r) => { resolveOld = r; });
  const old = harness.context.sendHello();
  harness.windows.getAll = async () => [
    {id: 42, type: "normal", focused: true, left: 0, top: 0, width: 1600, height: 900}];
  await harness.context.sendHello();
  const before = harness.port.sent.length;
  resolveOld([{id: 41, type: "normal", focused: true, left: 0, top: 0, width: 1600, height: 900}]);
  await old;
  expect(harness.port.sent.length === before, "a delayed old hello is discarded");
  expect(harness.port.sent.filter((m) => m.type === "hello").at(-1)?.windows[0].id === 42,
         "the newest focused window remains authoritative");
}

async function testReplyCannotCrossReconnection() {
  let finishInjection;
  const harness = loadWorker({windowById: WINDOW_BY_ID,
    inject: () => new Promise((resolve) => { finishInjection = resolve; })});
  const pending = harness.context.handleQuery({id: 55, need: "field", windowId: 41, valueChars: 400});
  while (!finishInjection) await new Promise((r) => setTimeout(r, 5));
  const replacement = {sent: [], postMessage(m) { this.sent.push(m); }};
  harness.context.replacementPort = replacement;
  vm.runInContext("port = replacementPort", harness.context);
  finishInjection([frame(0, {focus: true, textInput: true, value: "old context"})]);
  await pending;
  expect(!replacement.sent.some((m) => m.type === "result"),
         "an old query cannot answer a new desktop connection with a reused request ID");
}

async function testSingleFrameFocus() {
  const harness = loadWorker({
    windowById: WINDOW_BY_ID,
    inject: () => [frame(0, { focus: true, textInput: true, value: "hello" })],
  });
  const result = await query(harness, { type: "query", id: 1, need: "field", windowId: 41, valueChars: 400 });
  expect(!!result && result.ok === true, "a single focused frame answers");
  expect(!!result && result.value === "hello", "the field value is returned");
  expect(!!result && result.textInput === true, "the text input flag is returned");
}

async function testNestedFrameFocus() {
  // The case that used to fail: the top document reports focus because the focus
  // is inside its iframe, and its activeElement is the frame owner.
  const harness = loadWorker({
    windowById: WINDOW_BY_ID,
    inject: () => [
      frame(0, { focus: true, delegated: true }),
      frame(7, { focus: true, textInput: true, value: "world" }),
    ],
  });
  const result = await query(harness, { type: "query", id: 2, need: "field", windowId: 41, valueChars: 400 });
  expect(!!result, "a nested-frame focus still answers");
  expect(!!result && result.ok === true, "the pass-through document does not turn it into a conflict");
  expect(!!result && result.value === "world", "the value comes from the frame that owns the control");
  expect(!!result && result.frameId === 7, "the reported frame is the owning frame");
}

async function testNestedFramePassword() {
  const harness = loadWorker({
    windowById: WINDOW_BY_ID,
    inject: () => [
      frame(0, { focus: true, delegated: true }),
      frame(5, { focus: true, password: true, textInput: true, value: null }),
    ],
  });
  const result = await query(harness, { type: "query", id: 3, need: "field", windowId: 41, valueChars: 400 });
  expect(!!result && result.ok === true, "a password field inside an iframe answers");
  expect(!!result && result.password === true, "the password flag survives the frame selection");
  expect(!!result && result.value === "", "a password answer never carries a value");
}

async function testUnreachableFocusedFrame() {
  // Only the pass-through document answered: the control sits in a frame the
  // extension cannot inject into. A miss is the fail-closed answer.
  const harness = loadWorker({
    windowById: WINDOW_BY_ID,
    inject: () => [frame(0, { focus: true, delegated: true })],
  });
  const result = await query(harness, { type: "query", id: 4, need: "probe", windowId: 41, valueChars: 0 });
  expect(!!result && result.ok === false, "an unreachable focused frame is not answered");
  expect(!!result && result.reason === "focused_frame_unreachable", "and it reports its own reason");
}

async function testAmbiguousOwningFrames() {
  const harness = loadWorker({
    windowById: WINDOW_BY_ID,
    inject: () => [
      frame(0, { focus: true, textInput: true, value: "a" }),
      frame(9, { focus: true, textInput: true, value: "b" }),
    ],
  });
  const result = await query(harness, { type: "query", id: 5, need: "probe", windowId: 41, valueChars: 0 });
  expect(!!result && result.ok === false, "two owning frames are not guessed between");
  expect(!!result && result.reason === "focus_ambiguous", "and they report ambiguity");
}

async function testNoFocusedFrame() {
  const harness = loadWorker({
    windowById: WINDOW_BY_ID,
    inject: () => [frame(0, { focus: false })],
  });
  const result = await query(harness, { type: "query", id: 6, need: "probe", windowId: 41, valueChars: 0 });
  expect(!!result && result.ok === false && result.reason === "no_focus", "a page without focus misses");
}

async function testRestrictedPage() {
  const harness = loadWorker({
    windowById: { 41: { id: 41, tabs: [{ id: 11, active: true, url: "chrome://settings/" }] } },
    inject: () => {
      throw new Error("must not be injected");
    },
  });
  const result = await query(harness, { type: "query", id: 7, need: "probe", windowId: 41, valueChars: 0 });
  expect(!!result && result.ok === false && result.reason === "restricted_page",
         "an internal page is refused without injection");
}

async function testOpenShadowRootIsDescended() {
  const harness = loadWorker({});
  // document.activeElement retargets to the host, so the only way to see the real
  // control is to walk into the open shadow root.
  const inner = element("INPUT", { type: "password", value: "hunter2" });
  const host = element("SL-INPUT", { shadowRoot: { activeElement: inner } });
  harness.context.document = { activeElement: host, hasFocus: () => true };
  const result = harness.context.readFocusedField(400);
  expect(result.password === true, "a password control inside an open shadow root is identified");
  expect(result.value === null, "and a password value is still never read");
  expect(result.textInput === true, "and the control is reported as editable");
}

async function testNestedOpenShadowRootsAreDescended() {
  const harness = loadWorker({});
  const input = element("TEXTAREA", { value: "hello" });
  const middle = element("INNER-BOX", { shadowRoot: { activeElement: input } });
  const host = element("OUTER-BOX", { shadowRoot: { activeElement: middle } });
  harness.context.document = { activeElement: host, hasFocus: () => true };
  const result = harness.context.readFocusedField(400);
  expect(result.textInput === true, "nested open shadow roots are walked to the control");
  expect(result.value === "hello", "the value comes from the innermost control");
  expect(result.delegated === false, "and the frame is the owner, not a pass-through");
}

async function testClosedShadowRootCannotBeSeenThrough() {
  const harness = loadWorker({});
  // A closed shadow root is invisible from the outside: shadowRoot is null, so the
  // host looks like a plain non-editable element. This is exactly why the desktop
  // side must refuse Safe without a positively identified editable control.
  const host = element("SL-PASSWORD", { type: "password", value: "hunter2" });
  harness.context.document = { activeElement: host, hasFocus: () => true };
  const result = harness.context.readFocusedField(400);
  expect(result.password === false, "a closed shadow root cannot be inspected");
  expect(result.textInput === false, "so it is reported as an unidentified element");
  expect(result.value === null, "and nothing is read from the host");

  // The same case through the worker: the answer must carry textInput:false so the
  // fail-closed mapping has something to reject.
  const viaWorker = loadWorker({
    windowById: WINDOW_BY_ID,
    inject: () => [{ frameId: 0, documentId: "top", result }],
  });
  const answer = await query(viaWorker, { type: "query", id: 8, need: "probe", windowId: 41, valueChars: 0 });
  expect(!!answer && answer.ok === true, "the message itself is delivered");
  expect(!!answer && answer.textInput === false, "and it advertises the missing identification");
}

async function testFrameOwnerInsideShadowRoot() {
  const harness = loadWorker({});
  // Focus that went through a shadow root into a nested document: the frame owner is
  // the real answer, and the document holding it is only a pass-through.
  const frameOwner = element("IFRAME");
  const host = element("MY-EMBED", { shadowRoot: { activeElement: frameOwner } });
  harness.context.document = { activeElement: host, hasFocus: () => true };
  const result = harness.context.readFocusedField(0);
  expect(result.delegated === true, "a frame owner behind a shadow boundary is still a pass-through");
}

async function testFocusChangeSendsImmediately() {
  const harness = loadWorker({
    windows: [{ id: 41, type: "normal", focused: true, left: 0, top: 0, width: 1600, height: 900 }],
  });
  const deadline = Date.now() + 1000;
  while (Date.now() < deadline && !harness.port.sent.some((m) => m.type === "hello")) {
    await new Promise((r) => setTimeout(r, 5));
  }
  const before = harness.port.sent.filter((m) => m.type === "hello").length;
  harness.windows.onFocusChanged.fire(42);
  // The trailing debounce is 200 ms; a snapshot must go out well before it.
  await new Promise((r) => setTimeout(r, 60));
  const after = harness.port.sent.filter((m) => m.type === "hello").length;
  expect(after > before, "a window focus change sends a hello immediately, not after the debounce");
}

(async () => {
  await testHelloOnConnect();
  await testSingleFrameFocus();
  await testNestedFrameFocus();
  await testNestedFramePassword();
  await testUnreachableFocusedFrame();
  await testAmbiguousOwningFrames();
  await testNoFocusedFrame();
  await testRestrictedPage();
  await testOpenShadowRootIsDescended();
  await testNestedOpenShadowRootsAreDescended();
  await testClosedShadowRootCannotBeSeenThrough();
  await testFrameOwnerInsideShadowRoot();
  await testFocusChangeSendsImmediately();
  await testForcedScaleWithPageZoom();
  await testUnfocusedFieldIsNotRead();
  await testLastFocusSurvivesLossAndReconnect();
  await testReconnectOncePromptlyThenBackOff();
  await testStaleHelloCannotOverwriteNewFocus();
  await testReplyCannotCrossReconnection();

  if (failures === 0) {
    console.log("browser_bridge_worker_contract: all checks passed");
    process.exit(0);
  }
  console.error("browser_bridge_worker_contract: " + failures + " check(s) failed");
  process.exit(1);
})();
