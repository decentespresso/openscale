#ifndef WIFI_SETUP_PAGE_H
#define WIFI_SETUP_PAGE_H

static const char HDS_WIFI_SETUP_STYLE[] PROGMEM = R"css(
.wifi-setup{--wifi-border:var(--line,#d8dde3);--wifi-ink:var(--ink,#1f2328);--wifi-muted:var(--muted,#5f6875);color:var(--wifi-ink)}
.wifi-setup label{display:grid;gap:6px;font-size:14px;font-weight:600;min-width:0}
.wifi-setup input,.wifi-setup select,.wifi-setup button{box-sizing:border-box;font:inherit;min-width:0;max-width:100%;height:44px;border:1px solid var(--wifi-border);border-radius:6px;padding:9px 11px;background:var(--surface,#fff);color:var(--wifi-ink)}
.wifi-setup input,.wifi-setup select{width:100%}
.wifi-setup button{cursor:pointer;font-weight:600}
.wifi-setup button:disabled,.wifi-setup select:disabled{cursor:default;opacity:.55}
.wifi-setup input:focus-visible,.wifi-setup select:focus-visible,.wifi-setup button:focus-visible{outline:2px solid #0b7180;outline-offset:2px}
.wifi-setup #wifi-form{display:block;max-width:760px}
.wifi-fields,.wifi-network-tools{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:12px;max-width:760px;margin:12px 0;align-items:end}
.wifi-network-tools{grid-template-columns:168px minmax(0,1fr)}
.wifi-network-select{display:grid;height:44px;min-width:0}
.wifi-network-select>*{grid-area:1/1}
.wifi-scan-progress{display:flex;align-items:center;gap:10px;box-sizing:border-box;height:44px;padding:9px 11px;border:1px solid var(--wifi-border);border-radius:6px;background:var(--surface,#fff);font-weight:400}
.wifi-spinner{box-sizing:border-box;width:18px;height:18px;flex:none;border:2px solid var(--wifi-border);border-top-color:#0b7180;border-radius:50%;animation:wifi-spin .8s linear infinite}
@keyframes wifi-spin{to{transform:rotate(360deg)}}
@media(prefers-reduced-motion:reduce){.wifi-spinner{animation:none}}
.wifi-actions{display:flex;flex-wrap:wrap;align-items:center;gap:12px;margin-top:12px}
.wifi-actions label{display:flex;align-items:center;font-weight:400;margin-right:auto}
.wifi-setup #show-password{width:18px;height:18px;padding:0;margin:0;accent-color:#0b7180}
.wifi-setup #wifi-connect-button{background:#0b7180;color:#fff;border-color:#0b7180;min-width:128px}
.wifi-setup #wifi-connect-button:hover{background:#09616e;border-color:#09616e}
.wifi-setup #reset-wifi-button{display:block;margin:16px 0 32px;color:var(--danger,#b42318);min-width:0}
.wifi-setup #wifi-current,.wifi-setup #wifi-status{min-height:1.5em;overflow-wrap:anywhere;unicode-bidi:plaintext;font-size:14px;line-height:1.5}
.wifi-setup #wifi-current{color:var(--wifi-muted)}
.wifi-setup #wifi-status[data-tone=error]{color:var(--danger,#b42318)}
.wifi-setup #wifi-status[data-tone=success]{color:var(--green-dark,#118544)}
.wifi-setup #wifi-status[data-tone=neutral]{color:var(--wifi-muted)}
.wifi-setup #wifi-reconnect{display:block;overflow-wrap:anywhere;margin:8px 0;font-size:14px}
.wifi-setup [hidden]{display:none}
.wifi-reset-dialog{box-sizing:border-box;width:min(92vw,460px);max-height:90vh;border:1px solid var(--wifi-border);border-radius:8px;padding:18px;background:var(--surface,#fff)}
.wifi-reset-dialog::backdrop{background:rgba(12,25,20,.7)}
.wifi-reset-dialog h2{margin:0 0 12px;font-size:18px;line-height:1.25}
.wifi-reset-dialog p{margin:0 0 18px;font-size:14px;line-height:1.5}
.wifi-dialog-actions{display:flex;justify-content:flex-end;gap:10px}
.wifi-dialog-actions button{flex:1}
.wifi-setup #wifi-reset-confirm{background:#b42318;color:#fff;border-color:#b42318}
.wifi-setup #wifi-reset-confirm:hover{background:#922118;border-color:#922118}
@media(max-width:520px){.wifi-fields,.wifi-network-tools{grid-template-columns:minmax(0,1fr)}.wifi-setup button{width:100%}.wifi-actions{align-items:stretch}.wifi-actions label{width:100%}.wifi-setup #show-password{width:18px}}
)css";

static const char HDS_WIFI_SETUP_SCRIPT[] PROGMEM = R"js(
(() => {
  const form = document.getElementById('wifi-form');
  if (!form) return;
  const ssidInput = document.getElementById('ssid');
  const passwordInput = document.getElementById('password');
  const scanButton = document.getElementById('wifi-scan-button');
  const networks = document.getElementById('wifi-networks');
  const scanProgress = document.getElementById('wifi-scan-progress');
  const resetButton = document.getElementById('reset-wifi-button');
  const resetDialog = document.getElementById('wifi-reset-dialog');
  const showPassword = document.getElementById('show-password');
  const current = document.getElementById('wifi-current');
  const status = document.getElementById('wifi-status');
  const reconnect = document.getElementById('wifi-reconnect');
  let busy = false;
  let scannedSsid = null;
  let deviceName = 'hds';
  let mdnsAvailable = false;
  const linkedAttempt = Number(new URLSearchParams(location.search).get('wifi_attempt')) || 0;

  const trimEdge = value => value.replace(/^[\s\u0085\p{Default_Ignorable_Code_Point}]+|[\s\u0085\p{Default_Ignorable_Code_Point}]+$/gu, '');
  const bytes = value => new TextEncoder().encode(value).length;
  const invalidText = value => /[\u0000-\u001f\u007f-\u009f]/u.test(value) ||
    Array.from(value).some(character => {
      const point = character.codePointAt(0);
      return point >= 0xd800 && point <= 0xdfff;
    });

  function message(text, tone = 'neutral') {
    status.textContent = text;
    status.dataset.tone = tone;
  }

  function setBusy(value) {
    busy = value;
    form.querySelectorAll('input,button').forEach(element => { element.disabled = value; });
    scanButton.disabled = value;
    resetButton.disabled = value;
    networks.disabled = value || networks.options.length <= 1;
    document.querySelectorAll('#name-form input,#name-form button,#name input,#name button')
      .forEach(element => { element.disabled = value; });
    form.setAttribute('aria-busy', String(value));
  }

  async function request(path, body) {
    const response = await fetch(path, {
      method: body === undefined ? 'GET' : 'POST',
      cache: 'no-store',
      headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(4000)
    });
    if (!response.ok) throw new Error(response.status === 409 ? 'wifi_busy' :
      response.status === 400 ? 'wifi_credentials_invalid' : 'request_failed');
    return response.json();
  }

  const connectionInterrupted = error => ['TimeoutError', 'AbortError', 'TypeError'].includes(error.name);

  function updateCurrent(data) {
    if (/^[a-z0-9](?:[a-z0-9-]{0,22}[a-z0-9])?$/.test(data.mdns_name)) deviceName = data.mdns_name;
    mdnsAvailable = data.mdns_available === true;
    current.textContent = data.access_point ? `Setup network: DecentScale | ${data.ip}` :
      data.connected ? `Connected: ${data.ssid} | ${data.ip}` :
      data.credentials_saved ? `Reconnecting: ${data.ssid}` : 'No WiFi network saved';
    if (!ssidInput.value && data.credentials_saved) ssidInput.value = data.ssid;
  }

  function showReconnect(id, ip) {
    if (!mdnsAvailable && !ip) {
      reconnect.hidden = true;
      return;
    }
    const host = ip || `${deviceName}.local`;
    const url = new URL(`http://${host}`);
    if (id) url.searchParams.set('wifi_attempt', String(id));
    reconnect.href = url.href;
    reconnect.textContent = `Open scale at ${url.host}`;
    reconnect.hidden = false;
  }

  function failureText(error) {
    const messages = {
      authentication_failed: 'The new WiFi password was not accepted. Check the password.',
      network_not_found: 'Network not found.',
      dhcp_timeout: 'The network did not assign an IP address.',
      connection_timeout: 'The connection timed out.',
      wifi_credentials_save_failed: 'Could not save WiFi settings.',
      wifi_credentials_invalid: 'Use a network name of 1-32 bytes and an empty password, 8-63 bytes, or 64 hexadecimal characters.',
      wifi_busy: 'Another WiFi operation is running.',
      aborted: 'The WiFi change was cancelled.'
    };
    return messages[error] || 'Could not reach the scale.';
  }

  async function waitForOperation(id, kind) {
    const started = Date.now();
    while (Date.now() - started < 60000) {
      await new Promise(resolve => setTimeout(resolve, 1000));
      let data;
      try { data = await request('/setup/wifi/status'); }
      catch {
        current.textContent = 'Waiting for the scale to reconnect';
        message(kind === 'reset' ? 'Contact with the scale was lost while resetting. Waiting for the result...' :
          'Contact with the scale was lost. If the new WiFi settings fail, it will try the previous network automatically. Waiting for the result...');
        continue;
      }
      updateCurrent(data);
      if (data.operation_id !== id) {
        message('This WiFi request is no longer current. Its result is not confirmed.');
        setBusy(data.busy === true);
        return;
      }
      if (data.state === 'succeeded') {
        if (kind === 'reset' && data.access_point && !data.credentials_saved) {
          message('WiFi settings cleared. Setup network: DecentScale.', 'success');
          showReconnect(0, data.ip);
        } else if (data.connected && data.ssid === data.requested_ssid && data.credentials_saved) {
          message(`Connected to ${data.ssid}. WiFi settings saved.`, 'success');
          showReconnect(id, data.ip);
        } else {
          message('The connection result is not confirmed.');
        }
        setBusy(false);
        return;
      }
      if (data.state === 'failed') {
        const recovery = data.connected ? `Back on ${data.ssid}. Your previous WiFi settings were kept.` :
          data.access_point ? `${data.credentials_saved ? 'The previous network is unavailable. ' : ''}Connect to DecentScale (password 12345678), then open 192.168.1.1.` :
          'Your previous WiFi settings were kept.';
        message(`${failureText(data.error)} ${recovery}`, 'error');
        if (data.connected || data.access_point) showReconnect(id, data.ip);
        setBusy(false);
        return;
      }
      if (kind === 'remote' && !data.busy) { setBusy(false); return; }
      message(data.state === 'restoring' ? 'The new WiFi settings did not work. Reconnecting to the previous network...' :
        data.state === 'verifying' ? 'Connected with the new settings. Checking the connection...' : 'Testing the new WiFi settings...');
    }
    message('Could not confirm the result. Check which WiFi the scale is on, then open the link below. If the new settings fail, the scale will try the previous network automatically.');
    setBusy(false);
  }

  async function changeWifi(ssid, pass, kind, scanned = false) {
    if (busy) return;
    setBusy(true);
    reconnect.hidden = true;
    message(kind === 'reset' ? 'Clearing WiFi settings...' : 'Submitting WiFi connection test...');
    let accepted;
    try { accepted = await request('/setup/wifi', scanned ? { ssid, pass, scanned: true } : { ssid, pass }); }
    catch (error) {
      message(error.message === 'wifi_busy' || error.message === 'wifi_credentials_invalid' ?
        failureText(error.message) : 'Request not confirmed. Check the scale connection before trying again.', 'error');
      setBusy(false);
      return;
    }
    passwordInput.value = '';
    if (!Number.isInteger(accepted.operation_id) || accepted.restarting !== false) {
      message('This firmware does not support a verified direct WiFi change.', 'error');
      setBusy(false);
      return;
    }
    if (/^[a-z0-9](?:[a-z0-9-]{0,22}[a-z0-9])?$/.test(accepted.mdns_name)) deviceName = accepted.mdns_name;
    mdnsAvailable = accepted.mdns_available === true;
    showReconnect(accepted.operation_id, kind === 'reset' ? '192.168.1.1' : undefined);
    message(kind === 'reset' ? 'Clearing settings. Reconnect to DecentScale with password 12345678.' :
      `Trying new WiFi settings for ${accepted.ssid}. If they fail, the scale will automatically return to the previous network. Previous settings stay saved until success.`);
    await waitForOperation(accepted.operation_id, kind);
  }

  form.addEventListener('submit', event => {
    event.preventDefault();
    const scanned = scannedSsid !== null && ssidInput.value === scannedSsid;
    const ssid = scanned ? ssidInput.value : trimEdge(ssidInput.value);
    const pass = scanned ? passwordInput.value : trimEdge(passwordInput.value);
    if (!ssid || bytes(ssid) > 32 || invalidText(ssid) || invalidText(pass) ||
        !(pass.length === 0 || (bytes(pass) >= 8 && bytes(pass) <= 63) || /^[a-f0-9]{64}$/i.test(pass))) {
      message(failureText('wifi_credentials_invalid'), 'error');
      return;
    }
    ssidInput.value = ssid;
    passwordInput.value = pass;
    changeWifi(ssid, pass, 'switch', scanned);
  });

  showPassword.addEventListener('change', () => {
    passwordInput.type = showPassword.checked ? 'text' : 'password';
  });
  networks.addEventListener('change', () => {
    scannedSsid = networks.value || null;
    if (networks.value) { ssidInput.value = networks.value; passwordInput.focus(); }
  });
  ssidInput.addEventListener('input', () => {
    scannedSsid = null;
    networks.value = '';
  });
  resetButton.addEventListener('click', () => {
    if (busy || resetDialog.open) return;
    resetDialog.returnValue = '';
    resetDialog.showModal();
  });
  resetDialog.addEventListener('close', () => {
    if (resetDialog.returnValue === 'reset') changeWifi('', '', 'reset');
  });

  scanButton.addEventListener('click', async () => {
    if (busy) return;
    setBusy(true);
    scannedSsid = null;
    networks.replaceChildren();
    networks.hidden = true;
    scanProgress.hidden = false;
    message('Searching for networks...');
    try {
      await request('/setup/wifi/scan', {});
      const started = Date.now();
      while (Date.now() - started < 15000) {
        await new Promise(resolve => setTimeout(resolve, 1000));
        let data;
        try { data = await request('/setup/wifi/scan'); }
        catch (error) {
          if (!connectionInterrupted(error)) throw error;
          continue;
        }
        if (data.state === 'scanning') continue;
        if (data.state !== 'complete' || !Array.isArray(data.networks)) throw new Error('scan_failed');
        const options = data.networks.map(network => {
          const option = document.createElement('option');
          option.value = network.ssid;
          option.textContent = `${network.ssid} (${network.rssi} dBm, ${network.secure ? 'secured' : 'open'})`;
          return option;
        });
        const placeholder = document.createElement('option');
        placeholder.value = '';
        placeholder.textContent = options.length ? 'Select a network' : 'No networks found';
        networks.replaceChildren(placeholder, ...options);
        message(options.length ? `${options.length} networks found.` : 'No networks found.');
        return;
      }
      throw new Error('scan_failed');
    } catch (error) {
      message(error.message === 'wifi_busy' ? failureText(error.message) : 'Network search failed. You can enter a network name.', 'error');
    } finally {
      scanProgress.hidden = true;
      networks.hidden = false;
      setBusy(false);
    }
  });

  request('/setup/wifi/status').then(data => {
    updateCurrent(data);
    if (busy) return;
    if (linkedAttempt && data.operation_id !== linkedAttempt) {
      message('This WiFi request is no longer current. Its result is not confirmed.');
    }
    if ((linkedAttempt && data.operation_id === linkedAttempt) || data.busy) {
      setBusy(true);
      waitForOperation(data.operation_id, linkedAttempt && !data.requested_ssid ? 'reset' :
        linkedAttempt ? 'switch' : 'remote');
    }
  }).catch(() => { current.textContent = 'Connection status unavailable'; });
})();
)js";

static const char HDS_WIFI_SETUP_PAGE[] PROGMEM = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Half Decent Scale setup</title><link rel="stylesheet" href="/setup/wifi.css">
<style>body{font:16px system-ui,sans-serif;max-width:760px;margin:32px auto;padding:0 16px;color:#1f2328}h1{font-size:25px}h2{font-size:18px}#name{display:grid;gap:8px;margin-top:24px}#name input,#name button{font:inherit;height:44px;padding:9px 11px;border:1px solid #d8dde3;border-radius:6px;background:#fff}#name-status{overflow-wrap:anywhere}</style>
<script src="/setup/wifi.js" defer></script></head><body>
<h1>Half Decent Scale setup</h1><section class="wifi-setup" id="wifi-popup" aria-labelledby="wifi-title">
<h2 id="wifi-title">WiFi</h2><p id="wifi-current" role="status">Checking connection...</p>
<div class="wifi-network-tools"><button id="wifi-scan-button" type="button">Find networks</button><label for="wifi-networks">Nearby networks<span class="wifi-network-select"><select id="wifi-networks" disabled><option value="">Select a network</option></select><span id="wifi-scan-progress" class="wifi-scan-progress" role="status" hidden><span class="wifi-spinner" aria-hidden="true"></span>Searching...</span></span></label></div>
<form id="wifi-form" autocomplete="off"><div class="wifi-fields"><label for="ssid">Network name<input id="ssid" autocomplete="off" autocapitalize="none" spellcheck="false" required></label><label for="password">Password<input id="password" type="password" autocomplete="off" autocapitalize="none" spellcheck="false"></label></div>
<div class="wifi-actions"><label for="show-password"><input id="show-password" type="checkbox">Show password</label><button id="wifi-connect-button" type="submit">Connect</button></div></form>
<p id="wifi-status" role="status" aria-live="polite" data-tone="neutral"></p><a id="wifi-reconnect" hidden></a><button id="reset-wifi-button" type="button">Reset WiFi settings</button></section>
<form id="name"><label for="device-name">Device name</label><input id="device-name" name="name" placeholder="hds" maxlength="24" required><button>Rename</button></form><p id="name-status" role="status"></p>
<dialog id="wifi-reset-dialog" class="wifi-setup wifi-reset-dialog" aria-labelledby="wifi-reset-title" aria-describedby="wifi-reset-description">
<h2 id="wifi-reset-title">Reset WiFi settings?</h2><p id="wifi-reset-description">The scale will disconnect and open DecentScale setup. Other settings stay unchanged.</p>
<form method="dialog" class="wifi-dialog-actions"><button value="cancel" autofocus>Cancel</button><button id="wifi-reset-confirm" value="reset">Reset WiFi</button></form></dialog>
<script>
document.getElementById('name').addEventListener('submit',async event=>{
event.preventDefault();const status=document.getElementById('name-status');
try{const response=await fetch('/setup/name',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:document.getElementById('device-name').value})});
if(!response.ok)throw Error(response.status===409?'Another WiFi operation is running.':'Could not save the device name.');
const body=await response.json();status.textContent=body.restarting?`Saved as ${body.name}. Restarting.`:`Already named ${body.name}.`;
}catch(error){status.textContent=error.message;}});
</script></body></html>)html";

#endif
