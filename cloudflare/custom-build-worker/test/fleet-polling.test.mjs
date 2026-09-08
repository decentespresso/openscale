import assert from "node:assert/strict";
import {test} from "node:test";
import {readFile} from "node:fs/promises";
import {runInNewContext} from "node:vm";
import {startFleetPolling} from "../../../docs/custom-build/fleet-polling.mjs";

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
  assert.equal(eligible(), false);
  editing = false;
  context.activeRequests = 1;
  assert.equal(eligible(), false);
  context.activeRequests = 0;
  context.scales = [{desired_combination: "b", installed_combination: "b"}];
  assert.equal(eligible(), false);
  context.scales = [{desired_combination: null, installed_combination: "b"}];
  assert.equal(eligible(), false);
});
