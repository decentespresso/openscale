import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import * as helper from "../browser/hds-app-prompts.mjs";

const source = readFileSync(process.argv[2], "utf8");
const heartbeat = [3, 10, 3, 255, 255, 0, 10];

function deferred() {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
}

async function settle() {
  for (let index = 0; index < 12; index++) await new Promise(setImmediate);
}

class Element {
  hidden = false;
  style = {};
  textContent = "";
  className = "";
  attributes = new Map();
  listeners = new Map();
  classList = { add() {}, remove() {} };
  addEventListener(type, listener) {
    this.listeners.set(type, [...(this.listeners.get(type) || []), listener]);
  }
  async click() {
    for (const listener of this.listeners.get("click") || []) await listener();
    if (this.onclick) await this.onclick();
    await settle();
  }
  querySelector() { return this.image ||= new Element(); }
  getAttribute(name) { return this.attributes.get(name); }
  setAttribute(name, value) { this.attributes.set(name, value); }
  removeAttribute(name) { this.attributes.delete(name); }
  appendChild() {}
}

function maker(options = {}) {
  const elements = new Map();
  const element = (id) => {
    if (!elements.has(id)) elements.set(id, new Element());
    return elements.get(id);
  };
  element("mk").hidden = true;
  const timers = new Map();
  let timerId = 0;
  let now = 0;
  const schedule = (callback, delay, repeat) => {
    const id = ++timerId;
    timers.set(id, { callback, delay, repeat });
    return id;
  };
  const writes = [];
  const inFlight = new Map();
  let notifications = false, reads = 0, connects = 0, disconnects = 0;
  let promptFailures = options.failPrompt ? Infinity : 0, displayed = "";
  let readGate = options.readGate, connectGate = options.connectGate, pickerGate = options.pickerGate;
  let tareGate = null, heartbeatGate = null;
  const device = new EventTarget();
  device.name = "Decent Scale";
  const write = (kind) => async (bytes) => {
    const connection = connects;
    assert.equal(inFlight.get(connection) || 0, 0, "GATT writes must be serialized");
    inFlight.set(connection, 1);
    try {
      const frame = Array.from(bytes);
      if (kind === "command" && frame[1] === 15 && tareGate) await tareGate.promise;
      if (kind === "command" && frame[1] === 10 && heartbeatGate) {
        const gate = heartbeatGate; heartbeatGate = null;
        await gate.promise;
        throw new Error("Old heartbeat failed");
      }
      writes.push({ kind, bytes: frame });
      if (kind === "prompt" && promptFailures > 0) { promptFailures--; throw new Error("Prompt transport failed"); }
      if (kind === "prompt") displayed = frame[0] === 0 ? "" : String.fromCharCode(...frame.slice(1));
    } finally { inFlight.set(connection, 0); }
  };
  const readChar = new EventTarget();
  readChar.startNotifications = async () => { notifications = true; return readChar; };
  const commandChar = { writeValue: write("command"), writeValueWithResponse: write("command") };
  const promptChar = {
    async readValue() {
      assert.ok(notifications, "Weight notifications must start before prompt discovery");
      reads++;
      const gate = readGate; readGate = null;
      if (gate) await gate.promise;
      return new DataView(Uint8Array.of(1, 18, 10).buffer);
    },
    writeValueWithResponse: write("prompt"),
  };
  const server = {
    device, connected: false,
    async connect() {
      connects++;
      const gate = connectGate; connectGate = null;
      if (gate) await gate.promise;
      this.connected = true;
      return this;
    },
    disconnect() {
      disconnects++;
      if (this.connected) {
        displayed = "";
        this.connected = false;
        device.dispatchEvent(new Event("gattserverdisconnected"));
      }
    },
    async getPrimaryService(uuid) {
      assert.equal(uuid, 0xfff0);
      return {
        async getCharacteristic(characteristic) {
          if (characteristic === 0xfff4) return readChar;
          if (characteristic === 0x36f5) return commandChar;
          assert.equal(characteristic, helper.APP_PROMPTS_UUID);
          if (options.legacy) throw new Error("NotFoundError");
          return promptChar;
        },
      };
    },
  };
  device.gatt = server;
  const window = {
    MAKER: {
      serve: options.serve ?? 3,
      steps: [
        { ing: "Gin", typ: "Gin", mg: 35000, mix: "Add" },
        { ing: "Bitters", qty: "2dashes", mix: "Add" },
        { ing: "Cherry", qty: "1", cnt: true, mix: "Garnish" },
      ],
      howto: "Stir, then serve",
    },
    pdtBatch: options.batch || 1,
    pdtScaleQ: (quantity, batch) => quantity.replace(/^\d+/, (amount) => Number(amount) * batch),
    addEventListener() {}, scrollTo() {},
  };
  const context = {
    window, document: { getElementById: element, createElement: () => new Element(), body: new Element() },
    navigator: {
      bluetooth: {
        async getDevices() { return options.granted ? [device] : []; },
        async requestDevice() {
          const gate = pickerGate; pickerGate = null;
          if (gate) await gate.promise;
          return device;
        },
      },
    },
    setTimeout: (callback, delay) => schedule(callback, delay, false),
    setInterval: (callback, delay) => schedule(callback, delay, true),
    clearTimeout: (id) => timers.delete(id), clearInterval: (id) => timers.delete(id),
    Date: class extends Date { static now() { return now; } },
    Uint8Array, console,
  };
  new vm.Script(source, {
    filename: process.argv[2],
    importModuleDynamically: async () => {
      if (options.missingModule) throw new Error("Module unavailable");
      return helper;
    },
  }).runInNewContext(context);
  return {
    element, server, writes, window,
    get displayed() { return displayed; },
    failNextPrompts(count) { promptFailures = count; },
    get reads() { return reads; }, get connects() { return connects; }, get disconnects() { return disconnects; },
    get intervals() { return [...timers.values()].filter((timer) => timer.repeat).length; },
    texts() { return writes.filter(({ kind, bytes }) => kind === "prompt" && bytes[0] === 1)
      .map(({ bytes }) => String.fromCharCode(...bytes.slice(1))); },
    holdTare(gate) { tareGate = gate; }, holdHeartbeat(gate) { heartbeatGate = gate; },
    async open() { window.openMaker(); await settle(); },
    async tick(delay, elapsed = delay) {
      now += elapsed;
      for (const [id, timer] of [...timers]) {
        if (timer.delay !== delay || !timers.has(id)) continue;
        if (!timer.repeat) timers.delete(id);
        timer.callback();
      }
      await settle();
    },
  };
}

async function connectedMaker(options) {
  const app = maker(options);
  await app.open();
  if (!options?.granted) await app.element("mkConnect").click();
  assert.equal(app.element("mkConnect").textContent, "Disconnect");
  return app;
}

async function testRecipeLifecycle() {
  const app = await connectedMaker({ batch: 2 });
  assert.equal(app.texts().at(-1), "Place shaker; tare");
  await app.element("mkTare").click();
  await app.tick(100, 400);
  assert.equal(app.texts().at(-1), "Add Gin: 70.0g");
  for (let index = 0; index < 4; index++) app.window.mkSim(70);
  await app.tick(1500);
  assert.equal(app.texts().at(-1), "Add Gin: 70.0g");
  await app.element("mkNext").click();
  assert.equal(app.texts().at(-1), "Add Bitt: 4 dashes");
  await app.element("mkNext").click();
  assert.equal(app.texts().at(-1), "Garnish Cherry: 2");
  await app.element("mkPrev").click();
  assert.equal(app.texts().at(-1), "Add Bitt: 4 dashes");
  await app.element("mkNext").click();
  await app.element("mkNext").click();
  assert.equal(app.texts().at(-1), "Mix, then serve");
  await app.element("mkNext").click();
  assert.deepEqual(app.writes.at(-1), { kind: "prompt", bytes: [0] });
  await app.tick(100, 2000);
  await app.element("mkBack").click();
  assert.ok(app.server.connected);
  assert.equal(app.disconnects, 0);
  assert.equal(app.intervals, 1);
  await app.tick(4000);
  assert.deepEqual(app.writes.at(-1), { kind: "command", bytes: heartbeat });
  await app.open();
  assert.equal(app.texts().at(-1), "Place shaker; tare");
  await app.element("mkConnect").click();
  assert.equal(app.server.connected, false);
  assert.equal(app.intervals, 0);
  await app.element("mkConnect").click();
  assert.equal(app.texts().at(-1), "Place shaker; tare");
}

async function testCancelledConnections() {
  for (const stage of ["pickerGate", "connectGate", "readGate"]) {
    const gate = deferred();
    const app = maker({ [stage]: gate });
    await app.open();
    const connection = app.element("mkConnect").click();
    await settle();
    if (stage === "readGate") assert.equal(app.reads, 1);
    await app.element("mkBack").click();
    gate.resolve();
    await connection; await settle();
    assert.ok(app.element("mk").hidden);
    assert.equal(app.server.connected, false, `${stage}: connects=${app.connects}, disconnects=${app.disconnects}, reads=${app.reads}`);
    assert.equal(app.intervals, 0);
    assert.deepEqual(app.writes, []);
  }
  const gate = deferred();
  const app = maker({ readGate: gate, granted: true });
  await app.open();
  assert.equal(app.reads, 1);
  await app.tick(6000);
  assert.equal(app.server.connected, false);
  gate.resolve(); await settle();
  assert.equal(app.intervals, 0);
  assert.deepEqual(app.writes, []);
  await app.element("mkConnect").click();
  assert.equal(app.texts().at(-1), "Place shaker; tare");
}

async function testCloseAndReopenDuringDiscovery() {
  const gate = deferred();
  const app = maker({ readGate: gate, granted: true });
  await app.open();
  assert.equal(app.reads, 1);
  await app.element("mkBack").click();
  await app.open();
  assert.equal(app.texts().at(-1), "Place shaker; tare");
  const disconnects = app.disconnects;
  gate.resolve(); await settle();
  assert.ok(app.server.connected);
  assert.equal(app.disconnects, disconnects);
  assert.equal(app.texts().length, 1);
  assert.equal(app.intervals, 1);
}

async function testStaleTareAndHeartbeat() {
  const app = await connectedMaker();
  await app.element("mkTare").click();
  await app.element("mkBack").click();
  const count = app.texts().length;
  await app.tick(500);
  assert.equal(app.texts().length, count);
  assert.deepEqual(app.writes.at(-1), { kind: "prompt", bytes: [0] });
  await app.open();
  const tare = deferred(); app.holdTare(tare);
  const pending = app.element("mkTare").click();
  await settle();
  await app.element("mkBack").click();
  await app.open();
  tare.resolve(); await pending; await app.tick(500);
  assert.equal(app.texts().at(-1), "Place shaker; tare");
  const beat = deferred(); app.holdHeartbeat(beat);
  await app.tick(4000);
  await app.element("mkConnect").click();
  await app.element("mkConnect").click();
  beat.resolve(); await settle();
  assert.ok(app.server.connected);
  assert.equal(app.intervals, 1);
}

async function testFallback() {
  for (const options of [{ legacy: true }, { missingModule: true }, { failPrompt: true }]) {
    const app = await connectedMaker(options);
    await app.element("mkTare").click(); await app.tick(500);
    assert.ok(app.writes.some(({ kind, bytes }) => kind === "command" && bytes[1] === 15));
    assert.equal(app.element("mkIng").textContent, "Gin");
    await app.element("mkBack").click();
    assert.ok(app.server.connected);
  }
}

async function testGarnishAfterServing() {
  const app = await connectedMaker({ serve: 2 });
  await app.element("mkTare").click();
  await app.tick(100, 400);
  for (let index = 0; index < 4; index++) app.window.mkSim(35);
  await app.element("mkNext").click();
  app.window.mkSim(0);
  await app.tick(100, 400);
  app.window.mkSim(2);
  await app.element("mkNext").click();
  assert.equal(app.texts().at(-1), "Mix, then serve");
  await app.element("mkNext").click();
  assert.equal(app.texts().at(-1), "Garnish Cherry: 1");
  await app.element("mkPrev").click();
  assert.equal(app.texts().at(-1), "Mix, then serve");
  await app.element("mkNext").click();
  await app.element("mkNext").click();
  assert.equal(app.displayed, "");
  assert.equal(app.element("mkWeight").textContent, "37.0");
}

async function testPromptCleanup() {
  for (const recovery of ["close", "complete", "heartbeat"]) {
    const app = await connectedMaker();
    app.failNextPrompts(2);
    await app.element("mkTare").click(); await app.tick(500);
    assert.equal(app.displayed, "Place shaker; tare");
    if (recovery === "close") await app.element("mkBack").click();
    if (recovery === "complete") {
      for (let index = 0; index < 4; index++) await app.element("mkNext").click();
    }
    await app.tick(100, 2000);
    await app.tick(4000);
    assert.equal(app.displayed, "", recovery);
    assert.ok(app.server.connected);
    assert.equal(app.intervals, 1);
  }
  const app = await connectedMaker();
  app.failNextPrompts(Infinity);
  await app.element("mkTare").click(); await app.tick(500);
  await app.element("mkBack").click();
  assert.equal(app.displayed, "Place shaker; tare");
  const beforeHeartbeat = app.writes.length;
  await app.tick(4000);
  assert.ok(!app.writes.slice(beforeHeartbeat).some(({ kind }) => kind === "command"));
  assert.equal(app.displayed, "");
  assert.equal(app.server.connected, false);
  assert.equal(app.intervals, 0);
}

await testRecipeLifecycle();
await testCancelledConnections();
await testCloseAndReopenDuringDiscovery();
await testStaleTareAndHeartbeat();
await testFallback();
await testGarnishAfterServing();
await testPromptCleanup();
console.log("App Prompts patched cocktail-site checks passed");
