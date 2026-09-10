import assert from "node:assert/strict";
import {test} from "node:test";
import {readFile} from "node:fs/promises";
import {runInNewContext} from "node:vm";
import {startFleetPolling} from "../../../docs/custom-build/fleet-polling.mjs";
import {deploymentState} from "../../../docs/custom-build/fleet-state.mjs";

test("polling pauses, waits for completion and stops requesting once inactive", async () => {
  let active = false;
  let calls = 0;
  let finish;
  const scheduled = [];
  startFleetPolling(() => active, () => {
    calls += 1;
    return new Promise(resolve => { finish = resolve; });
  }, (callback, delay) => {
    assert.equal(delay, 30000);
    scheduled.push(callback);
  });
  await scheduled.shift()();
  assert.equal(calls, 0);
  active = true;
  const pending = scheduled.shift()();
  assert.equal(calls, 1);
  assert.equal(scheduled.length, 0);
  active = false;
  finish();
  await pending;
  await scheduled.shift()();
  assert.equal(calls, 1);
  assert.equal(scheduled.length, 1);
});

test("background refresh updates installation status without replacing focused controls", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  const loader = source.slice(source.indexOf("  const loadFleet ="), source.indexOf("  const renderBuilds ="));
  const row = {dataset: {deviceId: "scale"}, state: "Install pending"};
  let installed = "old";
  let editing = true;
  let renders = 0;
  const context = {
    generation: 0,
    document: {hidden: false},
    scales: [], builds: [], buildStates: {}, selectedDeviceIds: new Set(["scale"]),
    fleetStatus: {}, buildStatus: {}, scaleRows: {children: [row]},
    editingFleet: () => editing,
    api: async () => ({scales: [{device_id: "scale", desired_combination: "new",
      installed_combination: installed, last_seen_at: new Date().toISOString(),
      desired_updated_at: new Date(0).toISOString()}], builds: [], build_states: {new: "ready"}}),
    renderScaleStatus: (target, scale) => { target.state = deploymentState(scale, {new: "ready"}); },
    renderBuilds: () => { renders++; }, renderScales: () => { renders++; },
  };
  runInNewContext(`${loader}\nglobalThis.refresh = loadFleet;`, context);
  await context.refresh(true);
  assert.equal(row.state, "Install pending");
  installed = "new";
  await context.refresh(true);
  assert.equal(row.state, "Up to date");
  assert.equal(renders, 0);
  assert.equal(context.selectedDeviceIds.has("scale"), true);
  editing = false;
  await context.refresh(true);
  assert.equal(renders, 2);
});

test("deployment reports distinguish installation from assignment and stale confirmation", () => {
  const now = Date.now();
  const scale = {desired_combination: "new", installed_combination: "old",
    install_combination: "new", install_state: "installing",
    install_updated_at: new Date(now).toISOString(), desired_updated_at: new Date(now - 1000).toISOString()};
  assert.equal(deploymentState(scale, {}, now), "Installing");
  assert.equal(deploymentState(scale, {}, now + 16 * 60000), "Status unconfirmed");
  assert.equal(deploymentState({...scale, install_state: "failed"}, {}, now), "Install failed");
  assert.equal(deploymentState({...scale, installed_combination: "new"}, {}, now), "Up to date");
  assert.equal(deploymentState({...scale, desired_combination: "different"}, {}, now), "Update assigned");
  assert.equal(deploymentState({...scale, desired_updated_at: new Date(now + 1).toISOString()}, {}, now), "Update assigned");
});

test("fleet polling requires a visible idle page and an outstanding assignment", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  const registration = source.slice(source.indexOf("  startFleetPolling("), source.indexOf("\n\n  setMode(Boolean(fleetKey))"));
  let eligible;
  let editing = false;
  const context = {
    fleetKey: "present",
    document: {hidden: false},
    activeRequests: 0,
    scales: [{desired_combination: "b", installed_combination: "a"}],
    editingFleet: () => editing,
    loadFleet: () => {},
    startFleetPolling: callback => { eligible = callback; },
  };
  runInNewContext(registration, context);
  assert.equal(eligible(), true);
  context.document.hidden = true;
  assert.equal(eligible(), false);
  context.document.hidden = false;
  editing = true;
  assert.equal(eligible(), true);
  editing = false;
  context.activeRequests = 1;
  assert.equal(eligible(), false);
  context.activeRequests = 0;
  context.scales = [{desired_combination: "b", installed_combination: "b"}];
  assert.equal(eligible(), false);
  context.scales = [{desired_combination: null, installed_combination: "b"}];
  assert.equal(eligible(), false);
});
