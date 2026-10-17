#ifndef WIFI_SETUP_PAGE_H
#define WIFI_SETUP_PAGE_H

static const char HDS_WIFI_SETUP_STYLE[] PROGMEM = R"css(
.wifi-setup{--wifi-border:var(--line,#d8dde3);--wifi-ink:var(--ink,#1f2328);--wifi-muted:var(--muted,#5f6875);color:var(--wifi-ink)}
.wifi-setup label{display:grid;gap:6px;font-size:14px;font-weight:600;min-width:0}
.wifi-setup input,.wifi-setup select,.wifi-setup button{box-sizing:border-box;font:inherit;min-width:0;max-width:100%;height:44px;border:1px solid var(--wifi-border);border-radius:6px;padding:9px 11px;background:var(--surface,#fff);color:var(--wifi-ink)}
.wifi-setup input,.wifi-setup select{width:100%}
.wifi-setup button{cursor:pointer;font-weight:600}
.wifi-setup button:disabled,.wifi-setup select:disabled{cursor:default;opacity:.55}
.wifi-setup input:focus-visible,.wifi-setup select:focus-visible,.wifi-setup button:focus-visible,.wifi-setup #wifi-check-result:focus-visible{outline:2px solid #0b7180;outline-offset:2px}
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
.wifi-setup #wifi-connect-button,.wifi-setup #wifi-network-continue{background:#0b7180;color:#fff;border-color:#0b7180;min-width:128px}
.wifi-setup #wifi-connect-button:hover,.wifi-setup #wifi-network-continue:hover{background:#09616e;border-color:#09616e}
.wifi-setup #reset-wifi-button{display:block;margin:16px 0 32px;color:var(--danger,#b42318);min-width:0}
.wifi-setup #wifi-current,.wifi-setup #wifi-status{min-height:1.5em;overflow-wrap:anywhere;unicode-bidi:plaintext;font-size:14px;line-height:1.5}
.wifi-setup #wifi-current{color:var(--wifi-muted)}
.wifi-setup #wifi-status[data-tone=error]{color:var(--danger,#b42318)}
.wifi-setup #wifi-status[data-tone=success]{color:var(--green-dark,#118544)}
.wifi-setup #wifi-status[data-tone=neutral]{color:var(--wifi-muted)}
.wifi-setup #wifi-open-scale,.wifi-setup #wifi-check-result{display:block;overflow-wrap:anywhere;margin:8px 0;font-size:14px}
.wifi-setup #wifi-network-ssid{overflow-wrap:anywhere;unicode-bidi:plaintext}
.wifi-setup [hidden]{display:none!important}
.wifi-dialog{box-sizing:border-box;width:min(92vw,460px);max-height:90vh;border:1px solid var(--wifi-border);border-radius:8px;padding:18px;background:var(--surface,#fff)}
.wifi-dialog::backdrop{background:rgba(12,25,20,.7)}
.wifi-dialog h2{margin:0 0 12px;font-size:18px;line-height:1.25}
.wifi-dialog p{margin:0 0 18px;font-size:14px;line-height:1.5}
.wifi-dialog .wifi-address{margin-bottom:18px}
.wifi-dialog-actions{display:flex;justify-content:flex-end;gap:10px}
.wifi-dialog-actions button{flex:1;font-size:14px;white-space:nowrap}
@media(max-width:360px){.wifi-dialog-actions{flex-direction:column}}
.wifi-setup #wifi-reset-confirm{background:#b42318;color:#fff;border-color:#b42318}
.wifi-setup #wifi-reset-confirm:hover{background:#922118;border-color:#922118}
@media(max-width:520px){.wifi-fields,.wifi-network-tools{grid-template-columns:minmax(0,1fr)}.wifi-setup button{width:100%}.wifi-actions{align-items:stretch}.wifi-actions label{width:100%}.wifi-setup #show-password{width:18px}}
)css";

static const char HDS_WIFI_SETUP_SCRIPT[] PROGMEM = R"js(
(() => {
  const form = document.getElementById('wifi-form');
  if (!form) return;
  form.dataset.wifiSetup = 'verified';
  const ssidInput = document.getElementById('ssid');
  const passwordInput = document.getElementById('password');
  const scanButton = document.getElementById('wifi-scan-button');
  const networkTools = scanButton.closest('.wifi-network-tools');
  const networks = document.getElementById('wifi-networks');
  const scanProgress = document.getElementById('wifi-scan-progress');
  const resetButton = document.getElementById('reset-wifi-button');
  const resetDialog = document.getElementById('wifi-reset-dialog');
  const networkDialog = document.getElementById('wifi-network-dialog');
  const networkContinue = document.getElementById('wifi-network-continue');
  networkContinue.type = 'button';
  networkContinue.textContent = 'Continue';
  const networkAddress = document.getElementById('wifi-network-address') || document.createElement('input');
  if (!networkAddress.isConnected) {
    const label = document.createElement('label');
    label.className = 'wifi-address';
    label.htmlFor = 'wifi-network-address';
    label.textContent = 'Scale IP address';
    networkAddress.id = 'wifi-network-address';
    networkAddress.inputMode = 'decimal';
    networkAddress.autocomplete = 'off';
    networkAddress.placeholder = 'IP shown on the scale';
    label.append(networkAddress);
    networkDialog.insertBefore(label, networkDialog.querySelector('form'));
  }
  networkAddress.parentElement.firstChild.textContent = 'Scale IP address';
  const networkTarget = document.getElementById('wifi-network-target') || document.createElement('select');
  if (!networkTarget.isConnected) {
    const label = document.createElement('label');
    label.className = 'wifi-address';
    label.htmlFor = 'wifi-network-target';
    label.textContent = 'Open using';
    networkTarget.id = 'wifi-network-target';
    networkTarget.append(new Option('IP address', 'ip'), new Option('Hostname', 'hostname'));
    label.append(networkTarget);
    networkDialog.insertBefore(label, networkAddress.parentElement);
  }
  const checkButton = document.getElementById('wifi-check-result');
  checkButton.textContent = 'Check connection';
  const tareButton = document.getElementById('tare-button');
  const showPassword = document.getElementById('show-password');
  const current = document.getElementById('wifi-current');
  const status = document.getElementById('wifi-status');
  const openScale = document.getElementById('wifi-open-scale');
  const networkDetails = Array.from(networkDialog.children).filter(element => element.id !== 'wifi-network-title');
  const operationView = document.createElement('div');
  operationView.id = 'wifi-operation-view';
  operationView.hidden = true;
  const operationProgress = document.createElement('p');
  operationProgress.style.textAlign = 'center';
  const operationSpinner = document.createElement('span');
  operationSpinner.className = 'wifi-spinner';
  operationSpinner.style.display = 'inline-block';
  operationSpinner.setAttribute('aria-hidden', 'true');
  operationProgress.append(operationSpinner);
  const operationActions = document.createElement('div');
  operationActions.className = 'wifi-dialog-actions';
  const retryButton = document.createElement('button');
  retryButton.id = 'wifi-retry';
  retryButton.type = 'button';
  retryButton.textContent = 'Retry';
  const closeButton = document.createElement('button');
  closeButton.type = 'button';
  closeButton.textContent = 'Close';
  closeButton.autofocus = true;
  operationActions.append(retryButton, closeButton);
  operationView.append(operationProgress, operationActions);
  networkDialog.append(operationView);
  let busy = false;
  let deviceReady = false;
  let deviceMismatch = false;
  let accessPoint = true;
  let scannedSsid = null;
  let pendingCheck = null;
  let completedCheck = null;
  let operationWatch = null;
  let connectedSsid = '';
  const query = new URLSearchParams(location.search);
  const linkedAttempt = Number(query.get('wifi_attempt')) || 0;
  let expectedDevice = query.get('wifi_device') || '';
  if (!/^[a-f0-9]{12}$/.test(expectedDevice)) expectedDevice = '';

  const trimEdge = value => value.replace(/^[\s\u0085\p{Default_Ignorable_Code_Point}]+|[\s\u0085\p{Default_Ignorable_Code_Point}]+$/gu, '');
  const bytes = value => new TextEncoder().encode(value).length;
  const invalidText = value => /[\u0000-\u001f\u007f-\u009f]/u.test(value) ||
    Array.from(value).some(character => {
      const point = character.codePointAt(0);
      return point >= 0xd800 && point <= 0xdfff;
    });

  function message(text, tone = 'neutral') {
    if (!pendingCheck?.handoff) {
      completedCheck = null;
      openScale.hidden = true;
      openScale.removeAttribute('href');
      openScale.removeAttribute('aria-haspopup');
      openScale.removeAttribute('aria-controls');
    }
    status.textContent = text;
    status.dataset.tone = tone;
    updateOperationDialog();
    if (linkedAttempt) status.scrollIntoView({ block: 'center' });
  }

  function updateOperationDialog() {
    if (operationView.hidden) return;
    const waiting = busy && !pendingCheck?.handoff && status.dataset.tone !== 'error';
    operationSpinner.hidden = !waiting;
    retryButton.hidden = waiting || pendingCheck?.handoff || status.dataset.tone === 'success' || deviceMismatch;
    retryButton.disabled = busy;
    document.getElementById('wifi-network-title').textContent = waiting ? 'Connecting WiFi' :
      pendingCheck?.handoff ? 'Connection test succeeded' : 'WiFi result';
  }

  function showOperationDialog() {
    networkDialog.dataset.view = 'operation';
    networkDialog.setAttribute('aria-describedby', 'wifi-status');
    operationView.hidden = false;
    networkDetails.forEach(element => { element.hidden = true; });
    operationActions.before(status, openScale);
    networkDialog.returnValue = '';
    updateOperationDialog();
    if (!networkDialog.open) networkDialog.showModal();
  }

  function setBusy(value) {
    busy = value || !deviceReady || deviceMismatch;
    form.querySelectorAll('input,button').forEach(element => { element.disabled = busy; });
    networkTools.hidden = accessPoint;
    scanButton.disabled = busy || accessPoint;
    resetButton.disabled = busy;
    networks.disabled = busy || accessPoint || networks.options.length <= 1;
    document.querySelectorAll('#name-form input,#name-form button,#name input,#name button')
      .forEach(element => { element.disabled = busy; });
    form.setAttribute('aria-busy', String(busy));
    updateOperationDialog();
  }

  async function request(path, body) {
    const response = await fetch(path, {
      method: body === undefined ? 'GET' : 'POST',
      cache: 'no-store',
      headers: body === undefined ? {} : {
        'Content-Type': 'application/json', 'X-HDS-Device-ID': expectedDevice
      },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(4000)
    });
    if (!response.ok) {
      const data = await response.json().catch(() => ({}));
      if (data.error === 'wifi_device_mismatch') rejectDevice();
      throw new Error(data.error || (response.status === 409 ? 'wifi_busy' :
        response.status === 400 ? 'wifi_credentials_invalid' : 'request_failed'));
    }
    return response.json();
  }

  const connectionInterrupted = error => ['TimeoutError', 'AbortError', 'TypeError'].includes(error.name);

  function updateCurrent(data) {
    accessPoint = data.access_point === true;
    networkTools.hidden = accessPoint;
    connectedSsid = data.connected && !data.access_point ? data.ssid : '';
    current.textContent = data.access_point ? `Setup network: DecentScale | ${data.ip}` :
      data.connected ? `Connected: ${data.ssid} | ${data.ip}` :
      data.credentials_saved ? `Reconnecting: ${data.ssid}` : 'No WiFi network saved';
    if (!ssidInput.value && data.credentials_saved) ssidInput.value = data.ssid;
  }

  function rejectDevice() {
    deviceMismatch = true;
    pendingCheck = null;
    message('This address reached a different scale. The WiFi result is not confirmed. Open the IP address shown on the original scale.', 'error');
    current.textContent = 'Different scale';
    setBusy(true);
    if (tareButton) tareButton.disabled = true;
    checkButton.hidden = true;
    if (networkDialog.open && networkDialog.dataset.view !== 'operation') networkDialog.close();
  }

  function matchesDevice(data) {
    if (!deviceMismatch && /^[a-f0-9]{12}$/.test(data.device_id) &&
        (!expectedDevice || data.device_id === expectedDevice)) {
      expectedDevice = data.device_id;
      form.dataset.wifiDevice = expectedDevice;
      return true;
    }
    rejectDevice();
    return false;
  }

  function showNetworkDialog() {
    const operation = pendingCheck || completedCheck;
    if (!operation || (networkDialog.open && networkDialog.dataset.view !== 'operation')) return;
    const completed = operation === completedCheck;
    operationView.hidden = true;
    networkDetails.forEach(element => { element.hidden = false; });
    checkButton.before(status, openScale);
    networkDialog.dataset.view = 'instructions';
    networkDialog.setAttribute('aria-describedby', 'wifi-network-description wifi-network-recovery');
    document.getElementById('wifi-network-title').textContent = completed ? 'WiFi result' :
      operation.handoff ? 'Connection test succeeded' : 'WiFi change started';
    const ssid = document.getElementById('wifi-network-ssid');
    ssid.textContent = operation.kind === 'reset'
      ? 'DecentScale' : operation.ssid;
    document.getElementById('wifi-network-description').replaceChildren(
      document.createTextNode(completed ? operation.kind === 'reset' ? 'Setup network: ' : 'Connected to ' : 'The scale is switching to '), ssid,
      document.createTextNode(completed ? '. Select Continue to open the scale.' :
        '. Connect this phone or computer to that network, then select Continue.'));
    document.getElementById('wifi-network-recovery').textContent = completed
      ? 'If its IP changes, use the address on the scale.' : operation.kind === 'reset'
        ? 'Setup password: 12345678. The scale will use 192.168.1.1.'
        : 'If the switch fails, the scale returns to the previous network. If its IP changes, use the address on the scale.';
    const hostname = operationHostname(operation);
    const hostnameOption = networkTarget.querySelector('[value="hostname"]');
    hostnameOption.textContent = hostname ? `Hostname (${hostname})` : 'Hostname';
    hostnameOption.disabled = !hostname;
    networkTarget.value = 'ip';
    networkAddress.value = completed ? operation.ip : operation.kind === 'reset' ? '192.168.1.1' :
      validIp(operation.tested_ip) ? operation.tested_ip : '';
    updateNetworkTarget();
    networkDialog.returnValue = '';
    if (!networkDialog.open) networkDialog.showModal();
    networkContinue.focus();
  }

  function continueCheck() {
    const operation = pendingCheck || completedCheck;
    if (!operation) return;
    const usingHostname = networkTarget.value === 'hostname';
    const host = usingHostname ? operationHostname(operation) : networkAddress.value.trim();
    networkTarget.setCustomValidity(usingHostname && !host ? 'Select IP address.' : '');
    networkAddress.setCustomValidity(!usingHostname && !validIp(host)
      ? 'Enter the IPv4 address shown on the scale.' : '');
    if (!networkTarget.reportValidity() || !networkAddress.reportValidity()) return;
    const url = operationUrl(host, operation.operation_id);
    if (networkDialog.open) networkDialog.close('checking');
    location.assign(url.href);
  }

  function updateNetworkTarget() {
    const usingHostname = networkTarget.value === 'hostname';
    networkAddress.parentElement.hidden = usingHostname;
    networkAddress.disabled = usingHostname;
    networkTarget.setCustomValidity('');
    networkAddress.setCustomValidity('');
  }

  function operationHostname(operation) {
    return operation.mdns_available &&
      /^[a-z0-9](?:[a-z0-9-]{0,22}[a-z0-9])?$/.test(operation.mdns_name)
      ? `${operation.mdns_name}.local` : '';
  }

  function validIp(ip) {
    return typeof ip === 'string' && /^(?:\d{1,3}\.){3}\d{1,3}$/.test(ip) &&
      ip !== '0.0.0.0' && ip.split('.').every(part => Number(part) <= 255 && String(Number(part)) === part);
  }

  function operationUrl(host, id) {
    const url = new URL(`http://${host}/setup/wifi/continue`);
    url.searchParams.set('wifi_device', expectedDevice);
    url.searchParams.set('wifi_attempt', id);
    return url;
  }

  function showVerifiedAddress(data) {
    if (!expectedDevice || data.device_id !== expectedDevice ||
        !validIp(data.ip)) return;
    const url = operationUrl(data.ip, data.operation_id);
    completedCheck = { ...data, kind: data.access_point ? 'reset' : 'switch' };
    openScale.href = url.href;
    openScale.textContent = `Open scale at ${data.ip}`;
    openScale.setAttribute('aria-haspopup', 'dialog');
    openScale.setAttribute('aria-controls', 'wifi-network-dialog');
    openScale.hidden = false;
  }

  function finishOperation(data) {
    pendingCheck = null;
    checkButton.hidden = true;
    if (networkDialog.open && networkDialog.dataset.view !== 'operation') networkDialog.close('checking');
    setBusy(data.busy === true);
  }

  async function acknowledgeTest(data) {
    if (data.error || !validIp(data.tested_ip) ||
        data.requested_ssid !== pendingCheck?.ssid || !(data.connected || data.access_point)) {
      finishOperation(data);
      message('The connection test result is not confirmed.');
      return false;
    }
    pendingCheck = { ...pendingCheck, ...data, ssid: data.requested_ssid, handoff: true };
    message(`Connection test succeeded for ${data.requested_ssid}. Starting the final switch...`, 'success');
    try {
      const accepted = await request('/setup/wifi/activate', { operation_id: data.operation_id });
      if (!matchesDevice(accepted)) return false;
      if (accepted.operation_id !== data.operation_id || accepted.restarting !== false) {
        finishOperation(data);
        message('The final WiFi switch is not confirmed.', 'error');
        return false;
      }
      message(`Connection test succeeded for ${data.requested_ssid}. The scale is switching to that network. Open the scale link to continue.`, 'success');
    } catch (error) {
      if (deviceMismatch) return false;
      if (!connectionInterrupted(error)) {
        finishOperation(data);
        message('The final WiFi switch could not start. Its result is not confirmed. Reload this page before testing again.', 'error');
        return false;
      }
      message(`Connection test succeeded for ${data.requested_ssid}, but the final switch request was not confirmed. Open the scale link to check the result.`);
    }
    openScale.href = operationUrl(data.tested_ip, data.operation_id).href;
    openScale.textContent = `Open scale at http://${data.tested_ip}`;
    openScale.setAttribute('aria-haspopup', 'dialog');
    openScale.setAttribute('aria-controls', 'wifi-network-dialog');
    openScale.hidden = false;
    openScale.scrollIntoView({ block: 'nearest' });
    return true;
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

  function waitForOperation(id, kind) {
    if (!operationWatch) operationWatch = pollOperation(id, kind).finally(() => { operationWatch = null; });
    return operationWatch;
  }

  async function pollOperation(id, kind) {
    let started = Date.now();
    while (Date.now() - started < 60000) {
      await new Promise(resolve => setTimeout(resolve, 1000));
      let data;
      try { data = await request('/setup/wifi/status'); }
      catch {
        if (deviceMismatch) return;
        current.textContent = 'Waiting for the scale to reconnect';
        message(pendingCheck?.handoff
          ? `Connection test succeeded for ${pendingCheck.ssid}. The final switch is not yet confirmed. Open the scale link, join that network, then Continue.`
          : kind === 'reset' ? 'Contact with the scale was lost while resetting. Waiting for the result...' :
            'Testing the new network. Stay on the original network; the scale will return to report the result. Waiting for the result...');
        continue;
      }
      if (!matchesDevice(data)) return;
      if (['ready', 'succeeded', 'failed'].includes(data.state)) updateCurrent(data);
      else current.textContent = data.state === 'returning' || data.state === 'restoring'
        ? 'Returning to the original network' : `Checking connection to ${data.requested_ssid}`;
      if (data.operation_id !== id) {
        finishOperation(data);
        message('This WiFi request is no longer current. Its result is not confirmed.');
        return;
      }
      if (data.state === 'ready') {
        if (pendingCheck?.handoff) {
          if (data.busy) continue;
          finishOperation(data);
          message('The final WiFi switch did not start. Stay on the original network and retry.', 'error');
          return;
        }
        if (!await acknowledgeTest(data)) return;
        started = Date.now();
        continue;
      }
      if (data.state === 'succeeded') {
        finishOperation(data);
        if (!data.error && kind === 'reset' && data.access_point && !data.credentials_saved) {
          message('WiFi settings cleared. Setup network: DecentScale.', 'success');
          showVerifiedAddress(data);
        } else if (!data.error && data.connected && data.ssid === data.requested_ssid && data.credentials_saved) {
          message(`Connected to ${data.ssid}. WiFi settings saved.`, 'success');
          showVerifiedAddress(data);
        } else {
          message('The connection result is not confirmed.');
        }
        return;
      }
      if (data.state === 'failed') {
        const recovery = data.connected ? `Back on ${data.ssid}. Your previous WiFi settings were kept.` :
          data.access_point ? `${data.credentials_saved ? 'The previous network is unavailable. ' : ''}Connect to DecentScale (password 12345678), then open 192.168.1.1.` :
          'Your previous WiFi settings were kept.';
        finishOperation(data);
        message(`${failureText(data.error)} ${recovery}`, 'error');
        return;
      }
      if (kind === 'remote' && !data.busy) { setBusy(false); return; }
      if (pendingCheck?.handoff) continue;
      message(data.state === 'returning' ? 'Connection test succeeded. Returning to the original network to report the result...' :
        data.state === 'restoring' ? 'The new WiFi settings did not work. Reconnecting to the previous network...' :
        data.state === 'verifying' ? 'Connected with the new settings. Checking the connection...' :
          pendingCheck?.testOnly ? 'Testing the new network. Stay on the original network; the scale will return to report the result.' : 'Testing the new WiFi settings...');
    }
    message(pendingCheck?.handoff
      ? `Connection test succeeded for ${pendingCheck.ssid}. The final switch is not confirmed here. Open the scale link and join that network to verify it. Failed settings restore the previous network automatically.`
      : 'Could not confirm the result. Stay on the original network and reload this page to check again. Failed settings restore the previous network automatically.');
    checkButton.hidden = pendingCheck?.kind !== 'reset';
    setBusy(false);
  }

  async function changeWifi(ssid, pass, kind, scanned = false) {
    if (busy) return;
    setBusy(true);
    pendingCheck = null;
    checkButton.hidden = true;
    message(kind === 'reset' ? 'Clearing WiFi settings...' : 'Submitting WiFi connection test...');
    if (kind === 'switch') showOperationDialog();
    let accepted;
    const testOnly = kind === 'switch' && ssid !== connectedSsid;
    const credentials = { ssid, pass, ...(scanned ? { scanned: true } : {}), ...(testOnly ? { test_only: true } : {}) };
    try { accepted = await request('/setup/wifi', credentials); }
    catch (error) {
      if (deviceMismatch) return;
      message(error.message === 'wifi_busy' || error.message === 'wifi_credentials_invalid' ?
        failureText(error.message) : 'Request not confirmed. Check the scale connection before trying again.', 'error');
      setBusy(false);
      return;
    }
    if (!matchesDevice(accepted)) return;
    passwordInput.value = '';
    if (!Number.isInteger(accepted.operation_id) || accepted.restarting !== false ||
        (testOnly && accepted.test_only !== true)) {
      message('This firmware does not support a verified direct WiFi change.', 'error');
      setBusy(false);
      return;
    }
    pendingCheck = { ...accepted, kind, testOnly };
    current.textContent = kind === 'reset' ? 'Resetting WiFi' : `Checking connection to ${accepted.ssid}`;
    message(kind === 'reset' ? 'Clearing settings. Reconnect to DecentScale with password 12345678.' :
      testOnly ? `Testing ${accepted.ssid}. Stay on the original network while the scale tests the connection and returns with the result. Previous settings stay saved.` :
        `Checking ${accepted.ssid}. Previous settings stay saved until success.`);
    checkButton.hidden = kind !== 'reset';
    waitForOperation(accepted.operation_id, kind);
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
  checkButton.addEventListener('click', event => {
    event.preventDefault();
    showNetworkDialog();
  });
  networkTarget.addEventListener('change', updateNetworkTarget);
  networkContinue.addEventListener('click', continueCheck);
  closeButton.addEventListener('click', () => networkDialog.close());
  retryButton.addEventListener('click', () => {
    networkDialog.close('retry');
    passwordInput.focus();
  });
  openScale.addEventListener('click', event => {
    if (!pendingCheck?.handoff && !completedCheck) return;
    event.preventDefault();
    showNetworkDialog();
  });
  networkDialog.addEventListener('close', () => {
    const wasOperation = networkDialog.dataset.view === 'operation';
    checkButton.before(status, openScale);
    operationView.hidden = true;
    networkDetails.forEach(element => { element.hidden = false; });
    delete networkDialog.dataset.view;
    if (wasOperation) return;
    if (!pendingCheck || networkDialog.returnValue === 'checking') return;
    message(pendingCheck.handoff
      ? `Connection test succeeded for ${pendingCheck.ssid}. The final switch continues on the scale. Open the scale link when you are ready to join that network.`
      : 'The WiFi operation continues on the scale. Its result is not confirmed. Check again when this phone or computer is on the scale\'s network.');
    checkButton.hidden = pendingCheck.kind !== 'reset';
  });

  scanButton.addEventListener('click', async () => {
    if (busy || accessPoint) return;
    setBusy(true);
    scannedSsid = null;
    networks.replaceChildren();
    networks.hidden = true;
    scanProgress.hidden = false;
    message('Searching for networks...');
    try {
      const accepted = await request('/setup/wifi/scan', {});
      if (!matchesDevice(accepted)) return;
      const started = Date.now();
      while (Date.now() - started < 15000) {
        await new Promise(resolve => setTimeout(resolve, 1000));
        let data;
        try { data = await request('/setup/wifi/scan'); }
        catch (error) {
          if (!connectionInterrupted(error)) throw error;
          continue;
        }
        if (!matchesDevice(data)) return;
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
      if (deviceMismatch) return;
      message(error.message === 'wifi_busy' ? failureText(error.message) : 'Network search failed. You can enter a network name.', 'error');
    } finally {
      scanProgress.hidden = true;
      networks.hidden = false;
      setBusy(false);
    }
  });

  setBusy(false);
  if (tareButton) tareButton.disabled = true;
  const nameForm = document.getElementById('name-form') || document.getElementById('name');
  if (nameForm) nameForm.addEventListener('submit', event => {
    if (!busy) return;
    event.preventDefault();
    event.stopImmediatePropagation();
  }, true);
  request('/setup/wifi/status').then(data => {
    if (!matchesDevice(data)) return;
    deviceReady = true;
    if (tareButton) tareButton.disabled = false;
    updateCurrent(data);
    setBusy(data.busy === true);
    if (linkedAttempt && data.operation_id !== linkedAttempt) {
      message('This WiFi request is no longer current. Its result is not confirmed.');
      setBusy(data.busy === true);
      return;
    }
    if ((linkedAttempt && data.operation_id === linkedAttempt) || data.busy || data.state === 'ready') {
      const kind = linkedAttempt && !data.requested_ssid ? 'reset' : linkedAttempt ? 'switch' : 'remote';
      pendingCheck = { ...data, ssid: data.requested_ssid, kind, testOnly: data.state === 'ready' };
      setBusy(true);
      waitForOperation(data.operation_id, kind);
    }
  }).catch(() => {
    current.textContent = 'Connection status unavailable';
    if (linkedAttempt) message('Could not reach the scale. The WiFi result is not confirmed. Check the network and the IP address shown on the scale.');
  });
})();
)js";

static const char HDS_WIFI_SETUP_PAGE[] PROGMEM = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Half Decent Scale setup</title><link rel="stylesheet" href="/setup/wifi.css">
<style>body{font:16px system-ui,sans-serif;max-width:760px;margin:32px auto;padding:0 16px;color:#1f2328}h1{font-size:25px}h2{font-size:18px}#name{display:grid;gap:8px;margin-top:24px}#name input,#name button{font:inherit;height:44px;padding:9px 11px;border:1px solid #d8dde3;border-radius:6px;background:#fff}#name-status{overflow-wrap:anywhere}</style>
<script src="/setup/wifi.js" defer></script></head><body>
<h1>Half Decent Scale setup</h1><section class="wifi-setup" id="wifi-popup" aria-labelledby="wifi-title">
<h2 id="wifi-title">WiFi</h2><p id="wifi-current" role="status">Checking connection...</p>
<div class="wifi-network-tools"><button id="wifi-scan-button" type="button" disabled>Find networks</button><label for="wifi-networks">Nearby networks<span class="wifi-network-select"><select id="wifi-networks" disabled><option value="">Select a network</option></select><span id="wifi-scan-progress" class="wifi-scan-progress" role="status" hidden><span class="wifi-spinner" aria-hidden="true"></span>Searching...</span></span></label></div>
<form id="wifi-form" autocomplete="off"><div class="wifi-fields"><label for="ssid">Network name<input id="ssid" autocomplete="off" autocapitalize="none" spellcheck="false" required disabled></label><label for="password">Password<input id="password" type="password" autocomplete="off" autocapitalize="none" spellcheck="false" disabled></label></div>
<div class="wifi-actions"><label for="show-password"><input id="show-password" type="checkbox" disabled>Show password</label><button id="wifi-connect-button" type="submit" disabled>Connect</button></div></form>
<p id="wifi-status" role="status" aria-live="polite" data-tone="neutral"></p><a id="wifi-open-scale" hidden></a><a id="wifi-check-result" href="#wifi-network-dialog" aria-haspopup="dialog" aria-controls="wifi-network-dialog" hidden>Check connection</a><button id="reset-wifi-button" type="button" disabled>Reset WiFi settings</button></section>
<form id="name"><label for="device-name">Device name</label><input id="device-name" name="name" placeholder="hds" maxlength="24" required disabled><button disabled>Rename</button></form><p id="name-status" role="status"></p>
<dialog id="wifi-reset-dialog" class="wifi-setup wifi-dialog" aria-labelledby="wifi-reset-title" aria-describedby="wifi-reset-description">
<h2 id="wifi-reset-title">Reset WiFi settings?</h2><p id="wifi-reset-description">The scale will disconnect and open DecentScale setup. Other settings stay unchanged.</p>
<form method="dialog" class="wifi-dialog-actions"><button value="cancel" autofocus>Cancel</button><button id="wifi-reset-confirm" value="reset">Reset WiFi</button></form></dialog>
<dialog id="wifi-network-dialog" class="wifi-setup wifi-dialog" aria-labelledby="wifi-network-title" aria-describedby="wifi-network-description wifi-network-recovery">
<h2 id="wifi-network-title">WiFi change started</h2><p id="wifi-network-description">The scale is trying to join <strong id="wifi-network-ssid"></strong>. Connect this phone or computer to that network, then select Continue.</p><p id="wifi-network-recovery"></p>
<label class="wifi-address" for="wifi-network-target">Open using<select id="wifi-network-target"><option value="ip">IP address</option><option value="hostname">Hostname</option></select></label>
<label class="wifi-address" for="wifi-network-address">Scale IP address<input id="wifi-network-address" inputmode="decimal" autocomplete="off" placeholder="IP shown on the scale"></label>
<form method="dialog" class="wifi-dialog-actions"><button value="later">Check later</button><button id="wifi-network-continue" type="button" autofocus>Continue</button></form></dialog>
<script>
document.getElementById('name').addEventListener('submit',async event=>{
event.preventDefault();const status=document.getElementById('name-status');
if(!document.getElementById('wifi-form').dataset.wifiDevice){status.textContent='Checking scale identity...';return;}
try{const response=await fetch('/setup/name',{method:'POST',headers:{'Content-Type':'application/json','X-HDS-Device-ID':document.getElementById('wifi-form').dataset.wifiDevice},body:JSON.stringify({name:document.getElementById('device-name').value})});
if(response.status===409&&(await response.clone().json()).error==='wifi_device_mismatch')throw Error('This address reached a different scale. Open the original scale before renaming it.');
if(!response.ok)throw Error(response.status===409?'Another WiFi operation is running.':'Could not save the device name.');
const body=await response.json();status.textContent=body.restarting?`Saved as ${body.name}. Restarting.`:`Already named ${body.name}.`;
}catch(error){status.textContent=error.message;}});
</script></body></html>)html";

#endif
