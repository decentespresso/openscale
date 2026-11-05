export const APP_PROMPTS_UUID = "8b8dc7c0-6bf7-4bdf-9a2c-0ba9cb9a4a01";
export const MAX_PROMPT_BYTES = 18;
const MAX_SUPPORTED_PROMPT_BYTES = 128;
const HEARTBEAT = [3, 10, 3, 255, 255, 0, 10];

function writeValue(characteristic, bytes) {
  const properties = characteristic.properties;
  if (characteristic.writeValueWithResponse && (!properties || properties.write)) {
    return characteristic.writeValueWithResponse(bytes);
  }
  if (characteristic.writeValueWithoutResponse && properties && properties.writeWithoutResponse) {
    return characteristic.writeValueWithoutResponse(bytes);
  }
  return characteristic.writeValue(bytes);
}

export async function connectAppPrompts(server) {
  let active = true;
  let queue = Promise.resolve();
  let promptCharacteristic = null;
  let locallyEnabled = false;
  let maxPromptBytes = MAX_PROMPT_BYTES;
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
            capabilities.getUint8(1) > 0 && capabilities.getUint8(1) <= MAX_SUPPORTED_PROMPT_BYTES &&
            capabilities.getUint8(2) === 10) {
          promptCharacteristic = characteristic;
          maxPromptBytes = capabilities.getUint8(1);
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
    get maxPromptBytes() { return maxPromptBytes; },
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
      if (typeof text !== "string" || text.length < 1 || text.length > maxPromptBytes ||
          !/^[\x20-\x7e]+$/.test(text)) {
        return Promise.reject(new RangeError(`Prompt must contain 1-${maxPromptBytes} printable ASCII characters`));
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

const ascii = (text) => String(text || "").normalize("NFKD").replace(/[^\x20-\x7e]/g, "").replace(/\s+/g, " ").trim();

const trim = (text) => text.replace(/^[\s,;:&(/.-]+|[\s,;:&(/.-]+$/g, "");

function shorten(text, room) {
  if (text.length <= room) return text;
  const words = text.replace(/\s*\([^)]*\)/g, "").replace(/^the /i, "").split(" ");
  if (words.join(" ").length <= room) return words.join(" ");
  const ending = (k) => /^[a-z&]/.test(words[k]) ? "" : words.slice(k).join(" ");
  if (words.length >= 3) {
    for (let k = 2; k < words.length; k++) {
      const candidate = ending(k) && words[0] + " " + ending(k);
      if (candidate && candidate.length <= room) return trim(candidate);
    }
    for (let k = 1; k < words.length; k++) {
      const candidate = ending(k);
      if (candidate && candidate.length <= room) return trim(candidate);
    }
  }
  const start = words.join(" ");
  const space = start.slice(0, room + 1).lastIndexOf(" ");
  return trim(space >= 3 ? start.slice(0, space) : start.slice(0, room));
}

export function ingredientPrompt(action, ingredient, quantity = "", maxPromptBytes = MAX_PROMPT_BYTES) {
  if (!Number.isInteger(maxPromptBytes) || maxPromptBytes < 1 || maxPromptBytes > MAX_SUPPORTED_PROMPT_BYTES) {
    throw new RangeError("Invalid prompt length limit");
  }
  const verb = ascii(action);
  const prefix = !verb || /^add$/i.test(verb) ? "" : verb + " ";
  const label = ascii(ingredient) || "ingredient";
  const amount = ascii(quantity);
  if (maxPromptBytes > MAX_PROMPT_BYTES) {
    const text = prefix + label + (amount && amount !== "1" ? ": " + amount : "");
    if (text.length > maxPromptBytes) throw new RangeError("Ingredient exceeds prompt length limit");
    return text;
  }
  const words = new Set(label.split(" ").map(trim));
  const whole = (name) => name.split(" ").every((word) => words.has(word));
  const room = maxPromptBytes - prefix.length;
  if (room < 3) throw new RangeError("Action leaves no room for an ingredient");
  const withAmount = (separator) => {
    const suffix = separator + amount;
    const name = room - suffix.length >= Math.min(label.length, 6) && shorten(label, room - suffix.length);
    return name && (whole(name) || !whole(shorten(label, room))) ? { name, text: prefix + name + suffix } : null;
  };
  if (amount && amount !== "1") {
    const spaced = withAmount(": "), tight = withAmount(":");
    const best = tight && (!spaced || tight.name.length > spaced.name.length) ? tight : spaced;
    if (best) return best.text;
  }
  return prefix + shorten(label, room);
}
