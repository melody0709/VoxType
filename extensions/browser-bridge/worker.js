"use strict";
// VoxType Browser Bridge - Manifest V3 service worker.
//
// WHY THIS EXISTS
//   Chromium keeps a process-wide accessibility mode once any platform consumer
//   (UIA/MSAA) asks for it, and releasing our COM objects does not close it: the
//   browser stays slower until it is restarted. VoxType used to read the focused
//   field through UIA on every recording. This worker answers the same question -
//   "is the focused field a password control, and what is in it" - straight from
//   the DOM, which never touches the accessibility tree.
//
// DESIGN RULES (keep them when editing)
//   1. Passive until asked. No content script is registered anywhere; the target
//      page is only touched by an on-demand chrome.scripting.executeScript. No
//      observers, no timers, no polling while nobody is dictating.
//   2. A password value must never leave the page. readFocusedField() is written
//      so the value branch is unreachable for a password control.
//   3. Never guess. An ambiguous or missing focused frame is reported as a miss;
//      the desktop side treats every miss as "unknown", which stays fail-closed
//      for the shared recognition history.
//   4. The idle cost is one WebSocket and a 20-second heartbeat. It is measured
//      and disclosed, not claimed to be zero.

const HOST_NAME = "com.voxtype.browser_bridge";
const PROTOCOL_VERSION = 1;
const RECONNECT_ALARM = "voxtype-bridge-reconnect";
const RECONNECT_MINUTES = 0.5;
const RECONNECT_MAX_MINUTES = 5;
const MAX_VALUE_CHARS = 400; // matches browser_bridge::kMaxValueCharacters
const HELLO_DEBOUNCE_MS = 200;
const WS_URL = "ws://127.0.0.1:19226/voxtype/browser/v2";
const WS_SUBPROTOCOL = "voxtype-context-v2";
const PAIRING_KEY = "voxtypePairingV2";
const MAX_MESSAGE_BYTES = 65536;

// Reasons the desktop side recognises; anything else becomes "query_failed".
const KNOWN_REASONS = [
  "restricted_page",
  "no_focus",
  "focus_ambiguous",
  "focused_frame_unreachable",
  "no_target",
  "query_failed",
];

let port = null;
let reconnectMinutes = RECONNECT_MINUTES;
let lastFocusedWindowId = -1;
let helloTimer = 0;
let helloRevision = 0;
let socket = null;
let connectPending = null;
let pairingPending = null;
let keepaliveTimer = 0;
let connectionStatus = {state: "unpaired", reason: ""};

function showStatus(state, reason = "") {
  connectionStatus = {state, reason};
  void chrome.action.setBadgeText({text: state === "connected" ? "ON" : state === "unpaired" ? "PAIR" : state === "error" ? "ERR" : "WAIT"});
  void chrome.action.setTitle({title: "VoxType: " + state + (reason ? " (" + reason + ")" : "") + ". Click to pair again."});
}

function validHex(value, bytes) {
  return typeof value === "string" && value.length === bytes * 2 && /^[0-9a-f]+$/.test(value);
}

function randomHex(bytes) {
  return Array.from(crypto.getRandomValues(new Uint8Array(bytes)), (b) => b.toString(16).padStart(2, "0")).join("");
}

function hexBytes(value) {
  return Uint8Array.from(value.match(/../g), (b) => parseInt(b, 16));
}

async function pairingKey(secret) {
  return crypto.subtle.importKey("raw", hexBytes(secret), {name: "HMAC", hash: "SHA-256"}, false, ["sign", "verify"]);
}

async function signProof(key, message) {
  const bytes = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(message));
  return Array.from(new Uint8Array(bytes), (b) => b.toString(16).padStart(2, "0")).join("");
}

async function loadPairing() {
  await chrome.storage.local.setAccessLevel({accessLevel: "TRUSTED_CONTEXTS"});
  return (await chrome.storage.local.get(PAIRING_KEY))[PAIRING_KEY];
}

function validPairing(value) {
  return value?.wsVersion === 2 && value?.port === 19226 && validHex(value.instance, 16) &&
    validHex(value.profile, 16) && validHex(value.secret, 32);
}

function post(message, expectedPort = port) {
  if (!expectedPort || port !== expectedPort) return false;
  try {
    port.postMessage({version: PROTOCOL_VERSION, ...message});
    return true;
  } catch {
    return false;
  }
}

// The desktop side matches these bounds against the window it anchored when
// recording started, so exactly one window may be flagged lastFocused: a browser
// that is genuinely focused wins, otherwise the window that was focused last.
function describeWindows(windows) {
  const normal = windows.filter((w) => w.type === "normal");
  const anyFocused = normal.some((w) => w.focused === true);
  return normal
    .map((w) => ({
      id: w.id,
      left: Math.round(w.left || 0),
      top: Math.round(w.top || 0),
      width: Math.round(w.width || 0),
      height: Math.round(w.height || 0),
      lastFocused: anyFocused ? w.focused === true : w.id === lastFocusedWindowId,
      focused: w.focused === true,
    }))
    .filter((w) => w.width > 0 && w.height > 0);
}

async function sendHello() {
  const revision = ++helloRevision;
  let windows = [];
  try {
    const all = await chrome.windows.getAll();
    if (revision !== helloRevision) return;
    const focused = all.find((w) => w.type === "normal" && w.focused === true);
    if (focused) lastFocusedWindowId = focused.id;
    else if (lastFocusedWindowId < 0) {
      const last = await chrome.windows.getLastFocused();
      if (revision !== helloRevision) return;
      lastFocusedWindowId = last.id;
    }
    windows = describeWindows(all);
  } catch (error) {
    console.warn("[voxtype] windows.getAll failed:", String((error && error.message) || error));
    windows = [];
  }
  if (revision !== helloRevision) return;
  const focused = windows.filter((w) => w.lastFocused).map((w) => w.id);
  const sent = post({type: "hello", ext: chrome.runtime.id, windows});
  console.log("[voxtype] hello: windows=" + windows.length + " lastFocused=" +
              (focused.length ? focused.join(",") : "none") + " sent=" + sent);
}

function scheduleHello() {
  if (helloTimer) clearTimeout(helloTimer);
  helloTimer = setTimeout(() => {
    helloTimer = 0;
    void sendHello();
  }, HELLO_DEBOUNCE_MS);
}

// Runs in the page's isolated world. It must stay self-contained: no closures
// over the worker scope, JSON-serialisable result only.
//
// `focus` uses Document.hasFocus(), which is true for a document when it - or any
// element inside it - holds focus. Focusing a control inside a nested frame
// therefore makes EVERY ancestor document report focus as well, so one recording
// can produce several `focus:true` results and the caller has to pick the frame
// that really owns the focused control.
//
// `delegated` is what makes that pick decidable: when focus moves into a nested
// browsing context, the parent document's activeElement becomes the frame owner
// element (IFRAME/FRAME). Such a document is a *pass-through*, never the frame
// holding the focused control.
function readFocusedField(valueChars) {
  const hasFocus = document.hasFocus() === true;
  // document.activeElement is retargeted to the HOST element whenever focus sits
  // behind a shadow boundary, exactly like a frame owner. Returning the host would
  // describe a <my-password-field> wrapper instead of the <input type="password">
  // it contains, so descend through open shadow roots to the control that really
  // holds focus. Depth-bounded against a pathological tree.
  let el = document.activeElement;
  let delegated = false;
  for (let depth = 0; el && depth < 8; ++depth) {
    const hostTag = String(el.tagName || "").toUpperCase();
    if (hostTag === "IFRAME" || hostTag === "FRAME") {
      delegated = true;  // focus went into a nested document, which answers for itself
      break;
    }
    const inner = el.shadowRoot ? el.shadowRoot.activeElement : null;
    // A CLOSED shadow root reports shadowRoot === null, so a host that hides one is
    // indistinguishable here and simply looks like a non-editable element. That is
    // why the desktop side additionally refuses to treat "not an editable control"
    // as Safe: the value below is only ever read for a positively identified
    // control, never for a host we could not see through.
    if (!inner) break;
    el = inner;
  }
  const tag = el ? String(el.tagName || "").toUpperCase() : "";
  const inputType =
    el && tag === "INPUT" ? String(el.getAttribute("type") || "text").toLowerCase() : "";
  const password = tag === "INPUT" && inputType === "password";
  const editable =
    !!el &&
    ((tag === "INPUT" && /^(text|search|url|tel|email|number|password)$/.test(inputType)) ||
      tag === "TEXTAREA" ||
      el.isContentEditable === true);
  let value = null;
  // Structurally guarded: a password control cannot reach the read below, so its
  // value never crosses the boundary into this worker.
  if (hasFocus && editable && !password && valueChars > 0) {
    let text = "";
    if (tag === "INPUT" || tag === "TEXTAREA") text = String(el.value || "");
    else text = String(el.innerText || el.textContent || "");
    value = text.length > valueChars ? text.slice(text.length - valueChars) : text;
  }
  return {focus: hasFocus, delegated, password, textInput: editable, value,
          dpr: devicePixelRatio, screenLeft: screen.availLeft, screenTop: screen.availTop};
}

async function inject(tabId, valueChars) {
  // Frames the extension cannot reach (restricted nested documents) would fail
  // the whole call, so fall back to the main frame instead of reporting a miss.
  try {
    return await chrome.scripting.executeScript({
      target: {tabId, allFrames: true},
      args: [valueChars],
      func: readFocusedField,
    });
  } catch {
    return await chrome.scripting.executeScript({
      target: {tabId},
      args: [valueChars],
      func: readFocusedField,
    });
  }
}

async function handleQuery(message) {
  const queryPort = port;
  const id = message.id;
  const valueChars = message.need === "field"
    ? Math.min(MAX_VALUE_CHARS, Math.max(0, Number(message.valueChars) || 0))
    : 0;
  console.log("[voxtype] query id=" + id + " need=" + message.need +
              " window=" + message.windowId + " valueChars=" + valueChars);
  try {
    if (!Number.isInteger(message.windowId)) throw new Error("no_target");
    const window = await chrome.windows.get(message.windowId, {populate: true});
    const tab = (window.tabs || []).find((t) => t.active === true);
    if (!tab || !/^https?:\/\//i.test(tab.url || "")) throw new Error("restricted_page");
    const zoom = await chrome.tabs.getZoom(tab.id);
    const injections = await inject(tab.id, valueChars);
    const top = (injections || []).find((r) => r.frameId === 0)?.result;
    const scale = top?.dpr / zoom;
    if (!Number.isFinite(scale) || scale < 0.25 || scale > 16 ||
        !Number.isInteger(top.screenLeft) || !Number.isInteger(top.screenTop)) {
      throw new Error("query_failed");
    }
    const focused = (injections || []).filter((r) => r && r.result && r.result.focus === true);
    if (focused.length === 0) throw new Error("no_focus");
    // Focus inside a nested frame makes every ancestor document report focus, so
    // more than one candidate is the normal case on any page with an iframe. A
    // document whose activeElement is a frame owner only passed focus through, so
    // dropping those leaves exactly one frame - the one holding the control.
    const owning = focused.filter((r) => r.result.delegated !== true);
    if (owning.length === 0) {
      // Everything that reported focus is a pass-through, i.e. the focused control
      // sits in a frame we could not reach (closed shadow root, or a scheme the
      // extension cannot inject). A miss keeps the privacy answer fail-closed
      // instead of guessing "not a password" from the parent document.
      throw new Error("focused_frame_unreachable");
    }
    if (owning.length > 1) throw new Error("focus_ambiguous");
    const hit = owning[0];
    const result = hit.result;
    console.log("[voxtype] query id=" + id + " answered: frame=" + hit.frameId +
                " textInput=" + (result.textInput === true) +
                " password=" + (result.password === true) +
                " chars=" + (result.value == null ? 0 : String(result.value).length));
    post({
      type: "result",
      id,
      ok: true,
      password: result.password === true,
      textInput: result.textInput === true,
      focus: true,
      value: result.value == null ? "" : String(result.value),
      windowId: message.windowId,
      tabId: tab.id,
      frameId: Number.isInteger(hit.frameId) ? hit.frameId : 0,
      documentId: String(hit.documentId || ""),
      windowLeft: window.left,
      windowTop: window.top,
      windowWidth: window.width,
      windowHeight: window.height,
      scaleMicros: Math.round(scale * 1000000),
      screenLeft: top.screenLeft,
      screenTop: top.screenTop,
    }, queryPort);
  } catch (error) {
    const known = error && KNOWN_REASONS.includes(error.message);
    const reason = known ? error.message : "query_failed";
    console.warn("[voxtype] query id=" + id + " failed: " + reason +
                 (known ? "" : " (" + String((error && error.message) || error) + ")"));
    post({type: "result", id, ok: false, reason}, queryPort);
  }
}

function disconnect(reason = "") {
  port = null;
  if (keepaliveTimer) { clearInterval(keepaliveTimer); keepaliveTimer = 0; }
  showStatus(reason ? "error" : "disconnected", reason);
  // A desktop restart should reconnect once promptly; repeated failures back off.
  if (reconnectMinutes === RECONNECT_MINUTES) setTimeout(connect, 250);
  chrome.alarms.create(RECONNECT_ALARM, {delayInMinutes: reconnectMinutes});
  reconnectMinutes = Math.min(RECONNECT_MAX_MINUTES, reconnectMinutes * 2);
}

function connect() {
  if (socket || connectPending || pairingPending) return connectPending;
  connectPending = connectWebSocket().catch(() => showStatus("error", "credential_storage_unavailable"))
    .finally(() => { connectPending = null; });
  return connectPending;
}

async function connectWebSocket() {
  const credential = await loadPairing();
  if (!validPairing(credential)) { showStatus("unpaired"); return; }
  const key = await pairingKey(credential.secret);
  if (socket || pairingPending) return;
  const connection = new WebSocket(WS_URL, WS_SUBPROTOCOL);
  socket = connection;
  showStatus("connecting");
  let state = "challenge";
  let failure = "";
  let transcript = "";
  let lastPong = Date.now();
  const authTimer = setTimeout(() => { if (socket === connection && state !== "ready") connection.close(); }, 5000);
  const expectedPort = {postMessage: (message) => {
    if (socket !== connection || state !== "ready" || connection.readyState !== WebSocket.OPEN) throw new Error("disconnected");
    const json = JSON.stringify(message);
    if (json.length > MAX_MESSAGE_BYTES || connection.bufferedAmount > MAX_MESSAGE_BYTES) throw new Error("backpressure");
    connection.send(json);
  }};
  connection.onmessage = (event) => {
    void (async () => {
      if (socket !== connection) return;
      if (typeof event.data !== "string" || event.data.length > MAX_MESSAGE_BYTES) throw new Error("invalid_message");
      const message = JSON.parse(event.data);
      if (state === "challenge") {
        if (message.version !== 2 || message.type !== "challenge" || message.instance !== credential.instance || !validHex(message.nonce, 32)) throw new Error("pair_again");
        transcript = credential.instance + ":" + randomHex(32) + ":" + message.nonce;
        state = "server";
        connection.send(JSON.stringify({version: 2, type: "auth_start", profile: credential.profile, nonce: transcript.split(":")[1]}));
      } else if (state === "server") {
        if (message.version !== 2 || message.type !== "auth_server" || !validHex(message.proof, 32)) throw new Error("authentication_failed");
        state = "verifying";
        const verified = await crypto.subtle.verify("HMAC", key, hexBytes(message.proof), new TextEncoder().encode("server:" + transcript));
        if (socket !== connection) return;
        if (!verified) throw new Error("authentication_failed");
        const proof = await signProof(key, "client:" + transcript);
        if (socket !== connection) return;
        state = "accepted";
        connection.send(JSON.stringify({version: 2, type: "auth_client", proof}));
      } else if (state === "accepted") {
        if (message.version !== 2 || message.type !== "auth_ok") throw new Error("authentication_failed");
        state = "ready";
        clearTimeout(authTimer);
        port = expectedPort;
        reconnectMinutes = RECONNECT_MINUTES;
        showStatus("connected");
        void sendHello();
        keepaliveTimer = setInterval(() => {
          if (socket !== connection || connection.readyState !== WebSocket.OPEN) return;
          if (Date.now() - lastPong > 60000) { connection.close(); return; }
          connection.send(JSON.stringify({version: 2, type: "ping"}));
        }, 20000);
      } else if (state === "ready" && message.version === 2 && message.type === "pong") {
        lastPong = Date.now();
      } else if (state === "ready" && message.version === PROTOCOL_VERSION && message.type === "query" &&
                 Number.isSafeInteger(message.id) && message.id >= 0 && message.id <= 1000000000000 &&
                 ["probe", "field"].includes(message.need)) {
        void handleQuery(message);
      } else throw new Error("invalid_message");
    })().catch((error) => {
      if (socket !== connection) return;
      console.warn("[voxtype] WebSocket rejected: " + error.message);
      failure = ["pair_again", "authentication_failed", "invalid_message"].includes(error.message) ? error.message : "invalid_message";
      connection.close();
    });
  };
  connection.onclose = () => {
    clearTimeout(authTimer);
    if (socket !== connection) return;
    socket = null;
    disconnect(failure);
  };
  connection.onerror = () => { if (socket === connection) connection.close(); };
}

// Native Messaging is only an explicit, bounded pairing bootstrap. No alarm,
// startup event or connection failure may repeatedly launch a host process.
function pair() {
  if (pairingPending) return pairingPending;
  let reconnectAfterPairing = false;
  pairingPending = (async () => {
    await chrome.alarms.clear(RECONNECT_ALARM);
    const old = socket;
    socket = null;
    port = null;
    if (keepaliveTimer) { clearInterval(keepaliveTimer); keepaliveTimer = 0; }
    if (old) old.close();
    const previous = await loadPairing();
    reconnectAfterPairing = validPairing(previous);
    const profile = validHex(previous?.profile, 16) ? previous.profile : randomHex(16);
    const credential = await new Promise((resolve, reject) => {
      const bootstrap = chrome.runtime.connectNative(HOST_NAME);
      let settled = false;
      const finish = (error, value) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        bootstrap.disconnect();
        if (error) reject(error); else resolve(value);
      };
      const timer = setTimeout(() => finish(new Error("pairing_timeout")), 7000);
      bootstrap.onMessage.addListener((message) => {
        if (message?.version !== 1 || message.type !== "paired" || !validPairing(message) || message.profile !== profile) {
          finish(new Error(message?.reason || "incompatible_desktop"));
        } else finish(null, message);
      });
      bootstrap.onDisconnect.addListener(() => {
        const error = chrome.runtime.lastError;
        if (!settled) finish(new Error(error?.message || "pairing_disconnected"));
      });
      bootstrap.postMessage({version: 1, type: "pair", ext: chrome.runtime.id, profile});
    });
    await chrome.storage.local.set({[PAIRING_KEY]: credential});
    reconnectAfterPairing = true;
  })().catch((error) => {
    showStatus("error", error.message);
    console.warn("[voxtype] Pairing failed: " + error.message);
  }).finally(() => { pairingPending = null; if (reconnectAfterPairing) void connect(); });
  return pairingPending;
}

chrome.runtime.onInstalled.addListener(connect);
chrome.runtime.onStartup.addListener(connect);
chrome.runtime.onMessage.addListener((message, sender, respond) => {
  if (sender.id !== chrome.runtime.id || sender.url !== chrome.runtime.getURL("connection.html")) return false;
  if (message?.type === "connection_status") { respond(connectionStatus); return false; }
  if (message?.type === "pair") { void pair().then(() => respond(connectionStatus)); return true; }
  return false;
});
chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name === RECONNECT_ALARM) connect();
});

// Window bookkeeping exists only to keep `hello` accurate; it costs nothing while
// idle and never touches a page.
// A focus change is a discrete event, and the desktop matches the window it
// anchored against the `lastFocused` flag of this snapshot. Debouncing it would
// answer a user who switches windows and dictates immediately from the *previous*
// window, so send at once and refresh once more after the debounce in case the
// browser had not yet updated its own window state when the event fired. Bounds
// changes stay debounced - those are high-frequency during a drag.
chrome.windows.onFocusChanged.addListener((id) => {
  if (id >= 0) lastFocusedWindowId = id;
  void sendHello();
  scheduleHello();
});
chrome.windows.onCreated.addListener(scheduleHello);
chrome.windows.onRemoved.addListener((id) => {
  if (id === lastFocusedWindowId) lastFocusedWindowId = -1;
  scheduleHello();
});
chrome.windows.onBoundsChanged.addListener(scheduleHello);

connect();
