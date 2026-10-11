const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE_PATH || 'playwright');

const root = path.resolve(__dirname, '..');
const header = fs.readFileSync(path.join(root, 'include/wifi_setup_page.h'), 'utf8');
const embedded = (name, delimiter) => header.match(
  new RegExp(`${name}[\\s\\S]*?R"${delimiter}\\(([\\s\\S]*?)\\)${delimiter}";`)
)[1];
const script = embedded('HDS_WIFI_SETUP_SCRIPT', 'js');
const style = embedded('HDS_WIFI_SETUP_STYLE', 'css');
const pages = {
  main: fs.readFileSync(path.join(root, 'plugins/default-web-apps/assets/index.html'), 'utf8'),
  inline: embedded('HDS_WIFI_SETUP_PAGE', 'html')
};
const deviceId = 'ccba973327f0';

async function openSource(browser, html, viewport, options = {}) {
  const context = await browser.newContext({ viewport });
  const page = await context.newPage();
  const previousHtml = html.replace(/<label class="wifi-address" for="wifi-network-target"[\s\S]*?<\/label>/, '');
  const servedHtml = options.previousMarkup ? (options.previousMarkup === 'with-address' ? previousHtml
    .replace('Scale IP address', 'Scale IP address (if the hostname is unavailable)') :
    previousHtml.replace(/<label class="wifi-address" for="wifi-network-address"[\s\S]*?<\/label>/, ''))
    .replace('id="wifi-network-continue" type="button" autofocus>Continue',
      'id="wifi-network-continue" value="continue" autofocus>Check connection')
    .replace('<a id="wifi-check-result" href="#wifi-network-dialog" aria-haspopup="dialog" aria-controls="wifi-network-dialog" hidden>Check connection</a>',
      '<button id="wifi-check-result" type="button" hidden>Check WiFi result</button>')
    .replace('WiFi change started', 'Check your WiFi network') : html;
  const sourceHost = options.provisioning ? '192.168.1.1' : '192.168.50.30';
  let posts = [];
  let variant = options.variant || 'success';
  let testReady = options.holdTest !== true;
  let activated = false;
  let activations = [];
  let polls = 0;
  let status = {
    operation_id: 0, state: 'idle', error: '', requested_ssid: '',
    ssid: options.provisioning ? '' : 'Old network', ip: sourceHost,
    connected: !options.provisioning, credentials_saved: !options.provisioning,
    access_point: options.provisioning === true, busy: false,
    mdns_name: options.mdnsName || 'hds', mdns_available: options.mdns !== false, device_id: deviceId
  };
  if (options.initialReady) status = { ...status, operation_id: 4294967295, test_only: true,
    requested_ssid: 'New network', state: 'ready', tested_ip: '192.168.60.40' };
  const errors = [];
  let failedRequests = [];
  context.on('requestfailed', request => {
    failedRequests = [...failedRequests, { url: request.url(), failure: request.failure()?.errorText }];
  });
  context.on('page', target => target.on('pageerror', error => errors.push(error.message)));
  page.on('pageerror', error => errors.push(error.message));
  await context.addInitScript(() => {
    const original = window.setTimeout;
    window.setTimeout = (callback, delay, ...args) => original(callback, delay === 1000 ? 20 : delay, ...args);
  });
  await context.routeWebSocket('**/snapshot', socket => {
    socket.onMessage(() => socket.send(JSON.stringify({ type: 'status', mdns_name: 'hds' })));
  });
  await context.route(/^http:\/\/(?:hds\.local|kitchen-scale\.local|192\.168\.50\.30|192\.168\.60\.4[01]|192\.168\.1\.1)\//, async route => {
    const request = route.request();
    const url = new URL(request.url());
    const json = data => route.fulfill({ contentType: 'application/json', body: JSON.stringify(data) });
    if (variant === 'hostname-unavailable' && url.hostname === 'hds.local') return route.abort('failed');
    if (activated && /^192\.168\.60\./.test(url.hostname) && url.hostname !== status.ip) return route.abort('failed');
    if (url.pathname === '/setup/wifi/continue') {
      if (variant === 'wrong-device' || url.searchParams.get('wifi_device') !== deviceId) {
        return route.fulfill({ status: 409, body: 'This address reached a different scale. The WiFi result is not confirmed.' });
      }
      if (variant === 'stale-route' || url.searchParams.get('wifi_attempt') !== String(status.operation_id)) {
        return route.fulfill({ status: 409, body: 'This WiFi request is no longer current. Its result is not confirmed.' });
      }
      return route.fulfill({ contentType: 'text/html',
        body: `<script>location.replace(${JSON.stringify(`/${url.search}`)})</script>` });
    }
    if (url.pathname === '/') return route.fulfill({ contentType: 'text/html', body: servedHtml });
    if (url.pathname === '/setup/wifi.js') return route.fulfill({ contentType: 'application/javascript', body: script });
    if (url.pathname === '/setup/wifi.css') return route.fulfill({ contentType: 'text/css', body: style });
    if (url.pathname === '/webapps.json') return json({ schema: 1, apps: [] });
    if (url.pathname.startsWith('/shared/')) {
      return route.fulfill({ contentType: url.pathname.endsWith('.css') ? 'text/css' : 'application/javascript',
        body: fs.readFileSync(path.join(root, 'plugins/default-web-apps/assets', url.pathname.slice(1)), 'utf8') });
    }
    if (url.pathname === '/setup/wifi/activate' && request.method() === 'POST') {
      assert.equal(url.hostname, sourceHost);
      assert.equal(request.headers()['x-hds-device-id'], deviceId);
      const data = request.postDataJSON();
      assert.deepEqual(data, { operation_id: status.operation_id });
      activations = [...activations, data];
      if (variant === 'activation-rejected') return route.fulfill({ status: 409,
        contentType: 'application/json', body: JSON.stringify({ error: 'wifi_operation_not_ready' }) });
      activated = true;
      status = { ...status, state: 'succeeded', ssid: status.requested_ssid, connected: true,
        access_point: false, credentials_saved: true, ip: options.finalIp || '192.168.60.40', busy: false };
      if (variant === 'activation-interrupted') return route.abort('failed');
      return json({ operation_id: status.operation_id, device_id: deviceId, restarting: false });
    }
    if (request.method() === 'POST') {
      assert.equal(url.hostname, sourceHost);
      assert.equal(url.pathname, '/setup/wifi');
      assert.equal(request.headers()['x-hds-device-id'], deviceId);
      const data = request.postDataJSON();
      posts = [...posts, data];
      status = { ...status, operation_id: 4294967295, state: data.test_only ? 'testing' : 'succeeded', error: '',
        requested_ssid: data.ssid, ssid: data.ssid, connected: data.ssid !== '',
        credentials_saved: data.ssid !== '', access_point: data.ssid === '', busy: false,
        test_only: data.test_only === true,
        ip: data.ssid === '' ? '192.168.1.1' : data.ssid === 'Old network' ? sourceHost : '192.168.60.40' };
      return json({ operation_id: status.operation_id, device_id: deviceId, ssid: data.ssid,
        mdns_name: status.mdns_name, mdns_available: status.mdns_available, restarting: false,
        test_only: data.test_only === true });
    }
    if (url.pathname === '/setup/wifi/status') {
      polls += 1;
      if (status.operation_id && variant === 'rollback') return json({ ...status,
        state: 'failed', error: 'authentication_failed', ssid: 'Old network', ip: sourceHost, connected: true });
      if (status.operation_id && variant === 'wrong-trial') return json({ ...status, device_id: 'ffeeddccbbaa' });
      if (status.operation_id && variant === 'stale-trial') return json({ ...status, operation_id: 123 });
      if (status.operation_id && variant === 'unreachable') return route.abort('failed');
      if (status.test_only && !activated) return json({ ...status,
        state: testReady ? 'ready' : options.testState || 'testing', ssid: options.provisioning ? '' : 'Old network',
        ip: sourceHost, connected: !options.provisioning, access_point: options.provisioning === true,
        credentials_saved: !options.provisioning, busy: !testReady,
        tested_ip: variant === 'bad-tested-ip' ? 'javascript:alert(1)' : '192.168.60.40' });
      if (status.operation_id && url.hostname === sourceHost && status.ip !== sourceHost) return route.abort('failed');
      if (status.operation_id && variant === 'wrong-status') return json({ ...status, device_id: 'ffeeddccbbaa' });
      if (status.operation_id && variant === 'stale-status') return json({ ...status, operation_id: 123 });
      return json(status);
    }
    return route.fulfill({ status: 404 });
  });
  await page.goto(`http://${sourceHost}/${options.initialReady ? '?wifi_attempt=123' : ''}`);
  await page.waitForFunction(() => !document.getElementById('wifi-connect-button').disabled);
  return { context, page, posts: () => posts, errors, failures: () => failedRequests,
    activations: () => activations, polls: () => polls, releaseTest: () => { testReady = true; },
    variant: value => { variant = value; } };
}

async function submit(page, ssid = 'New network') {
  await page.locator('#ssid').fill(ssid);
  await page.locator('#password').fill('pass word');
  await page.getByRole('button', { name: 'Connect', exact: true }).click();
}

async function openCheckDialog(page) {
  await page.waitForFunction(() => document.getElementById('password').value === '');
  await page.waitForFunction(() => !document.getElementById('wifi-open-scale').hidden ||
    !document.getElementById('wifi-check-result').hidden);
  const link = await page.locator('#wifi-check-result').isVisible() ? '#wifi-check-result' : '#wifi-open-scale';
  await page.locator(link).waitFor({ state: 'visible' });
  assert.equal(await page.locator('#wifi-network-continue').isVisible(), false);
  const originalUrl = page.url();
  const views = page.context().pages().length;
  await page.locator(link).click();
  await page.locator('#wifi-network-dialog').waitFor({ state: 'visible' });
  assert.equal(page.url(), originalUrl);
  assert.equal(page.context().pages().length, views);
}

async function openDestination(session, ip) {
  if (await session.page.locator('#wifi-network-dialog').getAttribute('data-view') !== 'instructions') await openCheckDialog(session.page);
  if (ip !== undefined) await session.page.locator('#wifi-network-address').fill(ip);
  await session.page.getByRole('button', { name: 'Continue', exact: true }).click();
  return session.page;
}

async function checkHandoffs(browser, html, label, viewport) {
  for (const options of [{}, { provisioning: true }, { mdns: false }, { variant: 'activation-interrupted' },
    { finalIp: '192.168.60.41' }, { finalIp: '192.168.60.41', useHostname: true },
    { useHostname: true }, { useHostname: true, mdnsName: 'kitchen-scale' },
    { variant: 'hostname-unavailable' }, { mdnsName: 'bad/name' }, { previousMarkup: true },
    { previousMarkup: 'with-address' }]) {
    const session = await openSource(browser, html, viewport, { ...options, holdTest: true });
    try {
      await submit(session.page);
      await session.page.waitForFunction(() => document.getElementById('password').value === '');
      assert.equal(await session.page.locator('#wifi-open-scale').isVisible(), false);
      assert.equal(await session.page.locator('#wifi-check-result').isVisible(), false);
      assert.equal(await session.page.locator('#wifi-network-dialog').isVisible(), true);
      assert.equal(await session.page.locator('#wifi-operation-view .wifi-spinner').isVisible(), true);
      assert.match(await session.page.locator('#wifi-current').textContent(), /Checking connection to/);
      assert.equal(session.activations().length, 0);
      session.releaseTest();
      await session.page.locator('#wifi-open-scale').waitFor({ state: 'visible' });
      await session.page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-handoff-ready-${label}-${viewport.width}.png`) });
      await openCheckDialog(session.page);
      const link = new URL(await session.page.locator('#wifi-open-scale').getAttribute('href'));
      assert.equal(link.hostname, '192.168.60.40');
      assert.equal(link.pathname, '/setup/wifi/continue');
      assert.match(await session.page.locator('#wifi-network-title').textContent(), /test succeeded/);
      assert.equal(await session.page.locator('#wifi-network-target').inputValue(), 'ip');
      assert.equal(await session.page.locator('#wifi-network-address').inputValue(), '192.168.60.40');
      assert.equal(await session.page.locator('#wifi-network-address').isEnabled(), true);
      assert.match(await session.page.locator('#wifi-network-address').evaluate(input => input.parentElement.textContent), /^Scale IP address\s*$/);
      assert.equal(await session.page.locator('#wifi-network-target option').count(), 2);
      const hostnameEnabled = options.mdns !== false && options.mdnsName !== 'bad/name';
      assert.equal(await session.page.locator('#wifi-network-target option[value="hostname"]').evaluate(option => !option.disabled), hostnameEnabled);
      assert.equal(await session.page.locator('#wifi-network-target option[value="hostname"]').textContent(),
        hostnameEnabled ? `Hostname (${options.mdnsName || 'hds'}.local)` : 'Hostname');
      assert.equal(session.posts().length, 1);
      assert.equal(session.activations().length, 1);
      assert.equal(await session.page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
      assert.equal(await session.page.locator('#wifi-network-title').evaluate(element => {
        const title = element.getBoundingClientRect();
        const dialog = element.closest('dialog').getBoundingClientRect();
        return title.top >= dialog.top && title.bottom <= dialog.bottom;
      }), true, 'The dialog title must not be clipped');
      assert.equal(await session.page.locator('#wifi-network-dialog button').evaluateAll(buttons =>
        buttons.every(button => button.scrollWidth <= button.clientWidth && button.scrollHeight <= button.clientHeight)), true);
      await session.page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-handoff-${label}-${viewport.width}.png`) });
      for (const ip of ['', 'javascript:alert(1)', '192.168.60.999', '192.168.060.40', '0.0.0.0']) {
        const count = session.context.pages().length;
        const originalUrl = session.page.url();
        await session.page.locator('#wifi-network-address').fill(ip);
        await session.page.getByRole('button', { name: 'Continue', exact: true }).click();
        assert.equal(session.context.pages().length, count);
        assert.equal(session.page.url(), originalUrl);
        assert.equal(await session.page.locator('#wifi-network-address').evaluate(input => input.checkValidity()), false);
      }
      if (options.useHostname) {
        await session.page.locator('#wifi-network-target').selectOption('hostname');
        assert.equal(await session.page.locator('#wifi-network-address').isVisible(), false);
        assert.equal(await session.page.locator('#wifi-network-address').isEnabled(), false);
        await session.page.locator('#wifi-network-target').selectOption('ip');
        assert.equal(await session.page.locator('#wifi-network-address').isVisible(), true);
        assert.equal(await session.page.locator('#wifi-network-address').isEnabled(), true);
        await session.page.locator('#wifi-network-target').selectOption('hostname');
        await session.page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-handoff-hostname-${label}-${viewport.width}.png`) });
      } else {
        await session.page.locator('#wifi-network-address').fill('192.168.60.40');
      }
      const ip = options.useHostname ? undefined : options.finalIp;
      const destination = await openDestination(session, ip);
      await destination.waitForFunction(() => document.getElementById('wifi-status')?.textContent.includes('WiFi settings saved')).catch(async error => {
        console.error({ options, url: destination.url(), body: await destination.locator('body').textContent(),
          errors: session.errors, failedRequests: session.failures() });
        throw error;
      });
      await destination.screenshot({ path: path.join(root, '.pio.nosync', `wifi-handoff-result-${label}-${viewport.width}.png`) });
      assert.equal(await destination.locator('#wifi-status').evaluate(element => {
        const rect = element.getBoundingClientRect();
        const front = document.elementFromPoint(rect.left + rect.width / 2, rect.top + rect.height / 2);
        return rect.top >= 0 && rect.bottom <= innerHeight && element.contains(front);
      }), true, 'The verified handoff result must be visible and unobstructed');
      const url = new URL(destination.url());
      assert.equal(url.hostname, options.useHostname ? `${options.mdnsName || 'hds'}.local` : ip || '192.168.60.40');
      assert.equal(url.searchParams.get('wifi_device'), deviceId);
      assert.equal(url.searchParams.get('wifi_attempt'), '4294967295');
      assert.deepEqual([...url.searchParams.keys()].sort(), ['wifi_attempt', 'wifi_device']);
      assert.equal(await destination.evaluate(() => window.opener), null);
      assert.equal(session.posts().length, 1);
      assert.equal(session.activations().length, 1);
      assert.equal(session.context.pages().length, 1);
      assert.equal(await session.page.locator('#wifi-network-dialog').isVisible(), false);
      assert.equal(await session.page.locator('#wifi-check-result').isVisible(), false);
      if (options.variant === 'hostname-unavailable') {
        assert.equal(session.failures().some(request => new URL(request.url).hostname === 'hds.local'), false);
      }
      assert.deepEqual(session.errors, []);
    } finally { await session.context.close(); }
  }
}

async function checkRejectedResults(browser, html, viewport) {
  for (const variant of ['wrong-device', 'stale-route', 'wrong-status', 'stale-status']) {
    const session = await openSource(browser, html, viewport, { variant });
    try {
      await submit(session.page);
      await session.page.locator('#wifi-open-scale').waitFor({ state: 'visible' });
      const destination = await openDestination(session);
      await destination.waitForFunction(() => document.body.textContent.includes('different scale') ||
        document.body.textContent.includes('no longer current'));
      assert.equal(await destination.locator('#wifi-status[data-tone="success"]').count(), 0);
      assert.equal(session.posts().length, 1);
      assert.deepEqual(session.errors, []);
    } finally { await session.context.close(); }
  }
}

async function checkRejectedTests(browser, html, viewport) {
  const stale = await openSource(browser, html, viewport, { initialReady: true });
  try {
    assert.match(await stale.page.locator('#wifi-status').textContent(), /no longer current/);
    assert.equal(stale.activations().length, 0);
    assert.equal(await stale.page.locator('#wifi-open-scale').isVisible(), false);
    assert.deepEqual(stale.errors, []);
  } finally { await stale.context.close(); }
  for (const variant of ['wrong-trial', 'stale-trial', 'bad-tested-ip', 'activation-rejected']) {
    const session = await openSource(browser, html, viewport, { variant });
    try {
      await submit(session.page);
      await session.page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('different scale') ||
        document.getElementById('wifi-status').textContent.includes('no longer current') ||
        document.getElementById('wifi-status').textContent.includes('not confirmed') ||
        document.getElementById('wifi-status').textContent.includes('could not start'));
      assert.equal(await session.page.locator('#wifi-open-scale').isVisible(), false);
      assert.equal(await session.page.locator('#wifi-check-result').isVisible(), false);
      assert.equal(await session.page.locator('#wifi-network-dialog').isVisible(), true);
      assert.equal(session.activations().length, variant === 'activation-rejected' ? 1 : 0);
      assert.equal(session.context.pages().length, 1);
      assert.deepEqual(session.errors, []);
    } finally { await session.context.close(); }
  }
}

async function checkSameNetworkAndRecovery(browser, html, viewport) {
  const blocked = await openSource(browser, html, viewport);
  try {
    await submit(blocked.page);
    await blocked.page.locator('#wifi-open-scale').waitFor({ state: 'visible' });
    await openCheckDialog(blocked.page);
    await blocked.page.getByRole('button', { name: 'Check later', exact: true }).click();
    assert.equal(await blocked.page.locator('#wifi-network-dialog').isVisible(), false);
    assert.equal(await blocked.page.locator('#wifi-open-scale').isVisible(), true);
    assert.match(await blocked.page.locator('#wifi-status').textContent(), /test succeeded/i);
    assert.equal(blocked.context.pages().length, 1);
    assert.equal(blocked.posts().length, 1);
    assert.equal(blocked.activations().length, 1);
    await openCheckDialog(blocked.page);
    assert.equal(await blocked.page.locator('#wifi-network-dialog').isVisible(), true);
  } finally { await blocked.context.close(); }
  const session = await openSource(browser, html, viewport);
  try {
    await submit(session.page, 'Old network');
    await session.page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('WiFi settings saved'));
    assert.equal(await session.page.locator('#wifi-network-dialog').isVisible(), true);
    assert.equal(session.context.pages().length, 1);
    assert.equal(session.posts().length, 1);
  } finally { await session.context.close(); }
  const recovery = await openSource(browser, html, viewport, { variant: 'rollback' });
  try {
    await submit(recovery.page);
    await recovery.page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('Back on Old network'));
    assert.equal(recovery.context.pages().length, 1);
    assert.equal(recovery.posts().length, 1);
    assert.equal(recovery.activations().length, 0);
    assert.equal(await recovery.page.locator('#wifi-open-scale').isVisible(), false);
    assert.equal(await recovery.page.locator('#wifi-network-dialog').isVisible(), true);
    assert.equal(await recovery.page.locator('#wifi-status').getAttribute('data-tone'), 'error');
  } finally { await recovery.context.close(); }
  const reset = await openSource(browser, html, viewport, { mdns: false });
  try {
    await reset.page.getByRole('button', { name: 'Reset WiFi settings', exact: true }).click();
    await reset.page.getByRole('button', { name: 'Reset WiFi', exact: true }).click();
    await openCheckDialog(reset.page);
    assert.equal(await reset.page.locator('#wifi-network-target').inputValue(), 'ip');
    assert.equal(await reset.page.locator('#wifi-network-address').inputValue(), '192.168.1.1');
    const destination = await openDestination(reset);
    await destination.waitForFunction(() => document.getElementById('wifi-status')?.textContent.includes('WiFi settings cleared'));
    assert.equal(new URL(destination.url()).hostname, '192.168.1.1');
    assert.deepEqual(reset.posts(), [{ ssid: '', pass: '' }]);
  } finally { await reset.context.close(); }
}

async function checkReloadedTests(browser, html, viewport) {
  for (const testState of ['testing', 'verifying', 'returning']) {
    const session = await openSource(browser, html, viewport, { holdTest: true, testState });
    try {
      await submit(session.page);
      await session.page.waitForFunction(() => document.getElementById('password').value === '');
      await session.page.reload();
      await session.page.waitForFunction(() => document.getElementById('wifi-status').textContent.length > 0);
      assert.equal(await session.page.locator('#wifi-connect-button').isEnabled(), false);
      assert.equal(session.activations().length, 0);
      session.releaseTest();
      await session.page.locator('#wifi-open-scale').waitFor({ state: 'visible' });
      assert.equal(session.posts().length, 1);
      assert.equal(session.activations().length, 1);
      await openDestination(session);
      await session.page.waitForFunction(() => document.getElementById('wifi-status')?.textContent.includes('WiFi settings saved'));
      assert.equal(await session.page.locator('#wifi-connect-button').isEnabled(), true);
      assert.deepEqual(session.errors, []);
    } finally { await session.context.close(); }
  }
}

async function checkProgressPopup(browser, html, label, viewport) {
  const session = await openSource(browser, html, viewport, { holdTest: true });
  try {
    await submit(session.page);
    await session.page.waitForFunction(() => document.getElementById('password').value === '');
    assert.equal(await session.page.locator('dialog[open]').count(), 1);
    assert.equal(await session.page.locator('#wifi-network-dialog').getAttribute('data-view'), 'operation');
    assert.equal(await session.page.locator('#wifi-operation-view .wifi-spinner').isVisible(), true);
    assert.equal(await session.page.locator('#wifi-operation-view #wifi-status').isVisible(), true);
    assert.equal(await session.page.locator('#wifi-network-continue').isVisible(), false);
    assert.equal(await session.page.locator('#wifi-open-scale').isVisible(), false);
    assert.equal(await session.page.locator('#wifi-retry').isVisible(), false);
    await session.page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-progress-${label}-${viewport.width}.png`) });
    session.releaseTest();
    await session.page.locator('#wifi-open-scale').waitFor({ state: 'visible' });
    assert.equal(await session.page.locator('#wifi-operation-view .wifi-spinner').isVisible(), false);
    assert.equal(await session.page.locator('#wifi-operation-view #wifi-open-scale').isVisible(), true);
    assert.equal(await session.page.locator('dialog[open]').count(), 1);
    await session.page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-progress-success-${label}-${viewport.width}.png`) });
    await openCheckDialog(session.page);
    assert.equal(await session.page.locator('dialog[open]').count(), 1);
    assert.equal(await session.page.locator('#wifi-operation-view').isVisible(), false);
    assert.equal(await session.page.locator('#wifi-network-target').inputValue(), 'ip');
    assert.equal(await session.page.locator('#wifi-network-address').inputValue(), '192.168.60.40');
    await session.page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-progress-instructions-${label}-${viewport.width}.png`) });
    await openDestination(session);
    await session.page.waitForFunction(() => document.getElementById('wifi-status')?.textContent.includes('WiFi settings saved'));
    assert.equal(await session.page.locator('dialog[open]').count(), 0);
    assert.equal(session.context.pages().length, 1);
    assert.equal(session.posts().length, 1);
    assert.equal(session.activations().length, 1);
    assert.deepEqual(session.errors, []);
  } finally { await session.context.close(); }
  const failure = await openSource(browser, html, viewport, { variant: 'rollback' });
  try {
    await submit(failure.page);
    await failure.page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('Back on Old network'));
    assert.equal(await failure.page.locator('#wifi-operation-view #wifi-status').getAttribute('data-tone'), 'error');
    assert.equal(await failure.page.locator('#wifi-operation-view .wifi-spinner').isVisible(), false);
    assert.equal(await failure.page.locator('#wifi-open-scale').isVisible(), false);
    assert.equal(await failure.page.locator('#wifi-retry').isEnabled(), true);
    await failure.page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-progress-failure-${label}-${viewport.width}.png`) });
    await failure.page.getByRole('button', { name: 'Retry', exact: true }).click();
    assert.equal(await failure.page.locator('dialog[open]').count(), 0);
    assert.equal(await failure.page.locator('#password').inputValue(), '');
    assert.equal(await failure.page.evaluate(() => document.activeElement.id), 'password');
    assert.equal(failure.posts().length, 1);
    failure.variant('success');
    await failure.page.locator('#password').fill('pass word');
    await failure.page.getByRole('button', { name: 'Connect', exact: true }).click();
    await failure.page.locator('#wifi-open-scale').waitFor({ state: 'visible' });
    await failure.page.getByRole('button', { name: 'Close', exact: true }).click();
    await failure.page.locator('#wifi-popup #wifi-open-scale').waitFor({ state: 'visible' });
    assert.equal(await failure.page.locator('#wifi-popup #wifi-open-scale').isVisible(), true);
    assert.equal(failure.posts().length, 2);
    assert.deepEqual(failure.errors, []);
  } finally { await failure.context.close(); }
}

(async () => {
  fs.mkdirSync(path.join(root, '.pio.nosync'), { recursive: true });
  const browser = await chromium.launch({ headless: true });
  try {
    if (process.argv.includes('--popup-only')) {
      for (const viewport of [{ width: 1280, height: 900 }, { width: 390, height: 844 }, { width: 320, height: 568 }]) {
        for (const [label, html] of Object.entries(pages)) {
          await checkProgressPopup(browser, html, label, viewport);
          console.log(`${label} ${viewport.width}px: progress, success, same-popup instructions, failure, retry and close passed`);
        }
      }
      return;
    }
    for (const viewport of [{ width: 1280, height: 900 }, { width: 390, height: 844 }, { width: 320, height: 568 }]) {
      for (const [label, html] of Object.entries(pages)) {
        await checkHandoffs(browser, html, label, viewport);
        await checkRejectedResults(browser, html, viewport);
        await checkRejectedTests(browser, html, viewport);
        await checkSameNetworkAndRecovery(browser, html, viewport);
        await checkReloadedTests(browser, html, viewport);
        console.log(`${label} ${viewport.width}px: IP-default and explicit hostname handoff, editable DHCP address, identity/operation guards, provisioning, rollback and reset passed`);
      }
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
