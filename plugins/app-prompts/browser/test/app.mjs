import { connectAppPrompts, MAX_PROMPT_BYTES } from "../hds-app-prompts.mjs";

const byId = (id) => document.getElementById(id);
const heartbeat = Object.freeze([3, 10, 3, 255, 255, 0, 10]);
const tare = Object.freeze([3, 15, 1, 0, 0, 1, 12]);
const steps = Object.freeze([
  Object.freeze({ title: "Place glass", detail: "Place an empty glass on the scale, then tare.", prompt: "Place glass; tare" }),
  Object.freeze({ title: "Add gin", detail: "Add 35 g of gin.", prompt: "Add Gin: 35g" }),
  Object.freeze({ title: "Add tonic", detail: "Add 100 g of tonic. The scale still shows cumulative weight.", prompt: "Add Tonic: 100g" }),
  Object.freeze({ title: "Garnish", detail: "Add one lime wedge.", prompt: "Add 1 lime wedge" }),
  Object.freeze({ title: "Serve", detail: "Stir gently, then serve.", prompt: "Stir, then serve" }),
]);
let session = null;
let generation = 0;
let connecting = false;
let busy = false;
let stepIndex = 0;
let finished = false;
let lastReading = 0;

function log(message) {
  const entry = document.createElement("li");
  const time = document.createElement("time");
  time.textContent = new Date().toLocaleTimeString();
  entry.append(time, document.createTextNode(message));
  byId("events").prepend(entry);
  while (byId("events").children.length > 20) byId("events").lastElementChild.remove();
}

function notice(message = "") {
  byId("notice").textContent = message;
  byId("notice").hidden = !message;
}

function validPrompt() {
  const text = byId("prompt").value;
  return text.length > 0 && text.length <= MAX_PROMPT_BYTES && /^[\x20-\x7e]+$/.test(text);
}

function render() {
  const connected = Boolean(session?.helper);
  byId("connect").textContent = connecting || connected ? "Disconnect" : "Connect scale";
  byId("connect").disabled = !window.isSecureContext || !navigator.bluetooth;
  byId("tare").disabled = !connected || busy;
  byId("send").disabled = !session?.helper?.supported || busy || !validPrompt();
  byId("clear").disabled = !connected || busy;
  byId("previous").disabled = stepIndex === 0 || busy;
  byId("next").disabled = busy || (!finished && stepIndex === steps.length - 1);
  byId("finish").disabled = busy || finished;
  byId("count").textContent = `${byId("prompt").value.length} / ${MAX_PROMPT_BYTES}`;
  byId("prompt-error").textContent = validPrompt() ? "" : "Use 1-18 printable ASCII characters.";
  byId("next").textContent = finished ? "Restart recipe" : "Next step \u2192";
  byId("step-count").textContent = finished ? "Complete" : `Step ${stepIndex + 1} / ${steps.length}`;
  byId("progress").value = finished ? steps.length : stepIndex + 1;
  byId("step-title").textContent = finished ? "Enjoy your drink" : steps[stepIndex].title;
  byId("step-detail").textContent = finished ? "" : steps[stepIndex].detail;
  byId("heartbeat-status").textContent = !connected ? "Idle" : byId("heartbeat").checked ? "On" : "Paused";
}

function receiveWeight(current, event) {
  if (session !== current) return;
  const value = event.target.value;
  if (value.byteLength !== 7 || value.getUint8(0) !== 3) return;
  if (![0xce, 0xca].includes(value.getUint8(1))) return;
  const checksum = Array.from({ length: 6 }, (_, index) => value.getUint8(index)).reduce((a, b) => a ^ b, 0);
  if (checksum !== value.getUint8(6)) return;
  byId("weight").textContent = (value.getInt16(2, false) / 10).toFixed(1);
  byId("weight-status").textContent = "Live";
  lastReading = Date.now();
}

function release(current) {
  clearTimeout(current.heartbeatTimer);
  current.weightCharacteristic?.removeEventListener("characteristicvaluechanged", current.onWeight);
  current.device?.removeEventListener("gattserverdisconnected", current.onDisconnect);
  current.helper?.dispose();
  if (current.device?.gatt.connected) current.device.gatt.disconnect();
}

function disconnect(message = "Disconnected") {
  generation += 1;
  const previous = session;
  session = null;
  connecting = false;
  busy = false;
  if (previous) release(previous);
  byId("connection").textContent = "Disconnected";
  byId("capability").textContent = "Not checked";
  byId("last-prompt").textContent = "None";
  byId("weight").textContent = "--.-";
  byId("weight-status").textContent = "No readings";
  lastReading = 0;
  log(message);
  render();
}

function assertCurrent(current) {
  if (session !== current || current.generation !== generation) throw new Error("Connection cancelled");
}

async function sendHeartbeat(current) {
  if (session !== current || !current.helper || !byId("heartbeat").checked) return;
  try {
    await current.helper.send(heartbeat);
  } catch (error) {
    if (session === current) {
      notice(`Connection ended: ${error.message}`);
      disconnect("Heartbeat failed; disconnected");
    }
  }
}

function scheduleHeartbeat(current) {
  current.heartbeatTimer = setTimeout(async () => {
    await sendHeartbeat(current);
    if (session === current) scheduleHeartbeat(current);
  }, 4000);
}

async function connect() {
  if (session || connecting) return disconnect();
  const current = { generation: ++generation };
  session = current;
  connecting = true;
  byId("connection").textContent = "Connecting";
  notice();
  render();
  try {
    const device = await navigator.bluetooth.requestDevice({ filters: [{ name: "Decent Scale" }], optionalServices: [0xfff0] });
    assertCurrent(current);
    current.device = device;
    current.onDisconnect = () => { if (session === current) disconnect("Scale disconnected"); };
    device.addEventListener("gattserverdisconnected", current.onDisconnect);
    const server = await device.gatt.connect();
    if (session !== current) {
      if (session?.device !== device && server.connected) server.disconnect();
      throw new Error("Connection cancelled");
    }
    const service = await server.getPrimaryService(0xfff0);
    assertCurrent(current);
    current.weightCharacteristic = await service.getCharacteristic(0xfff4);
    assertCurrent(current);
    current.onWeight = (event) => receiveWeight(current, event);
    current.weightCharacteristic.addEventListener("characteristicvaluechanged", current.onWeight);
    await current.weightCharacteristic.startNotifications();
    assertCurrent(current);
    const helper = await connectAppPrompts(server);
    if (session !== current) {
      helper.dispose();
      throw new Error("Connection cancelled");
    }
    current.helper = helper;
    await sendHeartbeat(current);
    assertCurrent(current);
    byId("capability").textContent = helper.supported ? "App Prompts v1 / 18 characters" : "Unsupported / browser only";
    if (!finished) await showPrompt(current, steps[stepIndex].prompt);
    assertCurrent(current);
    connecting = false;
    byId("connection").textContent = device.name || "Connected";
    log(helper.supported ? "Connected; App Prompts v1 detected" : "Connected; using browser-only steps");
    scheduleHeartbeat(current);
    render();
  } catch (error) {
    if (session !== current) return;
    disconnect("Connection not established");
    notice(error.name === "NotFoundError" ? "No scale selected." : `Could not connect: ${error.message}`);
  }
}

async function operate(operation) {
  if (busy) return;
  const current = session;
  busy = true;
  render();
  try {
    await operation(current);
  } catch (error) {
    if (session === current) notice(error.message);
  } finally {
    if (session === current) {
      busy = false;
      render();
    }
  }
}

async function showPrompt(current, text) {
  if (!current?.helper?.supported) return;
  const sent = await current.helper.showPrompt(text);
  if (session !== current) return;
  if (sent) {
    byId("last-prompt").textContent = text;
    log(`Prompt write accepted: ${text}`);
  } else {
    byId("capability").textContent = "Unavailable / browser only";
    byId("last-prompt").textContent = "Unconfirmed";
    log("Prompt unavailable; browser-only steps continue");
  }
}

async function clearPrompt(current) {
  if (!current?.helper) return;
  const cleared = await current.helper.clearPrompt();
  if (session !== current) return;
  byId("last-prompt").textContent = cleared ? "Cleared" : "Unconfirmed";
  log(cleared ? "Clear write accepted" : "Clear unavailable; disconnect to remove any prompt");
}

function changeStep(index) {
  return operate(async (current) => {
    stepIndex = index;
    finished = false;
    render();
    await showPrompt(current, steps[index].prompt);
  });
}

byId("connect").addEventListener("click", connect);
byId("prompt").addEventListener("input", render);
byId("prompt-form").addEventListener("submit", (event) => {
  event.preventDefault();
  if (validPrompt()) operate((current) => showPrompt(current, byId("prompt").value));
});
byId("clear").addEventListener("click", () => operate(clearPrompt));
byId("tare").addEventListener("click", () => operate(async (current) => {
  if (!current?.helper) return;
  await current.helper.send(tare);
  if (session === current) log("Tare write accepted");
}));
byId("previous").addEventListener("click", () => changeStep(Math.max(0, stepIndex - 1)));
byId("next").addEventListener("click", () => changeStep(finished ? 0 : Math.min(steps.length - 1, stepIndex + 1)));
byId("finish").addEventListener("click", () => operate(async (current) => {
  finished = true;
  render();
  await clearPrompt(current);
}));
byId("heartbeat").addEventListener("change", () => {
  log(byId("heartbeat").checked ? "Heartbeat resumed" : "Heartbeat paused; scale may disconnect before the 10 s lease expires");
  if (session?.helper && byId("heartbeat").checked) sendHeartbeat(session);
  render();
});
window.addEventListener("pagehide", () => { if (session) disconnect("Page closed"); });
setInterval(() => {
  if (session?.helper && lastReading && Date.now() - lastReading > 3000) byId("weight-status").textContent = "Stale";
}, 1000);
byId("origin").textContent = location.host;
if (!window.isSecureContext) notice("Web Bluetooth requires HTTPS or localhost. For the USB-connected tablet, open http://localhost:8765/test/ after USB forwarding is ready.");
else if (!navigator.bluetooth) notice("Web Bluetooth is unavailable. Open this app in Android Chrome or a supported desktop browser.");
render();
