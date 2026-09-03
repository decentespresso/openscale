const deviceIdPattern = /^[0-9a-f]{32}$/;
const hashPattern = /^[0-9a-f]{64}$/;
const pairCodePattern = /^[0-9A-F]{6}-[0-9]{6}$/;
const fleetSecretPattern = /^[A-Z2-7]{32}$/;
const pairLifetimeMs = 12 * 60 * 60 * 1000;
const pairRefreshMs = 30 * 1000;
const claimWindowMs = 60 * 1000;
const claimsPerWindow = 10;
const pairCreationWindowMs = 60 * 60 * 1000;
const maxFleetBuilds = 100;
const storageBatchSize = 128;
export const pairCreationsPerClient = 10;
export const pairCreationsGlobal = 500;

export class FleetError extends Error {
  constructor(status, code, retryAfter = null) {
    super(code);
    this.status = status;
    this.code = code;
    this.retryAfter = retryAfter;
  }
}

function hex(bytes) {
  return Array.from(new Uint8Array(bytes), byte => byte.toString(16).padStart(2, "0")).join("");
}

async function sha256(value) {
  return hex(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value)));
}

function sameHash(left, right) {
  if (!hashPattern.test(left || "") || !hashPattern.test(right || "")) return false;
  let difference = 0;
  for (let index = 0; index < left.length; index++) difference |= left.charCodeAt(index) ^ right.charCodeAt(index);
  return difference === 0;
}

function bearer(request) {
  const header = request.headers.get("Authorization") || "";
  return header.startsWith("Bearer ") ? header.slice(7) : "";
}

function requireObject(value, keys) {
  if (!value || typeof value !== "object" || Array.isArray(value) ||
      Object.keys(value).sort().join(",") !== [...keys].sort().join(",")) {
    throw new FleetError(400, "invalid_request");
  }
  return value;
}

async function readJson(request, maxBytes = 1024) {
  if (!request.headers.get("Content-Type")?.toLowerCase().startsWith("application/json")) {
    throw new FleetError(415, "json_required");
  }
  const declaredLength = Number(request.headers.get("Content-Length") || 0);
  if (declaredLength > maxBytes) throw new FleetError(413, "request_too_large");
  const payload = await request.arrayBuffer();
  if (!payload.byteLength || payload.byteLength > maxBytes) throw new FleetError(413, "request_too_large");
  try {
    return JSON.parse(new TextDecoder().decode(payload));
  } catch {
    throw new FleetError(400, "invalid_json");
  }
}

function normalizedPairCode(value) {
  const code = typeof value === "string" ? value.trim().toUpperCase() : "";
  if (!pairCodePattern.test(code)) throw new FleetError(400, "invalid_pair_code");
  return code;
}

async function fleetId(request) {
  const secret = bearer(request).replaceAll("-", "").toUpperCase();
  if (!fleetSecretPattern.test(secret)) throw new FleetError(401, "invalid_fleet_key");
  return sha256(secret);
}

async function deviceCredentials(request) {
  const deviceId = (request.headers.get("X-OpenScale-Device-ID") || "").toLowerCase();
  const secret = bearer(request);
  if (!deviceIdPattern.test(deviceId) || !/^[0-9a-f]{64}$/.test(secret)) {
    throw new FleetError(401, "invalid_device_credentials");
  }
  return {deviceId, secretHash: await sha256(secret)};
}

async function authenticatedDevice(request, storage) {
  const credentials = await deviceCredentials(request);
  const key = `device:${credentials.deviceId}`;
  const device = await storage.get(key);
  if (!device || !sameHash(device.secret_hash, credentials.secretHash)) {
    throw new FleetError(401, "invalid_device_credentials");
  }
  return {deviceId: credentials.deviceId, device, key, secretHash: credentials.secretHash};
}

function retryAfter(expiresAt, now) {
  return Math.max(1, Math.ceil((expiresAt - now) / 1000));
}

async function enforcePairCreationRate(request, storage, now) {
  const clientKey = request.headers.get("X-OpenScale-Client-Key") || "";
  if (!hashPattern.test(clientKey)) throw new FleetError(503, "rate_limit_unavailable");
  const stored = await storage.get("pair-rates");
  const rates = !stored || stored.expires_at <= now
    ? {global: 0, clients: {}, expires_at: now + pairCreationWindowMs}
    : stored;
  const clientCount = rates.clients[clientKey] || 0;
  if (clientCount >= pairCreationsPerClient || rates.global >= pairCreationsGlobal) {
    throw new FleetError(429, "pairing_creation_rate_limited", retryAfter(rates.expires_at, now));
  }
  await storage.put("pair-rates", {
    global: rates.global + 1,
    clients: {...rates.clients, [clientKey]: clientCount + 1},
    expires_at: rates.expires_at,
  });
}

async function pairDevice(request, storage) {
  const body = requireObject(await readJson(request), ["pair_code"]);
  const pairCode = normalizedPairCode(body.pair_code);
  const serialHint = pairCode.slice(0, 6);
  const credentials = await deviceCredentials(request);
  return storage.transaction(async transaction => {
    const now = Date.now();
    const deviceKey = `device:${credentials.deviceId}`;
    const storedDevice = await transaction.get(deviceKey);
    if (storedDevice && !sameHash(storedDevice.secret_hash, credentials.secretHash)) {
      throw new FleetError(401, "invalid_device_credentials");
    }
    if (!storedDevice) await enforcePairCreationRate(request, transaction, now);
    const device = storedDevice || {
      secret_hash: credentials.secretHash,
      serial_hint: serialHint,
      name: `Scale ${serialHint}`,
      fleet_id: null,
      desired_combination: null,
      desired_updated_at: null,
      installed_combination: null,
      firmware_version: null,
      last_seen_at: null,
    };
    if (device.serial_hint !== serialHint) throw new FleetError(400, "serial_hint_mismatch");
    if (device.pair_created_at && now - device.pair_created_at < pairRefreshMs) {
      throw new FleetError(
        429,
        "pairing_too_fast",
        retryAfter(device.pair_created_at + pairRefreshMs, now),
      );
    }
    const pairKey = `pair:${pairCode}`;
    const existing = await transaction.get(pairKey);
    if (existing && existing.device_id !== credentials.deviceId && existing.expires_at > now) {
      throw new FleetError(409, "pair_code_collision");
    }
    if (device.pair_code) await transaction.delete(`pair:${device.pair_code}`);
    const expiresAt = now + pairLifetimeMs;
    await transaction.put({
      [pairKey]: {device_id: credentials.deviceId, expires_at: expiresAt},
      [deviceKey]: {
        ...device,
        pair_code: pairCode,
        pair_created_at: now,
      },
    });
    return {pair_code: pairCode, expires_at: new Date(expiresAt).toISOString()};
  });
}

async function enforceClaimRate(request, storage) {
  const clientKey = request.headers.get("X-OpenScale-Client-Key") || "";
  if (!hashPattern.test(clientKey)) throw new FleetError(503, "rate_limit_unavailable");
  const now = Date.now();
  const key = `claim-rate:${clientKey}`;
  const stored = await storage.get(key);
  const rate = !stored || stored.expires_at <= now
    ? {count: 0, expires_at: now + claimWindowMs}
    : stored;
  if (rate.count >= claimsPerWindow) {
    throw new FleetError(429, "pairing_rate_limited", retryAfter(rate.expires_at, now));
  }
  await storage.put(key, {...rate, count: rate.count + 1});
}

async function expirePair(storage, pairKey, pair) {
  const deviceKey = `device:${pair.device_id}`;
  const device = await storage.get(deviceKey);
  if (device?.pair_code === pairKey.slice(5)) {
    if (device.fleet_id) {
      await storage.put(deviceKey, {...device, pair_code: null, pair_created_at: null});
    } else {
      await storage.delete(deviceKey);
    }
  }
  await storage.delete(pairKey);
}

export async function fleetExpirations(storage) {
  const [pairs, claims, pairRates] = await Promise.all([
    storage.list({prefix: "pair:"}),
    storage.list({prefix: "claim-rate:"}),
    storage.get("pair-rates"),
  ]);
  return [
    ...[...pairs.values()].map(pair => pair.expires_at),
    ...[...claims.values()].map(rate => rate.expires_at),
    pairRates?.expires_at,
  ].filter(Number.isSafeInteger);
}

export async function cleanupFleetExpirations(storage, now = Date.now()) {
  await storage.transaction(async transaction => {
    const pairs = await transaction.list({prefix: "pair:"});
    for (const [key, pair] of pairs) {
      if (pair.expires_at <= now) await expirePair(transaction, key, pair);
    }
    const claims = await transaction.list({prefix: "claim-rate:"});
    for (const [key, rate] of claims) {
      if (rate.expires_at <= now) await transaction.delete(key);
    }
    const pairRates = await transaction.get("pair-rates");
    if (pairRates?.expires_at <= now) await transaction.delete("pair-rates");
  });
}

function fleetDevices(fleet) {
  return Array.isArray(fleet?.devices) ? fleet.devices.filter(deviceId => deviceIdPattern.test(deviceId)) : [];
}

function fleetBuilds(fleet) {
  return Array.isArray(fleet?.builds)
    ? fleet.builds.filter(build => hashPattern.test(build?.combination_hash || ""))
    : [];
}

function normalizedText(value, maxLength, code) {
  const text = typeof value === "string" ? value.trim() : "";
  if (!text || text.length > maxLength || /[\u0000-\u001f\u007f]/.test(text)) {
    throw new FleetError(400, code);
  }
  return text;
}

function normalizedName(value) {
  return normalizedText(value, 40, "invalid_scale_name");
}

function normalizedBuildLabel(value) {
  return normalizedText(value, 40, "invalid_build_label");
}

function normalizedFirmwareVersion(value) {
  return normalizedText(value, 64, "invalid_firmware_version");
}

async function readyBuildReference(env, combinationHash) {
  if (!hashPattern.test(combinationHash || "")) {
    throw new FleetError(400, "invalid_combination_hash");
  }
  const object = await env.BUILDS.get(`v1/${combinationHash}/build-manifest.json`);
  if (!object) throw new FleetError(409, "build_not_ready");
  let manifest;
  try {
    manifest = await object.json();
  } catch {
    throw new FleetError(409, "build_not_ready");
  }
  if (manifest?.custom_build !== true || manifest.combination_hash !== combinationHash ||
      typeof manifest.firmware_version !== "string" || !manifest.firmware_version ||
      manifest.firmware_version.length > 64) {
    throw new FleetError(409, "build_not_ready");
  }
  const features = Array.isArray(manifest.features)
    ? manifest.features.filter(value => typeof value === "string" && value.length <= 64)
    : [];
  const plugins = Array.isArray(manifest.plugins) ? manifest.plugins
    .filter(value => value && typeof value.id === "string" && value.id.length <= 64)
    .map(value => ({
      id: value.id,
      version: typeof value.version === "string" && value.version.length <= 32 ? value.version : "",
    })) : [];
  return {
    combination_hash: combinationHash,
    firmware_version: manifest.firmware_version,
    features,
    plugins,
  };
}

async function buildStates(hashes, storage, env) {
  const uniqueHashes = [...new Set(hashes.filter(hash => hashPattern.test(hash || "")))];
  return Object.fromEntries(await Promise.all(uniqueHashes.map(async hash => {
    if (await env.BUILDS.head(`v1/${hash}/build-manifest.json`)) return [hash, "ready"];
    const record = await storage.get(`build:${hash}`);
    return [hash, ["queued", "building", "failed"].includes(record?.state) ? record.state : "missing"];
  })));
}

function scaleRecord(deviceId, device) {
  return {
    device_id: deviceId,
    serial_hint: device.serial_hint,
    name: device.name,
    desired_combination: hashPattern.test(device.desired_combination || "")
      ? device.desired_combination : null,
    desired_updated_at: typeof device.desired_updated_at === "string"
      ? device.desired_updated_at : null,
    installed_combination: hashPattern.test(device.installed_combination || "")
      ? device.installed_combination : null,
    firmware_version: typeof device.firmware_version === "string" ? device.firmware_version : null,
    last_seen_at: typeof device.last_seen_at === "string" ? device.last_seen_at : null,
  };
}

async function fleetScaleRecords(ownerFleetId, fleet, storage) {
  return (await Promise.all(fleetDevices(fleet).map(async deviceId => {
    const device = await storage.get(`device:${deviceId}`);
    return device && device.fleet_id === ownerFleetId ? scaleRecord(deviceId, device) : null;
  }))).filter(Boolean).sort((left, right) => left.name.localeCompare(right.name));
}

async function fleetSnapshot(ownerFleetId, storage, env) {
  const fleet = await storage.get(`fleet:${ownerFleetId}`);
  const scales = await fleetScaleRecords(ownerFleetId, fleet, storage);
  const builds = fleetBuilds(fleet);
  const states = await buildStates([
    ...builds.map(build => build.combination_hash),
    ...scales.map(scale => scale.desired_combination),
  ], storage, env);
  return {
    builds: builds.map(build => ({...build, state: states[build.combination_hash]})),
    scales,
    build_states: states,
  };
}

async function claimDevice(request, storage) {
  await enforceClaimRate(request, storage);
  const body = requireObject(await readJson(request), ["pair_code"]);
  const pairCode = normalizedPairCode(body.pair_code);
  const ownerFleetId = await fleetId(request);
  return storage.transaction(async transaction => {
    const pairKey = `pair:${pairCode}`;
    const pair = await transaction.get(pairKey);
    if (!pair || pair.expires_at <= Date.now()) {
      throw new FleetError(404, "pair_code_not_found");
    }
    const deviceKey = `device:${pair.device_id}`;
    const device = await transaction.get(deviceKey);
    if (!device) throw new FleetError(404, "device_not_found");
    const oldFleetKey = device.fleet_id ? `fleet:${device.fleet_id}` : null;
    const newFleetKey = `fleet:${ownerFleetId}`;
    const [oldFleet, newFleet] = await Promise.all([
      oldFleetKey ? transaction.get(oldFleetKey) : null,
      transaction.get(newFleetKey),
    ]);
    const oldDevices = fleetDevices(oldFleet).filter(id => id !== pair.device_id);
    const newDevices = [...new Set([...fleetDevices(newFleet), pair.device_id])];
    const transferred = oldFleetKey && oldFleetKey !== newFleetKey;
    const updatedDevice = {
      ...device,
      ...(transferred ? {
        desired_combination: null,
        desired_updated_at: null,
        name: `Scale ${device.serial_hint}`,
      } : {}),
      fleet_id: ownerFleetId,
      pair_code: null,
      pair_created_at: null,
    };
    const updates = {
      [deviceKey]: updatedDevice,
      [newFleetKey]: {...newFleet, devices: newDevices},
    };
    if (oldFleetKey && oldFleetKey !== newFleetKey) {
      updates[oldFleetKey] = {...oldFleet, devices: oldDevices};
    }
    await transaction.put(updates);
    await transaction.delete(pairKey);
    return {device_id: pair.device_id, serial_hint: device.serial_hint, name: updatedDevice.name};
  });
}

async function listScales(request, storage, env) {
  const ownerFleetId = await fleetId(request);
  const fleet = await storage.get(`fleet:${ownerFleetId}`);
  const scales = await fleetScaleRecords(ownerFleetId, fleet, storage);
  const states = await buildStates(scales.map(scale => scale.desired_combination), storage, env);
  return {scales, build_states: states};
}

async function fleetOverview(request, storage, env) {
  return fleetSnapshot(await fleetId(request), storage, env);
}

async function listBuilds(request, storage, env) {
  const ownerFleetId = await fleetId(request);
  const fleet = await storage.get(`fleet:${ownerFleetId}`);
  const builds = fleetBuilds(fleet);
  const states = await buildStates(builds.map(build => build.combination_hash), storage, env);
  return {builds: builds.map(build => ({...build, state: states[build.combination_hash]}))};
}

async function addBuild(request, storage, env) {
  const ownerFleetId = await fleetId(request);
  const body = requireObject(await readJson(request), ["combination_hash"]);
  const key = `fleet:${ownerFleetId}`;
  if (!(await storage.get(key))) throw new FleetError(404, "fleet_not_found");
  const reference = await readyBuildReference(env, body.combination_hash);
  return storage.transaction(async transaction => {
    const fleet = await transaction.get(key);
    const builds = fleetBuilds(fleet);
    const existing = builds.find(build => build.combination_hash === reference.combination_hash);
    if (existing) return {...existing, state: "ready"};
    if (builds.length >= maxFleetBuilds) throw new FleetError(409, "build_library_full");
    const added = {
      ...reference,
      label: `Build ${reference.combination_hash.slice(0, 12).toUpperCase()}`,
      added_at: new Date().toISOString(),
    };
    await transaction.put(key, {...fleet, devices: fleetDevices(fleet), builds: [...builds, added]});
    return {...added, state: "ready"};
  });
}

async function renameBuild(request, storage, combinationHash) {
  const ownerFleetId = await fleetId(request);
  const body = requireObject(await readJson(request), ["label"]);
  const label = normalizedBuildLabel(body.label);
  return storage.transaction(async transaction => {
    const key = `fleet:${ownerFleetId}`;
    const fleet = await transaction.get(key);
    const builds = fleetBuilds(fleet);
    const selected = builds.find(build => build.combination_hash === combinationHash);
    if (!selected) throw new FleetError(404, "build_not_found");
    const updated = {...selected, label};
    await transaction.put(key, {
      ...fleet,
      builds: builds.map(build => build.combination_hash === combinationHash ? updated : build),
    });
    return updated;
  });
}

async function removeBuild(request, storage, combinationHash) {
  const ownerFleetId = await fleetId(request);
  return storage.transaction(async transaction => {
    const key = `fleet:${ownerFleetId}`;
    const fleet = await transaction.get(key);
    const builds = fleetBuilds(fleet);
    if (!builds.some(build => build.combination_hash === combinationHash)) {
      throw new FleetError(404, "build_not_found");
    }
    const devices = await Promise.all(fleetDevices(fleet).map(deviceId =>
      transaction.get(`device:${deviceId}`)));
    if (devices.some(device => device?.fleet_id === ownerFleetId &&
        device.desired_combination === combinationHash)) {
      throw new FleetError(409, "build_assigned");
    }
    await transaction.put(key, {
      ...fleet,
      builds: builds.filter(build => build.combination_hash !== combinationHash),
    });
    return {removed_combination: combinationHash};
  });
}

async function updateScale(request, storage, deviceId) {
  const ownerFleetId = await fleetId(request);
  const body = requireObject(await readJson(request), ["name"]);
  const name = normalizedName(body.name);
  return storage.transaction(async transaction => {
    const deviceKey = `device:${deviceId}`;
    const device = await transaction.get(deviceKey);
    if (!device || device.fleet_id !== ownerFleetId) throw new FleetError(404, "device_not_found");
    const updated = {...device, name};
    await transaction.put(deviceKey, updated);
    return {
      device_id: deviceId,
      serial_hint: updated.serial_hint,
      name: updated.name,
      desired_combination: updated.desired_combination,
    };
  });
}

async function updateAssignments(request, storage, env) {
  const ownerFleetId = await fleetId(request);
  const rawBody = await readJson(request, 64 * 1024);
  const allScales = rawBody?.all === true;
  const body = requireObject(
    rawBody,
    allScales ? ["all", "combination_hash"] : ["combination_hash", "device_ids"],
  );
  const combinationHash = body.combination_hash;
  if (combinationHash !== null) await readyBuildReference(env, combinationHash);
  let requestedDeviceIds = null;
  if (!allScales) {
    if (!Array.isArray(body.device_ids) || !body.device_ids.length ||
        body.device_ids.some(deviceId => !deviceIdPattern.test(deviceId || ""))) {
      throw new FleetError(400, body.device_ids?.length ? "invalid_device_id" : "empty_target_set");
    }
    requestedDeviceIds = [...new Set(body.device_ids)];
  }
  return storage.transaction(async transaction => {
    const fleet = await transaction.get(`fleet:${ownerFleetId}`);
    const deviceIds = allScales ? fleetDevices(fleet) : requestedDeviceIds;
    if (!deviceIds.length) throw new FleetError(400, "empty_target_set");
    const devices = await Promise.all(deviceIds.map(deviceId =>
      transaction.get(`device:${deviceId}`)));
    if (devices.some(device => !device)) throw new FleetError(404, "device_not_found");
    if (devices.some(device => device.fleet_id !== ownerFleetId)) {
      throw new FleetError(403, "cross_fleet_device");
    }
    const now = new Date().toISOString();
    const updates = Object.fromEntries(deviceIds.map((deviceId, index) => {
      const device = devices[index];
      return [`device:${deviceId}`, {
        ...device,
        desired_combination: combinationHash,
        desired_updated_at: device.desired_combination === combinationHash
          ? device.desired_updated_at : now,
      }];
    }));
    const entries = Object.entries(updates);
    for (let index = 0; index < entries.length; index += storageBatchSize) {
      await transaction.put(Object.fromEntries(entries.slice(index, index + storageBatchSize)));
    }
    return {combination_hash: combinationHash, device_ids: deviceIds, updated: deviceIds.length};
  });
}

function deviceAssignmentPayload(device) {
  return {
    linked: Boolean(device.fleet_id),
    desired_combination: hashPattern.test(device.desired_combination || "")
      ? device.desired_combination : null,
  };
}

async function deviceAssignment(request, storage) {
  const authenticated = await authenticatedDevice(request, storage);
  return deviceAssignmentPayload(authenticated.device);
}

async function deviceCheckIn(request, storage) {
  const body = requireObject(
    await readJson(request),
    ["firmware_version", "installed_combination"],
  );
  if (body.installed_combination !== null &&
      !hashPattern.test(body.installed_combination || "")) {
    throw new FleetError(400, "invalid_combination_hash");
  }
  const firmwareVersion = normalizedFirmwareVersion(body.firmware_version);
  const authenticated = await authenticatedDevice(request, storage);
  return storage.transaction(async transaction => {
    const device = await transaction.get(authenticated.key);
    if (!device || !sameHash(device.secret_hash, authenticated.secretHash)) {
      throw new FleetError(401, "invalid_device_credentials");
    }
    const updated = {
      ...device,
      installed_combination: body.installed_combination,
      firmware_version: firmwareVersion,
      last_seen_at: new Date().toISOString(),
    };
    await transaction.put(authenticated.key, updated);
    return deviceAssignmentPayload(updated);
  });
}

export function isDeviceApi(pathname) {
  return pathname === "/api/v1/device/pair" || pathname === "/api/v1/device/assignment" ||
    pathname === "/api/v1/device/check-in";
}

export function isFleetApi(pathname) {
  return [
    "/api/v1/fleet/claim",
    "/api/v1/fleet/scales",
    "/api/v1/fleet/overview",
    "/api/v1/fleet/builds",
    "/api/v1/fleet/assignments",
  ].includes(pathname) || /^\/api\/v1\/fleet\/(scales\/[0-9a-f]{32}|builds\/[0-9a-f]{64})$/.test(pathname);
}

export async function handleFleetRequest(request, storage, env) {
  const url = new URL(request.url);
  if (url.pathname === "/device/pair" && request.method === "POST") return pairDevice(request, storage);
  if (url.pathname === "/device/assignment" && request.method === "GET") return deviceAssignment(request, storage);
  if (url.pathname === "/device/check-in" && request.method === "POST") return deviceCheckIn(request, storage);
  if (url.pathname === "/fleet/claim" && request.method === "POST") return claimDevice(request, storage);
  if (url.pathname === "/fleet/scales" && request.method === "GET") return listScales(request, storage, env);
  if (url.pathname === "/fleet/overview" && request.method === "GET") return fleetOverview(request, storage, env);
  if (url.pathname === "/fleet/builds" && request.method === "GET") return listBuilds(request, storage, env);
  if (url.pathname === "/fleet/builds" && request.method === "POST") return addBuild(request, storage, env);
  if (url.pathname === "/fleet/assignments" && request.method === "POST") {
    return updateAssignments(request, storage, env);
  }
  const scaleMatch = url.pathname.match(/^\/fleet\/scales\/([0-9a-f]{32})$/);
  if (scaleMatch && request.method === "PATCH") return updateScale(request, storage, scaleMatch[1]);
  const buildMatch = url.pathname.match(/^\/fleet\/builds\/([0-9a-f]{64})$/);
  if (buildMatch && request.method === "PATCH") return renameBuild(request, storage, buildMatch[1]);
  if (buildMatch && request.method === "DELETE") return removeBuild(request, storage, buildMatch[1]);
  throw new FleetError(404, "not_found");
}
