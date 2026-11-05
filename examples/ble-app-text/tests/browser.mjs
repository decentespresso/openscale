import assert from "node:assert/strict";
import { APP_PROMPTS_UUID, connectAppPrompts, ingredientPrompt } from "../browser/hds-app-prompts.mjs";

assert.equal(APP_PROMPTS_UUID, "8b8dc7c0-6bf7-4bdf-9a2c-0ba9cb9a4a01");
const heartbeat = [3, 10, 3, 255, 255, 0, 10];

function scale({ present = true, capabilities = [1, 18, 10, 1], failPrompt = false, afterClear = null, withoutResponse = false } = {}) {
  const writes = [];
  let inFlight = 0;
  let promptFailures = failPrompt ? Infinity : 0, displayed = "";
  const write = (kind) => async (bytes) => {
    assert.equal(inFlight++, 0);
    await new Promise((resolve) => setTimeout(resolve, 2));
    writes.push([kind, ...bytes]);
    inFlight--;
    if (kind === "prompt" && promptFailures > 0) { promptFailures--; throw new Error("write failed"); }
    if (kind === "prompt") displayed = bytes[0] === 0 ? "" : String.fromCharCode(...bytes.slice(1));
    if (kind === "prompt" && bytes[0] === 0 && afterClear) afterClear(server);
  };
  const device = new EventTarget();
  const server = {
    connected: true, device,
    async getPrimaryService(uuid) {
      assert.equal(uuid, 0xfff0);
      return {
        async getCharacteristic(uuid) {
          if (uuid === 0x36f5) return withoutResponse
            ? { properties: { write: false, writeWithoutResponse: true }, writeValueWithoutResponse: write("command"),
                writeValueWithResponse() { throw new Error("Write with response unsupported"); } }
            : { writeValueWithResponse: write("command") };
          assert.equal(uuid, APP_PROMPTS_UUID);
          if (!present) throw new Error("NotFoundError");
          return {
            readValue: async () => new DataView(Uint8Array.from(capabilities).buffer),
            writeValueWithResponse: write("prompt"),
          };
        },
      };
    },
  };
  return { server, writes, get displayed() { return displayed; }, failNextPrompts(count) { promptFailures = count; } };
}

const supportedScale = scale();
const supported = await connectAppPrompts(supportedScale.server);
assert.equal(supported.supported, true);
assert.equal(supported.enabled, true);
assert.equal(supported.maxPromptBytes, 18);
assert.deepEqual(await Promise.all([
  supported.showPrompt("Add Gin: 35g"), supported.send([3, 15, 1]), supported.clearPrompt(),
]), [true, undefined, true]);
assert.deepEqual(supportedScale.writes, [
  ["command", ...heartbeat],
  ["prompt", 1, ...new TextEncoder().encode("Add Gin: 35g")], ["command", 3, 15, 1], ["prompt", 0],
]);
for (const text of ["", "A".repeat(19), "Gin\n", "\u00e9", null]) {
  await assert.rejects(supported.showPrompt(text), RangeError);
}
assert.equal(await supported.showPrompt("A".repeat(18)), true);
const mutable = [3, 10];
const sent = supported.send(mutable);
mutable[1] = 255;
await sent;
assert.deepEqual(supportedScale.writes.at(-1), ["command", 3, 10]);

for (const options of [{ present: false }, { capabilities: [2, 18, 10] }, { capabilities: [1] }]) {
  const olderScale = scale(options);
  const older = await connectAppPrompts(olderScale.server);
  assert.equal(older.supported, false);
  assert.equal(await older.showPrompt("Add Gin: 35g"), false);
  await older.send([3, 10]);
  assert.deepEqual(olderScale.writes, [["command", ...heartbeat], ["command", 3, 10]]);
  older.dispose();
}
const failingScale = scale({ failPrompt: true });
const failing = await connectAppPrompts(failingScale.server);
assert.equal(await failing.showPrompt("Add Gin: 35g"), false);
assert.equal(failing.supported, false);
await failing.send([3, 10]);
assert.deepEqual(failingScale.writes.at(-1), ["command", 3, 10]);

for (const recovery of ["clear", "heartbeat"]) {
  const recoveringScale = scale();
  const recovering = await connectAppPrompts(recoveringScale.server);
  await recovering.showPrompt("Add Gin: 35g");
  recoveringScale.failNextPrompts(2);
  assert.deepEqual(await Promise.all([
    recovering.showPrompt("Add Rum: 20g"), recovering.showPrompt("Queued instruction"),
  ]), [false, false]);
  assert.equal(recoveringScale.displayed, "Add Gin: 35g");
  assert.equal(recovering.supported, false);
  if (recovery === "clear") assert.equal(await recovering.clearPrompt(), true);
  else await recovering.send(heartbeat);
  assert.equal(recoveringScale.displayed, "");
  assert.equal(recovering.supported, false);
  assert.equal(await recovering.showPrompt("Stay browser-only"), false);
  assert.ok(!recoveringScale.writes.some(([kind, op, ...bytes]) =>
    kind === "prompt" && op === 1 && String.fromCharCode(...bytes) === "Queued instruction"));
  recovering.dispose();
}

const staleScale = scale();
const stale = await connectAppPrompts(staleScale.server);
await stale.showPrompt("Add Gin: 35g");
staleScale.failNextPrompts(Infinity);
assert.equal(await stale.showPrompt("Add Rum: 20g"), false);
await assert.rejects(stale.send(heartbeat), /clear stale/);
assert.ok(!staleScale.writes.slice(1).some(([kind, ...bytes]) => kind === "command" &&
  bytes.length === heartbeat.length && bytes.every((byte, index) => byte === heartbeat[index])));
await stale.send([3, 15, 1]);
assert.deepEqual(staleScale.writes.at(-1), ["command", 3, 15, 1]);
stale.dispose();

const interruptedScale = scale({ afterClear(server) {
  server.connected = false;
  server.device.dispatchEvent(new Event("gattserverdisconnected"));
  server.connected = true;
} });
const interrupted = await connectAppPrompts(interruptedScale.server);
await interrupted.showPrompt("Add Gin: 35g");
interruptedScale.failNextPrompts(2);
await interrupted.showPrompt("Add Rum: 20g");
await assert.rejects(interrupted.send(heartbeat), /disconnected/);
assert.equal(interrupted.supported, false);
assert.deepEqual(interruptedScale.writes.filter(([kind]) => kind === "command"), [["command", ...heartbeat]]);

const beforeDisconnect = supportedScale.writes.length;
const pending = supported.send([3, 15]);
supportedScale.server.connected = false;
supportedScale.server.device.dispatchEvent(new Event("gattserverdisconnected"));
supportedScale.server.connected = true;
await assert.rejects(pending, /disconnected/);
assert.equal(supported.supported, false);
assert.equal(await supported.showPrompt("Old prompt"), false);
assert.equal(supportedScale.writes.length, beforeDisconnect);

assert.equal(ingredientPrompt("Add", "Gin", "35g"), "Gin: 35g");
const longPrompt = ingredientPrompt("Add", "Plymouth Navy Strength Gin", "135.5g");
assert.ok(longPrompt.length <= 18 && longPrompt.endsWith(": 135.5g"));
assert.equal(ingredientPrompt("Add", "B\u00e9n\u00e9dictine", "5g"), "Benedictine: 5g");
assert.equal(ingredientPrompt("Add", "Gin", "A".repeat(18)), "Gin");
assert.equal(ingredientPrompt("Add", "Gin", "35g", 12), "Gin: 35g");
const fullPrompt = "Carpano Antica Sweet Vermouth: 31g";
assert.equal(ingredientPrompt("Add", "Carpano Antica Sweet Vermouth", "31g", 128), fullPrompt);
assert.equal(ingredientPrompt("Garnish", "Mint Sprig", "1", 128), "Garnish Mint Sprig");
assert.equal(ingredientPrompt("Rinse", "Absinthe", "5.0g", 128), "Rinse Absinthe: 5.0g");
assert.equal(ingredientPrompt("Add", "A".repeat(123), "31g", 128).length, 128);
assert.throws(() => ingredientPrompt("Add", "A".repeat(124), "31g", 128), RangeError);
for (const limit of [0, 129, -1, 1.5, NaN, "128"]) {
  assert.throws(() => ingredientPrompt("Add", "Gin", "35g", limit), RangeError);
}

for (const limit of [1, 17, 19, 64, 128]) {
  const device = scale({ capabilities: [1, limit, 10, 1] });
  const prompts = await connectAppPrompts(device.server);
  assert.equal(prompts.supported, true);
  assert.equal(prompts.maxPromptBytes, limit);
  assert.throws(() => { prompts.maxPromptBytes = 1; }, TypeError);
  const text = "A".repeat(limit);
  assert.equal(await prompts.showPrompt(text), true);
  assert.deepEqual(device.writes.at(-1), ["prompt", 1, ...new TextEncoder().encode(text)]);
  await assert.rejects(prompts.showPrompt(text + "A"), RangeError);
  await assert.rejects(prompts.showPrompt("caf\u00e9"), RangeError);
  assert.equal(await prompts.clearPrompt(), true);
  assert.equal(device.displayed, "");
  prompts.dispose();
}

for (const capabilities of [[1, 0, 10, 1], [1, 129, 10, 1], [1, 128, 10, 2], [1, 128, 9, 1],
  [2, 128, 10, 1], [1, 128], [1, 128, 10, 1, 0]]) {
  const prompts = await connectAppPrompts(scale({ capabilities }).server);
  assert.equal(prompts.supported, false);
  prompts.dispose();
}

const longScale = scale({ capabilities: [1, 128, 10, 1] });
const long = await connectAppPrompts(longScale.server);
assert.equal(await long.showPrompt(fullPrompt), true);
longScale.failNextPrompts(2);
assert.deepEqual(await Promise.all([long.showPrompt("A".repeat(128)), long.showPrompt(fullPrompt)]), [false, false]);
assert.equal(longScale.displayed, fullPrompt);
await long.send(heartbeat);
assert.equal(longScale.displayed, "");
long.dispose();

const commandOnly = scale({ withoutResponse: true });
const withoutResponse = await connectAppPrompts(commandOnly.server);
await withoutResponse.send(heartbeat);
assert.deepEqual(commandOnly.writes, [["command", ...heartbeat], ["command", ...heartbeat]]);
withoutResponse.dispose();

const disabledScale = scale({ capabilities: [1, 18, 10, 0] });
const disabled = await connectAppPrompts(disabledScale.server);
assert.equal(disabled.supported, true);
assert.equal(disabled.enabled, false);
assert.equal(await disabled.showPrompt("Add Gin: 35g"), false);
await disabled.send(heartbeat);
assert.equal(disabledScale.writes.some(([kind, operation]) => kind === "prompt" && operation === 1), false);
assert.equal(await disabled.clearPrompt(), true);
disabled.dispose();
const prototype = await connectAppPrompts(scale({ capabilities: [1, 18, 10] }).server);
assert.equal(prototype.enabled, true);
assert.equal(await prototype.showPrompt("Prototype"), true);
prototype.dispose();
console.log("App Text browser checks passed");
