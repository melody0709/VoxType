"use strict";
// Runs the real field reader/routing code in the already-open Chrome, exclusively
// against this temporary local fixture. Browser control stays in chrome-cdp.
const fs = require("node:fs");
const path = require("node:path");
const http = require("node:http");
const {spawn} = require("node:child_process");
const {createHash} = require("node:crypto");
const assert = require("node:assert/strict");
const root = path.resolve(__dirname, "..");
const entry = path.join(require("node:os").homedir(), ".gemini", "config", "skills", "chrome-cdp", "scripts", "cdp.js");
assert(fs.existsSync(entry), "Install the chrome-cdp skill before running this live check; missing entry: " + entry);
const extensionPage = "chrome-extension://cjmhlnmnolcbkjpfdbnpejfnigacbpai/connection.html";
const origin = "http://127.0.0.1:19327";
const evidenceDir = path.join(root, ".plan/feat/evidence/browser-websocket");
const source = fs.readFileSync(path.join(root, "extensions/browser-bridge/worker.js"), "utf8");
const begin = source.indexOf("function readFocusedField(");
const end = source.indexOf("function disconnect(", begin);
assert(begin >= 0 && end > begin, "the real worker's reader and router must exist");
const functions = source.slice(begin, end);
const reasons = source.match(/const KNOWN_REASONS = (\[[\s\S]*?\]);/)[1];

function run(executable, args) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, args, {cwd: root, windowsHide: true, stdio: ["ignore", "pipe", "pipe"]});
    let output = "";
    child.stdout.on("data", (bytes) => { output += bytes; });
    child.stderr.on("data", () => {});
    child.on("error", reject);
    child.on("close", (code) => code === 0 ? resolve(output.trim()) : reject(new Error("Browser contract command failed (" + code + "): " + (args[0] === entry ? args.slice(1,4).join(" ") : path.basename(executable)) + (executable.endsWith("browser_bridge_test.exe") ? " / " + output.trim() : ""))));
  });
}
async function cdp(...args) { return run(process.execPath, [entry, ...args]); }
async function extensionEval(script) { return JSON.parse(await cdp("--cdp", "eval", extensionPage, script)); }
const fixture = `<!doctype html><html><head><title>VoxType synthetic browser fixture</title></head><body>
<input id="plain" value="synthetic top context"><input id="password" type="password" value="synthetic password">
<div id="open"></div><div id="closed"></div><button id="focus-closed">Focus closed shadow</button><iframe src="http://localhost:19327/frame"></iframe>
<script>
const openInput = document.createElement('input'); openInput.value = 'synthetic open shadow';
document.querySelector('#open').attachShadow({mode:'open'}).append(openInput);
const closedInput = document.createElement('input'); closedInput.type = 'password'; closedInput.value = 'synthetic closed password';
document.querySelector('#closed').attachShadow({mode:'closed'}).append(closedInput);
document.querySelector('#focus-closed').addEventListener('click',()=>closedInput.focus());
Object.defineProperty(document.querySelector('#password'),'value',{get(){throw new Error('password value read');}});
</script></body></html>`;
const server = http.createServer((request, response) => {
  response.setHeader("Content-Type", "text/html; charset=utf-8");
  response.setHeader("Cache-Control", "no-store");
  if (request.url === "/frame") response.end('<input id="frame" value="synthetic cross-origin iframe"><iframe src="/nested"></iframe>');
  else if (request.url === "/nested") response.end('<input id="nested" value="synthetic nested iframe">');
  else if (request.url === "/") response.end(fixture);
  else { response.statusCode = 404; response.end("Not found"); }
});
let opened = false;
(async () => {
  await new Promise((resolve, reject) => { server.once("error", reject); server.listen(19327, "127.0.0.1", resolve); });
  const tab = JSON.parse(await cdp("open", origin + "/"));
  opened = true;
  assert(Number.isInteger(tab.id));
  let loaded;
  for (let attempt=0; attempt<20; ++attempt) {
    loaded=JSON.parse(await cdp("eval", String(tab.id), "({ready:document.readyState,url:location.href})"));
    if(loaded.ready === "complete") break;
    await new Promise(resolve=>setTimeout(resolve,250));
  }
  assert.equal(loaded.url, origin + "/");
  assert.equal(loaded.ready, "complete");
  const target = await extensionEval(`(async()=>{
    const tab=await chrome.tabs.get(${tab.id});
    if(tab.url!==${JSON.stringify(origin + "/")}) throw new Error('fixture identity');
    await chrome.tabs.update(tab.id,{active:true});
    await chrome.windows.update(tab.windowId,{focused:true});
    await chrome.tabs.setZoom(tab.id,1);
    return {tabId:tab.id,windowId:tab.windowId};
  })()`);
  const results = [];
  const cases = [
    {name:"plain", expected:"synthetic top context"},
    {name:"password", password:true},
    {name:"frame", expected:"synthetic cross-origin iframe", nested:true},
    {name:"nested", expected:"synthetic nested iframe", nested:true},
    {name:"open", expected:"synthetic open shadow"},
    {name:"closed", unidentified:true},
    {name:"zoom", expected:"synthetic top context", zoom:2.5},
  ];
  for (const test of cases) {
    const focus = await extensionEval(`(async()=>{
      await chrome.tabs.update(${target.tabId},{active:true});
      await chrome.windows.update(${target.windowId},{focused:true});
      await chrome.tabs.setZoom(${target.tabId},${test.zoom || 1});
      return chrome.scripting.executeScript({target:{tabId:${target.tabId},allFrames:true},args:[${JSON.stringify(test.name)}],
        func:(name)=>{let el=document.getElementById(name==='zoom'?'plain':name);
          if(name==='open') el=document.getElementById('open')?.shadowRoot?.querySelector('input');
          if(name==='closed') {el=document.getElementById('focus-closed');if(el)el.click();}
          else if(el) el.focus(); return {found:!!el};}});
    })()`);
    assert(focus.some((frame) => frame.result.found), "fixture focus target: " + test.name);
    const answer = await extensionEval(`(async()=>{
      const actual=globalThis.chrome;
      const chrome={windows:{get:async(...args)=>{const w=await actual.windows.get(...args);
        if(!w.tabs?.some(t=>t.active&&t.id===${target.tabId}))throw new Error('no_target');return w;}},
        tabs:{getZoom:(...args)=>actual.tabs.getZoom(...args)},scripting:{executeScript:(...args)=>actual.scripting.executeScript(...args)}};
      const MAX_VALUE_CHARS=400, KNOWN_REASONS=${reasons};
      const console={log(){},warn(){}}; const port={}; let answer;
      function post(message,expected){if(expected===port){answer=message;return true;}return false;}
      ${functions}
      await handleQuery({id:1,need:'field',windowId:${target.windowId},valueChars:400});
      return answer;
    })()`);
    assert(answer?.ok, "real Chrome query succeeds: " + test.name + " / " + answer?.reason);
    assert.equal(answer.password, !!test.password);
    assert.equal(answer.textInput, !test.unidentified);
    assert.equal(answer.value, test.expected || "", "only the expected synthetic field is returned");
    if (test.nested) assert(answer.frameId > 0, "the focused child frame wins over ancestor documents");
    if (test.zoom) assert.equal(answer.scaleMicros, results[0].scaleMicros, "page zoom does not change browser UI scale");
    results.push({case:test.name,passed:true,password:answer.password,textInput:answer.textInput,frameId:answer.frameId,scaleMicros:answer.scaleMicros});
    if (test.name === "zoom") {
      const geometry = {...answer, value:""};
      fs.mkdirSync(evidenceDir, {recursive:true});
      const replyPath = path.join(evidenceDir, "chrome-geometry-reply.json");
      fs.writeFileSync(replyPath, JSON.stringify(geometry, null, 2));
      const check = await run(path.join(root, "build/artifacts/tests/browser_bridge_test.exe"), ["--live-browser-geometry", replyPath]);
      assert(check.includes("live browser geometry: PASS"));
    }
  }
  fs.writeFileSync(path.join(evidenceDir,"chrome-field-contract.json"), JSON.stringify({utc:new Date().toISOString(),workerSha256:createHash("sha256").update(source).digest("hex"),results},null,2));
  console.log("Chrome live contract: " + results.length + " field/frame/shadow/zoom cases and native window geometry passed");
})().catch((error) => { console.error(error.message); process.exitCode = 1; }).finally(async () => {
  if (opened) await cdp("close", origin).catch(() => {});
  server.close();
  server.closeAllConnections();
});
