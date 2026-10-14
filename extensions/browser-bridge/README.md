# VoxType Browser Bridge

A Manifest V3 companion extension for Chrome and Edge. It lets VoxType keep its
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
  local VoxType process over a named pipe that is limited to your user account
  and logon session.
- An ambiguous answer (two frames claiming focus, no focused window, a page the
  extension cannot reach) is reported as a miss. VoxType treats every miss as
  "unknown", which keeps transcripts out of the uploadable history.
- Stopping or uninstalling this extension removes the entire channel. The
  desktop side never falls back to accessibility.

## Development install

**The two steps are independent and both are required.** Loading the extension
without registering the host (or the other way round) produces exactly one
symptom: the desktop console shows the browser context as `not_connected`, because
the browser path never falls back to accessibility. If the desktop already existed
when you loaded the extension, **reload the extension after registering** — it
reconnects on load.

1. Build and install the runtime: `build.bat` (payload lands in
   `build/run/x64-release/`).
2. Register the native messaging host for Chrome and Edge:

   ```powershell
   pwsh -File build\run\x64-release\scripts\register_browser_bridge.ps1 -Action Register
   ```

   The script owns exactly two HKCU values, records what it overwrote, refuses to
   replace a registration it did not create, and needs no administrator rights.
   `-Action Unregister` restores the previous state. It must point at the
   *installed* runtime, so re-run it after moving that directory.
3. Open `chrome://extensions` (or `edge://extensions`), enable **Developer
   mode**, choose **Load unpacked**, and select the installed folder
   `build\run\x64-release\extensions\browser-bridge`.
4. Keep VoxType running, then dictate into a web page.

### Reading the diagnostics

The service worker logs its trace (`[voxtype] …`) to its own console: open
`chrome://extensions` → the extension → **service worker**. It shows the
`connectNative` result, every `hello` (window count and which one is
`lastFocused`) and every query with its outcome. Keep the console open while you
dictate; there is no persisted log.

The desktop side reports the mirror image in its own debug console and runtime
log, with the reason and the rectangles that decided it, for example:

```
reason=no_window_match peers=1 matched=0 advertised=[0,0,1920x1040] anchor=[0,0,1936x1056]
```

A successful dictation shows `LayerName(6)` = `L4_BROWSER_BRIDGE`.

Companion 1.0.1 also reports the current window rectangle and monitor work-area
origin on demand. Its UI scale is `devicePixelRatio / chrome.tabs.getZoom()`:
Windows DPI, a forced browser UI scale, and webpage zoom are separate quantities.
The desktop routes by the native browser PID and last-focused window, then checks
the reply's monitor-relative rectangle before accepting any text/privacy answer.
It never infers identity from an arbitrary proportional window or page DPR alone.
After upgrading the desktop, reload the unpacked extension to apply these fields.

Disconnects get one quick retry after 250 ms; repeated failures use the existing
alarm backoff. Window snapshots discard obsolete asynchronous responses, retain
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
```

That script is the contract for frame selection, the password rule and the
`hello` timing. It is deliberately not part of `build.bat`: the product build has
no Node dependency. It accepts an explicit worker path
(`node tests/browser_bridge_worker_contract.cjs <worker.js>`) so a modified copy
can be checked against the same assertions.

## Known limits

- Chrome, Edge and other Chromium-family browsers only. Firefox needs a
  different manifest and registration and is not shipped yet; Firefox windows are
  still protected from accessibility requests by the desktop side.
- `chrome://`, the extension gallery, PDF viewers, `file://` and browser chrome
  report a miss, by design.
- A page whose focused frame cannot be injected reports a miss.
- Installation is manual (Load unpacked) in this version. Store publication and
  silent enterprise deployment are separate work.
