const deviceIdPattern = /^[0-9a-f]{32}$/;
const hashPattern = /^[0-9a-f]{64}$/;
const pairCodePattern = /^[0-9A-F]{6}-[0-9]{6}$/;
const fleetSecretPattern = /^[A-Z2-7]{32}$/;
const pairLifetimeMs = 12 * 60 * 60 * 1000;
const pairRefreshMs = 30 * 1000;
const claimWindowMs = 60 * 1000;
const claimsPerWindow = 10;

export class FleetError extends Error {
  constructor(status, code) {
    super(code);
    this.status = status;
    this.code = code;
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

async function readJson(request) {
  if (!request.headers.get("Content-Type")?.toLowerCase().startsWith("application/json")) {
    throw new FleetError(415, "json_required");
  }
  const declaredLength = Number(request.headers.get("Content-Length") || 0);
  if (declaredLength > 1024) throw new FleetError(413, "request_too_large");
  const payload = await request.arrayBuffer();
  if (!payload.byteLength || payload.byteLength > 1024) throw new FleetError(413, "request_too_large");
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

async function authenticatedDevice(request, storage, allowCreate = false, serialHint = "") {
  const credentials = await deviceCredentials(request);
  const key = `device:${credentials.deviceId}`;
  let device = await storage.get(key);
  if (!device && allowCreate) {
    device = {
      secret_hash: credentials.secretHash,
      serial_hint: serialHint,
      name: `Scale ${serialHint}`,
      fleet_id: null,
      desired_combination: null,
    };
    await storage.put(key, device);
  }
  if (!device || !sameHash(device.secret_hash, credentials.secretHash)) {
    throw new FleetError(401, "invalid_device_credentials");
  }
  return {deviceId: credentials.deviceId, device, key};
}

async function pairDevice(request, storage) {
  const body = requireObject(await readJson(request), ["pair_code"]);
  const pairCode = normalizedPairCode(body.pair_code);
  const serialHint = pairCode.slice(0, 6);
  return storage.transaction(async transaction => {
    const authenticated = await authenticatedDevice(request, transaction, true, serialHint);
    if (authenticated.device.serial_hint !== serialHint) throw new FleetError(400, "serial_hint_mismatch");
    const now = Date.now();
    if (authenticated.device.pair_created_at && now - authenticated.device.pair_created_at < pairRefreshMs) {
      throw new FleetError(429, "pairing_too_fast");
    }
    const pairKey = `pair:${pairCode}`;
    const existing = await transaction.get(pairKey);
    if (existing && existing.device_id !== authenticated.deviceId && existing.expires_at > now) {
      throw new FleetError(409, "pair_code_collision");
    }
    if (authenticated.device.pair_code) await transaction.delete(`pair:${authenticated.device.pair_code}`);
    const expiresAt = now + pairLifetimeMs;
    await transaction.put({
      [pairKey]: {device_id: authenticated.deviceId, expires_at: expiresAt},
      [authenticated.key]: {
        ...authenticated.device,
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
  if (rate.count >= claimsPerWindow) throw new FleetError(429, "pairing_rate_limited");
  await storage.put(key, {...rate, count: rate.count + 1});
}

function fleetDevices(fleet) {
  return Array.isArray(fleet?.devices) ? fleet.devices.filter(deviceId => deviceIdPattern.test(deviceId)) : [];
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
      if (pair) await transaction.delete(pairKey);
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
    const updates = {
      [deviceKey]: {...device, fleet_id: ownerFleetId, pair_code: null, pair_created_at: null},
      [newFleetKey]: {devices: newDevices},
    };
    if (oldFleetKey && oldFleetKey !== newFleetKey) updates[oldFleetKey] = {devices: oldDevices};
    await transaction.put(updates);
    await transaction.delete(pairKey);
    return {device_id: pair.device_id, serial_hint: device.serial_hint, name: device.name};
  });
}

async function listScales(request, storage) {
  const ownerFleetId = await fleetId(request);
  const fleet = await storage.get(`fleet:${ownerFleetId}`);
  const devices = await Promise.all(fleetDevices(fleet).map(async deviceId => {
    const device = await storage.get(`device:${deviceId}`);
    return device && device.fleet_id === ownerFleetId ? {
      device_id: deviceId,
      serial_hint: device.serial_hint,
      name: device.name,
      desired_combination: device.desired_combination,
    } : null;
  }));
  return {scales: devices.filter(Boolean).sort((left, right) => left.name.localeCompare(right.name))};
}

function normalizedName(value) {
  const name = typeof value === "string" ? value.trim() : "";
  if (!name || name.length > 40 || /[\u0000-\u001f\u007f]/.test(name)) {
    throw new FleetError(400, "invalid_scale_name");
  }
  return name;
}

async function updateScale(request, storage, deviceId) {
  const ownerFleetId = await fleetId(request);
  const body = await readJson(request);
  const keys = body && typeof body === "object" && !Array.isArray(body) ? Object.keys(body).sort() : [];
  if (!keys.length || keys.some(key => !["desired_combination", "name"].includes(key))) {
    throw new FleetError(400, "invalid_request");
  }
  return storage.transaction(async transaction => {
    const deviceKey = `device:${deviceId}`;
    const device = await transaction.get(deviceKey);
    if (!device || device.fleet_id !== ownerFleetId) throw new FleetError(404, "device_not_found");
    const desiredCombination = body.desired_combination === undefined
      ? device.desired_combination
      : body.desired_combination;
    if (desiredCombination !== null && !hashPattern.test(desiredCombination || "")) {
      throw new FleetError(400, "invalid_combination_hash");
    }
    const updated = {
      ...device,
      name: body.name === undefined ? device.name : normalizedName(body.name),
      desired_combination: desiredCombination,
    };
    await transaction.put(deviceKey, updated);
    return {
      device_id: deviceId,
      serial_hint: updated.serial_hint,
      name: updated.name,
      desired_combination: updated.desired_combination,
    };
  });
}

async function deviceAssignment(request, storage) {
  const authenticated = await authenticatedDevice(request, storage);
  return {
    linked: Boolean(authenticated.device.fleet_id),
    name: authenticated.device.name,
    serial_hint: authenticated.device.serial_hint,
    desired_combination: authenticated.device.desired_combination,
  };
}

export function isDeviceApi(pathname) {
  return pathname === "/api/v1/device/pair" || pathname === "/api/v1/device/assignment";
}

export function isFleetApi(pathname) {
  return pathname === "/api/v1/fleet/claim" || pathname === "/api/v1/fleet/scales" ||
    /^\/api\/v1\/fleet\/scales\/[0-9a-f]{32}$/.test(pathname);
}

export async function handleFleetRequest(request, storage) {
  const url = new URL(request.url);
  if (url.pathname === "/device/pair" && request.method === "POST") return pairDevice(request, storage);
  if (url.pathname === "/device/assignment" && request.method === "GET") return deviceAssignment(request, storage);
  if (url.pathname === "/fleet/claim" && request.method === "POST") return claimDevice(request, storage);
  if (url.pathname === "/fleet/scales" && request.method === "GET") return listScales(request, storage);
  const match = url.pathname.match(/^\/fleet\/scales\/([0-9a-f]{32})$/);
  if (match && request.method === "PATCH") return updateScale(request, storage, match[1]);
  throw new FleetError(404, "not_found");
}
