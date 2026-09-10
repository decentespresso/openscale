import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {test} from "node:test";
import {runInNewContext} from "node:vm";

import {
  buildLabel,
  buildSummary,
  deploymentState,
  lastSeenLabel,
  shortHash,
} from "../../../docs/custom-build/fleet-state.mjs";
import {buildEstimate, buildTimeRange} from "../../../docs/custom-build/build-estimate.mjs";
import {elapsedTime, initBuildProgress} from "../../../docs/custom-build/build-progress.mjs";
import {
  catalogRevisionChanged,
  defaultSelection,
  firmwareRefLabel,
  optionReason,
  parseSelection,
  resolveSelection,
  selectionQuery,
} from "../../../docs/custom-build/selection.mjs";


const feature = (id, name, requires = [], firmware_refs = ["main"]) => ({
  id, name, requires, firmware_refs,
});

const plugin = (id, name, overrides = {}) => ({
  id,
  name,
  firmware_refs: ["main"],
  requires: [],
  depends_on: [],
  conflicts: [],
  conflicts_features: [],
  recommends: {features: [], plugins: []},
  ...overrides,
});

const catalog = {
  catalog_revision: "a".repeat(64),
  firmware_refs: ["main", "v1.2.3"],
  features: [
    feature("wifi", "WiFi"),
    feature("network", "Network", ["wifi"]),
    feature("stable-only", "Stable only", [], ["v1.2.3"]),
  ],
  plugins: [
    {...plugin("blocker", "Blocker", {conflicts_features: ["wifi"]}), default: true},
    plugin("client", "Client", {requires: ["network"]}),
    plugin("direct", "Direct", {conflicts: ["blocker"]}),
    plugin("helper", "Helper", {conflicts: ["blocker"]}),
    plugin("indirect", "Indirect", {depends_on: ["helper"]}),
    plugin("compatibility-root", "Compatibility root", {depends_on: ["direct", "blocker"]}),
    plugin("compatibility-recommender", "Compatibility recommender", {
      recommends: {features: [], plugins: ["direct", "blocker"]},
    }),
  ],
};


test("labels stable, preview, and development firmware refs", () => {
  assert.equal(firmwareRefLabel("v3.1.14"), "3.1.14 (stable)");
  assert.equal(firmwareRefLabel("v3.1.14-preview.1"), "3.1.14-preview.1 (preview)");
  assert.equal(firmwareRefLabel("main"), "main (development)");
});


test("ships 3.1.14 compatibility while main remains the cutover default", async () => {
  const shipped = JSON.parse(await readFile(
    new URL("../../../docs/custom-build/catalog.json", import.meta.url), "utf8",
  ));
  assert.deepEqual(shipped.firmware_refs, ["v3.1.14", "v3.1.14-preview.3", "v3.1.14-preview.4", "main"]);
  assert.equal(firmwareRefLabel(shipped.firmware_refs[1]), "3.1.14-preview.3 (preview)");
  assert.equal(firmwareRefLabel(shipped.firmware_refs[2]), "3.1.14-preview.4 (preview)");
  assert.equal(firmwareRefLabel(defaultSelection(shipped).firmware_ref), "main (development)");
  assert.equal(firmwareRefLabel(shipped.firmware_refs[0]), "3.1.14 (stable)");
});


test("blocks new direct and transitive conflicts without blocking removal", () => {
  const current = {firmware_ref: "main", features: [], plugins: ["blocker"]};
  assert.equal(optionReason(catalog, current, "plugin", "blocker"), "");
  assert.equal(optionReason(catalog, current, "feature", "wifi"), "Conflicts with Blocker");
  assert.equal(
    optionReason(catalog, current, "feature", "network"),
    "Requires WiFi, which conflicts with Blocker",
  );
  assert.equal(
    optionReason(catalog, current, "plugin", "client"),
    "Requires WiFi, which conflicts with Blocker",
  );
  assert.equal(optionReason(catalog, current, "plugin", "direct"), "Conflicts with Blocker");
  assert.equal(optionReason(catalog, current, "plugin", "compatibility-root"), "");
  assert.equal(
    optionReason(catalog, current, "plugin", "indirect"),
    "Requires Helper, which conflicts with Blocker",
  );
  assert.equal(
    optionReason(catalog, current, "feature", "stable-only"),
    "Unavailable for main",
  );
  assert.deepEqual(
    resolveSelection(catalog, {firmware_ref: "main", features: [], plugins: ["client"]}),
    {firmware_ref: "main", features: ["network", "wifi"], plugins: ["client"]},
  );
  assert.deepEqual(
    resolveSelection(catalog, {
      firmware_ref: "main",
      features: [],
      plugins: ["compatibility-recommender", "direct", "blocker"],
    }).plugins,
    ["blocker", "compatibility-recommender", "direct"],
  );
});


test("round-trips sorted URL selections and rejects invalid links atomically", () => {
  const selection = {firmware_ref: "main", features: ["network", "wifi"], plugins: ["client"]};
  const query = selectionQuery({
    firmware_ref: "main", features: ["wifi", "network"], plugins: ["client"],
  });
  assert.equal(query, "?ref=main&features=network%2Cwifi&plugins=client");
  assert.deepEqual(parseSelection(query, catalog), selection);
  assert.throws(() => parseSelection("?ref=main&features=unknown&plugins=client", catalog));
  assert.throws(() => parseSelection("?ref=main&features=wifi", catalog));
  assert.throws(() => parseSelection("?ref=main&features=wifi&plugins=blocker", catalog));
  assert.deepEqual(defaultSelection(catalog), {
    firmware_ref: "v1.2.3", features: [], plugins: ["blocker"],
  });
  assert.deepEqual(defaultSelection({...catalog, firmware_refs: ["v1.2.3"]}), {
    firmware_ref: "v1.2.3", features: [], plugins: ["blocker"],
  });
});


test("detects a new static catalog after a stale reload", async () => {
  const revisions = ["a".repeat(64), "b".repeat(64)];
  const requests = [];
  const fetchCatalog = async (url, options) => {
    requests.push({url, options});
    return {ok: true, json: async () => ({catalog_revision: revisions.shift()})};
  };
  assert.equal(await catalogRevisionChanged(fetchCatalog, "a".repeat(64)), false);
  assert.equal(await catalogRevisionChanged(fetchCatalog, "a".repeat(64)), true);
  assert.deepEqual(requests, [
    {url: "catalog.json", options: {cache: "no-store"}},
    {url: "catalog.json", options: {cache: "no-store"}},
  ]);
});


test("formats fleet build identity and deployment state", () => {
  const hash = "8c6df20ccb1b9855f19dce3868b9310ecb26238cccf48557843bd428ebbc1f63";
  const now = Date.parse("2026-09-03T12:00:00.000Z");
  const fresh = "2026-09-03T11:00:00.000Z";
  const assigned = "2026-09-03T11:30:00.000Z";
  assert.equal(shortHash(hash), "8C6DF20CCB1B");
  assert.equal(buildSummary({
    features: ["pull-ota", "wifi"],
    plugins: [{id: "grind-by-weight", version: "1.0.0"}, {id: "pressensor", version: "2.0.0"}],
  }), "pull-ota, wifi, grind-by-weight 1.0.0, pressensor 2.0.0");
  assert.equal(deploymentState({
    desired_combination: hash,
    installed_combination: hash,
    last_seen_at: fresh,
  }, {[hash]: "ready"}, now), "Up to date");
  assert.equal(deploymentState({
    desired_combination: hash,
    installed_combination: null,
    desired_updated_at: assigned,
    last_seen_at: fresh,
  }, {[hash]: "ready"}, now), "Update assigned");
  assert.equal(deploymentState({
    desired_combination: hash,
    installed_combination: null,
    desired_updated_at: fresh,
    last_seen_at: assigned,
  }, {[hash]: "ready"}, now), "Install pending");
  assert.equal(deploymentState({
    desired_combination: hash,
    installed_combination: null,
    last_seen_at: fresh,
  }, {[hash]: "building"}, now), "Build preparing");
  assert.equal(deploymentState({
    desired_combination: hash,
    installed_combination: null,
    last_seen_at: "2026-08-01T00:00:00.000Z",
  }, {[hash]: "ready"}, now), "Offline");
  assert.equal(deploymentState({
    desired_combination: null,
    installed_combination: null,
    last_seen_at: fresh,
  }, {}, now), "No update assigned");
  assert.equal(deploymentState({
    desired_combination: null,
    installed_combination: null,
    last_seen_at: null,
  }, {}, now), "Unknown");
  assert.equal(lastSeenLabel(fresh, now), "1h ago");
});


test("fleet browser consumes the aggregated overview without per-scale status requests", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  assert.ok(source.includes('/api/v1/fleet/overview'));
  assert.ok(source.includes('/api/v1/fleet/assignments'));
  assert.equal(source.includes('/api/v1/status/'), false);
});

test("fleet access survives new sessions and migrates only after successful persistence", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  const helpers = source.slice(source.indexOf("const storageKey"), source.indexOf("export function initFleet"));
  const storage = () => {
    const data = new Map();
    return {getItem: key => data.get(key) ?? null, setItem: (key, value) => data.set(key, value), removeItem: key => data.delete(key)};
  };
  const localStorage = storage();
  const sessionStorage = storage();
  const key = "A".repeat(32);
  const storageKey = "hds-custom-build-fleet-v2";
  sessionStorage.setItem(storageKey, JSON.stringify({version: 2, key}));
  const load = (local, session) => runInNewContext(`${helpers}\nloadStoredKey()`, {localStorage: local, sessionStorage: session});
  assert.equal(load({...localStorage, setItem: () => {throw new Error("blocked");}}, sessionStorage), key);
  assert.notEqual(sessionStorage.getItem(storageKey), null);
  assert.equal(load(localStorage, sessionStorage), key);
  assert.equal(sessionStorage.getItem(storageKey), null);
  assert.equal(load(localStorage, storage()), key);
  localStorage.setItem(storageKey, "invalid json");
  assert.equal(load(localStorage, storage()), "");
  localStorage.setItem("hds-custom-build-fleet-v1", JSON.stringify({version: 1, key}));
  assert.equal(load(localStorage, storage()), key);
  assert.equal(localStorage.getItem("hds-custom-build-fleet-v1"), null);
});

test("build labels hide generated hashes without replacing user labels", () => {
  assert.equal(buildLabel({label: "Build 462465C2997F"}, 0), "Build 1");
  assert.equal(buildLabel({label: "Kitchen scale"}, 1), "Kitchen scale");
});

test("build estimates follow selected features and handle timestamps and boundaries", () => {
  const energy = {features: ["energy-menu"], plugins: ["default-web-apps"]};
  assert.deepEqual(buildTimeRange(energy), [10, 12]);
  assert.deepEqual(buildTimeRange({plugins: ["default-web-apps"]}), [6, 9]);
  assert.deepEqual(buildTimeRange(), [5, 7]);
  assert.match(buildEstimate({state: "queued"}, energy), /Once started.*10-12 minutes/);
  const started = Date.parse("2026-09-05T12:00:00Z");
  const result = {state: "building", updated_at: new Date(started).toISOString()};
  assert.match(buildEstimate(result, energy, started - 60000), /^0 min elapsed/);
  assert.match(buildEstimate(result, energy, started + 9.9 * 60000), /^9 min elapsed.*10-12/);
  for (const minutes of [10, 12]) {
    assert.match(buildEstimate(result, energy, started + minutes * 60000), /Expected to finish soon/);
  }
  assert.match(buildEstimate(result, energy, started + 13 * 60000), /Taking longer than usual/);
  assert.equal(buildEstimate({...result, updated_at: "invalid"}, energy), "");
  assert.equal(buildEstimate({state: "building"}, energy), "");
  assert.equal(buildEstimate({state: "ready"}, energy), "");
});

test("fleet options expand without copy-hash controls", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  const html = await readFile(new URL("../../../docs/custom-build/index.html", import.meta.url), "utf8");
  assert.ok(source.includes("<details><summary>Options</summary>"));
  assert.equal(source.includes("copyHash"), false);
  assert.equal(html.includes("copy-hash"), false);
});

test("fleet build identity stays separate from editable names", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  assert.ok(source.includes('`${labelForBuild(hash)} · ${shortHash(hash, 8)}`'));
  assert.ok(source.includes('new Option(identityForBuild(build.combination_hash)'));
  assert.ok(source.includes('label.value = labelForBuild(build.combination_hash)'));
  assert.ok(source.includes('row.querySelector(".build-hash").textContent = shortHash(build.combination_hash, 8)'));
  assert.ok(source.includes('identityForBuild(scale.installed_combination)'));
  assert.ok(source.includes('identityForBuild(scale.desired_combination)'));
});

test("fleet names save on change without separate save actions", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  assert.equal(source.includes("save-build"), false);
  assert.equal(source.includes("save-scale"), false);
  for (const field of ["label", "name"]) {
    assert.ok(source.includes(`${field}.addEventListener("change", async () =>`));
    assert.ok(source.includes(`if (event.key === "Enter") ${field}.blur()`));
    assert.ok(source.includes(`${field}.value = previous;`));
  }
});

test("ready WiFi builds save next to the build action and navigate without assignment", async () => {
  const html = await readFile(new URL("../../../docs/custom-build/index.html", import.meta.url), "utf8");
  const app = await readFile(new URL("../../../docs/custom-build/app.js", import.meta.url), "utf8");
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  assert.equal(html.match(/id="add-fleet-build"/g).length, 1);
  assert.ok(html.indexOf('id="add-fleet-build"') < html.indexOf('id="fleet-panel"'));
  assert.ok(html.includes('>Saved builds</h4>'));
  assert.equal(/>[^<]*fleet[^<]*</i.test(html), false);
  assert.ok(app.includes('installMethod === "wifi" && currentBuildState === "ready"'));
  const save = source.slice(source.indexOf('addBuild.addEventListener("click"'), source.indexOf('selectAll.addEventListener("change"'));
  assert.ok(save.includes('buildSelect.value = combinationHash'));
  assert.ok(save.includes('scrollIntoView'));
  assert.ok(save.includes('if (await loadFleet() && getReadyHash() === combinationHash) showScales()'));
  assert.equal(save.includes('/assignments'), false);
  assert.ok(save.includes('savingBuild = true'));
});

test("fleet assignment uses only the explicit scale selection", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  const html = await readFile(new URL("../../../docs/custom-build/index.html", import.meta.url), "utf8");
  assert.equal(source.includes("assignAll"), false);
  assert.equal(html.includes('id="assign-all"'), false);
  assert.ok(html.includes('type="checkbox"> Select all'));
  assert.ok(source.includes("{device_ids: [...selectedDeviceIds]}"));
});

test("ready builds save without paired scales and require durable recovery access", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/fleet.js", import.meta.url), "utf8");
  const handler = source.slice(source.indexOf('addBuild.addEventListener("click"'), source.indexOf('selectAll.addEventListener("change"'));
  assert.ok(source.includes("addBuild.disabled = !readyHash || savingBuild"));
  assert.equal(source.includes("Link a scale first"), false);
  for (const mode of ["new", "existing", "storage-blocked", "request-failed", "not-ready"]) {
    const requests = [];
    const errors = [];
    const context = {
      fleetKey: mode === "existing" ? "existing-key" : "",
      scales: [], builds: [], savingBuild: false,
      getReadyHash: () => mode === "not-ready" ? "" : "a".repeat(64),
      crypto: {getRandomValues: value => value.fill(1)},
      encodeBase32: () => "new-key",
      activateKey: key => {
        if (mode === "storage-blocked") return false;
        context.fleetKey = key;
        return true;
      },
      addBuild: {addEventListener: (event, listener) => { context.click = listener; }},
      api: async (path, options) => {
        requests.push({path, key: context.fleetKey, body: JSON.parse(options.body)});
        if (mode === "request-failed") throw new Error("offline");
      },
      renderControls: () => {}, showToast: () => {}, showApiError: error => errors.push(error),
      loadFleet: async () => true,
      buildSelect: {focus: () => {}},
      document: {querySelector: () => ({scrollIntoView: () => {}})},
      matchMedia: () => ({matches: true}),
    };
    runInNewContext(handler, context);
    await context.click();
    assert.equal(context.savingBuild, false);
    assert.equal(requests.length, ["storage-blocked", "not-ready"].includes(mode) ? 0 : 1);
    if (requests.length) {
      assert.equal(requests[0].path, "/api/v1/fleet/builds");
      assert.equal(requests[0].key, mode === "existing" ? "existing-key" : "new-key");
      assert.equal(requests[0].body.combination_hash, "a".repeat(64));
    }
    assert.equal(errors.length, mode === "request-failed" ? 1 : 0);
  }
  const activate = source.slice(source.indexOf("const activateKey ="), source.indexOf('document.querySelector("#start-fleet")'));
  const context = {
    fleetKey: "", normalizedFleetKey: key => key, fleetPattern: /^[A-Z2-7]{32}$/,
    saveKey: () => false, showToast: () => {}, setMode: () => {}, setSettingsOpen: () => {}, loadFleet: () => {},
  };
  assert.equal(runInNewContext(activate + 'activateKey("A".repeat(32))', context), false);
  assert.equal(context.fleetKey, "");
});


test("theme follows the system until a saved preference overrides it", async () => {
  const html = await readFile(new URL("../../../docs/custom-build/index.html", import.meta.url), "utf8");
  const bootstrap = html.match(/<script id="theme-bootstrap">([\s\S]*?)<\/script>/)?.[1];
  assert.ok(bootstrap);
  const loadTheme = (stored, systemDark) => {
    const dataset = {};
    const themeColor = {content: ""};
    runInNewContext(bootstrap, {
      document: {
        documentElement: {dataset},
        querySelector: () => themeColor,
      },
      localStorage: {getItem: () => stored},
      matchMedia: () => ({matches: systemDark}),
    });
    return {dataset, themeColor: themeColor.content};
  };
  assert.deepEqual(loadTheme(null, false), {
    dataset: {themePreference: "system", theme: "light"},
    themeColor: "#0d6b4f",
  });
  assert.deepEqual(loadTheme(null, true), {
    dataset: {themePreference: "system", theme: "dark"},
    themeColor: "#111413",
  });
  assert.deepEqual(loadTheme(JSON.stringify({version: 1, preference: "dark"}), false), {
    dataset: {themePreference: "dark", theme: "dark"},
    themeColor: "#111413",
  });
  assert.deepEqual(loadTheme("not json", true), {
    dataset: {themePreference: "system", theme: "dark"},
    themeColor: "#111413",
  });
  const app = await readFile(new URL("../../../docs/custom-build/app.js", import.meta.url), "utf8");
  assert.ok(app.includes("localStorage.setItem(themeStorageKey"));
  assert.ok(app.includes('systemTheme.addEventListener("change"'));
});


test("USB-ready builds use a persistent updater cue instead of flashing", async () => {
  const source = await readFile(new URL("../../../docs/custom-build/app.js", import.meta.url), "utf8");
  assert.ok(source.includes('result.state === "ready" && installMethod === "usb"'));
  assert.ok(source.includes('classList.toggle("is-ready", updaterReady)'));
  assert.equal(source.includes("is-next-step"), false);
});

test("build activity reflects confirmed stages and stops on disconnect or completion", () => {
  assert.equal(elapsedTime(-1000), "0:00");
  assert.equal(elapsedTime(578000), "9:38");
  assert.equal(elapsedTime(3600000), "60:00");
  const element = () => ({dataset: {}, hidden: false, textContent: "", attributes: {},
    setAttribute(name, value) { this.attributes[name] = value; },
    removeAttribute(name) { delete this.attributes[name]; }});
  const steps = [element(), element(), element()];
  const children = new Map(["[data-build-elapsed]", "[data-build-checked]", ".build-activity", ".build-live-details"]
    .map(key => [key, element()]));
  const root = {...element(), querySelectorAll: () => steps, querySelector: key => children.get(key)};
  const update = initBuildProgress(root);
  try {
    update({state: "building", updated_at: new Date(Date.now() - 578000).toISOString()});
    assert.equal(root.dataset.active, "true");
    assert.deepEqual(steps.map(step => step.dataset.state), ["complete", "current", "pending"]);
    assert.equal(steps[1].attributes["aria-current"], "step");
    assert.match(children.get("[data-build-elapsed]").textContent, /^Elapsed 9:38/);
    update({state: "building", connectionLost: true});
    assert.equal(root.dataset.active, "false");
    assert.match(children.get("[data-build-checked]").textContent, /interrupted/);
    update({state: "building", pollingPaused: true});
    assert.equal(root.dataset.active, "false");
    update({state: "ready"});
    assert.equal(root.hidden, false);
    assert.equal(children.get(".build-live-details").hidden, true);
    assert.equal(steps[2].attributes["aria-current"], "step");
    assert.equal(steps[1].attributes["aria-current"], undefined);
  } finally {
    update({state: "missing"});
  }
  assert.equal(root.hidden, true);
});
