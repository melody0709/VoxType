"use strict";
const statusLabel = document.getElementById("status");
const pairButton = document.getElementById("pair");
const labels = {
  connected: "Connected over WebSocket",
  connecting: "Paired · connecting…",
  disconnected: "Paired · disconnected; retrying automatically",
  unpaired: "Not paired",
  error: "Connection needs attention",
};
const errors = {
  desktop_unavailable: "Start VoxType, then pair again.",
  port_unavailable: "Another program is using VoxType's connection port.",
  pairing_timeout: "VoxType did not respond. Keep it running and try again.",
  incompatible_desktop: "Update VoxType and this extension together.",
  authentication_failed: "Pair again to restore trusted access.",
  pair_again: "VoxType's pairing changed. Pair again.",
  credential_storage_unavailable: "Reload the extension and try again.",
};
async function refresh() {
  try {
    const status = await chrome.runtime.sendMessage({type: "connection_status"});
    statusLabel.textContent = (labels[status?.state] || "Disconnected") +
      (status?.reason ? ". " + (errors[status.reason] || "Check that VoxType is running and pairing is registered in its settings.") : "");
    pairButton.textContent = status?.state === "connected" ? "Pair again" : "Pair with VoxType";
  } catch {
    statusLabel.textContent = "Extension unavailable. Reload it, then try again.";
  }
}
pairButton.addEventListener("click", async () => {
  pairButton.disabled = true;
  statusLabel.textContent = "Pairing…";
  try { await chrome.runtime.sendMessage({type: "pair"}); }
  catch { statusLabel.textContent = "Pairing failed. Keep VoxType running and register pairing in its settings."; }
  finally { pairButton.disabled = false; await refresh(); }
});
void refresh();
setInterval(refresh, 1000);
