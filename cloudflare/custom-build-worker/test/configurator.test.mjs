import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {test} from "node:test";
import {runInNewContext} from "node:vm";

import {
  buildSummary,
  deploymentState,
  lastSeenLabel,
  shortHash,
} from "../../../docs/custom-build/fleet-state.mjs";
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
  assert.deepEqual(shipped.firmware_refs, ["v3.1.14", "main"]);
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
  }), "pull-ota, wifi, grind-by-weight 1.0.0 +1");
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
