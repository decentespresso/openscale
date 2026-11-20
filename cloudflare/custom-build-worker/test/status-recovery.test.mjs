import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {test} from "node:test";
import {runInNewContext} from "node:vm";

const source = await readFile(new URL("../../../docs/custom-build/app.js", import.meta.url), "utf8");
const checkSource = source.slice(source.indexOf("const checkStatus ="), source.indexOf("const recommendationAvailable ="));
const clickSource = source.slice(source.indexOf('buildButton.addEventListener("click"'), source.indexOf('document.querySelector("#reset")'));
const selection = Object.freeze({firmware_ref: "main", features: Object.freeze([]), plugins: Object.freeze([])});

function apiError(status, code = "firmware_ref_unavailable") {
  return Object.assign(new Error(code), {status, result: {error: code}});
}

function harness(respond) {
  const states = [];
  const requests = [];
  const confirmations = [];
  const timers = new Map();
  let nextTimer = 0;
  let click;
  const context = {
    selectionGeneration: 1,
    selectionController: new AbortController(),
    currentSelection: selection,
    currentBuildState: "missing",
    statusTimer: undefined,
    pollTimer: undefined,
    pollRemaining: 0,
    clearTimeout: id => timers.delete(id),
    setTimeout: (callback, delay) => {
      const id = ++nextTimer;
      timers.set(id, {callback, delay});
      return id;
    },
    setStatus: (result, generation) => {
      states.push({result: JSON.parse(JSON.stringify(result)), generation});
      context.currentBuildState = result.state;
    },
    apiRequest: async (path, options) => {
      requests.push({path, method: options.method, body: JSON.parse(options.body)});
      return respond(requests.length);
    },
    clearCatalogReload: () => {},
    handleCatalogStale: (error, generation) => {
      if (error.result?.error !== "catalog_stale") return false;
      context.setStatus({state: "updating"}, generation);
      return true;
    },
    showToast: () => {},
    window: {confirm: message => { confirmations.push(message); return true; }},
    buildButton: {disabled: false, addEventListener: (event, listener) => { click = listener; }},
  };
  const check = runInNewContext(`${checkSource}\n${clickSource}\ncheckStatus`, context);
  const runNextTimer = async () => {
    assert.equal(timers.size, 1);
    const [id, timer] = timers.entries().next().value;
    timers.delete(id);
    await timer.callback();
  };
  return {check, context, states, requests, confirmations, timers, runNextTimer, click: () => click()};
}

test("initial transient status failures retry safely and recover without requesting a build", async () => {
  for (const failure of [new TypeError("offline"), apiError(408), apiError(503)]) {
    const app = harness(async attempt => {
      if (attempt === 1) throw failure;
      return {state: "missing"};
    });
    await app.check(selection, 1);
    assert.equal(app.states.at(-1).result.state, "unavailable");
    assert.equal(app.states.at(-1).result.retryDelay, 2000);
    assert.equal(app.timers.values().next().value.delay, 2000);
    await app.runNextTimer();
    assert.equal(app.states.at(-1).result.state, "missing");
    assert.equal(app.timers.size, 0);
    assert.deepEqual(app.requests.map(request => request.path), ["/api/v1/status", "/api/v1/status"]);
    assert.ok(app.requests.every(request => request.method === "POST" && request.body.firmware_ref === "main"));
    assert.equal(app.confirmations.length, 0);
  }
});

test("status recovery has a bounded retry budget and manual retry resets it", async () => {
  const app = harness(async attempt => {
    if (attempt <= 7) throw apiError(503);
    return {state: "missing"};
  });
  await app.check(selection, 1);
  const delays = [];
  while (app.timers.size) {
    delays.push(app.timers.values().next().value.delay);
    await app.runNextTimer();
  }
  assert.deepEqual(delays, [2000, 4000, 8000, 16000, 30000]);
  assert.equal(app.requests.length, 6);
  assert.equal(app.states.at(-1).result.retryDelay, 0);
  assert.equal(app.states.at(-1).result.errorCode, "firmware_ref_unavailable");
  await app.click();
  assert.equal(app.requests.length, 7);
  assert.equal(app.states.at(-1).result.retryDelay, 2000);
  await app.runNextTimer();
  assert.equal(app.requests.length, 8);
  assert.equal(app.states.at(-1).result.state, "missing");
  assert.ok(app.requests.every(request => request.path === "/api/v1/status"));
  assert.equal(app.confirmations.length, 0);
});

test("changing the selection invalidates a previously scheduled retry", async () => {
  const app = harness(async () => { throw apiError(503); });
  await app.check(selection, 1);
  app.context.selectionGeneration = 2;
  const stateCount = app.states.length;
  await app.runNextTimer();
  assert.equal(app.requests.length, 1);
  assert.equal(app.states.length, stateCount);
  assert.equal(app.timers.size, 0);
});

test("late status responses and failures cannot overwrite a newer selection", async () => {
  for (const reject of [false, true]) {
    let finish;
    const app = harness(() => new Promise((resolve, rejectPromise) => {
      finish = reject ? () => rejectPromise(apiError(503)) : () => resolve({state: "ready"});
    }));
    const pending = app.check(selection, 1);
    app.context.selectionGeneration = 2;
    app.context.setStatus({state: "missing"}, 2);
    finish();
    await pending;
    assert.equal(app.states.at(-1).generation, 2);
    assert.equal(app.states.at(-1).result.state, "missing");
    assert.equal(app.timers.size, 0);
  }
});

test("aborts, permanent selection errors, and stale catalogs do not start generic retries", async () => {
  for (const failure of [
    Object.assign(new Error("aborted"), {name: "AbortError"}),
    apiError(400, "unsupported_firmware_ref"),
    apiError(409, "catalog_stale"),
  ]) {
    const app = harness(async () => { throw failure; });
    await app.check(selection, 1);
    assert.equal(app.requests.length, 1);
    assert.equal(app.timers.size, 0);
    if (failure.status === 400) {
      assert.equal(app.states.at(-1).result.state, "unavailable");
      assert.equal(app.states.at(-1).result.retryDelay, 0);
    } else {
      assert.equal(app.states.at(-1).result.state, failure.name === "AbortError" ? "checking" : "updating");
    }
  }
});

test("successful queued or building status hands off without another initial retry", async () => {
  for (const state of ["queued", "building"]) {
    const app = harness(async attempt => {
      if (attempt === 1) throw apiError(502);
      return {state, combination_hash: "a".repeat(64)};
    });
    await app.check(selection, 1);
    await app.runNextTimer();
    assert.equal(app.states.at(-1).result.state, state);
    assert.equal(app.timers.size, 0);
    assert.equal(app.requests.length, 2);
  }
});

test("status renderer explains source lookup errors and enables manual checks after retries stop", () => {
  const elements = new Map();
  const element = selector => {
    if (!elements.has(selector)) elements.set(selector, {
      textContent: "",
      classList: {toggle: () => {}},
      setAttribute: () => {},
      replaceChildren: () => {},
    });
    return elements.get(selector);
  };
  const context = {
    selectionGeneration: 1,
    currentSelection: selection,
    currentCombinationHash: "",
    currentBuildState: "missing",
    lastBuildResult: null,
    installMethod: "wifi",
    pollRemaining: 300,
    updateBuildProgress: () => {},
    buildEstimate: () => "",
    buildButton: {textContent: "Request build", disabled: false},
    schedulePoll: () => {},
    document: {querySelector: element, dispatchEvent: () => {}},
    CustomEvent: class {},
  };
  const renderSource = source.slice(source.indexOf("const setStatus ="), source.indexOf("const apiRequest ="));
  const renderStatus = runInNewContext(`${renderSource}\nsetStatus`, context);
  for (const errorCode of ["builder_ref_unavailable", "firmware_ref_unavailable"]) {
    renderStatus({state: "unavailable", errorCode, retryDelay: 2000}, 1);
    assert.equal(element("#build-state").textContent, "Reconnecting");
    assert.equal(context.buildButton.disabled, true);
    assert.match(element("#status-message").textContent, /GitHub.*firmware source/);
    assert.match(element("#status-message").textContent, /Checking again in 2 seconds/);
    renderStatus({state: "unavailable", errorCode, retryDelay: 0}, 1);
    assert.equal(context.buildButton.textContent, "Retry check");
    assert.equal(context.buildButton.disabled, false);
  }
  renderStatus({state: "unavailable", retryDelay: 0}, 1);
  assert.equal(element("#status-message").textContent, "Build availability could not be checked.");
  renderStatus({state: "checking"}, 1);
  assert.equal(context.buildButton.disabled, true);
  renderStatus({state: "missing"}, 1);
  assert.equal(context.buildButton.textContent, "Request build");
  assert.equal(context.buildButton.disabled, false);
});
