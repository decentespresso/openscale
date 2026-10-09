(() => {
  const form = document.getElementById('wifi-form');
  if (!form || form.dataset.wifiSetup) return;
  form.dataset.wifiSetup = 'legacy';
  const ssidInput = document.getElementById('ssid');
  const passwordInput = document.getElementById('password');
  const resetButton = document.getElementById('reset-wifi-button');
  const resetDialog = document.getElementById('wifi-reset-dialog');
  const showPassword = document.getElementById('show-password');
  const status = document.getElementById('wifi-status');
  let busy = false;
  document.querySelector('.wifi-network-tools').hidden = true;
  document.getElementById('wifi-current').textContent = 'Connection status unavailable';
  const trimEdge = value => value.replace(/^[\s\u0085\p{Default_Ignorable_Code_Point}]+|[\s\u0085\p{Default_Ignorable_Code_Point}]+$/gu, '');
  const bytes = value => new TextEncoder().encode(value).length;
  const invalidText = value => /[\u0000-\u001f\u007f-\u009f]/u.test(value) ||
    Array.from(value).some(character => {
      const point = character.codePointAt(0);
      return point >= 0xd800 && point <= 0xdfff;
    });

  function setBusy(value) {
    busy = value;
    document.querySelectorAll('#wifi-form input,#wifi-form button,#reset-wifi-button,#name-form input,#name-form button')
      .forEach(element => { element.disabled = value; });
    form.setAttribute('aria-busy', String(value));
  }

  async function save(ssid, pass) {
    if (busy) return;
    setBusy(true);
    status.textContent = 'Saving WiFi settings...';
    status.dataset.tone = 'neutral';
    try {
      const response = await fetch('/setup/wifi', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid, pass }), signal: AbortSignal.timeout(4000)
      });
      if (!response.ok) throw new Error('WiFi settings could not be saved.');
      passwordInput.value = '';
      status.textContent = response.status === 202
        ? 'Request accepted. The connection result is not confirmed; reopen the scale to check.'
        : ssid ? 'Settings saved. The scale is restarting. The connection result is not confirmed.'
        : 'Settings cleared. The scale is restarting in DecentScale setup mode.';
    } catch (error) {
      status.textContent = ['TypeError', 'TimeoutError', 'AbortError'].includes(error.name)
        ? 'Request not confirmed. Check the scale connection before trying again.' : error.message;
      status.dataset.tone = 'error';
      setBusy(false);
    }
  }

  form.addEventListener('submit', event => {
    event.preventDefault();
    const ssid = trimEdge(ssidInput.value);
    const pass = trimEdge(passwordInput.value);
    if (!ssid || bytes(ssid) > 32 || invalidText(ssid) || invalidText(pass) ||
        !(pass.length === 0 || (bytes(pass) >= 8 && bytes(pass) <= 63) || /^[a-f0-9]{64}$/i.test(pass))) {
      status.textContent = 'Invalid WiFi name or password.';
      status.dataset.tone = 'error';
      return;
    }
    ssidInput.value = ssid;
    passwordInput.value = pass;
    save(ssid, pass);
  });
  showPassword.addEventListener('change', () => {
    passwordInput.type = showPassword.checked ? 'text' : 'password';
  });
  resetButton.addEventListener('click', () => {
    if (busy || resetDialog.open) return;
    resetDialog.returnValue = '';
    resetDialog.showModal();
  });
  resetDialog.addEventListener('close', () => {
    if (resetDialog.returnValue === 'reset') save('', '');
  });
})();
