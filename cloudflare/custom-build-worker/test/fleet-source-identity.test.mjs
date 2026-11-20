import assert from "node:assert/strict";
import {createHash} from "node:crypto";
import {mock, test} from "node:test";

import {handleFleetRequest} from "../src/fleet.mjs";

const combinationHash = "3".repeat(64);
const sourceCommit = "ac3df004e5856c6f84cad2a5ff89ba037ea058c6";
const fleetSecret = "A".repeat(32);
const fleetKey = `fleet:${createHash("sha256").update(fleetSecret).digest("hex")}`;

function savedBuild(overrides = {}) {
  return {
    combination_hash: combinationHash,
    firmware_version: "3.1.16-custom",
    label: "Coffee bar",
    added_at: "2026-11-05T19:03:35Z",
    features: [],
    plugins: [],
    ...overrides,
  };
}

function manifest(overrides = {}) {
  return {
    ...savedBuild(),
    custom_build: true,
    firmware_ref: "main",
    firmware_version: "3.1.16-main-custom",
    base_source: sourceCommit,
    ...overrides,
  };
}

function fixture(builds, body = JSON.stringify(manifest())) {
  const values = new Map([[fleetKey, {devices: [], builds}]]);
  const storage = {
    get: async key => values.get(key),
    put: mock.fn(async (key, value) => values.set(key, value)),
    transaction: action => action(storage),
  };
  const env = {BUILDS: {
    head: async () => body === null ? null : {},
    get: mock.fn(async () => body === null ? null : {json: async () => JSON.parse(body)}),
  }};
  const api = (path, method = "GET", payload) => handleFleetRequest(new Request(`https://coordinator${path}`, {
    method,
    headers: {
      Authorization: `Bearer ${fleetSecret}`,
      ...(payload === undefined ? {} : {"Content-Type": "application/json"}),
    },
    body: payload === undefined ? undefined : JSON.stringify(payload),
  }), storage, env);
  return {api, env, storage, values};
}

test("new saved builds retain manifest source and commit without repeated manifest reads", async () => {
  for (const firmwareRef of ["main", "v3.1.16"]) {
    const firmwareVersion = firmwareRef === "main" ? "3.1.16-main-custom" : "3.1.16-custom";
    const {api, env} = fixture([], JSON.stringify(manifest({
      firmware_ref: firmwareRef, firmware_version: firmwareVersion,
    })));
    const added = await api("/fleet/builds", "POST", {combination_hash: combinationHash});
    assert.equal(added.firmware_ref, firmwareRef);
    assert.equal(added.base_source, sourceCommit);
    assert.equal(added.firmware_version, firmwareVersion);
    for (const path of ["/fleet/builds", "/fleet/overview"]) {
      const {builds} = await api(path);
      assert.equal(builds[0].firmware_ref, firmwareRef);
      assert.equal(builds[0].base_source, sourceCommit);
    }
    assert.equal(env.BUILDS.get.mock.callCount(), 1);
  }
});

test("existing saved builds recover authoritative source identity without a storage migration", async () => {
  const original = savedBuild();
  const {api, storage, values} = fixture([original]);
  for (const path of ["/fleet/builds", "/fleet/overview"]) {
    const {builds} = await api(path);
    assert.deepEqual(builds, [{
      ...original,
      firmware_version: "3.1.16-main-custom",
      firmware_ref: "main",
      base_source: sourceCommit,
      state: "ready",
    }]);
  }
  assert.deepEqual(values.get(fleetKey).builds, [original]);
  assert.equal(storage.put.mock.callCount(), 0);
  const resaved = await api("/fleet/builds", "POST", {combination_hash: combinationHash});
  assert.equal(resaved.firmware_ref, "main");
  assert.equal(resaved.base_source, sourceCommit);
  assert.equal(resaved.label, original.label);
});

test("legacy missing or invalid manifest provenance remains unknown", async () => {
  for (const overrides of [
    {firmware_ref: null, base_source: null},
    {firmware_ref: "../main", base_source: "ac3df0"},
    {firmware_ref: "main\n", base_source: "A".repeat(40)},
    {firmware_ref: "v3.1.16 ", base_source: "z".repeat(40)},
    {firmware_ref: "v" + "1".repeat(64) + ".1.1", base_source: {}},
  ]) {
    const {api} = fixture([savedBuild()], JSON.stringify(manifest(overrides)));
    const {builds} = await api("/fleet/overview");
    assert.equal(builds[0].firmware_ref, null);
    assert.equal(builds[0].base_source, null);
    assert.equal(builds[0].label, "Coffee bar");
  }
  for (const body of [null, "not json", "{}", JSON.stringify(manifest({combination_hash: "4".repeat(64)}))]) {
    const {api} = fixture([savedBuild()], body);
    const {builds} = await api("/fleet/builds");
    assert.equal(builds[0].firmware_ref, null);
    assert.equal(builds[0].base_source, null);
    assert.equal(builds[0].firmware_version, "3.1.16-custom");
  }
});

test("saved source identity comes from the manifest, never client overrides", async () => {
  const {api, storage} = fixture([]);
  await assert.rejects(api("/fleet/builds", "POST", {
    combination_hash: combinationHash,
    firmware_ref: "main",
    base_source: "a".repeat(40),
  }), {code: "invalid_request"});
  assert.equal(storage.put.mock.callCount(), 0);
});
