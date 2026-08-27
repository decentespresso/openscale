import assert from "node:assert/strict";
import {test} from "node:test";

import {
  catalogRevisionChanged,
  defaultSelection,
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
  ],
};


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
    firmware_ref: "main", features: [], plugins: ["blocker"],
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
