import assert from "node:assert/strict";
import {createHash} from "node:crypto";
import {test} from "node:test";
import worker, {BuildCoordinator} from "../src/worker.mjs";

const origin = "https://decentespresso.github.io";
const deviceId = "1".repeat(32);
const deviceSecret = "2".repeat(64);
const fleetSecret = "A".repeat(32);
const otherFleetSecret = "B".repeat(32);
const hash = value => createHash("sha256").update(value).digest("hex");
const fleetKey = `fleet:${hash(fleetSecret)}`;
const pairCode = "97BACC-123456";
const combinationHash = "3".repeat(64);

function environment() {
  const peerId = "4".repeat(32);
  const fleet = {devices: [deviceId, peerId], builds: [{combination_hash: combinationHash}]};
  const values = new Map([
    [fleetKey, fleet],
    [`device:${peerId}`, {fleet_id: hash(fleetSecret), name: "Peer"}],
    [`device:${deviceId}`, {
      secret_hash: hash(deviceSecret), fleet_id: hash(fleetSecret), serial_hint: "97BACC",
      name: "Kitchen", desired_combination: combinationHash, desired_updated_at: "2026-10-01T00:00:00Z",
      installed_combination: combinationHash, firmware_version: "3.1.15-custom",
      pair_code: pairCode, pair_created_at: Date.now(),
    }],
    [`pair:${pairCode}`, {device_id: deviceId, expires_at: Date.now() + 60000}],
  ]);
  const storage = {
    get: async key => values.get(key),
    put: async (key, value) => {
      for (const [name, entry] of typeof key === "object" ? Object.entries(key) : [[key, value]]) {
        values.set(name, entry);
      }
    },
    delete: async key => values.delete(key),
    list: async ({prefix}) => new Map([...values].filter(([key]) => key.startsWith(prefix))),
    transaction: async action => action(storage),
    setAlarm: async () => {},
    deleteAlarm: async () => {},
  };
  const env = {
    ALLOWED_ORIGIN: origin, RATE_LIMIT_SALT: "salt-with-at-least-thirty-two-characters",
    BUILDS: {head: async () => null},
  };
  const coordinator = new BuildCoordinator({storage}, env);
  env.COORDINATOR = {idFromName: name => name,
    get: () => ({fetch: (input, options) => coordinator.fetch(new Request(input, options))})};
  return {env, values, fleet, peerId};
}

function request(env, path, {method = "GET", secret = fleetSecret, device = false, body, selectedOrigin = origin} = {}) {
  return worker.fetch(new Request(`https://example.test/api/v1${path}`, {
    method, body: body === undefined ? undefined : JSON.stringify(body),
    headers: {
      Origin: selectedOrigin, Authorization: `Bearer ${secret}`,
      ...(device ? {"X-OpenScale-Device-ID": deviceId} : {}),
      ...(body === undefined ? {} : {"Content-Type": "application/json"}),
      "CF-Connecting-IP": "192.0.2.10",
    },
  }), env);
}

test("unpairing removes only the owned scale and preserves credentials, firmware and saved builds", async () => {
  const {env, values, fleet, peerId} = environment();
  const response = await request(env, `/fleet/scales/${deviceId}`, {method: "DELETE"});
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), {device_id: deviceId});
  assert.deepEqual(values.get(fleetKey), {...fleet, devices: [peerId]});
  assert.equal(values.has(`pair:${pairCode}`), false);
  const device = values.get(`device:${deviceId}`);
  assert.equal(device.fleet_id, null);
  assert.equal(device.desired_combination, null);
  assert.equal(device.desired_updated_at, null);
  assert.equal(device.name, "Scale 97BACC");
  assert.equal(device.pair_code, null);
  assert.equal(device.secret_hash, hash(deviceSecret));
  assert.equal(device.installed_combination, combinationHash);
  assert.equal(device.firmware_version, "3.1.15-custom");
  for (const path of ["/fleet/scales", "/fleet/overview"]) {
    const result = await request(env, path);
    assert.deepEqual((await result.json()).scales.map(scale => scale.device_id), [peerId]);
  }
  const assignment = await request(env, "/device/assignment", {secret: deviceSecret, device: true});
  assert.equal(assignment.status, 200);
  assert.deepEqual(await assignment.json(), {linked: false, desired_combination: null});
  const staleClaim = await request(env, "/fleet/claim", {method: "POST", body: {pair_code: pairCode}});
  assert.equal(staleClaim.status, 404);
});

test("unpairing rejects other fleets, invalid credentials, disallowed origins and unknown devices without writes", async () => {
  const {env, values} = environment();
  const before = [...values];
  for (const [options, status] of [
    [{secret: otherFleetSecret}, 404],
    [{secret: "invalid"}, 401],
    [{selectedOrigin: "https://attacker.example"}, 403],
  ]) {
    const response = await request(env, `/fleet/scales/${deviceId}`, {method: "DELETE", ...options});
    assert.equal(response.status, status);
    assert.deepEqual([...values], before);
  }
  const unknown = await request(env, `/fleet/scales/${"f".repeat(32)}`, {method: "DELETE"});
  assert.equal(unknown.status, 404);
  assert.deepEqual([...values], before);
});

test("an unpaired scale reuses its credentials and can physically pair to another fleet", async () => {
  const {env, values, peerId} = environment();
  assert.equal((await request(env, `/fleet/scales/${deviceId}`, {method: "DELETE"})).status, 200);
  const newPairCode = "97BACC-654321";
  const paired = await request(env, "/device/pair", {
    method: "POST", secret: deviceSecret, device: true, body: {pair_code: newPairCode},
  });
  assert.equal(paired.status, 200);
  const claimed = await request(env, "/fleet/claim", {
    method: "POST", secret: otherFleetSecret, body: {pair_code: newPairCode},
  });
  assert.equal(claimed.status, 200);
  assert.equal((await claimed.json()).device_id, deviceId);
  assert.deepEqual(values.get(fleetKey).devices, [peerId]);
  assert.equal(values.get(`device:${deviceId}`).desired_combination, null);
  const result = await request(env, "/fleet/scales", {secret: otherFleetSecret});
  assert.deepEqual((await result.json()).scales.map(scale => scale.device_id), [deviceId]);
});
