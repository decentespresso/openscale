const assert = require("node:assert/strict");
const path = require("node:path");
const { chromium } = require("playwright");

const url = process.env.HDS_TEST_APP_URL || "http://localhost:8765/test/";
const screenshots = process.env.HDS_TEST_SCREENSHOTS;

async function mockBluetooth(page, supported = true) {
  await page.addInitScript(({ supported }) => {
    const writes = [];
    let writing = false;
    let overlap = false;
    class Characteristic extends EventTarget {
      constructor(kind) { super(); this.kind = kind; }
      async startNotifications() { return this; }
      async readValue() { return new DataView(Uint8Array.of(1, 18, 10).buffer); }
      async writeValueWithResponse(bytes) {
        if (writing) overlap = true;
        writing = true;
        await new Promise((resolve) => setTimeout(resolve, 10));
        writing = false;
        if (this.kind === "prompt" && window.ble.failPrompt) throw new Error("Prompt write failed");
        writes.push({ kind: this.kind, bytes: Array.from(bytes) });
      }
    }
    const weight = new Characteristic("weight");
    const prompt = new Characteristic("prompt");
    const command = new Characteristic("command");
    const service = {
      async getCharacteristic(uuid) {
        if (uuid === 0xfff4) return weight;
        if (uuid === 0x36f5) return command;
        if (!supported) throw new Error("Characteristic not found");
        return prompt;
      },
    };
    const device = new EventTarget();
    device.name = "Decent Scale";
    device.gatt = {
      connected: false,
      device,
      async connect() { this.connected = true; return this; },
      async getPrimaryService() { return service; },
      disconnect() { this.connected = false; device.dispatchEvent(new Event("gattserverdisconnected")); },
    };
    Object.defineProperty(navigator, "bluetooth", { value: { async requestDevice() { return device; } } });
    window.ble = {
      writes,
      failPrompt: false,
      get overlap() { return overlap; },
      weight(grams, valid = true) {
        const packet = new Uint8Array(7);
        const view = new DataView(packet.buffer);
        packet[0] = 3;
        packet[1] = 0xce;
        view.setInt16(2, Math.round(grams * 10), false);
        packet[6] = packet.slice(0, 6).reduce((a, b) => a ^ b, 0) ^ (valid ? 0 : 1);
        weight.value = view;
        weight.dispatchEvent(new Event("characteristicvaluechanged"));
      },
    };
  }, { supported });
}

async function waitText(page, id, text) {
  await page.waitForFunction(({ id, text }) => document.getElementById(id).textContent === text, { id, text });
}

async function checkLayout(page) {
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false, "Horizontal overflow");
  const reading = await page.locator(".weight").boundingBox();
  const value = await page.locator("#weight").boundingBox();
  const unit = await page.locator(".weight > span").boundingBox();
  assert.ok(value.x >= reading.x && unit.x + unit.width <= reading.x + reading.width, "Weight exceeds its container");
  for (const selector of ["header button", "#weight", "#prompt", "#next", "#heartbeat-status"]) {
    assert.ok(await page.locator(selector).isVisible(), `${selector} is not visible`);
  }
}

(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
    const errors = [];
    page.on("pageerror", (error) => errors.push(error.message));
    await mockBluetooth(page);
    await page.goto(url);
    await page.locator("#connect").click();
    await waitText(page, "last-prompt", "Place glass; tare");
    await waitText(page, "connection", "Decent Scale");
    assert.equal(await page.evaluate(() => ble.writes.filter((write) => write.kind === "command" && write.bytes[1] === 15).length), 0, "Connection must not tare");
    await page.evaluate(() => ble.weight(-12.3));
    await waitText(page, "weight", "-12.3");
    await page.evaluate(() => ble.weight(999.9, false));
    await waitText(page, "weight", "-12.3");
    await page.locator("#tare").click();
    await page.waitForFunction(() => ble.writes.some((write) => write.kind === "command" && write.bytes.join() === "3,15,1,0,0,1,12"));
    for (const text of ["Add Gin: 35g", "Add Tonic: 100g", "Add 1 lime wedge", "Stir, then serve"]) {
      await page.locator("#next").click();
      await waitText(page, "last-prompt", text);
    }
    await page.locator("#finish").click();
    await waitText(page, "last-prompt", "Cleared");
    await waitText(page, "step-count", "Complete");
    await page.locator("#next").click();
    await waitText(page, "last-prompt", "Place glass; tare");
    await page.locator("#prompt").fill("Gin");
    await page.locator("#send").click();
    await waitText(page, "last-prompt", "Gin");
    await page.locator("#prompt").fill("caf\u00e9");
    assert.ok(await page.locator("#send").isDisabled());
    await page.locator("#prompt").fill("WWWWWWWWWWWWWWWWWW");
    await page.locator("#send").click();
    await waitText(page, "last-prompt", "WWWWWWWWWWWWWWWWWW");
    await checkLayout(page);
    if (screenshots) await page.screenshot({ path: path.join(screenshots, "desktop-ui.png"), fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    await checkLayout(page);
    if (screenshots) await page.screenshot({ path: path.join(screenshots, "mobile-ui.png"), fullPage: true });
    await page.setViewportSize({ width: 320, height: 720 });
    await page.evaluate(() => ble.weight(-3276.8));
    await waitText(page, "weight", "-3276.8");
    await checkLayout(page);
    await page.locator("#heartbeat").uncheck();
    await waitText(page, "heartbeat-status", "Paused");
    await page.evaluate(() => { ble.failPrompt = true; });
    await page.locator("#send").click();
    await waitText(page, "capability", "Unavailable / browser only");
    await page.locator("#heartbeat").check();
    await waitText(page, "connection", "Disconnected");
    assert.equal(await page.evaluate(() => ble.overlap), false, "Overlapping GATT writes");
    assert.deepEqual(errors, []);
    await page.close();

    const legacy = await browser.newPage();
    await mockBluetooth(legacy, false);
    await legacy.goto(url);
    await legacy.locator("#connect").click();
    await waitText(legacy, "connection", "Decent Scale");
    assert.ok(await legacy.locator("#send").isDisabled());
    await legacy.evaluate(() => ble.weight(135));
    await waitText(legacy, "weight", "135.0");
    await legacy.locator("#next").click();
    await waitText(legacy, "step-title", "Add gin");
    await legacy.locator("#connect").click();
    await waitText(legacy, "connection", "Disconnected");
    await legacy.close();
    console.log("PASS: local test app, mocked BLE recipe, tare, signed weight, invalid input, fallback, failed cleanup, serialized writes, desktop/mobile layout");
  } finally {
    await browser.close();
  }
})().catch((error) => { console.error(error); process.exitCode = 1; });
