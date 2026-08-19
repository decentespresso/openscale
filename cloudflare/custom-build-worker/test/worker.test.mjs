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
  const stableCommit = "5".repeat(40);
  const dependencyPlugin = (depends_on = []) => ({
    version: "1.0.0",
    firmware_refs: ["main"],
    requires: [],
    depends_on,
    conflicts: [],
    patches: {},
    assets: [],
  });
  const serviceCatalog = {
    schema: 2,
    firmware_refs: ["main", "v1.2.3"],
    platformio_environment: "esp32s3-custom",
    firmware: {
      main: {
        custom_version: "3.1.13-dev-custom",
        partition_schema: {path: "partitions/default.csv", sha256: "2".repeat(64)},
      },
      "v1.2.3": {
        custom_version: "1.2.3-custom",
        partition_schema: {path: "partitions/default.csv", sha256: "2".repeat(64)},
      },
    },
    features: {
      wifi: [], mdns: [], webserver: [], littlefs: [], "elegant-ota": [], "energy-menu": [],
    },
    plugins: {
      "asset-sort": {
        version: "1.0.0",
        firmware_refs: ["main"],
        requires: [],
        depends_on: ["base-plugin"],
        conflicts: [],
        patches: {},
        assets: [
          {target: "plugins/z.txt", sha256: "3".repeat(64)},
          {target: "Plugins/A.txt", sha256: "4".repeat(64)},
        ],
      },
      "base-plugin": {
        version: "1.0.0",
        firmware_refs: ["main"],
        requires: ["wifi"],
        depends_on: [],
        conflicts: [],
        patches: {},
        assets: [],
      },
      alpha: dependencyPlugin(),
      bravo: dependencyPlugin(["delta"]),
      charlie: dependencyPlugin(["alpha"]),
      delta: dependencyPlugin(),
    },
  };
  const keyPair = await crypto.subtle.generateKey(
    {name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256"},
    true,
    ["sign", "verify"],
  );
  const privateKey = await crypto.subtle.exportKey("pkcs8", keyPair.privateKey);
  const env = {
    ALLOWED_ORIGIN: origin,
    ALLOWED_FIRMWARE_REFS: "main,v1.2.3",
    BUILDER_REF: "main",
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
    if (target.includes("/commits/v1.2.3")) return Response.json({sha: stableCommit});
    if (target.includes("raw.githubusercontent.com")) return Response.json(serviceCatalog);
    if (target.includes("/access_tokens")) return Response.json({token: "installation-token"});
    if (target.includes("/dispatches")) {
      dispatches += 1;
      const request = JSON.parse(options.body);
      assert.equal(request.ref, "main");
      assert.equal(request.inputs.builder_ref, "main");
      assert.equal(request.inputs.builder_commit, commit);
      assert.equal(request.inputs.source_commit, commit);
      if (dispatches === 1) {
        assert.equal(request.inputs.combination_hash, "280d0fb981fa56f7753846b33b91a5b053145d41360551f8c27c0b38e7ee955c");
        assert.equal(request.inputs.features, "");
        assert.equal(request.inputs.plugins, "");
      }
      return new Response(null, {status: 204});
    }
    throw new Error(`unexpected fetch: ${target}`);
  };
  try {
    const selection = {firmware_ref: "main", features: [], plugins: []};
    const missing = await api(env, "/api/v1/status", "POST", selection);
    assert.equal(missing.status, 200);
    const missingStatus = await missing.json();
    const hash = missingStatus.combination_hash;
    assert.equal(hash, "280d0fb981fa56f7753846b33b91a5b053145d41360551f8c27c0b38e7ee955c");
    assert.equal(missingStatus.state, "missing");

    const energyMenu = await api(env, "/api/v1/status", "POST", {
      firmware_ref: "main", features: ["energy-menu"], plugins: [],
    });
    assert.equal(
      (await energyMenu.json()).combination_hash,
      "35a0475ceda855f8ffcfa6c1c4896dd8e9cefce7f4458886f72c93acb69d4afe",
    );

    const stable = await api(env, "/api/v1/status", "POST", {
      firmware_ref: "v1.2.3", features: [], plugins: [],
    });
    assert.equal(stable.status, 200);
    assert.notEqual((await stable.json()).combination_hash, hash);

    const sortedAssets = await api(env, "/api/v1/status", "POST", {
      firmware_ref: "main", features: [], plugins: ["asset-sort"],
    });
    assert.equal((await sortedAssets.json()).combination_hash, "b4cb18df073d1db707c1e1bd7a0ce723af1bb04071cc09d93ef410a8b543d708");

    const dependencyRoots = await api(env, "/api/v1/status", "POST", {
      firmware_ref: "main", features: [], plugins: ["bravo", "charlie"],
    });
    const dependencyClosure = await api(env, "/api/v1/status", "POST", {
      firmware_ref: "main", features: [], plugins: ["alpha", "bravo", "charlie", "delta"],
    });
    assert.equal(
      (await dependencyRoots.json()).combination_hash,
      (await dependencyClosure.json()).combination_hash,
    );

    const unknown = await api(env, "/api/v1/status", "POST", {
      firmware_ref: "main", features: ["unknown"], plugins: [],
    });
    assert.equal(unknown.status, 400);

    const queued = await api(env, "/api/v1/build", "POST", selection);
    assert.equal(queued.status, 202);
    assert.equal((await queued.json()).state, "queued");
    const attemptId = (await env.COORDINATOR.coordinator.state.storage.get(`build:${hash}`)).attempt_id;
    const duplicate = await api(env, "/api/v1/build", "POST", selection);
    assert.equal(duplicate.status, 202);
    assert.equal(dispatches, 1);

    const building = await worker.fetch(new Request(`https://example.test/internal/v1/status/${hash}`, {
      method: "PUT",
      body: JSON.stringify({state: "building", attempt_id: attemptId}),
      headers: {Authorization: `Bearer ${token}`, "Content-Type": "application/json"},
    }), env);
    assert.equal(building.status, 204);
    assert.equal((await (await api(env, `/api/v1/status/${hash}`)).json()).state, "building");

    const binaryPayloads = {
      "firmware.bin": "firmware",
      "bootloader.bin": "bootloader",
      "partitions.bin": "partitions",
      "littlefs.bin": "littlefs",
    };
    const payloads = {
      "HDS_FW_custom.zip": "archive",
      "dependencies.txt": "dependencies",
    };
    assert.equal((await put(env, hash, "HDS_FW_custom.zip", payloads["HDS_FW_custom.zip"], "wrong-token-with-at-least-32-chars")).status, 401);
    assert.equal((await put(env, hash, "firmware.bin", binaryPayloads["firmware.bin"])).status, 404);
    assert.equal((await put(env, hash, "HDS_FW_custom.zip", new Uint8Array(4 * 1024 * 1024 + 1))).status, 413);
    for (const [filename, payload] of Object.entries(payloads)) {
      assert.equal((await put(env, hash, filename, payload)).status, 201);
    }
    const manifestBinaries = {};
    for (const [filename, payload] of Object.entries(binaryPayloads)) {
      manifestBinaries[filename] = {
        bytes: encoder.encode(payload).byteLength,
        sha256: await sha256(payload),
      };
    }
    const completeManifest = {
      combination_hash: hash,
      binaries: manifestBinaries,
      archive: {
        bytes: encoder.encode(payloads["HDS_FW_custom.zip"]).byteLength,
        sha256: await sha256(payloads["HDS_FW_custom.zip"]),
      },
      dependencies: {
        bytes: encoder.encode(payloads["dependencies.txt"]).byteLength,
        sha256: await sha256(payloads["dependencies.txt"]),
      },
    };
    assert.equal((await put(env, hash, "build-manifest.json", JSON.stringify(completeManifest))).status, 201);
    const readyUpdate = await worker.fetch(new Request(`https://example.test/internal/v1/status/${hash}`, {
      method: "PUT",
      body: JSON.stringify({state: "ready", attempt_id: attemptId}),
      headers: {Authorization: `Bearer ${token}`, "Content-Type": "application/json"},
    }), env);
    assert.equal(readyUpdate.status, 204);
    const ready = await (await api(env, `/api/v1/status/${hash}`)).json();
    assert.equal(ready.state, "ready");
    assert.deepEqual(Object.keys(ready.downloads), ["HDS_FW_custom.zip"]);
    assert.equal(ready.downloads["HDS_FW_custom.zip"], `https://example.test/v1/${hash}/HDS_FW_custom.zip`);
    assert.equal((await put(env, hash, "HDS_FW_custom.zip", "replacement")).status, 409);

    const preflight = await worker.fetch(new Request("https://example.test/api/v1/status", {
      method: "OPTIONS",
      headers: {Origin: origin, "Access-Control-Request-Method": "POST"},
    }), env);
    assert.equal(preflight.status, 204);
    assert.equal(preflight.headers.get("Access-Control-Allow-Origin"), origin);

    const retrySelection = {firmware_ref: "main", features: ["mdns"], plugins: []};
    const firstFailure = await api(env, "/api/v1/build", "POST", retrySelection);
    assert.equal(firstFailure.status, 202);
    const retryHash = (await firstFailure.json()).combination_hash;
    const firstAttempt = (await env.COORDINATOR.coordinator.state.storage.get(`build:${retryHash}`)).attempt_id;
    assert.equal((await worker.fetch(new Request(`https://example.test/internal/v1/status/${retryHash}`, {
      method: "PUT",
      body: JSON.stringify({state: "failed", attempt_id: firstAttempt}),
      headers: {Authorization: `Bearer ${token}`, "Content-Type": "application/json"},
    }), env)).status, 204);
    const retry = await api(env, "/api/v1/build", "POST", retrySelection);
    assert.equal(retry.status, 202);
    const secondAttempt = (await env.COORDINATOR.coordinator.state.storage.get(`build:${retryHash}`)).attempt_id;
    assert.notEqual(secondAttempt, firstAttempt);
    assert.equal((await worker.fetch(new Request(`https://example.test/internal/v1/status/${retryHash}`, {
      method: "PUT",
      body: JSON.stringify({state: "building", attempt_id: firstAttempt}),
      headers: {Authorization: `Bearer ${token}`, "Content-Type": "application/json"},
    }), env)).status, 409);
    assert.equal((await worker.fetch(new Request(`https://example.test/internal/v1/status/${retryHash}`, {
      method: "PUT",
      body: JSON.stringify({state: "failed", attempt_id: secondAttempt}),
      headers: {Authorization: `Bearer ${token}`, "Content-Type": "application/json"},
    }), env)).status, 204);
    const terminal = await api(env, "/api/v1/build", "POST", retrySelection);
    assert.equal(terminal.status, 409);
    assert.deepEqual(await terminal.json(), {
      state: "failed",
      combination_hash: retryHash,
      attempts: 2,
      max_attempts: 2,
      retryable: false,
      updated_at: (await env.COORDINATOR.coordinator.state.storage.get(`build:${retryHash}`)).updated_at,
    });
    const burstFeatures = ["wifi", "mdns", "webserver", "littlefs", "elegant-ota"];
    const burstSelections = Array.from({length: 31}, (_, index) =>
      burstFeatures.filter((_, bit) => (index + 1) & (1 << bit)),
    ).filter(features => ![["mdns"], ["webserver"]].some(skip =>
      skip.length === features.length && skip.every((feature, index) => feature === features[index]),
    ));
    for (const features of burstSelections.slice(0, 18)) {
      assert.equal((await api(env, "/api/v1/build", "POST", {
        firmware_ref: "main", features, plugins: [],
      })).status, 202);
    }
    const limited = await api(env, "/api/v1/build", "POST", {
      firmware_ref: "main", features: ["webserver"], plugins: [],
    });
    assert.equal(limited.status, 429);
    assert.ok(Number(limited.headers.get("Retry-After")) <= 7 * 24 * 60 * 60);
    const rates = await env.COORDINATOR.coordinator.state.storage.get("rates");
    assert.equal(rates.global, 21);
    assert.deepEqual(Object.values(rates.clients), [21]);
    assert.equal(Object.hasOwn(rates, "day"), false);
    assert.equal(new Date(rates.expires_at).getUTCDay(), 1);
    assert.equal(new Date(rates.expires_at).toISOString().slice(11), "00:00:00.000Z");
    await env.COORDINATOR.coordinator.state.storage.put("rates", {
      ...rates, global: 140, clients: {},
    });
    const globallyLimited = await api(env, "/api/v1/build", "POST", {
      firmware_ref: "main", features: ["webserver"], plugins: [],
    });
    assert.equal(globallyLimited.status, 429);
    await env.COORDINATOR.coordinator.state.storage.put("rates", {
      ...rates, week: rates.week - 1, global: 140, clients: {},
    });
    assert.equal((await api(env, "/api/v1/build", "POST", {
      firmware_ref: "main", features: ["webserver"], plugins: [],
    })).status, 202);

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


test("expires stale build attempts", async () => {
  const storage = new Storage();
  const coordinator = new BuildCoordinator({storage}, {});
  const hash = "a".repeat(64);
  const attemptId = "11111111-1111-4111-8111-111111111111";
  await storage.put({
    [`build:${hash}`]: {
      state: "queued",
      combination_hash: hash,
      attempts: 1,
      attempt_id: attemptId,
      lease_expires_at: 0,
      updated_at: new Date(0).toISOString(),
    },
    pending: {[hash]: {attempt_id: attemptId, lease_expires_at: 0}},
  });
  await coordinator.alarm();
  const record = await storage.get(`build:${hash}`);
  assert.equal(record.state, "failed");
  assert.equal(Object.hasOwn(record, "lease_expires_at"), false);
  assert.deepEqual(await storage.get("pending"), {});
});
