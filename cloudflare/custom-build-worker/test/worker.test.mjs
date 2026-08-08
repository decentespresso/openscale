import assert from "node:assert/strict";
import { test } from "node:test";

import worker from "../src/worker.mjs";


const encoder = new TextEncoder();
const token = "test-token-with-at-least-32-characters";


class Bucket {
  constructor() {
    this.objects = new Map();
  }

  async put(key, body, options) {
    const bytes = new Uint8Array(body);
    this.objects.set(key, {
      body: bytes,
      size: bytes.byteLength,
      customMetadata: options.customMetadata,
      httpMetadata: options.httpMetadata,
      httpEtag: `"${key}"`,
    });
  }

  async head(key) {
    return this.objects.get(key) || null;
  }

  async get(key) {
    return this.objects.get(key) || null;
  }
}


async function sha256(payload) {
  const bytes = typeof payload === "string" ? encoder.encode(payload) : payload;
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, "0")).join("");
}


async function put(env, hash, filename, payload, authorization = token) {
  const bytes = typeof payload === "string" ? encoder.encode(payload) : payload;
  return worker.fetch(new Request(`https://example.test/internal/v1/${hash}/${filename}`, {
    method: "PUT",
    body: bytes,
    headers: {
      Authorization: `Bearer ${authorization}`,
      "X-OpenScale-SHA256": await sha256(bytes),
    },
  }), env);
}


test("publishes complete immutable cache entries", async () => {
  const env = {
    ALLOWED_ORIGIN: "https://decentespresso.github.io",
    BUILDS: new Bucket(),
    UPLOAD_TOKEN: token,
  };
  const hash = "a".repeat(64);
  const payloads = {
    "firmware.bin": "firmware",
    "bootloader.bin": "bootloader",
    "partitions.bin": "partitions",
    "littlefs.bin": "littlefs",
    "dependencies.txt": "dependencies",
  };
  assert.equal((await put(env, hash, "firmware.bin", payloads["firmware.bin"], "wrong-token-with-at-least-32-chars")).status, 401);
  const missing = await worker.fetch(new Request(`https://example.test/v1/${hash}/firmware.bin`), env);
  assert.equal(missing.status, 404);
  const binaries = {};
  for (const [filename, payload] of Object.entries(payloads)) {
    assert.equal((await put(env, hash, filename, payload)).status, 201);
    if (filename.endsWith(".bin")) {
      binaries[filename] = { bytes: encoder.encode(payload).byteLength, sha256: await sha256(payload) };
    }
  }
  const manifest = JSON.stringify({
    combination_hash: hash,
    binaries,
    dependencies: {
      bytes: encoder.encode(payloads["dependencies.txt"]).byteLength,
      sha256: await sha256(payloads["dependencies.txt"]),
    },
  });
  assert.equal((await put(env, hash, "build-manifest.json", manifest)).status, 201);
  const download = await worker.fetch(new Request(`https://example.test/v1/${hash}/firmware.bin`, {
    headers: { Origin: env.ALLOWED_ORIGIN },
  }), env);
  assert.equal(download.status, 200);
  assert.equal(await download.text(), "firmware");
  assert.equal(download.headers.get("Access-Control-Allow-Origin"), env.ALLOWED_ORIGIN);
  assert.equal((await put(env, hash, "firmware.bin", "replacement")).status, 409);
});
