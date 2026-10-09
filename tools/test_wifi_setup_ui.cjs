const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE_PATH || 'playwright');

const root = path.resolve(__dirname, '..');
const header = fs.readFileSync(path.join(root, 'include/wifi_setup_page.h'), 'utf8');
const extract = (name, delimiter) => header.match(
  new RegExp(`${name}[\\s\\S]*?R"${delimiter}\\(([\\s\\S]*?)\\)${delimiter}";`)
)[1];
const script = extract('HDS_WIFI_SETUP_SCRIPT', 'js');
const style = extract('HDS_WIFI_SETUP_STYLE', 'css');
const inlinePage = extract('HDS_WIFI_SETUP_PAGE', 'html');
const mainPage = fs.readFileSync(path.join(root, 'plugins/default-web-apps/assets/index.html'), 'utf8');
const initialStatus = {
  operation_id: 0, state: 'idle', error: '', requested_ssid: '', ssid: 'Old network',
  ip: '192.168.50.30', connected: true, credentials_saved: true, access_point: false,
  busy: false, mdns_name: 'hds', mdns_available: true, device_id: 'ccba973327f0'
};

async function openPage(browser, html, viewport, legacy = false, startUrl = 'http://hds.local') {
  const context = await browser.newContext({ viewport });
  const page = await context.newPage();
  let posts = [];
  let variant = 'success';
  let scanVariant = 'success';
  let scanPolls = 0;
  let scanPosts = 0;
  let polls = 0;
  let unsupportedRequests = 0;
  let state = { ...initialStatus };
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('dialog', async dialog => {
    errors.push('Unexpected browser dialog');
    await dialog.dismiss();
  });
  await page.addInitScript(() => {
    const original = window.setTimeout;
    window.setTimeout = (callback, milliseconds, ...arguments) =>
      original(callback, milliseconds === 1000 ? 20 : milliseconds, ...arguments);
  });
  await page.routeWebSocket('**/snapshot', socket => {
    socket.onMessage(() => socket.send(JSON.stringify({
      type: 'status', mdns_name: 'hds', wifi_on_boot: true,
      wifi_credentials_saved: true, wifi_active: true, wifi_connected: true,
      wifi_mode: 'sta', grams: 0
    })));
  });
  await context.route(/^http:\/\/(?:hds\.local|192\.168\.50\.30)\//, async route => {
    const request = route.request();
    const pathname = new URL(request.url()).pathname;
    const json = data => route.fulfill({ contentType: 'application/json', body: JSON.stringify(data) });
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: html });
    if (pathname === '/setup/wifi/continue') {
      if (variant === 'handoff-legacy-device') return route.fulfill({ status: 404, body: 'Not found' });
      if (variant === 'handoff-wrong-device') return route.fulfill({ status: 409,
        body: 'This address reached a different scale. The WiFi result is not confirmed.' });
      return route.fulfill({ contentType: 'text/html', body: inlinePage });
    }
    if (pathname === '/setup/wifi.js') return route.fulfill({ status: legacy ? 404 : 200,
      contentType: 'application/javascript', body: legacy ? '' : script });
    if (pathname === '/setup/wifi.css') return route.fulfill({ status: legacy ? 404 : 200,
      contentType: 'text/css', body: legacy ? '' : style });
    if (['/shared/theme.js', '/shared/theme.css', '/shared/reconnecting-websocket.js',
      '/shared/wifi-legacy.js', '/shared/wifi-legacy.css'].includes(pathname)) {
      return route.fulfill({ contentType: pathname.endsWith('.css') ? 'text/css' : 'application/javascript',
        body: fs.readFileSync(path.join(root, 'plugins/default-web-apps/assets', pathname.slice(1)), 'utf8') });
    }
    if (pathname === '/webapps.json') return json({ schema: 1, apps: [] });
    if (legacy && ['/setup/wifi/scan', '/setup/wifi/status'].includes(pathname)) {
      unsupportedRequests += 1;
      return route.fulfill({ status: 404 });
    }
    if (pathname === '/setup/wifi/scan') {
      if (request.method() === 'POST') {
        scanPosts += 1;
        scanPolls = 0;
        return json({ operation_id: 7, restarting: false });
      }
      scanPolls += 1;
      if (scanVariant === 'pending') return json({ state: 'scanning', networks: [] });
      if (scanVariant === 'interrupted-poll' && scanPolls === 1) return route.abort('failed');
      if (scanVariant === 'timed-out-poll' && scanPolls === 1) {
        await new Promise(resolve => setTimeout(resolve, 4200));
      }
      if (scanVariant === 'failure') return json({ state: 'failed', networks: [] });
      return json({ state: 'complete', networks: [
        { ssid: 'New network', rssi: -42, secure: true },
        { ssid: ' Cafe \u200b ', rssi: -50, secure: true },
        { ssid: '<img src=x onerror=alert(1)>', rssi: -60, secure: false }
      ] });
    }
    if (pathname === '/setup/wifi' && request.method() === 'POST') {
      const data = request.postDataJSON();
      posts = [...posts, data];
      if (legacy) {
        if (variant === 'interrupted') return route.abort('failed');
        if (variant === 'failure') return route.fulfill({ status: 500,
          contentType: 'application/json', body: JSON.stringify({ error: 'wifi_credentials_save_failed' }) });
        return route.fulfill({ status: variant === 'accepted' ? 202 : 200, body: '' });
      }
      polls = 0;
      state = { ...initialStatus, operation_id: 9, state: 'testing', busy: true,
        requested_ssid: data.ssid, connected: false };
      return route.fulfill({ status: 202, contentType: 'application/json', body: JSON.stringify({
        operation_id: 9, state: 'queued', ssid: data.ssid, restarting: false,
        mdns_name: 'hds', mdns_available: true, device_id: initialStatus.device_id
      }) });
    }
    if (pathname === '/setup/wifi/status') {
      if (state.operation_id === 9) {
        polls += 1;
        if (variant.startsWith('handoff') && new URL(request.url()).hostname === '192.168.50.30') return route.abort('failed');
        if (variant === 'handoff-wrong-device' || variant === 'wrong-device') return json({
          ...state, state: 'succeeded', device_id: 'ffeeddccbbaa', connected: true, busy: false });
        if (variant.startsWith('interrupted') && polls <= 2) return route.abort('failed');
        if (variant === 'wrong-id') return json({ ...initialStatus, operation_id: 123, state: 'succeeded' });
        if (polls >= 2) {
          state = ['failure', 'interrupted-failure', 'ap-failure'].includes(variant) ? {
            ...state, state: 'failed', error: 'authentication_failed', connected: variant !== 'ap-failure',
            access_point: variant === 'ap-failure', ip: variant === 'ap-failure' ? '192.168.1.1' : state.ip,
            busy: false } : !state.requested_ssid ? { ...state,
            state: 'succeeded', connected: false, credentials_saved: false,
            access_point: true, ssid: '', ip: '192.168.1.1', busy: false } : { ...state,
            state: 'succeeded', connected: true, ssid: state.requested_ssid, busy: false };
        }
      }
      return json(state);
    }
    return route.fulfill({ status: 404 });
  });
  await page.goto(startUrl);
  await page.waitForFunction(legacy
    ? () => document.getElementById('wifi-form').dataset.wifiSetup === 'legacy'
    : () => document.getElementById('wifi-current').textContent.includes('Old network'));
  return { context, page, posts: () => posts, variant: value => { variant = value; },
    scanVariant: value => { scanVariant = value; }, scanPosts: () => scanPosts,
    unsupportedRequests: () => unsupportedRequests, operationPolls: () => polls, errors };
}

async function confirmNetwork(page, ssid) {
  await page.locator('#wifi-network-dialog').waitFor({ state: 'visible' });
  assert.equal(await page.locator('#wifi-network-ssid').textContent(), ssid);
  assert.notEqual(await page.locator('#wifi-status').getAttribute('data-tone'), 'success');
  await page.getByRole('button', { name: 'Continue', exact: true }).click();
}

async function run(browser, html, label, viewport) {
  const session = await openPage(browser, html, viewport);
  const { page } = session;
  assert.equal(await page.locator('#wifi-form').getAttribute('data-wifi-setup'), 'verified');
  assert.equal(await page.locator('#wifi-check-result').isVisible(), false);
  session.scanVariant('pending');
  const beforeScan = await page.locator('#wifi-networks').evaluate(element => {
    const rect = element.getBoundingClientRect();
    return { width: rect.width, height: rect.height, top: rect.top + scrollY };
  });
  await page.getByRole('button', { name: 'Find networks' }).click();
  assert.equal(await page.locator('#wifi-networks').isVisible(), false);
  assert.equal(await page.locator('#wifi-scan-progress').isVisible(), true);
  const whileScanning = await page.locator('#wifi-scan-progress').evaluate(element => {
    const rect = element.getBoundingClientRect();
    return { width: rect.width, height: rect.height, top: rect.top + scrollY };
  });
  assert.deepEqual(whileScanning, beforeScan);
  assert.equal(await page.locator('.wifi-spinner').evaluate(element =>
    getComputedStyle(element).animationName), 'wifi-spin');
  await page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-scanning-${label}-${viewport.width}.png`) });
  session.scanVariant('success');
  await page.waitForFunction(() => document.getElementById('wifi-networks').options.length === 4);
  assert.equal(await page.locator('#wifi-scan-progress').isVisible(), false);
  assert.equal(await page.locator('#wifi-networks').isVisible(), true);
  assert.match(await page.locator('#wifi-networks').textContent(), /<img src=x onerror=alert\(1\)>/);
  assert.equal(await page.locator('#wifi-popup img').count(), 0);
  for (const variant of ['interrupted-poll', 'timed-out-poll']) {
    session.scanVariant(variant);
    await page.getByRole('button', { name: 'Find networks' }).click();
    await page.waitForFunction(() => document.getElementById('wifi-status').textContent === '3 networks found.');
    assert.equal(await page.locator('#wifi-networks').isEnabled(), true);
  }
  assert.equal(session.scanPosts(), 3);
  session.scanVariant('failure');
  await page.getByRole('button', { name: 'Find networks' }).click();
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('Network search failed'));
  assert.equal(await page.locator('#wifi-scan-button').isEnabled(), true);
  assert.equal(await page.locator('#wifi-scan-progress').isVisible(), false);
  assert.equal(await page.locator('#wifi-networks').isVisible(), true);
  assert.equal(await page.locator('#wifi-networks').isEnabled(), false);
  session.scanVariant('success');
  await page.getByRole('button', { name: 'Find networks' }).click();
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent === '3 networks found.');
  await page.locator('#wifi-networks').selectOption('New network');
  assert.equal(await page.locator('#ssid').inputValue(), 'New network');
  await page.locator('#wifi-connect-button').hover();
  await page.waitForFunction(() => getComputedStyle(document.getElementById('wifi-connect-button'))
    .backgroundColor === 'rgb(9, 97, 110)');
  await page.locator('#show-password').check();
  assert.equal(await page.locator('#password').getAttribute('type'), 'text');
  await page.locator('#show-password').uncheck();
  assert.equal(await page.locator('#password').getAttribute('type'), 'password');
  await page.locator('#ssid').fill(' \u00a0\u200bNew network\ufeff ');
  await page.locator('#password').fill('\u0085\u2060 pass word \u00a0');
  await page.locator('#wifi-form').evaluate(form => {
    form.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
    form.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
  });
  await page.locator('#wifi-network-dialog').waitFor({ state: 'visible' });
  assert.equal(session.operationPolls(), 0);
  assert.equal(await page.evaluate(() => document.activeElement.textContent), 'Continue');
  await page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-network-${label}-${viewport.width}.png`) });
  await page.getByRole('button', { name: 'Check later', exact: true }).click();
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('operation continues'));
  assert.match(await page.locator('#wifi-status').textContent(), /operation continues.*not confirmed/);
  assert.equal(await page.locator('#wifi-connect-button').isEnabled(), false);
  assert.equal(session.posts().length, 1);
  await page.getByRole('button', { name: 'Check WiFi result' }).click();
  await page.keyboard.press('Escape');
  assert.equal(await page.locator('#wifi-network-dialog').isVisible(), false);
  await page.locator('#wifi-check-result').waitFor({ state: 'visible' });
  assert.equal(session.operationPolls(), 0);
  await page.getByRole('button', { name: 'Check WiFi result' }).click();
  await confirmNetwork(page, 'New network');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('WiFi settings saved'));
  assert.equal(await page.locator('#wifi-check-result').isVisible(), false);
  assert.deepEqual(session.posts(), [{ ssid: 'New network', pass: 'pass word' }]);
  assert.equal(await page.locator('#password').inputValue(), '');
  assert.equal(await page.evaluate(() => localStorage.length), 0);

  await page.locator('#ssid').fill(' \u200b ');
  await page.locator('#password').fill('password');
  await page.getByRole('button', { name: 'Connect', exact: true }).click();
  await page.waitForFunction(() => document.getElementById('wifi-status').dataset.tone === 'error');
  assert.equal(session.posts().length, 1);

  await page.locator('#wifi-networks').selectOption(' Cafe \u200b ');
  await page.locator('#password').fill(' pass word \ufeff ');
  await page.getByRole('button', { name: 'Connect', exact: true }).click();
  await confirmNetwork(page, ' Cafe \u200b ');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('WiFi settings saved'));
  assert.deepEqual(session.posts().at(-1), { ssid: ' Cafe \u200b ', pass: ' pass word \ufeff ', scanned: true });
  await page.locator('#ssid').fill(' Cafe \u200b ');
  await page.locator('#password').fill(' pass word \ufeff ');
  await page.getByRole('button', { name: 'Connect', exact: true }).click();
  await confirmNetwork(page, 'Cafe');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('WiFi settings saved'));
  assert.deepEqual(session.posts().at(-1), { ssid: 'Cafe', pass: 'pass word' });

  session.variant('interrupted-failure');
  await page.locator('#ssid').fill('Wrong password network');
  await page.locator('#password').fill('password');
  await page.getByRole('button', { name: 'Connect', exact: true }).click();
  await confirmNetwork(page, 'Wrong password network');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('Waiting for the result') &&
    document.getElementById('wifi-current').textContent === 'Waiting for the scale to reconnect');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('Back on Old network.'));
  assert.match(await page.locator('#wifi-status').textContent(), /password was not accepted.*previous WiFi settings were kept/);
  assert.equal(await page.locator('#wifi-reconnect').count(), 0);
  await page.locator('#wifi-popup').scrollIntoViewIfNeeded();
  await page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-restored-${label}-${viewport.width}.png`) });

  session.variant('ap-failure');
  await page.locator('#password').fill('password');
  await page.getByRole('button', { name: 'Connect', exact: true }).click();
  await confirmNetwork(page, 'Wrong password network');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('The previous network is unavailable'));
  assert.match(await page.locator('#wifi-status').textContent(), /Connect to DecentScale \(password 12345678\), then open 192\.168\.1\.1/);

  session.variant('interrupted');
  await page.locator('#ssid').fill('New network');
  await page.locator('#password').fill('password');
  await page.getByRole('button', { name: 'Connect', exact: true }).click();
  await confirmNetwork(page, 'New network');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('Waiting for the result'));
  assert.notEqual(await page.locator('#wifi-status').getAttribute('data-tone'), 'success');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('WiFi settings saved'));

  session.variant('wrong-id');
  await page.locator('#password').fill('password');
  await page.getByRole('button', { name: 'Connect', exact: true }).click();
  await confirmNetwork(page, 'New network');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('no longer current'));
  assert.notEqual(await page.locator('#wifi-status').getAttribute('data-tone'), 'success');

  const beforeReset = session.posts().length;
  await page.getByRole('button', { name: 'Reset WiFi settings' }).click();
  assert.equal(await page.getByRole('dialog').isVisible(), true);
  assert.equal(await page.evaluate(() => document.activeElement.textContent), 'Cancel');
  await page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-confirm-${label}-${viewport.width}.png`) });
  await page.getByRole('button', { name: 'Cancel', exact: true }).click();
  assert.equal(await page.getByRole('dialog').isVisible(), false);
  assert.equal(session.posts().length, beforeReset);
  await page.getByRole('button', { name: 'Reset WiFi settings' }).click();
  await page.keyboard.press('Escape');
  assert.equal(await page.getByRole('dialog').isVisible(), false);
  assert.equal(session.posts().length, beforeReset);
  session.variant('success');
  await page.getByRole('button', { name: 'Reset WiFi settings' }).click();
  await page.getByRole('button', { name: 'Reset WiFi', exact: true }).click();
  await confirmNetwork(page, 'DecentScale');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('WiFi settings cleared'));
  assert.deepEqual(session.posts().at(-1), { ssid: '', pass: '' });

  await page.locator('#wifi-popup').scrollIntoViewIfNeeded();
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
  const elements = await page.locator('#wifi-popup input:visible,#wifi-popup select:visible,#wifi-popup button:visible').evaluateAll(items =>
    items.map(item => ({ width: item.getBoundingClientRect().width, right: item.getBoundingClientRect().right })));
  assert(elements.every(item => item.width > 0 && item.right <= viewport.width));
  const resetGap = await page.locator('#reset-wifi-button').evaluate(button => {
    const nextSection = button.nextElementSibling || button.parentElement.nextElementSibling;
    return nextSection.getBoundingClientRect().top - button.getBoundingClientRect().bottom;
  });
  assert(resetGap >= 24, `Reset button gap is only ${resetGap}px`);
  await page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-${label}-${viewport.width}.png`) });
  await page.goto('http://hds.local/?wifi_attempt=9');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('WiFi settings cleared'));
  await page.goto('http://hds.local/?wifi_attempt=123');
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('not confirmed'));
  assert.notEqual(await page.locator('#wifi-status').getAttribute('data-tone'), 'success');
  session.variant('wrong-device');
  await page.goto(`http://hds.local/?wifi_attempt=9&wifi_device=${initialStatus.device_id}`);
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('different scale'));
  assert.equal(await page.locator('#wifi-connect-button').isEnabled(), false);
  assert.equal(await page.locator('#reset-wifi-button').isEnabled(), false);
  assert.equal(await page.locator('#device-name').isEnabled(), false);
  if (label === 'main') assert.equal(await page.locator('#tare-button').isEnabled(), false);
  if (label === 'main') {
    await page.emulateMedia({ colorScheme: 'dark' });
    await page.waitForFunction(() => document.documentElement.dataset.theme === 'dark');
    const colors = await page.locator('#wifi-popup').evaluate(element => {
      const theme = getComputedStyle(document.documentElement);
      return { text: getComputedStyle(element).color,
        ink: theme.getPropertyValue('--ink').trim(),
        surface: theme.getPropertyValue('--surface').trim(),
        dialogSurface: getComputedStyle(document.getElementById('wifi-reset-dialog')).backgroundColor };
    });
    assert.equal(colors.text, 'rgb(238, 242, 239)');
    assert.equal(colors.ink, '#eef2ef');
    assert.equal(colors.surface, '#171b19');
    assert.equal(colors.dialogSurface, 'rgb(23, 27, 25)');
    await page.locator('#wifi-popup').scrollIntoViewIfNeeded();
    await page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-dark-${label}-${viewport.width}.png`) });
  }
  assert.deepEqual(session.errors, []);
  await session.context.close();
  console.log(`${label} ${viewport.width}px: spinner, scan recovery, trim, validation, duplicate submission, fallback, interruptions, reset dialog and layout passed`);
}

async function runNetworkHandoff(browser, html, label, viewport) {
  for (const variant of ['handoff', 'handoff-wrong-device', 'handoff-legacy-device']) {
    const session = await openPage(browser, html, viewport, false, 'http://192.168.50.30');
    const { page } = session;
    session.variant(variant);
    await page.locator('#ssid').fill('<img src=x onerror=alert(1)>');
    await page.getByRole('button', { name: 'Connect', exact: true }).click();
    await page.locator('#wifi-network-dialog').waitFor({ state: 'visible' });
    assert.equal(await page.locator('#wifi-network-dialog img').count(), 0);
    assert.equal(session.operationPolls(), 0);
    assert.equal(new URL(page.url()).hostname, '192.168.50.30');
    await confirmNetwork(page, '<img src=x onerror=alert(1)>');
    await page.waitForURL(/hds\.local\/setup\/wifi\/continue\?.+wifi_device=/);
    const url = new URL(page.url());
    assert.equal(url.searchParams.get('wifi_attempt'), '9');
    assert.equal(url.searchParams.get('wifi_device'), initialStatus.device_id);
    assert.equal(url.searchParams.has('pass'), false);
    if (variant === 'handoff') {
      await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('WiFi settings saved'));
      assert.equal(await page.locator('#wifi-connect-button').isEnabled(), true);
    } else {
      assert.match(await page.locator('body').textContent(), variant === 'handoff-wrong-device'
        ? /different scale.*not confirmed/ : /Not found/);
      assert.equal(await page.locator('#wifi-connect-button').count(), 0);
    }
    assert.deepEqual(session.posts(), [{ ssid: '<img src=x onerror=alert(1)>', pass: '' }]);
    assert.deepEqual(session.errors, []);
    await session.context.close();
  }
  console.log(`${label} ${viewport.width}px: network-confirmed address handoff, safe SSID text, and wrong-scale protection passed`);
}

async function runLegacy(browser, viewport) {
  const session = await openPage(browser, mainPage, viewport, true);
  const { page } = session;
  assert.equal(await page.locator('#wifi-check-result').isVisible(), false);
  assert.equal(await page.locator('.wifi-network-tools').isVisible(), false);
  assert.equal(await page.locator('#wifi-current').textContent(), 'Connection status unavailable');
  await page.locator('#show-password').check();
  assert.equal(await page.locator('#password').getAttribute('type'), 'text');
  await page.locator('#show-password').uncheck();
  assert.equal(await page.locator('#password').getAttribute('type'), 'password');
  for (const [ssid, pass] of [[' \u200b ', 'password'], ['\u00e9'.repeat(17), 'password'],
    ['Network', 'short'], ['Network', 'pass\u0000word']]) {
    await page.locator('#ssid').fill(ssid);
    await page.locator('#password').fill(pass);
    await page.locator('#wifi-form').evaluate(form =>
      form.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true })));
    await page.waitForFunction(() => document.getElementById('wifi-status').dataset.tone === 'error');
    assert.equal(session.posts().length, 0);
  }
  await page.locator('#ssid').fill(' \u00a0\u200bNew network\ufeff ');
  await page.locator('#password').fill('\u0085\u2060 pass word \u00a0');
  await page.locator('#wifi-form').evaluate(form => {
    form.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
    form.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
  });
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('Settings saved'));
  assert.deepEqual(session.posts(), [{ ssid: 'New network', pass: 'pass word' }]);
  assert.match(await page.locator('#wifi-status').textContent(), /restarting.*not confirmed/);
  assert.notEqual(await page.locator('#wifi-status').getAttribute('data-tone'), 'success');
  assert.equal(await page.locator('#password').inputValue(), '');
  assert.equal(await page.locator('#wifi-connect-button').isEnabled(), false);
  assert.equal(await page.locator('#reset-wifi-button').isEnabled(), false);
  assert.equal(await page.locator('#name-form button').isEnabled(), false);
  assert.equal(await page.evaluate(() => localStorage.length), 0);

  const reload = async () => {
    await page.reload();
    await page.waitForFunction(() => document.getElementById('wifi-form').dataset.wifiSetup === 'legacy');
  };
  for (const variant of ['failure', 'interrupted', 'accepted']) {
    await reload();
    session.variant(variant);
    await page.locator('#ssid').fill('<img src=x onerror=alert(1)>');
    await page.locator('#password').fill('password');
    await page.getByRole('button', { name: 'Connect', exact: true }).click();
    await page.waitForFunction(() => document.getElementById('wifi-status').textContent !== 'Saving WiFi settings...');
    assert.equal(await page.locator('#wifi-popup img').count(), 0);
    assert.notEqual(await page.locator('#wifi-status').getAttribute('data-tone'), 'success');
    assert.equal(await page.locator('#wifi-connect-button').isEnabled(), variant !== 'accepted');
    assert.match(await page.locator('#wifi-status').textContent(), variant === 'failure'
      ? /could not be saved/ : /not confirmed/);
  }

  await reload();
  session.variant('success');
  const beforeReset = session.posts().length;
  await page.getByRole('button', { name: 'Reset WiFi settings' }).click();
  assert.equal(await page.getByRole('dialog').isVisible(), true);
  assert.equal(await page.evaluate(() => document.activeElement.textContent), 'Cancel');
  await page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-legacy-confirm-${viewport.width}.png`) });
  await page.getByRole('button', { name: 'Cancel', exact: true }).click();
  assert.equal(await page.getByRole('dialog').isVisible(), false);
  assert.equal(session.posts().length, beforeReset);
  await page.getByRole('button', { name: 'Reset WiFi settings' }).click();
  await page.keyboard.press('Escape');
  assert.equal(await page.getByRole('dialog').isVisible(), false);
  assert.equal(session.posts().length, beforeReset);
  await page.getByRole('button', { name: 'Reset WiFi settings' }).click();
  await page.getByRole('button', { name: 'Reset WiFi', exact: true }).click();
  await page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('Settings cleared'));
  assert.deepEqual(session.posts().at(-1), { ssid: '', pass: '' });
  assert.match(await page.locator('#wifi-status').textContent(), /restarting in DecentScale/);

  await reload();
  await page.locator('#wifi-popup').scrollIntoViewIfNeeded();
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
  const resetGap = await page.locator('#reset-wifi-button').evaluate(button =>
    button.nextElementSibling.getBoundingClientRect().top - button.getBoundingClientRect().bottom);
  assert(resetGap >= 24, `Legacy reset button gap is only ${resetGap}px`);
  await page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-legacy-${viewport.width}.png`) });
  assert.equal(session.unsupportedRequests(), 0);
  assert.deepEqual(session.errors, []);
  await session.context.close();
  console.log(`legacy ${viewport.width}px: connect, trim, validation, password visibility, duplicate submission, HTTP errors, interruptions, reset dialog and fallback styling passed`);
}

(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    for (const viewport of [{ width: 1280, height: 900 }, { width: 390, height: 844 }]) {
      await run(browser, mainPage, 'main', viewport);
      await run(browser, inlinePage, 'inline', viewport);
      await runLegacy(browser, viewport);
      await runNetworkHandoff(browser, mainPage, 'main', viewport);
      await runNetworkHandoff(browser, inlinePage, 'inline', viewport);
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
