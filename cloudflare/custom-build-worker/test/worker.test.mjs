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
  const previewCommit = "7".repeat(40);
  const dependencyPlugin = (depends_on = []) => ({
    version: "1.0.0",
    firmware_refs: ["main"],
    requires: [],
    depends_on,
    conflicts: [],
    conflicts_features: [],
    patches: {},
    assets: [],
  });
  const serviceCatalog = {
    schema: 2,
    custom_ota_signing_key_generation: 1,
    catalog_revision: "a".repeat(64),
    firmware_refs: ["main", "v1.2.3", "v3.1.14-preview.1"],
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
      "v3.1.14-preview.1": {
        custom_version: "3.1.14-preview.1-custom",
        partition_schema: {path: "partitions/default.csv", sha256: "2".repeat(64)},
      },
    },
    features: {
      wifi: {requires: [], firmware_refs: ["main"]},
      mdns: {requires: [], firmware_refs: ["main"]},
      webserver: {requires: [], firmware_refs: ["main"]},
      littlefs: {requires: [], firmware_refs: ["main"]},
      "elegant-ota": {requires: [], firmware_refs: ["main"]},
      "energy-menu": {requires: [], firmware_refs: ["main"]},
      "stable-only": {requires: [], firmware_refs: ["v1.2.3"]},
    },
    plugins: {
      "asset-sort": {
        version: "1.0.0",
        firmware_refs: ["main"],
        requires: [],
        depends_on: ["base-plugin"],
        conflicts: [],
        conflicts_features: [],
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
        conflicts_features: [],
        patches: {},
        assets: [],
      },
      alpha: dependencyPlugin(),
      bravo: dependencyPlugin(["delta"]),
      charlie: dependencyPlugin(["alpha"]),
      delta: dependencyPlugin(),
      "feature-blocker": dependencyPlugin(),
      "wifi-client": {...dependencyPlugin(), requires: ["wifi"]},
      "wifi-root": dependencyPlugin(["wifi-client"]),
      "plugin-blocker": {...dependencyPlugin(), conflicts: ["delta"]},
      "compatibility-root": dependencyPlugin(["plugin-blocker", "bravo"]),
      "compatibility-recommender": {
        ...dependencyPlugin(),
        recommends: {features: [], plugins: ["plugin-blocker", "bravo"]},
      },
    },
  };
  serviceCatalog.plugins["feature-blocker"].conflicts_features = ["wifi"];
  const keyPair = await crypto.subtle.generateKey(
    {name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256"},
    true,
    ["sign", "verify"],
  );
  const privateKey = await crypto.subtle.exportKey("pkcs8", keyPair.privateKey);
  const env = {
    ALLOWED_ORIGIN: origin,
    ALLOWED_FIRMWARE_REFS: "main,v1.2.3,v3.1.14-preview.1",
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
  let failDispatch = false;
  let servedCatalog = serviceCatalog;
  globalThis.fetch = async (url, options = {}) => {
    const target = String(url);
    if (target.includes("/commits/main")) return Response.json({sha: commit});
    if (target.includes("/commits/v1.2.3")) return Response.json({sha: stableCommit});
    if (target.includes("/commits/v3.1.14-preview.1")) return Response.json({sha: previewCommit});
    if (target.includes("raw.githubusercontent.com")) return Response.json(servedCatalog);
    if (target.includes("/access_tokens")) {
      assert.deepEqual(JSON.parse(options.body), {
        repositories: ["openscale"], permissions: {actions: "write"},
      });
      return Response.json({token: "installation-token"});
    }
    if (target.includes("/dispatches")) {
      dispatches += 1;
      const request = JSON.parse(options.body);
      assert.equal(request.ref, "main");
      assert.equal(request.inputs.builder_ref, "main");
      assert.equal(request.inputs.builder_commit, commit);
      assert.equal(request.inputs.source_commit, commit);
      if (dispatches === 1) {
        assert.equal(request.inputs.combination_hash, "8c6df20ccb1b9855f19dce3868b9310ecb26238cccf48557843bd428ebbc1f63");
        assert.equal(request.inputs.features, ",");
        assert.equal(request.inputs.plugins, ",");
      }
      if (failDispatch) return Response.json({message: "unavailable"}, {status: 503});
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
    assert.equal(hash, "8c6df20ccb1b9855f19dce3868b9310ecb26238cccf48557843bd428ebbc1f63");
    assert.equal(missingStatus.state, "missing");

    const energyMenu = await api(env, "/api/v1/status", "POST", {
      firmware_ref: "main", features: ["energy-menu"], plugins: [],
    });
    assert.equal(
      (await energyMenu.json()).combination_hash,
      "4ac4a6589c1362b75e2f3c9b9abc50a395ae88926be7adaf56152409976f0023",
    );

    const stable = await api(env, "/api/v1/status", "POST", {
      firmware_ref: "v1.2.3", features: [], plugins: [],
    });
    assert.equal(stable.status, 200);
    assert.notEqual((await stable.json()).combination_hash, hash);

    const preview = await api(env, "/api/v1/status", "POST", {
      firmware_ref: "v3.1.14-preview.1", features: [], plugins: [],
    });
    assert.equal(preview.status, 200);
    assert.equal(
      (await preview.json()).combination_hash,
      "29cabc732c6ab83b66e11e330e966951602aa8963feeb6ba0057e5a714af9ae7",
    );

    const sortedAssets = await api(env, "/api/v1/status", "POST", {
      firmware_ref: "main", features: [], plugins: ["asset-sort"],
    });
    assert.equal((await sortedAssets.json()).combination_hash, "e60d7ab0a7e8437c018691616b13253d90e031654fac5e58b9169fb7ce83b002");

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

    const revised = await api(env, "/api/v1/status", "POST", {
      ...selection, catalog_revision: serviceCatalog.catalog_revision,
    });
    assert.equal(revised.status, 200);
    const stale = await api(env, "/api/v1/status", "POST", {
      ...selection, catalog_revision: "b".repeat(64),
    });
    assert.equal(stale.status, 409);
    assert.deepEqual(await stale.json(), {error: "catalog_stale"});
    assert.equal((await api(env, "/api/v1/status", "POST", {
      ...selection, catalog_revision: "invalid",
    })).status, 400);

    for (const conflictingSelection of [
      {firmware_ref: "main", features: ["wifi"], plugins: ["feature-blocker"]},
      {firmware_ref: "main", features: [], plugins: ["feature-blocker", "wifi-root"]},
      {firmware_ref: "main", features: [], plugins: ["plugin-blocker", "bravo"]},
    ]) {
      assert.equal((await api(env, "/api/v1/status", "POST", conflictingSelection)).status, 400);
    }
    for (const compatibleSelection of [
      {firmware_ref: "main", features: [], plugins: ["compatibility-root"]},
      {
        firmware_ref: "main",
        features: [],
        plugins: ["compatibility-recommender", "plugin-blocker", "bravo"],
      },
    ]) {
      assert.equal((await api(env, "/api/v1/status", "POST", compatibleSelection)).status, 200);
    }
    assert.equal((await api(env, "/api/v1/status", "POST", {
      firmware_ref: "main", features: ["stable-only"], plugins: [],
    })).status, 400);
    assert.equal((await api(env, "/api/v1/status", "POST", {
      firmware_ref: "v1.2.3", features: ["stable-only"], plugins: [],
    })).status, 200);

    servedCatalog = {...serviceCatalog, custom_ota_signing_key_generation: 2};
    const rotatedKey = await api(env, "/api/v1/status", "POST", selection);
    assert.notEqual((await rotatedKey.json()).combination_hash, hash);
    servedCatalog = {...serviceCatalog, custom_ota_signing_key_generation: 0};
    assert.equal((await api(env, "/api/v1/status", "POST", selection)).status, 503);
    servedCatalog = serviceCatalog;

    const {catalog_revision, ...legacyCatalog} = serviceCatalog;
    servedCatalog = {
      ...legacyCatalog,
      features: Object.fromEntries(
        Object.entries(serviceCatalog.features).map(([id, definition]) => [id, definition.requires]),
      ),
    };
    assert.equal((await api(env, "/api/v1/status", "POST", selection)).status, 200);
    servedCatalog = serviceCatalog;

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
      "firmware.bin": binaryPayloads["firmware.bin"],
      "littlefs.bin": binaryPayloads["littlefs.bin"],
      "ota-manifest.json": "ota manifest",
      "ota-manifest.sig": "ota signature",
    };
    assert.equal((await put(env, hash, "HDS_FW_custom.zip", payloads["HDS_FW_custom.zip"], "wrong-token-with-at-least-32-chars")).status, 401);
    assert.equal((await put(env, hash, "bootloader.bin", binaryPayloads["bootloader.bin"])).status, 404);
    assert.equal((await put(env, hash, "HDS_FW_custom.zip", new Uint8Array(12 * 1024 * 1024 + 1))).status, 413);
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
      custom_ota: {
        manifest: {
          bytes: encoder.encode(payloads["ota-manifest.json"]).byteLength,
          sha256: await sha256(payloads["ota-manifest.json"]),
        },
        signature: {
          bytes: encoder.encode(payloads["ota-manifest.sig"]).byteLength,
          sha256: await sha256(payloads["ota-manifest.sig"]),
        },
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
    assert.equal(ready.manifest_url, `https://example.test/v1/${hash}/build-manifest.json`);
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
    assert.equal(
      (await env.COORDINATOR.coordinator.state.storage.get(`build:${retryHash}`)).failure_code,
      "build_failed",
    );
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
      body: JSON.stringify({state: "failed", failure_code: "build_failed", attempt_id: secondAttempt}),
      headers: {Authorization: `Bearer ${token}`, "Content-Type": "application/json"},
    }), env)).status, 204);
    const terminal = await api(env, "/api/v1/build", "POST", retrySelection);
    assert.equal(terminal.status, 409);
    assert.deepEqual(await terminal.json(), {
      state: "failed",
      failure_code: "build_failed",
      combination_hash: retryHash,
      attempts: 2,
      max_attempts: 2,
      retryable: false,
      updated_at: (await env.COORDINATOR.coordinator.state.storage.get(`build:${retryHash}`)).updated_at,
    });
    failDispatch = true;
    const dispatchFailure = await api(env, "/api/v1/build", "POST", {
      firmware_ref: "main", features: [], plugins: ["alpha"],
    });
    assert.equal(dispatchFailure.status, 503);
    const dispatchStatus = await dispatchFailure.json();
    assert.equal(dispatchStatus.state, "failed");
    assert.equal(dispatchStatus.failure_code, "dispatch_failed");
    assert.equal(dispatchStatus.retryable, true);
    failDispatch = false;
    const burstFeatures = ["wifi", "mdns", "webserver", "littlefs", "elegant-ota"];
    const burstSelections = Array.from({length: 31}, (_, index) =>
      burstFeatures.filter((_, bit) => (index + 1) & (1 << bit)),
    ).filter(features => ![["mdns"], ["webserver"]].some(skip =>
      skip.length === features.length && skip.every((feature, index) => feature === features[index]),
    ));
    for (const features of burstSelections.slice(0, 17)) {
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
    env.GITHUB_REPOSITORY = "decentespresso/other";
    const wrongRepository = await api(env, "/api/v1/status", "POST", selection);
    assert.equal(wrongRepository.status, 503);
    assert.deepEqual(await wrongRepository.json(), {error: "github_repository_misconfigured"});
    env.GITHUB_REPOSITORY = "decentespresso/openscale";
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
  assert.equal(record.failure_code, "build_timeout");
  assert.equal(Object.hasOwn(record, "lease_expires_at"), false);
  assert.deepEqual(await storage.get("pending"), {});
});
