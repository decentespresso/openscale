export const APP_PROMPTS_UUID = "8b8dc7c0-6bf7-4bdf-9a2c-0ba9cb9a4a01";
export const MAX_PROMPT_BYTES = 18;
const HEARTBEAT = [3, 10, 3, 255, 255, 0, 10];

function writeValue(characteristic, bytes) {
  return characteristic.writeValueWithResponse
    ? characteristic.writeValueWithResponse(bytes)
    : characteristic.writeValue(bytes);
}

export async function connectAppPrompts(server) {
  let active = true;
  let queue = Promise.resolve();
  let promptCharacteristic = null;
  let locallyEnabled = false;
  let promptsEnabled = false, clearPending = false;
  const enqueue = (operation) => {
    const result = queue.then(() => {
      if (!active || !server.connected) throw new Error("Scale disconnected");
      return operation();
    });
    queue = result.catch(() => {});
    return result;
  };
  const disconnected = () => {
    active = false;
    promptCharacteristic = null;
    promptsEnabled = false;
    locallyEnabled = false;
    clearPending = false;
    server.device.removeEventListener("gattserverdisconnected", disconnected);
  };
  server.device.addEventListener("gattserverdisconnected", disconnected);
  let commandCharacteristic;
  try {
    const service = await enqueue(() => server.getPrimaryService(0xfff0));
    commandCharacteristic = await enqueue(() => service.getCharacteristic(0x36f5));
    await enqueue(() => writeValue(commandCharacteristic, Uint8Array.from(HEARTBEAT)));
    const discoverPrompts = async () => {
      try {
        const characteristic = await enqueue(() => service.getCharacteristic(APP_PROMPTS_UUID));
        const capabilities = await enqueue(() => characteristic.readValue());
        if (active && server.connected && (capabilities.byteLength === 3 ||
            (capabilities.byteLength === 4 && capabilities.getUint8(3) <= 1)) && capabilities.getUint8(0) === 1 &&
            capabilities.getUint8(1) === MAX_PROMPT_BYTES && capabilities.getUint8(2) === 10) {
          promptCharacteristic = characteristic;
          promptsEnabled = true;
          locallyEnabled = capabilities.byteLength === 3 || capabilities.getUint8(3) === 1;
        }
      } catch {}
    };
    let discoveryTimer;
    try {
      await Promise.race([discoverPrompts(), new Promise((_, reject) => {
        discoveryTimer = setTimeout(() => {
          if (active) {
            disconnected();
            try { server.disconnect(); } catch {}
          }
          reject(new Error("App Prompts discovery timed out; reconnect the scale"));
        }, 2500);
      })]);
    } finally {
      clearTimeout(discoveryTimer);
    }
    if (!active || !server.connected) throw new Error("Scale disconnected");
  } catch (error) {
    server.device.removeEventListener("gattserverdisconnected", disconnected);
    throw error;
  }
  const writeClear = async () => {
    const characteristic = promptCharacteristic;
    if (!active || !server.connected || !characteristic) return false;
    try {
      await writeValue(characteristic, Uint8Array.of(0));
      clearPending = false;
      return true;
    } catch {
      promptsEnabled = false;
      clearPending = true;
      return false;
    }
  };
  const sendPrompt = async (bytes) => {
    if (!promptsEnabled || !active || !locallyEnabled) return false;
    try {
      return await enqueue(async () => {
        if (!promptsEnabled) return false;
        try {
          await writeValue(promptCharacteristic, bytes);
          return true;
        } catch {
          promptsEnabled = false;
          clearPending = true;
          await writeClear();
          return false;
        }
      });
    } catch { return false; }
  };
  return Object.freeze({
    get supported() { return active && server.connected && promptsEnabled; },
    get enabled() { return active && server.connected && promptsEnabled && locallyEnabled; },
    send(bytes) {
      const frame = Uint8Array.from(bytes);
      return enqueue(async () => {
        const heartbeat = frame.length === HEARTBEAT.length && frame.every((byte, index) => byte === HEARTBEAT[index]);
        if (clearPending && heartbeat && !await writeClear()) throw new Error("Cannot clear stale scale prompt");
        if (!active || !server.connected) throw new Error("Scale disconnected");
        return writeValue(commandCharacteristic, frame);
      });
    },
    showPrompt(text) {
      if (typeof text !== "string" || text.length < 1 || text.length > MAX_PROMPT_BYTES ||
          !/^[\x20-\x7e]+$/.test(text)) {
        return Promise.reject(new RangeError("Prompt must contain 1-18 printable ASCII characters"));
      }
      const bytes = Uint8Array.from([1, ...Array.from(text, (character) => character.charCodeAt(0))]);
      return sendPrompt(bytes);
    },
    async clearPrompt() {
      if (!promptCharacteristic || !active) return false;
      try { return await enqueue(writeClear); } catch { return false; }
    },
    dispose() {
      disconnected();
    },
  });
}

export function ingredientPrompt(action, ingredient, quantity = "") {
  const prefix = String(action || "Add").replace(/[^\x20-\x7e]/g, "").trim();
  const suffix = quantity ? `: ${quantity}` : "";
  if (!/^[\x20-\x7e]*$/.test(suffix) || prefix.length + suffix.length + 2 > MAX_PROMPT_BYTES) {
    throw new RangeError("Action and quantity leave no room for an ingredient");
  }
  const label = String(ingredient || "ingredient").normalize("NFKD")
    .replace(/[^\x20-\x7e]/g, "").trim();
  const room = MAX_PROMPT_BYTES - prefix.length - suffix.length - 1;
  return `${prefix} ${label.slice(0, room).trim()}${suffix}`;
}
