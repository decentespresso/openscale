import assert from "node:assert/strict";
import {test} from "node:test";

import worker, {BuildCoordinator} from "../src/worker.mjs";

const origin = "https://decentespresso.github.io";
const deviceId = "1".repeat(32);
const deviceSecret = "2".repeat(64);
const fleetSecret = "A".repeat(32);
const secondFleetSecret = "B".repeat(32);
const combinationHash = "3".repeat(64);

class Bucket {
  constructor() {
    this.objects = new Map();
  }

  async put(key, body = new Uint8Array()) {
    this.objects.set(key, {body, size: body.byteLength || 1});
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
    if (typeof key === "object") Object.entries(key).forEach(([name, entry]) => this.values.set(name, entry));
    else this.values.set(key, value);
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
  return {env, storage};
}

function request(env, path, {method = "GET", body, authorization, device = false, browser = false} = {}) {
  return worker.fetch(new Request(`https://example.test${path}`, {
    method,
    body: body === undefined ? undefined : JSON.stringify(body),
    headers: {
      ...(browser ? {Origin: origin} : {}),
      ...(authorization ? {Authorization: `Bearer ${authorization}`} : {}),
      ...(device ? {"X-OpenScale-Device-ID": deviceId} : {}),
      ...(body === undefined ? {} : {"Content-Type": "application/json"}),
      "CF-Connecting-IP": "192.0.2.10",
    },
  }), env);
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

  await env.BUILDS.put(`v1/${combinationHash}/build-manifest.json`);
  const update = await request(env, `/api/v1/fleet/scales/${deviceId}`, {
    method: "PATCH",
    body: {name: "Bar Left", desired_combination: combinationHash},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(update.status, 200);
  assert.equal((await update.json()).name, "Bar Left");

  const assignment = await request(env, "/api/v1/device/assignment", {
    authorization: deviceSecret,
    device: true,
  });
  assert.deepEqual(await assignment.json(), {
    linked: true,
    name: "Bar Left",
    serial_hint: "A3F921",
    desired_combination: combinationHash,
    state: "ready",
    combination_hash: combinationHash,
    manifest_url: `https://builds.example.test/v1/${combinationHash}/ota-manifest.json`,
    downloads: {"HDS_FW_custom.zip": `https://builds.example.test/v1/${combinationHash}/HDS_FW_custom.zip`},
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
  }]);
});

test("rejects unready assignments and invalid device credentials", async () => {
  const {env} = environment();
  const missing = await request(env, `/api/v1/fleet/scales/${deviceId}`, {
    method: "PATCH",
    body: {desired_combination: combinationHash},
    authorization: fleetSecret,
    browser: true,
  });
  assert.equal(missing.status, 409);
  assert.equal((await missing.json()).error, "build_not_ready");
  const assignment = await request(env, "/api/v1/device/assignment", {
    authorization: "4".repeat(64),
    device: true,
  });
  assert.equal(assignment.status, 401);
});
