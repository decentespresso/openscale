const dayMs = 86400000;
export const entryBytes = 12 * 1024 * 1024;
const storageBudget = 4_000_000_000;
const hashPattern = /^[0-9a-f]{64}$/;
const batchSize = 10;
const assetNames = ["build-manifest.json", "HDS_FW_custom.zip", "dependencies.txt", "firmware.bin",
  "littlefs.bin", "ota-manifest.json", "ota-manifest.sig"];

export class BudgetError extends Error {
  constructor(code) {
    super(code);
    this.code = code;
  }
}

export async function reserveBudget(storage, {write = false, hash = null, pin = false, reserve = false, filename = null, bytes = 0} = {}, now = Date.now()) {
  return storage.transaction(async transaction => {
    const day = Math.floor(now / dayMs);
    const month = new Date(now).toISOString().slice(0, 7);
    const stored = await transaction.get("budget:operations");
    const daily = stored?.day === day ? stored.daily : 0;
    const writes = stored?.month === month ? stored.writes : 0;
    const reads = stored?.month === month ? stored.reads : 0;
    if (![daily, writes, reads].every(value => Number.isSafeInteger(value) && value >= 0)) {
      throw new BudgetError("budget_unavailable");
    }
    if (daily >= 10000 || writes + Number(write) > 100000 || reads + Number(!write) > 250000) {
      throw new BudgetError("free_tier_operation_budget");
    }
    if (hash !== null) {
      if (!hashPattern.test(hash)) throw new BudgetError("invalid_combination_hash");
      const key = `retention:${hash}`;
      const record = await transaction.get(key);
      if (record?.expired) throw new BudgetError("build_expired");
      const sizes = filename ? {...record?.sizes, [filename]: Math.max(record?.sizes?.[filename] || 0, bytes)} : record?.sizes;
      if (filename && (!Number.isSafeInteger(bytes) || bytes <= 0 ||
          Object.values(sizes).reduce((sum, size) => sum + size, 0) > entryBytes)) {
        throw new BudgetError("build_storage_budget");
      }
      if (reserve && !record?.reserved) {
        const inventory = await transaction.get("budget:inventory");
        if (!inventory?.complete || !Number.isSafeInteger(inventory.bytes) || inventory.bytes < 0) {
          throw new BudgetError("storage_inventory_pending");
        }
        if (inventory.bytes + entryBytes > storageBudget) throw new BudgetError("free_tier_storage_budget");
        await transaction.put("budget:inventory", {...inventory, bytes: inventory.bytes + entryBytes});
      }
      await transaction.put(key, {
        ...record,
        createdAt: record?.createdAt ?? now,
        pinned: Boolean(record?.pinned || pin),
        reserved: Boolean(record?.reserved || reserve),
        ...(sizes ? {sizes} : {}),
      });
    }
    await transaction.put("budget:operations", {
      day, month, daily: daily + 1, writes: writes + Number(write), reads: reads + Number(!write),
    });
  });
}

export function guardedBucket(bucket, gate) {
  return {
    async head(key) {
      await gate({});
      return bucket.head(key);
    },
    async get(key) {
      const hash = key.split("/")[1];
      await gate({hash: hashPattern.test(hash || "") ? hash : null, pin: true});
      return bucket.get(key);
    },
    async put(key, body, options) {
      const hash = key.split("/")[1];
      await gate({write: true, hash, reserve: true, filename: key.split("/")[2], bytes: body.byteLength});
      return bucket.put(key, body, options);
    },
  };
}

export async function reserveBuild(storage, hash, firmwareRef) {
  await reserveBudget(storage, {hash, reserve: true});
  await storage.transaction(async transaction => {
    const key = `retention:${hash}`;
    const record = await transaction.get(key);
    await transaction.put(key, {...record, firmwareRef});
  });
}

export async function inventoryBatch(storage, bucket) {
  const inventory = await storage.get("budget:inventory") || {bytes: 0, complete: false};
  if (inventory.complete) return inventory;
  await reserveBudget(storage, {write: true});
  const page = await bucket.list({limit: 100, ...(inventory.cursor ? {cursor: inventory.cursor} : {})});
  const total = page.objects.reduce((sum, object) => sum + object.size, 0);
  for (const object of page.objects) {
    const hash = object.key.split("/")[1];
    if (!hashPattern.test(hash || "")) continue;
    await storage.transaction(async transaction => {
      const key = `retention:${hash}`;
      const record = await transaction.get(key);
      await transaction.put(key, {...record, pinned: true, legacy: true});
    });
  }
  const next = {bytes: inventory.bytes + total, complete: !page.truncated, cursor: page.cursor || null};
  await storage.put("budget:inventory", next);
  return next;
}

function eligible(record, now) {
  return record?.reserved && !record.pinned && !record.legacy && !record.released && record.firmwareRef === "main" &&
    Number.isFinite(record.createdAt) && now - record.createdAt >= 30 * dayMs;
}

async function expireBuild(storage, bucket, hash, now) {
  await reserveBudget(storage);
  const marked = await storage.transaction(async transaction => {
    const key = `retention:${hash}`;
    const record = await transaction.get(key);
    const build = await transaction.get(`build:${hash}`);
    if (!eligible(record, now) || ["queued", "building"].includes(build?.state)) return false;
    await transaction.put(key, {...record, expired: true});
    return true;
  });
  if (!marked) return false;
  await bucket.delete(assetNames.map(name => `v1/${hash}/${name}`));
  await storage.transaction(async transaction => {
    const key = `retention:${hash}`;
    const record = await transaction.get(key);
    if (record.released) return;
    const inventory = await transaction.get("budget:inventory");
    await transaction.put({
      [key]: {...record, released: true},
      "budget:inventory": {...inventory, bytes: inventory.bytes - entryBytes},
      [`build:${hash}`]: {state: "expired", combination_hash: hash, updated_at: new Date(now).toISOString()},
    });
  });
  return true;
}

export async function retentionBatch(storage, bucket, now = Date.now(), dryRun = true) {
  const inventory = await inventoryBatch(storage, bucket);
  if (!inventory.complete) return {mode: "inventory", ...inventory};
  const cursor = await storage.get("retention:cursor");
  const records = await storage.list({prefix: "retention:", limit: batchSize, ...(cursor ? {startAfter: cursor} : {})});
  const candidates = [];
  for (const [key, record] of records) {
    const hash = key.slice("retention:".length);
    if (!hashPattern.test(hash) || !eligible(record, now)) continue;
    const build = await storage.get(`build:${hash}`);
    if (["queued", "building"].includes(build?.state)) continue;
    candidates.push(hash);
    if (!dryRun) await expireBuild(storage, bucket, hash, now);
  }
  await storage.put("retention:cursor", records.size === batchSize ? [...records.keys()].at(-1) : "");
  const report = {mode: dryRun ? "dry-run" : "delete", checkedAt: new Date(now).toISOString(), checked: records.size, candidates,
    reservedCandidateBytes: candidates.length * entryBytes, reservedStorageBytes: inventory.bytes};
  await storage.put("budget:last-report", report);
  return report;
}
