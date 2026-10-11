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
  const servedHtml = options.previousMarkup ? html.replace(/<label class="wifi-address"[\s\S]*?<\/label>/, '')
    .replace('id="wifi-network-continue" type="button" autofocus>Check connection',
      'id="wifi-network-continue" value="continue" autofocus>Continue')
    .replace('WiFi change started', 'Check your WiFi network') : html;
  const sourceHost = options.provisioning ? '192.168.1.1' : '192.168.50.30';
  let posts = [];
  let variant = options.variant || 'success';
  let status = {
    operation_id: 0, state: 'idle', error: '', requested_ssid: '',
    ssid: options.provisioning ? '' : 'Old network', ip: sourceHost,
    connected: !options.provisioning, credentials_saved: !options.provisioning,
    access_point: options.provisioning === true, busy: false,
    mdns_name: options.mdnsName || 'hds', mdns_available: options.mdns !== false, device_id: deviceId
  };
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
  await context.route(/^http:\/\/(?:hds\.local|192\.168\.50\.30|192\.168\.60\.40|192\.168\.1\.1)\//, async route => {
    const request = route.request();
    const url = new URL(request.url());
    const json = data => route.fulfill({ contentType: 'application/json', body: JSON.stringify(data) });
    if (variant === 'hostname-unavailable' && url.hostname === 'hds.local') return route.abort('failed');
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
    if (request.method() === 'POST') {
      assert.equal(url.hostname, sourceHost);
      assert.equal(url.pathname, '/setup/wifi');
      assert.equal(request.headers()['x-hds-device-id'], deviceId);
      const data = request.postDataJSON();
      posts = [...posts, data];
      status = { ...status, operation_id: 4294967295, state: 'succeeded', error: '',
        requested_ssid: data.ssid, ssid: data.ssid, connected: data.ssid !== '',
        credentials_saved: data.ssid !== '', access_point: data.ssid === '', busy: false,
        ip: data.ssid === '' ? '192.168.1.1' : data.ssid === 'Old network' ? sourceHost : '192.168.60.40' };
      return json({ operation_id: status.operation_id, device_id: deviceId, ssid: data.ssid,
        mdns_name: status.mdns_name, mdns_available: status.mdns_available, restarting: false });
    }
    if (url.pathname === '/setup/wifi/status') {
      if (status.operation_id && url.hostname === sourceHost && status.ip !== sourceHost && variant !== 'rollback') {
        return route.abort('failed');
      }
      if (status.operation_id && variant === 'rollback') return json({ ...status,
        state: 'failed', error: 'authentication_failed', ssid: 'Old network', ip: sourceHost, connected: true });
      if (status.operation_id && variant === 'wrong-status') return json({ ...status, device_id: 'ffeeddccbbaa' });
      if (status.operation_id && variant === 'stale-status') return json({ ...status, operation_id: 123 });
      if (status.operation_id && variant === 'unreachable') return route.abort('failed');
      return json(status);
    }
    return route.fulfill({ status: 404 });
  });
  await page.goto(`http://${sourceHost}/`);
  await page.waitForFunction(() => !document.getElementById('wifi-connect-button').disabled);
  return { context, page, posts: () => posts, errors, failures: () => failedRequests,
    variant: value => { variant = value; } };
}

async function submit(page, ssid = 'New network') {
  await page.locator('#ssid').fill(ssid);
  await page.locator('#password').fill('pass word');
  await page.getByRole('button', { name: 'Connect', exact: true }).click();
}

async function openDestination(session, ip = '') {
  await session.page.locator('#wifi-network-address').fill(ip);
  const destination = session.context.waitForEvent('page');
  await session.page.getByRole('button', { name: 'Check connection', exact: true }).click();
  return destination;
}

async function checkHandoffs(browser, html, label, viewport) {
  for (const options of [{}, { provisioning: true }, { mdns: false }, { variant: 'hostname-unavailable' },
    { mdnsName: 'bad/name' }, { previousMarkup: true }]) {
    const session = await openSource(browser, html, viewport, options);
    try {
      await submit(session.page);
      await session.page.locator('#wifi-network-dialog').waitFor({ state: 'visible' });
      assert.match(await session.page.locator('#wifi-network-title').textContent(), /change started/);
      assert.equal(session.posts().length, 1);
      assert.equal(await session.page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
      assert.equal(await session.page.locator('#wifi-network-dialog button').evaluateAll(buttons =>
        buttons.every(button => button.scrollWidth <= button.clientWidth && button.scrollHeight <= button.clientHeight)), true);
      await session.page.screenshot({ path: path.join(root, '.pio.nosync', `wifi-handoff-${label}-${viewport.width}.png`) });
      if (options.variant === 'hostname-unavailable') {
        const unavailable = await openDestination(session);
        await unavailable.waitForLoadState().catch(() => {});
        assert.equal(await session.page.locator('#wifi-network-dialog').isVisible(), true);
      }
      if (options.mdns === false || options.mdnsName) {
        await session.page.getByRole('button', { name: 'Check connection', exact: true }).click();
        assert.equal(session.context.pages().length, 1);
      }
      for (const ip of ['javascript:alert(1)', '192.168.60.999', '192.168.060.40', '0.0.0.0']) {
        const count = session.context.pages().length;
        await session.page.locator('#wifi-network-address').fill(ip);
        await session.page.getByRole('button', { name: 'Check connection', exact: true }).click();
        assert.equal(session.context.pages().length, count);
        assert.equal(await session.page.locator('#wifi-network-address').evaluate(input => input.checkValidity()), false);
      }
      const ip = options.mdns === false || options.variant || options.mdnsName ? '192.168.60.40' : '';
      const destination = await openDestination(session, ip);
      await destination.waitForFunction(() => document.getElementById('wifi-status')?.textContent.includes('WiFi settings saved')).catch(async error => {
        console.error({ options, url: destination.url(), body: await destination.locator('body').textContent(),
          errors: session.errors, failedRequests: session.failures() });
        throw error;
      });
      const url = new URL(destination.url());
      assert.equal(url.hostname, ip || 'hds.local');
      assert.equal(url.searchParams.get('wifi_device'), deviceId);
      assert.equal(url.searchParams.get('wifi_attempt'), '4294967295');
      assert.deepEqual([...url.searchParams.keys()].sort(), ['wifi_attempt', 'wifi_device']);
      assert.equal(await destination.evaluate(() => window.opener), null);
      assert.equal(session.posts().length, 1);
      assert.notEqual(await session.page.locator('#wifi-status').getAttribute('data-tone'), 'success');
      assert.equal(await session.page.locator('#wifi-network-dialog').isVisible(), true);
      assert.deepEqual(session.errors, []);
    } finally { await session.context.close(); }
  }
}

async function checkRejectedResults(browser, html, viewport) {
  for (const variant of ['wrong-device', 'stale-route', 'wrong-status', 'stale-status']) {
    const session = await openSource(browser, html, viewport, { variant });
    try {
      await submit(session.page);
      const destination = await openDestination(session);
      await destination.waitForFunction(() => document.body.textContent.includes('different scale') ||
        document.body.textContent.includes('no longer current'));
      assert.equal(await destination.locator('#wifi-status[data-tone="success"]').count(), 0);
      assert.equal(session.posts().length, 1);
      assert.deepEqual(session.errors, []);
    } finally { await session.context.close(); }
  }
}

async function checkSameNetworkAndRecovery(browser, html, viewport) {
  const session = await openSource(browser, html, viewport);
  try {
    await submit(session.page, 'Old network');
    await session.page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('WiFi settings saved'));
    assert.equal(await session.page.locator('#wifi-network-dialog').isVisible(), false);
    assert.equal(session.context.pages().length, 1);
    assert.equal(session.posts().length, 1);
  } finally { await session.context.close(); }
  const recovery = await openSource(browser, html, viewport, { variant: 'rollback' });
  try {
    await submit(recovery.page);
    await recovery.page.locator('#wifi-network-dialog').waitFor({ state: 'visible' });
    await recovery.page.locator('#wifi-network-address').fill('192.168.50.30');
    await recovery.page.getByRole('button', { name: 'Check connection', exact: true }).click();
    await recovery.page.waitForFunction(() => document.getElementById('wifi-status').textContent.includes('Back on Old network'));
    assert.equal(recovery.context.pages().length, 1);
    assert.equal(recovery.posts().length, 1);
    assert.equal(await recovery.page.locator('#wifi-status').getAttribute('data-tone'), 'error');
  } finally { await recovery.context.close(); }
  const reset = await openSource(browser, html, viewport, { mdns: false });
  try {
    await reset.page.getByRole('button', { name: 'Reset WiFi settings', exact: true }).click();
    await reset.page.getByRole('button', { name: 'Reset WiFi', exact: true }).click();
    const destination = await openDestination(reset);
    await destination.waitForFunction(() => document.getElementById('wifi-status')?.textContent.includes('WiFi settings cleared'));
    assert.equal(new URL(destination.url()).hostname, '192.168.1.1');
    assert.deepEqual(reset.posts(), [{ ssid: '', pass: '' }]);
  } finally { await reset.context.close(); }
}

(async () => {
  fs.mkdirSync(path.join(root, '.pio.nosync'), { recursive: true });
  const browser = await chromium.launch({ headless: true });
  try {
    for (const viewport of [{ width: 1280, height: 900 }, { width: 390, height: 844 }, { width: 320, height: 568 }]) {
      for (const [label, html] of Object.entries(pages)) {
        await checkHandoffs(browser, html, label, viewport);
        await checkRejectedResults(browser, html, viewport);
        await checkSameNetworkAndRecovery(browser, html, viewport);
        console.log(`${label} ${viewport.width}px: changed-origin hostname/IP handoff, identity/operation guards, provisioning, rollback and reset passed`);
      }
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
