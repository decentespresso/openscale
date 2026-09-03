import assert from "node:assert/strict";
import {test} from "node:test";

import worker, {BuildCoordinator} from "../src/worker.mjs";
import {pairCreationsGlobal, pairCreationsPerClient} from "../src/fleet.mjs";

const origin = "https://decentespresso.github.io";
const deviceId = "1".repeat(32);
const deviceSecret = "2".repeat(64);
const fleetSecret = "A".repeat(32);
const secondFleetSecret = "B".repeat(32);
const combinationHash = "3".repeat(64);

class Bucket {
  constructor() {
    this.objects = new Map();
    this.headRequests = [];
  }

  async put(key, body = new Uint8Array()) {
    this.objects.set(key, {body, size: body.byteLength || body.length || 1});
  }

  async head(key) {
    this.headRequests.push(key);
    return this.objects.get(key) || null;
  }

  async get(key) {
    const object = this.objects.get(key);
    if (!object) return null;
    return {
      ...object,
      json: async () => JSON.parse(typeof object.body === "string"
        ? object.body : new TextDecoder().decode(object.body)),
    };
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
      assert.ok(Object.keys(key).length <= 128);
      Object.entries(key).forEach(([name, entry]) => this.values.set(name, entry));
    } else {
      this.values.set(key, value);
    }
  }

  async delete(key) {
    this.values.delete(key);
  }

  async list({prefix}) {
    return new Map([...this.values].filter(([key]) => key.startsWith(prefix)));
  }

  async setAlarm(value) {
    this.alarm = value;
  }

  async deleteAlarm() {
    this.alarm = null;
  }

  async transaction(action) {
    return action(this);
  }
}

function environment() {
  const storage = new Storage();
  const env = {
    ALLOWED_ORIGIN: origin,
    BUILDS: new Bucket(),
    PUBLIC_BASE_URL: "https://builds.example.test",
    RATE_LIMIT_SALT: "salt-with-at-least-thirty-two-characters",
  };
  const coordinator = new BuildCoordinator({storage}, env);
  env.COORDINATOR = {
    idFromName: name => name,
    get: () => ({fetch: (input, options) => coordinator.fetch(new Request(input, options))}),
  };
  return {coordinator, env, storage};
}

function request(env, path, {
  method = "GET", body, authorization, device = false, browser = false,
  selectedDeviceId = deviceId, ip = "192.0.2.10", clientKey,
} = {}) {
  return worker.fetch(new Request(`https://example.test${path}`, {
    method,
    body: body === undefined ? undefined : JSON.stringify(body),
    headers: {
      ...(browser ? {Origin: origin} : {}),
      ...(authorization ? {Authorization: `Bearer ${authorization}`} : {}),
      ...(device ? {"X-OpenScale-Device-ID": selectedDeviceId} : {}),
      ...(clientKey ? {"X-OpenScale-Client-Key": clientKey} : {}),
      ...(body === undefined ? {} : {"Content-Type": "application/json"}),
      "CF-Connecting-IP": ip,
    },
  }), env);
}

function buildManifest(hash, overrides = {}) {
  return {
    custom_build: true,
    combination_hash: hash,
    firmware_version: "3.1.14-custom",
    features: ["pull-ota"],
    plugins: [{id: "grind-by-weight", version: "1.0.0"}],
    ...overrides,
  };
}

async function readyBuild(env, hash, overrides = {}) {
  await env.BUILDS.put(
    `v1/${hash}/build-manifest.json`,
    JSON.stringify(buildManifest(hash, overrides)),
  );
}

async function linkScale(env, {
  selectedDeviceId,
  authorization,
  pairCode,
  recoveryKey = fleetSecret,
}) {
  const paired = await request(env, "/api/v1/device/pair", {
    method: "POST",
    body: {pair_code: pairCode},
    authorization,
    device: true,
    selectedDeviceId,
  });
  assert.equal(paired.status, 200);
  const claimed = await request(env, "/api/v1/fleet/claim", {
    method: "POST",
    body: {pair_code: pairCode},
    authorization: recoveryKey,
    browser: true,
  });
  assert.equal(claimed.status, 200);
}

test("pairs, assigns, authenticates, and physically relinks a scale", async () => {
  const {env, storage} = environment();
  const pair = await request(env, "/api/v1/device/pair", {
    method: "POST",
    body: {pair_code: "A3F921-482193"},
    authorization: deviceSecret,
    device: true,
  });
  assert.equal(pair.status, 200);
  assert.equal((await pair.json()).pair_code, "A3F921-482193");
  const storedDevice = storage.values.get(`device:${deviceId}`);
  assert.notEqual(storedDevice.secret_hash, deviceSecret);
  assert.equal(JSON.stringify([...storage.values.values()]).includes(deviceSecret), false);
  assert.equal(
    storage.values.get("pair:A3F921-482193").expires_at - storedDevice.pair_created_at,
    12 * 60 * 60 * 1000,
  );

  const claim = await request(env, "/api/v1/fleet/claim", {
    method: "POST",
    body: {pair_code: "A3F921-482193"},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(claim.status, 200);
  assert.equal((await claim.json()).device_id, deviceId);
  assert.equal((await request(env, "/api/v1/fleet/claim", {
    method: "POST",
    body: {pair_code: "A3F921-482193"},
    authorization: fleetSecret,
    browser: true,
  })).status, 404);

  const scales = await request(env, "/api/v1/fleet/scales", {authorization: fleetSecret, browser: true});
  assert.deepEqual((await scales.json()).scales.map(scale => scale.serial_hint), ["A3F921"]);

  await readyBuild(env, combinationHash);
  const update = await request(env, `/api/v1/fleet/scales/${deviceId}`, {
    method: "PATCH",
    body: {name: "Bar Left"},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(update.status, 200);
  assert.equal((await update.json()).name, "Bar Left");
  assert.equal((await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: combinationHash, device_ids: [deviceId]},
    authorization: fleetSecret,
    browser: true,
  })).status, 200);

  const assignment = await request(env, "/api/v1/device/assignment", {
    authorization: deviceSecret,
    device: true,
  });
  assert.deepEqual(await assignment.json(), {
    linked: true,
    desired_combination: combinationHash,
    state: "ready",
  });

  storage.values.set(`device:${deviceId}`, {
    ...storage.values.get(`device:${deviceId}`),
    pair_created_at: 0,
  });
  await request(env, "/api/v1/device/pair", {
    method: "POST",
    body: {pair_code: "A3F921-918274"},
    authorization: deviceSecret,
    device: true,
  });
  await request(env, "/api/v1/fleet/claim", {
    method: "POST",
    body: {pair_code: "A3F921-918274"},
    authorization: secondFleetSecret,
    browser: true,
  });
  const oldFleet = await request(env, "/api/v1/fleet/scales", {authorization: fleetSecret, browser: true});
  assert.deepEqual((await oldFleet.json()).scales, []);
  const oldFleetUpdate = await request(env, `/api/v1/fleet/scales/${deviceId}`, {
    method: "PATCH",
    body: {name: "Stale owner"},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(oldFleetUpdate.status, 404);
  const transferred = await request(env, "/api/v1/fleet/scales", {
    authorization: secondFleetSecret,
    browser: true,
  });
  assert.deepEqual((await transferred.json()).scales, [{
    device_id: deviceId,
    serial_hint: "A3F921",
    name: "Scale A3F921",
    desired_combination: null,
    desired_updated_at: null,
    installed_combination: null,
    firmware_version: null,
    last_seen_at: null,
  }]);
});

test("device check-in reports authoritative installed state without changing desired state", async () => {
  const {env, storage} = environment();
  const nextHash = "4".repeat(64);
  await readyBuild(env, combinationHash);
  await readyBuild(env, nextHash);
  await linkScale(env, {
    selectedDeviceId: deviceId,
    authorization: deviceSecret,
    pairCode: "A3F921-100001",
  });
  assert.equal((await storage.get(`device:${deviceId}`)).last_seen_at, null);
  assert.equal((await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: combinationHash, device_ids: [deviceId]},
    authorization: fleetSecret,
    browser: true,
  })).status, 200);

  const converged = await request(env, "/api/v1/device/check-in", {
    method: "POST",
    body: {installed_combination: combinationHash, firmware_version: "3.1.14-custom"},
    authorization: deviceSecret,
    device: true,
  });
  assert.deepEqual(await converged.json(), {
    linked: true,
    desired_combination: combinationHash,
    state: "ready",
  });
  const installed = await storage.get(`device:${deviceId}`);
  assert.equal(installed.installed_combination, combinationHash);
  assert.equal(installed.firmware_version, "3.1.14-custom");
  assert.ok(Number.isFinite(Date.parse(installed.last_seen_at)));

  await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: nextHash, device_ids: [deviceId]},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal((await storage.get(`device:${deviceId}`)).installed_combination, combinationHash);

  const official = await request(env, "/api/v1/device/check-in", {
    method: "POST",
    body: {installed_combination: null, firmware_version: "3.1.14"},
    authorization: deviceSecret,
    device: true,
  });
  assert.equal(official.status, 200);
  assert.equal((await official.json()).desired_combination, nextHash);
  assert.equal((await storage.get(`device:${deviceId}`)).installed_combination, null);

  await request(env, "/api/v1/device/check-in", {
    method: "POST",
    body: {installed_combination: nextHash, firmware_version: "3.1.15-custom"},
    authorization: deviceSecret,
    device: true,
  });
  const beforeRejectedCheckIn = await storage.get(`device:${deviceId}`);
  const rejected = await request(env, "/api/v1/device/check-in", {
    method: "POST",
    body: {installed_combination: combinationHash, firmware_version: "forged"},
    authorization: "9".repeat(64),
    device: true,
  });
  assert.equal(rejected.status, 401);
  assert.deepEqual(await storage.get(`device:${deviceId}`), beforeRejectedCheckIn);

  const overview = await request(env, "/api/v1/fleet/overview", {
    authorization: fleetSecret,
    browser: true,
  });
  const [scale] = (await overview.json()).scales;
  assert.equal(scale.desired_combination, nextHash);
  assert.equal(scale.installed_combination, nextHash);
  assert.equal(scale.firmware_version, "3.1.15-custom");
});

test("fleet build library stores references without owning artifacts", async () => {
  const {env, storage} = environment();
  await readyBuild(env, combinationHash);
  await linkScale(env, {
    selectedDeviceId: deviceId,
    authorization: deviceSecret,
    pairCode: "A3F921-100002",
  });
  const objectCount = env.BUILDS.objects.size;
  const added = await request(env, "/api/v1/fleet/builds", {
    method: "POST",
    body: {combination_hash: combinationHash},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(added.status, 200);
  assert.equal((await added.json()).combination_hash, combinationHash);
  await request(env, "/api/v1/fleet/builds", {
    method: "POST",
    body: {combination_hash: combinationHash},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(env.BUILDS.objects.size, objectCount);
  assert.equal((await request(env, "/api/v1/fleet/builds", {
    method: "POST",
    body: {combination_hash: "5".repeat(64)},
    authorization: fleetSecret,
    browser: true,
  })).status, 409);
  await env.BUILDS.put("v1/5555555555555555555555555555555555555555555555555555555555555555/build-manifest.json", "{}");
  assert.equal((await request(env, "/api/v1/fleet/builds", {
    method: "POST",
    body: {combination_hash: "5".repeat(64)},
    authorization: fleetSecret,
    browser: true,
  })).status, 409);

  const renamed = await request(env, `/api/v1/fleet/builds/${combinationHash}`, {
    method: "PATCH",
    body: {label: "Bar profile"},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal((await renamed.json()).label, "Bar profile");
  const listed = await request(env, "/api/v1/fleet/builds", {
    authorization: fleetSecret,
    browser: true,
  });
  assert.deepEqual((await listed.json()).builds.map(build => ({
    label: build.label,
    version: build.firmware_version,
    features: build.features,
    plugins: build.plugins,
    state: build.state,
  })), [{
    label: "Bar profile",
    version: "3.1.14-custom",
    features: ["pull-ota"],
    plugins: [{id: "grind-by-weight", version: "1.0.0"}],
    state: "ready",
  }]);

  await request(env, "/api/v1/device/check-in", {
    method: "POST",
    body: {installed_combination: combinationHash, firmware_version: "3.1.14-custom"},
    authorization: deviceSecret,
    device: true,
  });
  await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: combinationHash, device_ids: [deviceId]},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal((await request(env, `/api/v1/fleet/builds/${combinationHash}`, {
    method: "DELETE",
    authorization: fleetSecret,
    browser: true,
  })).status, 409);
  await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: null, device_ids: [deviceId]},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal((await request(env, `/api/v1/fleet/builds/${combinationHash}`, {
    method: "DELETE",
    authorization: fleetSecret,
    browser: true,
  })).status, 200);
  assert.ok(await env.BUILDS.head(`v1/${combinationHash}/build-manifest.json`));
  assert.equal((await storage.get(`device:${deviceId}`)).installed_combination, combinationHash);
  assert.deepEqual((await (await request(env, "/api/v1/fleet/builds", {
    authorization: fleetSecret,
    browser: true,
  })).json()).builds, []);
});

test("bulk assignments validate every target and resolve each build state once", async () => {
  const {env, storage} = environment();
  const firstId = "a".repeat(32);
  const secondId = "b".repeat(32);
  const foreignId = "c".repeat(32);
  const readyHash = "6".repeat(64);
  await readyBuild(env, readyHash);
  await linkScale(env, {
    selectedDeviceId: firstId,
    authorization: "a".repeat(64),
    pairCode: "AA0001-100001",
  });
  await linkScale(env, {
    selectedDeviceId: secondId,
    authorization: "b".repeat(64),
    pairCode: "BB0002-100002",
  });
  await linkScale(env, {
    selectedDeviceId: foreignId,
    authorization: "c".repeat(64),
    pairCode: "CC0003-100003",
    recoveryKey: secondFleetSecret,
  });

  const single = await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: readyHash, device_ids: [firstId]},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal((await single.json()).updated, 1);
  const multiple = await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: readyHash, device_ids: [firstId, firstId, secondId]},
    authorization: fleetSecret,
    browser: true,
  });
  assert.deepEqual((await multiple.json()).device_ids, [firstId, secondId]);
  assert.equal((await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: readyHash, device_ids: []},
    authorization: fleetSecret,
    browser: true,
  })).status, 400);
  assert.equal((await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: readyHash, device_ids: ["invalid"]},
    authorization: fleetSecret,
    browser: true,
  })).status, 400);
  assert.equal((await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: readyHash, device_ids: ["f".repeat(32)]},
    authorization: fleetSecret,
    browser: true,
  })).status, 404);

  const beforeCrossFleet = await storage.get(`device:${firstId}`);
  const crossFleet = await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: null, device_ids: [firstId, foreignId]},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(crossFleet.status, 403);
  assert.deepEqual(await storage.get(`device:${firstId}`), beforeCrossFleet);

  const cleared = await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: null, device_ids: [firstId, secondId]},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal((await cleared.json()).updated, 2);
  assert.equal((await storage.get(`device:${firstId}`)).desired_combination, null);
  const all = await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: readyHash, all: true},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal((await all.json()).updated, 2);
  assert.equal((await storage.get(`device:${foreignId}`)).desired_combination, null);

  env.BUILDS.headRequests = [];
  const overview = await request(env, "/api/v1/fleet/overview", {
    authorization: fleetSecret,
    browser: true,
  });
  const fleet = await overview.json();
  assert.equal(fleet.scales.length, 2);
  assert.equal(fleet.build_states[readyHash], "ready");
  assert.equal(env.BUILDS.headRequests.filter(key =>
    key === `v1/${readyHash}/build-manifest.json`).length, 1);
});

test("all-scale assignment chunks storage writes at the platform limit", async () => {
  const {env, storage} = environment();
  await readyBuild(env, combinationHash);
  await linkScale(env, {
    selectedDeviceId: deviceId,
    authorization: deviceSecret,
    pairCode: "A3F921-100005",
  });
  const fleetKey = [...storage.values.keys()].find(key => key.startsWith("fleet:"));
  const fleet = await storage.get(fleetKey);
  const extraIds = Array.from({length: 128}, (_, index) =>
    (index + 32).toString(16).padStart(32, "0"));
  await storage.put(Object.fromEntries(extraIds.map((id, index) => [`device:${id}`, {
    fleet_id: fleetKey.slice(6),
    desired_combination: null,
    name: `Scale ${index}`,
  }])));
  await storage.put(fleetKey, {...fleet, devices: [deviceId, ...extraIds]});

  const response = await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: combinationHash, all: true},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(response.status, 200);
  assert.equal((await response.json()).updated, 129);
  assert.equal((await storage.get(`device:${extraIds.at(-1)}`)).desired_combination, combinationHash);
});

test("legacy fleet records remain readable with unknown installed state", async () => {
  const {env, storage} = environment();
  await linkScale(env, {
    selectedDeviceId: deviceId,
    authorization: deviceSecret,
    pairCode: "A3F921-100004",
  });
  const current = await storage.get(`device:${deviceId}`);
  const {
    desired_updated_at,
    installed_combination,
    firmware_version,
    last_seen_at,
    ...legacy
  } = current;
  await storage.put(`device:${deviceId}`, legacy);
  const overview = await request(env, "/api/v1/fleet/overview", {
    authorization: fleetSecret,
    browser: true,
  });
  const result = await overview.json();
  assert.deepEqual(result.builds, []);
  assert.deepEqual(result.scales[0], {
    device_id: deviceId,
    serial_hint: "A3F921",
    name: "Scale A3F921",
    desired_combination: null,
    desired_updated_at: null,
    installed_combination: null,
    firmware_version: null,
    last_seen_at: null,
  });
});

test("rejects malformed legacy assignments and invalid device credentials", async () => {
  const {env, storage} = environment();
  await linkScale(env, {
    selectedDeviceId: deviceId,
    authorization: deviceSecret,
    pairCode: "A3F921-100006",
  });
  await env.BUILDS.put(`v1/${combinationHash}/build-manifest.json`, "{}");
  const legacy = await request(env, `/api/v1/fleet/scales/${deviceId}`, {
    method: "PATCH",
    body: {desired_combination: combinationHash},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(legacy.status, 400);
  assert.equal((await legacy.json()).error, "invalid_request");
  assert.equal((await storage.get(`device:${deviceId}`)).desired_combination, null);
  const malformed = await request(env, "/api/v1/fleet/assignments", {
    method: "POST",
    body: {combination_hash: combinationHash, device_ids: [deviceId]},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(malformed.status, 409);
  assert.equal((await malformed.json()).error, "build_not_ready");
  assert.equal((await storage.get(`device:${deviceId}`)).desired_combination, null);
  const assignment = await request(env, "/api/v1/device/assignment", {
    authorization: "4".repeat(64),
    device: true,
  });
  assert.equal(assignment.status, 401);
});

test("rate limits new pair records by edge-derived client and globally", async () => {
  const {env, storage} = environment();
  for (let index = 0; index < pairCreationsPerClient; index++) {
    const response = await request(env, "/api/v1/device/pair", {
      method: "POST",
      body: {pair_code: `A10000-${String(index).padStart(6, "0")}`},
      authorization: String(index + 1).padStart(64, "0"),
      device: true,
      selectedDeviceId: (index + 1).toString(16).padStart(32, "0"),
      clientKey: String(index).padStart(64, "0"),
    });
    assert.equal(response.status, 200);
  }
  const limited = await request(env, "/api/v1/device/pair", {
    method: "POST",
    body: {pair_code: "A10000-999999"},
    authorization: "f".repeat(64),
    device: true,
    selectedDeviceId: "f".repeat(32),
    clientKey: "f".repeat(64),
  });
  assert.equal(limited.status, 429);
  assert.equal((await limited.json()).error, "pairing_creation_rate_limited");
  assert.ok(Number(limited.headers.get("Retry-After")) > 0);

  await storage.put("pair-rates", {
    global: pairCreationsGlobal,
    clients: {},
    expires_at: Date.now() + 60_000,
  });
  const globallyLimited = await request(env, "/api/v1/device/pair", {
    method: "POST",
    body: {pair_code: "B20000-000001"},
    authorization: "e".repeat(64),
    device: true,
    selectedDeviceId: "e".repeat(32),
    ip: "198.51.100.20",
  });
  assert.equal(globallyLimited.status, 429);
  assert.equal((await globallyLimited.json()).error, "pairing_creation_rate_limited");
});

test("alarm removes expired unclaimed devices and preserves linked devices", async () => {
  const {coordinator, env, storage} = environment();
  const pairCode = "C30000-000001";
  assert.equal((await request(env, "/api/v1/device/pair", {
    method: "POST",
    body: {pair_code: pairCode},
    authorization: deviceSecret,
    device: true,
  })).status, 200);
  assert.ok(storage.alarm > Date.now());
  await storage.put(`pair:${pairCode}`, {device_id: deviceId, expires_at: Date.now() - 1});
  await coordinator.alarm();
  assert.equal(await storage.get(`pair:${pairCode}`), undefined);
  assert.equal(await storage.get(`device:${deviceId}`), undefined);

  const linkedDeviceId = "d".repeat(32);
  const linkedPairCode = "D40000-000001";
  assert.equal((await request(env, "/api/v1/device/pair", {
    method: "POST",
    body: {pair_code: linkedPairCode},
    authorization: "d".repeat(64),
    device: true,
    selectedDeviceId: linkedDeviceId,
  })).status, 200);
  assert.equal((await request(env, "/api/v1/fleet/claim", {
    method: "POST",
    body: {pair_code: linkedPairCode},
    authorization: fleetSecret,
    browser: true,
  })).status, 200);
  await storage.put(`device:${linkedDeviceId}`, {
    ...await storage.get(`device:${linkedDeviceId}`),
    pair_code: linkedPairCode,
    pair_created_at: Date.now() - 60_000,
  });
  await storage.put(`pair:${linkedPairCode}`, {device_id: linkedDeviceId, expires_at: Date.now() - 1});
  await coordinator.alarm();
  const linkedDevice = await storage.get(`device:${linkedDeviceId}`);
  assert.ok(linkedDevice.fleet_id);
  assert.equal(linkedDevice.pair_code, null);
  assert.equal(linkedDevice.pair_created_at, null);
  assert.deepEqual((await storage.get(`fleet:${linkedDevice.fleet_id}`)).devices, [linkedDeviceId]);
});

test("claim rate limiting remains intact", async () => {
  const {env} = environment();
  for (let index = 0; index < 10; index++) {
    const response = await request(env, "/api/v1/fleet/claim", {
      method: "POST",
      body: {pair_code: `E50000-${String(index).padStart(6, "0")}`},
      authorization: fleetSecret,
      browser: true,
    });
    assert.equal(response.status, 404);
  }
  const limited = await request(env, "/api/v1/fleet/claim", {
    method: "POST",
    body: {pair_code: "E50000-999999"},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(limited.status, 429);
  assert.equal((await limited.json()).error, "pairing_rate_limited");
  assert.ok(Number(limited.headers.get("Retry-After")) > 0);
});
