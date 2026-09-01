import {FleetError, handleFleetRequest} from "./fleet.mjs";

const hashPattern = /^[0-9a-f]{64}$/;
const trustedRepository = "decentespresso/openscale";
const trustedRepositoryName = "openscale";
const dayMs = 86400000;
const weekMs = 7 * dayMs;
const utcWeekOffsetMs = 3 * dayMs;
const clientBuildsPerWeek = 21;
const globalBuildsPerWeek = 140;
export const maxAttempts = 2;
const buildLeaseMs = 2 * 60 * 60 * 1000;

function utcWeekWindow(now) {
  const week = Math.floor((now + utcWeekOffsetMs) / weekMs);
  return {week, expiresAt: (week + 1) * weekMs - utcWeekOffsetMs};
}

function base64Url(bytes) {
  const binary = typeof bytes === "string" ? bytes : String.fromCharCode(...new Uint8Array(bytes));
  return btoa(binary).replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/, "");
}

function githubHeaders(extra = {}) {
  return {
    Accept: "application/vnd.github+json",
    "User-Agent": "openscale-custom-builds",
    "X-GitHub-Api-Version": "2022-11-28",
    ...extra,
  };
}

function repository(env) {
  const configured = env.GITHUB_REPOSITORY || trustedRepository;
  if (configured !== trustedRepository) throw new Error("github_repository_misconfigured");
  return trustedRepository;
}

async function createAppToken(env) {
  if (!env.GITHUB_APP_ID || !env.GITHUB_APP_INSTALLATION_ID || !env.GITHUB_APP_PRIVATE_KEY_PKCS8) {
    throw new Error("github_app_unavailable");
  }
  const now = Math.floor(Date.now() / 1000);
  const header = base64Url(JSON.stringify({alg: "RS256", typ: "JWT"}));
  const payload = base64Url(JSON.stringify({iat: now - 60, exp: now + 540, iss: env.GITHUB_APP_ID}));
  const unsigned = `${header}.${payload}`;
  let keyBytes;
  try {
    keyBytes = Uint8Array.from(atob(env.GITHUB_APP_PRIVATE_KEY_PKCS8), character => character.charCodeAt(0));
  } catch {
    throw new Error("github_app_unavailable");
  }
  const key = await crypto.subtle.importKey(
    "pkcs8",
    keyBytes,
    {name: "RSASSA-PKCS1-v1_5", hash: "SHA-256"},
    false,
    ["sign"],
  );
  const signature = await crypto.subtle.sign("RSASSA-PKCS1-v1_5", key, new TextEncoder().encode(unsigned));
  const response = await fetch(`https://api.github.com/app/installations/${env.GITHUB_APP_INSTALLATION_ID}/access_tokens`, {
    method: "POST",
    headers: githubHeaders({
      Authorization: `Bearer ${unsigned}.${base64Url(signature)}`,
      "Content-Type": "application/json",
    }),
    body: JSON.stringify({repositories: [trustedRepositoryName], permissions: {actions: "write"}}),
  });
  if (!response.ok) throw new Error("github_app_unavailable");
  const result = await response.json();
  if (!result.token) throw new Error("github_app_unavailable");
  return result.token;
}

async function dispatchBuild(env, build) {
  const workflow = env.GITHUB_WORKFLOW || "custom-build.yml";
  const token = await createAppToken(env);
  const response = await fetch(`https://api.github.com/repos/${repository(env)}/actions/workflows/${workflow}/dispatches`, {
    method: "POST",
    headers: githubHeaders({Authorization: `Bearer ${token}`, "Content-Type": "application/json"}),
    body: JSON.stringify({
      ref: build.builderRef,
      inputs: {
        firmware_ref: build.configuration.firmware_ref,
        features: build.configuration.features.join(",") || ",",
        plugins: build.configuration.plugins.join(",") || ",",
        builder_ref: build.builderRef,
        builder_commit: build.builderCommit,
        source_commit: build.sourceCommit,
        combination_hash: build.combinationHash,
        attempt_id: build.attemptId,
      },
    }),
  });
  if (response.status !== 204) throw new Error("dispatch_failed");
}

export class BuildCoordinator {
  constructor(state, env) {
    this.state = state;
    this.env = env;
  }

  async scheduleAlarm() {
    const [pending, rates] = await Promise.all([
      this.state.storage.get("pending"),
      this.state.storage.get("rates"),
    ]);
    const expirations = [
      ...Object.values(pending || {}).map(item => item.lease_expires_at),
      ...(rates?.expires_at ? [rates.expires_at] : []),
    ].filter(value => Number.isSafeInteger(value));
    if (expirations.length) await this.state.storage.setAlarm(Math.min(...expirations));
  }

  async updateAttempt(hash, update) {
    const now = Date.now();
    const result = await this.state.storage.transaction(async transaction => {
      const key = `build:${hash}`;
      const [current, storedPending] = await Promise.all([
        transaction.get(key),
        transaction.get("pending"),
      ]);
      if (!current || current.attempt_id !== update.attempt_id ||
          !["queued", "building"].includes(current.state)) return null;
      const remainingPending = Object.fromEntries(
        Object.entries(storedPending || {}).filter(([pendingHash]) => pendingHash !== hash),
      );
      const {lease_expires_at, ...currentWithoutLease} = current;
      const record = {
        ...currentWithoutLease,
        state: update.state,
        updated_at: new Date(now).toISOString(),
        ...(update.state === "building" ? {lease_expires_at: now + buildLeaseMs} : {}),
        ...(update.state === "failed" ? {failure_code: update.failure_code} : {}),
      };
      const pending = update.state === "building" ? {
        ...remainingPending,
        [hash]: {attempt_id: update.attempt_id, lease_expires_at: record.lease_expires_at},
      } : remainingPending;
      await transaction.put({[key]: record, pending});
      return record;
    });
    if (result) await this.scheduleAlarm();
    return result;
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname.startsWith("/device/") || url.pathname.startsWith("/fleet/")) {
      try {
        return Response.json(await handleFleetRequest(request, this.state.storage));
      } catch (error) {
        const failure = error instanceof FleetError ? error : new FleetError(500, "internal_error");
        return Response.json({error: failure.code}, {status: failure.status});
      }
    }
    const parts = url.pathname.split("/");
    const hash = parts.length === 3 && hashPattern.test(parts[2]) ? parts[2] : null;
    if (!hash) return Response.json({error: "not_found"}, {status: 404});
    if (parts[1] === "status" && request.method === "GET") {
      const key = `build:${hash}`;
      let record = await this.state.storage.get(key);
      if (["queued", "building"].includes(record?.state) && record.lease_expires_at <= Date.now()) {
        await this.alarm();
        record = await this.state.storage.get(key);
      }
      return Response.json(record || {state: "missing", combination_hash: hash});
    }
    if (parts[1] === "status" && request.method === "PUT") {
      const update = await request.json();
      const record = await this.updateAttempt(hash, update);
      return record ? Response.json(record) : Response.json({error: "stale_build_attempt"}, {status: 409});
    }
    if (parts[1] !== "build" || request.method !== "POST") {
      return Response.json({error: "not_found"}, {status: 404});
    }
    const build = await request.json();
    const now = Date.now();
    const rateWindow = utcWeekWindow(now);
    const reservation = await this.state.storage.transaction(async transaction => {
      const recordKey = `build:${hash}`;
      const [current, storedPending] = await Promise.all([
        transaction.get(recordKey),
        transaction.get("pending"),
      ]);
      const currentIsStale = ["queued", "building"].includes(current?.state) &&
        current.lease_expires_at <= now;
      const {lease_expires_at, ...currentWithoutLease} = current || {};
      const effectiveCurrent = currentIsStale ? {
        ...currentWithoutLease,
        state: "failed",
        failure_code: "build_timeout",
        updated_at: new Date(now).toISOString(),
      } : current;
      if (currentIsStale) {
        await transaction.put({
          [recordKey]: effectiveCurrent,
          pending: Object.fromEntries(
            Object.entries(storedPending || {}).filter(([pendingHash]) => pendingHash !== hash),
          ),
        });
      }
      if (effectiveCurrent && ["queued", "building"].includes(effectiveCurrent.state)) {
        return {record: effectiveCurrent};
      }
      if (effectiveCurrent?.state === "failed" && effectiveCurrent.attempts >= maxAttempts) {
        return {record: effectiveCurrent, terminal: true};
      }
      const storedRates = await transaction.get("rates");
      const rates = storedRates?.week === rateWindow.week ? storedRates : {
        week: rateWindow.week, global: 0, clients: {}, expires_at: rateWindow.expiresAt,
      };
      const clientCount = rates.clients[build.clientKey] || 0;
      if (clientCount >= clientBuildsPerWeek || rates.global >= globalBuildsPerWeek) {
        return {
          rateLimited: true,
          retryAfter: Math.max(1, Math.ceil((rates.expires_at - now) / 1000)),
        };
      }
      const attemptId = crypto.randomUUID();
      const leaseExpiresAt = now + buildLeaseMs;
      const record = {
        state: "queued",
        combination_hash: hash,
        attempts: (effectiveCurrent?.state === "failed" ? effectiveCurrent.attempts : 0) + 1,
        attempt_id: attemptId,
        lease_expires_at: leaseExpiresAt,
        updated_at: new Date(now).toISOString(),
      };
      const pending = {
        ...(storedPending || {}),
        [hash]: {attempt_id: attemptId, lease_expires_at: leaseExpiresAt},
      };
      await transaction.put({
        [recordKey]: record,
        pending,
        rates: {
          week: rateWindow.week,
          global: rates.global + 1,
          clients: {...rates.clients, [build.clientKey]: clientCount + 1},
          expires_at: rates.expires_at,
        },
      });
      return {record, dispatch: true};
    });
    if (reservation.rateLimited) {
      return Response.json(
        {error: "rate_limited"},
        {status: 429, headers: {"Retry-After": String(reservation.retryAfter)}},
      );
    }
    if (reservation.terminal) return Response.json(reservation.record, {status: 409});
    if (!reservation.dispatch) return Response.json(reservation.record);
    await this.scheduleAlarm();
    try {
      await dispatchBuild(this.env, {...build, attemptId: reservation.record.attempt_id});
      return Response.json(reservation.record, {status: 202});
    } catch {
      const failed = await this.updateAttempt(hash, {
        state: "failed",
        failure_code: "dispatch_failed",
        attempt_id: reservation.record.attempt_id,
      });
      return Response.json(failed || {error: "dispatch_failed"}, {status: 503});
    }
  }

  async alarm() {
    const now = Date.now();
    const pending = await this.state.storage.get("pending") || {};
    const pendingEntries = Object.entries(pending);
    const remaining = Object.fromEntries(
      pendingEntries.filter(([, attempt]) => attempt.lease_expires_at > now),
    );
    for (const [hash, attempt] of pendingEntries.filter(([, item]) => item.lease_expires_at <= now)) {
      const key = `build:${hash}`;
      const current = await this.state.storage.get(key);
      if (current?.attempt_id === attempt.attempt_id && ["queued", "building"].includes(current.state)) {
        const {lease_expires_at, ...record} = current;
        await this.state.storage.put(key, {
          ...record,
          state: "failed",
          failure_code: "build_timeout",
          updated_at: new Date(now).toISOString(),
        });
      }
    }
    await this.state.storage.put("pending", remaining);
    const rates = await this.state.storage.get("rates");
    if (rates?.expires_at <= now) await this.state.storage.delete("rates");
    await this.scheduleAlarm();
  }
}
