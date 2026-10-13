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
    renderControls: () => {},
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

test("background ownership refresh removes every unpaired row while another field is being edited", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  const loader = source.slice(source.indexOf("  const loadFleet ="), source.indexOf("  const renderBuilds ="));
  const scaleRows = {children: []};
  const rows = ["old-first", "old-second", "retained"].map(deviceId => ({
    dataset: {deviceId},
    remove() { scaleRows.children = scaleRows.children.filter(row => row !== this); },
  }));
  scaleRows.children = rows;
  let controlsUpdated = false;
  const context = {
    generation: 0, document: {hidden: false}, scales: [], builds: [], buildStates: {},
    selectedDeviceIds: new Set(rows.map(row => row.dataset.deviceId)),
    fleetStatus: {}, buildStatus: {}, scaleRows, editingFleet: () => true,
    api: async () => ({scales: [{device_id: "retained"}], builds: []}),
    renderScaleStatus: () => {},
    renderControls: () => { controlsUpdated = true; },
    renderBuilds: () => assert.fail("must preserve the field being edited"),
    renderScales: () => assert.fail("must preserve the field being edited"),
  };
  runInNewContext(`${loader}\nglobalThis.refresh = loadFleet;`, context);
  assert.equal(await context.refresh(true), true);
  assert.deepEqual(scaleRows.children, [rows[2]]);
  assert.deepEqual([...context.selectedDeviceIds], ["retained"]);
  assert.equal(context.fleetStatus.textContent, "1 scale");
  assert.equal(controlsUpdated, true);
});

test("returning to a visible idle browser refreshes ownership even without outstanding assignments", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  const registration = source.slice(source.indexOf("  const refreshFleetOnReturn ="), source.indexOf("  startFleetPolling("));
  const listeners = new Map();
  let calls = 0;
  const context = {
    fleetKey: "present", activeRequests: 0,
    document: {hidden: false, addEventListener: (event, callback) => listeners.set(event, callback)},
    window: {addEventListener: (event, callback) => listeners.set(event, callback)},
    loadFleet: background => { assert.equal(background, true); calls++; },
  };
  runInNewContext(registration, context);
  listeners.get("focus")();
  listeners.get("visibilitychange")();
  assert.equal(calls, 2);
  context.document.hidden = true;
  listeners.get("visibilitychange")();
  context.document.hidden = false;
  context.activeRequests = 1;
  listeners.get("focus")();
  context.activeRequests = 0;
  context.fleetKey = "";
  listeners.get("focus")();
  assert.equal(calls, 2);
});

test("unpair confirmation removes the scale locally only after a successful ownership change", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  const start = source.indexOf('      const unpair = row.querySelector(".unpair-scale")');
  const handler = source.slice(start, source.indexOf("      return row;", start));
  for (const mode of ["cancel", "failure", "success"]) {
    let click;
    let requests = 0;
    let errors = 0;
    let renders = 0;
    let refreshed = false;
    const button = {setAttribute: () => {}, addEventListener: (event, callback) => { click = callback; }};
    const context = {
      row: {querySelector: () => button}, scale: {device_id: "old", name: "Kitchen"},
      scales: [{device_id: "old", name: "Kitchen"}, {device_id: "retained"}],
      selectedDeviceIds: new Set(["old", "retained"]),
      confirm: async () => mode !== "cancel",
      api: async (path, options) => {
        requests++;
        assert.equal(path, "/api/v1/fleet/scales/old");
        assert.equal(options.method, "DELETE");
        if (mode === "failure") throw new Error("offline");
      },
      renderScales: () => { renders++; }, showToast: () => {},
      showApiError: () => { errors++; }, loadFleet: async () => { refreshed = true; },
    };
    runInNewContext(handler, context);
    await click();
    assert.equal(button.disabled, false);
    assert.equal(requests, mode === "cancel" ? 0 : 1);
    assert.equal(errors, mode === "failure" ? 1 : 0);
    assert.equal(renders, mode === "success" ? 1 : 0);
    assert.equal(refreshed, mode === "success");
    assert.equal(context.scales.some(scale => scale.device_id === "old"), mode !== "success");
    assert.equal(context.selectedDeviceIds.has("old"), mode !== "success");
    assert.equal(context.selectedDeviceIds.has("retained"), true);
  }
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
