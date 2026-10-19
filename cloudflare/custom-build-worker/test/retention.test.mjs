import assert from "node:assert/strict";
import {test} from "node:test";
import {entryBytes, guardedBucket, inventoryBatch, reserveBudget, reserveBuild, retentionBatch} from "../src/build-retention.mjs";

class Storage {
  values = new Map();
  pending = Promise.resolve();
  async get(key) { return this.values.get(key); }
  async put(key, value) {
    for (const [name, entry] of typeof key === "string" ? [[key, value]] : Object.entries(key)) this.values.set(name, entry);
  }
  async list({prefix, startAfter = "", limit}) {
    return new Map([...this.values].filter(([key]) => key.startsWith(prefix) && key > startAfter)
      .sort(([a], [b]) => a.localeCompare(b)).slice(0, limit));
  }
  transaction(action) {
    const result = this.pending.then(() => action(this));
    this.pending = result.catch(() => {});
    return result;
  }
}

const hash = "a".repeat(64);
const now = Date.now();
const old = now - 31 * 86400000;
const bucket = {list: async () => ({objects: [], truncated: false})};

test("inventory blocks writes, counts existing bytes and protects legacy builds", async () => {
  const storage = new Storage();
  await assert.rejects(reserveBuild(storage, hash, "main"), /storage_inventory_pending/);
  const result = await inventoryBatch(storage, {list: async options => {
    assert.equal(options.limit, 100);
    return {objects: [{key: `v1/${hash}/firmware.bin`, size: 100}, {key: "unrelated", size: 50}], truncated: false};
  }});
  assert.equal(result.bytes, 150);
  assert.equal(result.complete, true);
  assert.equal((await storage.get(`retention:${hash}`)).legacy, true);
  assert.equal((await storage.get(`retention:${hash}`)).pinned, true);
});

test("concurrent reservations cannot exceed storage cap or double reserve a build", async () => {
  const storage = new Storage();
  await storage.put("budget:inventory", {complete: true, bytes: 4_000_000_000 - entryBytes});
  const results = await Promise.allSettled([reserveBuild(storage, hash, "main"), reserveBuild(storage, "b".repeat(64), "main")]);
  assert.equal(results.filter(result => result.status === "fulfilled").length, 1);
  await reserveBuild(storage, hash, "main");
  assert.equal((await storage.get("budget:inventory")).bytes, 4_000_000_000);
});

test("partial uploads cannot exceed the reserved entry, and retries do not release bytes", async () => {
  const storage = new Storage();
  await inventoryBatch(storage, bucket);
  await reserveBuild(storage, hash, "main");
  await reserveBudget(storage, {hash, reserve: true, filename: "firmware.bin", bytes: entryBytes, write: true});
  await reserveBudget(storage, {hash, reserve: true, filename: "firmware.bin", bytes: 1, write: true});
  await assert.rejects(reserveBudget(storage, {hash, reserve: true, filename: "littlefs.bin", bytes: 1, write: true}), /build_storage_budget/);
  assert.equal((await storage.get("budget:inventory")).bytes, entryBytes);
});

test("operation quotas fail closed before bucket calls and reset on UTC boundaries", async () => {
  const storage = new Storage();
  await reserveBudget(storage, {}, now);
  const stored = await storage.get("budget:operations");
  await storage.put("budget:operations", {...stored, daily: 10000});
  const guarded = guardedBucket({head: () => assert.fail("bucket called after quota exhausted")}, options => reserveBudget(storage, options, now));
  await assert.rejects(guarded.head("file"), /free_tier_operation_budget/);
  await storage.put("budget:operations", {...stored, reads: 250000});
  await assert.rejects(reserveBudget(storage, {}, now), /free_tier_operation_budget/);
  await storage.put("budget:operations", {...stored, writes: 100000});
  await assert.rejects(reserveBudget(storage, {write: true}, now), /free_tier_operation_budget/);
  await reserveBudget(storage, {write: true}, Date.UTC(2027, 0, 1));
  assert.equal((await storage.get("budget:operations")).writes, 1);
});

test("dry run excludes stable, pinned, recent, unknown, legacy and active builds", async () => {
  const storage = new Storage();
  await inventoryBatch(storage, bucket);
  const cases = [
    {firmwareRef: "main"}, {firmwareRef: "v3.1.14"}, {firmwareRef: "main", pinned: true},
    {firmwareRef: "main", createdAt: now}, {}, {firmwareRef: "main", legacy: true}, {firmwareRef: "main"},
  ];
  for (const [index, record] of cases.entries()) {
    const id = String(index).repeat(64);
    await storage.put(`retention:${id}`, {reserved: true, createdAt: old, ...record});
    if (index === 6) await storage.put(`build:${id}`, {state: "building"});
  }
  const report = await retentionBatch(storage, {delete: () => assert.fail("dry run deleted an object")}, now);
  assert.deepEqual(report.candidates, ["0".repeat(64)]);
  assert.equal(report.mode, "dry-run");
});

test("downloads permanently protect rollback assets; deletion is idempotent and expires the identity", async () => {
  const storage = new Storage();
  await inventoryBatch(storage, bucket);
  await reserveBuild(storage, hash, "main");
  const key = `retention:${hash}`;
  await storage.put(key, {...await storage.get(key), createdAt: old});
  const guarded = guardedBucket({get: async () => ({size: 1})}, options => reserveBudget(storage, options));
  await guarded.get(`v1/${hash}/firmware.bin`);
  assert.deepEqual((await retentionBatch(storage, bucket, now)).candidates, []);
  const other = "b".repeat(64);
  await reserveBuild(storage, other, "main");
  await storage.put(`retention:${other}`, {...await storage.get(`retention:${other}`), createdAt: old});
  const deleted = [];
  const liveBucket = {delete: async keys => {deleted.push(...keys);}};
  await retentionBatch(storage, liveBucket, now, false);
  assert.equal(deleted.length, 7);
  assert.equal((await storage.get("budget:inventory")).bytes, entryBytes);
  await retentionBatch(storage, liveBucket, now, false);
  assert.equal(deleted.length, 7);
  await assert.rejects(reserveBuild(storage, other, "main"), /build_expired/);
  await assert.rejects(reserveBudget(storage, {hash: other, pin: true}), /build_expired/);
});

test("failed deletion retains reservation and resumes without reviving expired downloads", async () => {
  const storage = new Storage();
  await inventoryBatch(storage, bucket);
  await reserveBuild(storage, hash, "main");
  await storage.put(`retention:${hash}`, {...await storage.get(`retention:${hash}`), createdAt: old});
  await assert.rejects(retentionBatch(storage, {delete: async () => {throw new Error("offline");}}, now, false), /offline/);
  assert.equal((await storage.get("budget:inventory")).bytes, entryBytes);
  await assert.rejects(reserveBudget(storage, {hash, pin: true}), /build_expired/);
  await retentionBatch(storage, {delete: async () => {}}, now, false);
  assert.equal((await storage.get("budget:inventory")).bytes, 0);
});
