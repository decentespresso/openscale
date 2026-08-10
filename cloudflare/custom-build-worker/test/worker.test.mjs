import assert from "node:assert/strict";
import { Buffer } from "node:buffer";
import { test } from "node:test";

import worker, { BuildCoordinator } from "../src/worker.mjs";


const encoder = new TextEncoder();
const token = "test-token-with-at-least-32-characters";
const origin = "https://decentespresso.github.io";


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


class Storage {
  constructor() {
    this.values = new Map();
  }

  async get(key) {
    return this.values.get(key);
  }

  async put(key, value) {
    if (typeof key === "object") {
      Object.entries(key).forEach(([entryKey, entryValue]) => this.values.set(entryKey, entryValue));
    } else {
      this.values.set(key, value);
    }
  }

  async delete(key) {
    this.values.delete(key);
  }

  async setAlarm() {
  }

  async transaction(action) {
    return action(this);
  }
}


class CoordinatorNamespace {
  constructor(env) {
    this.coordinator = new BuildCoordinator({storage: new Storage()}, env);
  }

  idFromName(name) {
    return name;
  }

  get() {
    return {
      fetch: (input, options) => this.coordinator.fetch(new Request(input, options)),
    };
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


async function api(env, path, method = "GET", body) {
  return worker.fetch(new Request(`https://example.test${path}`, {
    method,
    body: body === undefined ? undefined : JSON.stringify(body),
    headers: {
      Origin: origin,
      ...(body === undefined ? {} : {"Content-Type": "application/json"}),
    },
  }), env);
}


test("publishes immutable cache entries and deduplicates public builds", async () => {
  const commit = "1".repeat(40);
  const serviceCatalog = {
    schema: 1,
    firmware_refs: ["main"],
    platformio_environment: "esp32s3-custom",
    partition_schema: {path: "partitions/default.csv", sha256: "2".repeat(64)},
    features: {wifi: [], mdns: [], webserver: [], littlefs: []},
    plugins: {},
  };
  const keyPair = await crypto.subtle.generateKey(
    {name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256"},
    true,
    ["sign", "verify"],
  );
  const privateKey = await crypto.subtle.exportKey("pkcs8", keyPair.privateKey);
  const env = {
    ALLOWED_ORIGIN: origin,
    ALLOWED_FIRMWARE_REFS: "main",
    BUILDS: new Bucket(),
    GITHUB_APP_ID: "123",
    GITHUB_APP_INSTALLATION_ID: "456",
    GITHUB_APP_PRIVATE_KEY_PKCS8: Buffer.from(privateKey).toString("base64"),
    GITHUB_REPOSITORY: "decentespresso/openscale",
    GITHUB_WORKFLOW: "custom-build.yml",
    PUBLIC_BASE_URL: "https://example.test",
    RATE_LIMIT_SALT: "rate-limit-salt-with-at-least-32-characters",
    UPLOAD_TOKEN: token,
  };
  env.COORDINATOR = new CoordinatorNamespace(env);
  const originalFetch = globalThis.fetch;
  let dispatches = 0;
  globalThis.fetch = async (url, options = {}) => {
    const target = String(url);
    if (target.includes("/commits/main")) return Response.json({sha: commit});
    if (target.includes("raw.githubusercontent.com")) return Response.json(serviceCatalog);
    if (target.includes("/access_tokens")) return Response.json({token: "installation-token"});
    if (target.includes("/dispatches")) {
      dispatches += 1;
      const request = JSON.parse(options.body);
      assert.equal(request.inputs.source_commit, commit);
      if (dispatches === 1) {
        assert.equal(request.inputs.combination_hash, "8b9b767cb97b5b2b2c8aec859219e8f9d207bab9cd41c0a986b2dab55e03bb86");
      }
      return new Response(null, {status: 204});
    }
    throw new Error(`unexpected fetch: ${target}`);
  };
  try {
    const selection = {firmware_ref: "main", features: ["wifi"], plugins: []};
    const missing = await api(env, "/api/v1/status", "POST", selection);
    assert.equal(missing.status, 200);
    const missingStatus = await missing.json();
    const hash = missingStatus.combination_hash;
    assert.equal(hash, "8b9b767cb97b5b2b2c8aec859219e8f9d207bab9cd41c0a986b2dab55e03bb86");
    assert.equal(missingStatus.state, "missing");

    const unknown = await api(env, "/api/v1/status", "POST", {
      firmware_ref: "main", features: ["unknown"], plugins: [],
    });
    assert.equal(unknown.status, 400);

    const queued = await api(env, "/api/v1/build", "POST", selection);
    assert.equal(queued.status, 202);
    assert.equal((await queued.json()).state, "queued");
    const duplicate = await api(env, "/api/v1/build", "POST", selection);
    assert.equal(duplicate.status, 202);
    assert.equal(dispatches, 1);

    const building = await worker.fetch(new Request(`https://example.test/internal/v1/status/${hash}`, {
      method: "PUT",
      body: JSON.stringify({state: "building"}),
      headers: {Authorization: `Bearer ${token}`, "Content-Type": "application/json"},
    }), env);
    assert.equal(building.status, 204);
    assert.equal((await (await api(env, `/api/v1/status/${hash}`)).json()).state, "building");

    const payloads = {
      "firmware.bin": "firmware",
      "bootloader.bin": "bootloader",
      "partitions.bin": "partitions",
      "littlefs.bin": "littlefs",
      "dependencies.txt": "dependencies",
    };
    assert.equal((await put(env, hash, "firmware.bin", payloads["firmware.bin"], "wrong-token-with-at-least-32-chars")).status, 401);
    for (const [filename, payload] of Object.entries(payloads)) {
      assert.equal((await put(env, hash, filename, payload)).status, 201);
    }
    const manifestBinaries = {};
    for (const [filename, payload] of Object.entries(payloads)) {
      if (filename.endsWith(".bin")) {
        manifestBinaries[filename] = {
          bytes: encoder.encode(payload).byteLength,
          sha256: await sha256(payload),
        };
      }
    }
    const completeManifest = {
      combination_hash: hash,
      binaries: manifestBinaries,
      dependencies: {
        bytes: encoder.encode(payloads["dependencies.txt"]).byteLength,
        sha256: await sha256(payloads["dependencies.txt"]),
      },
    };
    assert.equal((await put(env, hash, "build-manifest.json", JSON.stringify(completeManifest))).status, 201);
    const readyUpdate = await worker.fetch(new Request(`https://example.test/internal/v1/status/${hash}`, {
      method: "PUT",
      body: JSON.stringify({state: "ready"}),
      headers: {Authorization: `Bearer ${token}`, "Content-Type": "application/json"},
    }), env);
    assert.equal(readyUpdate.status, 204);
    const ready = await (await api(env, `/api/v1/status/${hash}`)).json();
    assert.equal(ready.state, "ready");
    assert.equal(ready.downloads["firmware.bin"], `https://example.test/v1/${hash}/firmware.bin`);
    assert.equal((await put(env, hash, "firmware.bin", "replacement")).status, 409);

    const preflight = await worker.fetch(new Request("https://example.test/api/v1/status", {
      method: "OPTIONS",
      headers: {Origin: origin, "Access-Control-Request-Method": "POST"},
    }), env);
    assert.equal(preflight.status, 204);
    assert.equal(preflight.headers.get("Access-Control-Allow-Origin"), origin);

    for (const feature of ["mdns", "webserver"]) {
      const accepted = await api(env, "/api/v1/build", "POST", {
        firmware_ref: "main", features: [feature], plugins: [],
      });
      assert.equal(accepted.status, 202);
    }
    const limited = await api(env, "/api/v1/build", "POST", {
      firmware_ref: "main", features: ["littlefs"], plugins: [],
    });
    assert.equal(limited.status, 429);

    const rejectedOrigin = await worker.fetch(new Request("https://example.test/api/v1/status", {
      method: "POST",
      body: JSON.stringify(selection),
      headers: {Origin: "https://example.invalid", "Content-Type": "application/json"},
    }), env);
    assert.equal(rejectedOrigin.status, 403);
  } finally {
    globalThis.fetch = originalFetch;
  }
});
