# VoxType Browser Bridge

A Manifest V3 companion extension (2.0) for Chrome and Edge 116 or later. It lets VoxType keep its
dictation context (the focused input field) in the browser **without asking the
browser for accessibility**.

## Why it exists

Chromium accumulates requests from platform accessibility consumers in a
process-wide mode. Once VoxType reads the focused field through UI Automation,
the browser keeps maintaining a web accessibility tree for the rest of the
session: closing VoxType or refreshing the page does not undo it, and only a
browser restart restores performance. Reading `document.activeElement` from an
extension never touches that path.

Since VoxType v0.11.9 the desktop application never issues an accessibility
request against a browser process. Without this companion the browser simply
reports no context — the transcript is still typed, the shared recognition
history is skipped (fail-closed), and nothing in the browser slows down.

## What it does while you dictate

Per recording, VoxType runs a privacy probe and, when field context is enabled,
a field-text query:

| Answer | Meaning |
| --- | --- |
| `password` | the focused element is a password control |
| `textInput` | the focused element accepts text |
| `value` | the tail of the field's current text (never read for a password control) |

The page is only touched by an on-demand `chrome.scripting.executeScript` on
each query. There is no static content script, no `MutationObserver`, no
polling, no timers and no per-page work while you are not dictating.

## Privacy

- A password control's value is **never read**, let alone transmitted. The check
  in `worker.js` is written so the value branch is unreachable for it.
- Only the focused field is inspected. No page text, no other field, no URL,
  no cookies, no passwords, no keystrokes.
- Only the tail (up to 400 characters) of the field is sent, and only to the
  authenticated local VoxType process over WebSocket. The desktop verifies the
  kernel TCP owner, browser ancestry, current user/session and focused window.
- Fresh HMAC-SHA256 challenges authenticate both ends before any window inventory
  or field text is sent. Credentials are never placed in a URL, log or page DOM.
  Extension storage is limited to trusted extension contexts. The desktop file
  has an owner-only ACL and current-user DPAPI encryption.
- An ambiguous answer (two frames claiming focus, no focused window, a page the
  extension cannot reach) is reported as a miss. VoxType treats every miss as
  "unknown", which keeps transcripts out of the uploadable history.
- Stopping or uninstalling this extension removes the entire channel. The
  desktop side never falls back to accessibility.

## Development install

Prepare pairing, load the extension, then explicitly pair each browser profile.
Normal startup/reconnection needs no Native Messaging process. A missing connection
always gives no context/Unknown; it never falls back to browser accessibility.

1. Build and install the runtime: `build.bat` (payload lands in
   `build/run/x64-release/`).
2. Prepare the short-lived native pairing bootstrap for Chrome and Edge. Either
   path shares the same registration ownership and rollback record:

- **Settings → General → Browser integration → Pair / Refresh.** It refuses to
  replace another application's registration. Chrome and Edge status are separate:
  connected, paired but offline, not paired, or setup needed. An open General tab
  refreshes status once per second. Registration prepares pairing; it does not
  claim that the extension has connected.
   - Or run the script:

   ```powershell
   pwsh -File build\run\x64-release\scripts\register_browser_bridge.ps1 -Action Register
   ```

  Both own exactly two HKCU values, record what they overwrote, refuse to
   replace a registration they did not create, and need no administrator rights.
   `-Action Unregister` restores the previous state. Registering from the
   settings window writes the same record file, so the script can still undo a
   registration made there. The registration must point at the *installed*
   runtime, so re-run it after moving that directory.
The status query checks HKCU before HKLM, both 32-bit and 64-bit registry views,
and Edge's Edge/Chromium/Chrome fallback keys. An empty or stale higher-priority
entry is reported instead of being hidden by a lower-priority entry.
   Interrupted registration can be retried or undone with `-Action Unregister`:
   each record retains the owned path from before the attempt. The shared JSON
   record accepts any field order and is replaced atomically before registry writes.
3. Open `chrome://extensions` (or `edge://extensions`), enable **Developer
   mode**, choose **Load unpacked**, and select the installed folder
   `build\run\x64-release\extensions\browser-bridge`.
4. Keep VoxType running. Click the extension icon, choose **Pair with VoxType**,
   and wait for **Connected over WebSocket**. Repeat in each browser profile.
   The bootstrap process exits after one bounded exchange; only the tray process
   remains. The extension's options page offers the same pairing controls.
5. Dictate into a normal web page. After an app restart or extension reload, the
   saved pairing reconnects automatically. If the pairing identity changed, use
   **Pair again**; no failure ever silently switches back to a native host.

The current MSI distributes the extension, manifest and helper script. It does
**not** create Native Messaging registrations automatically (`Package.wxs` owns
the install folder and shortcut; generated components own runtime files). Use
Settings or the helper for this user-level registration. Portable uses the same
steps. No HKLM registration or firewall exception is added by this feature.

### Credentials, conflicts and rollback

- The endpoint is fixed at `ws://127.0.0.1:19226/voxtype/browser/v2`, subprotocol
  `voxtype-context-v2`. No port scan, LAN listener or administrator permission.
  A conflict is reported as unavailable and never treated as a connection. Stop
  the conflicting application and restart VoxType.
- Each installation path/profile has independent credentials. The desktop store
  is `%LOCALAPPDATA%\VoxType\browser-websocket-pairing-<path-hash>.dat`, including
  Portable mode; credentials are not a Config preference or packaged payload.
  A moved directory needs registration repair and explicit pairing. Stop the old
  instance before starting the new one. A different user/session cannot reuse a
  connection; simultaneous sessions contending for the fixed port fail closed.
- **Settings → Unpair all** rotates the installation identity and disconnects
  all paired profiles. Re-pairing one profile rotates its previous credential.
  A corrupt store fails closed until an explicit **Unpair all** recovers it.
- MSI uninstall preserves current-user data, including pairing, just as it
  preserves configuration. Use **Unpair all** first to revoke future reuse.
  Removing the extension removes its local credential; desktop revocation clears
  retained pairings. The helper's **Unregister** restores only its owned registry
  entries; it is independent of credential revocation.
- Desktop v0.11.10 retains the 1.x forwarding path for compatibility. An old
  extension still creates a forwarding process and is labelled **legacy host
  active**; reload/update it for the single-process behavior. Extension 2.0 with
  an older desktop fails pairing explicitly and does not attempt legacy fallback.
  For rollback, stop the desktop, restore the tagged v0.11.9 desktop/1.x extension
  together, and retain the registration record. Do not delete or rewrite foreign
  keys. Full removal of 1.x support awaits the remaining acceptance items.

### Reading the diagnostics

The service worker logs its trace (`[voxtype] …`) to its own console: open
`chrome://extensions` → the extension → **service worker**. It shows the
pairing outcome, authentication failures, every `hello` (window count and which one is
`lastFocused`) and every query with its outcome. Keep the console open while you
dictate; there is no persisted log.

The desktop side reports the mirror image in its own debug console and runtime
log, with the reason and the rectangles that decided it, for example:

```
reason=no_window_match peers=1 matched=0 advertised=[0,0,1920x1040] anchor=[0,0,1936x1056]
```

A successful dictation shows `LayerName(6)` = `L4_BROWSER_BRIDGE`.

Companion 2.0 preserves the current window rectangle and monitor work-area
origin on demand. Its UI scale is `devicePixelRatio / chrome.tabs.getZoom()`:
Windows DPI, a forced browser UI scale, and webpage zoom are separate quantities.
The desktop routes by the native browser PID and last-focused window, then checks
the reply's monitor-relative rectangle before accepting any text/privacy answer.
It never infers identity from an arbitrary proportional window or page DPR alone.
After upgrading the desktop, reload the unpacked extension to apply these fields.

Disconnects get one quick retry after 250 ms; repeated failures use an alarm
backoff from 30 seconds to five minutes. Authenticated connections exchange a
heartbeat every 20 seconds to keep the MV3 worker alive; this is disclosed idle
work, separate from page reads. Missing traffic closes the channel within about
65 seconds. Window snapshots discard obsolete asynchronous responses, retain
the last focused window after focus loss, and identify actual focus separately
across browser profiles. A rejected second desktop launch never starts a second
pipe listener. These paths have offline regression coverage.

The extension id is pinned by the `key` field in `manifest.json`
(`cjmhlnmnolcbkjpfdbnpejfnigacbpai`), which is also the only origin listed in
`allowed_origins` of the native messaging host manifest — no other extension can
launch the host.

## Frames, iframes and shadow roots

Focus inside a nested frame is handled. `Document.hasFocus()` answers true for a
document when it *or any element inside it* holds focus, so focusing a control in
an iframe makes every ancestor document report focus too; the worker drops those
pass-through documents (their `activeElement` is the frame owner element) and
answers from the one frame that really owns the control. A focus in a frame the
extension cannot inject is reported as a miss rather than guessed from the parent.

The same retargeting happens at a shadow boundary: `document.activeElement` is
the *host* element, never the control inside. The page function therefore walks
**open** shadow roots (depth-bounded) down to the element that really holds focus,
so a web component wrapping a text or password input is identified correctly.

A **closed** shadow root cannot be inspected — `host.shadowRoot` is `null`, which
is exactly how a plain non-editable element looks. Such a host is reported with
`textInput: false`, and the desktop side refuses to treat "does not look like a
password" as safe without a positively identified editable control. The result is
a missing history entry, never a leaked password field.

## Developer checks

```powershell
node tests/browser_bridge_worker_contract.cjs   # runs the real worker against a stubbed chrome API
pwsh -File tests/browser_bridge_registration_test.ps1 # isolated registry tests; also in build.bat --test
build.bat --test                              # includes real WebSocket framing/authentication tests
node tools/browser_bridge_live_contract.cjs    # existing Chrome + extension options page, synthetic loopback fixture only
```

The live check resolves `cdp.js` under the current user's
`.gemini/config/skills/chrome-cdp/scripts/` directory. Install the chrome-cdp skill
there before running it.

The worker test is the contract for frame selection, the password rule and the
`hello` timing. It is deliberately not part of `build.bat`: the product build has
no Node dependency. It accepts an explicit worker path
(`node tests/browser_bridge_worker_contract.cjs <worker.js>`) so a modified copy
can be checked against the same assertions.

## Known limits

- This build was accepted against Chrome 155 on the current Windows machine.
  Edge, multiple profiles, sleep/session transitions and MSI upgrade/rollback
  in a VM are explicitly pending. Four DPI scales have static layout coverage;
  desktop screenshot/interaction QA was unavailable in this session.

- Chrome, Edge and other Chromium-family browsers only. Firefox needs a
  different manifest and registration and is not shipped yet; Firefox windows are
  still protected from accessibility requests by the desktop side.
- `chrome://`, the extension gallery, PDF viewers, `file://` and browser chrome
  report a miss, by design.
- A page whose focused frame cannot be injected reports a miss.
- Installation is manual (Load unpacked) in this version. Store publication and
  silent enterprise deployment are separate work.
