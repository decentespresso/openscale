const hashPattern = /^[0-9a-f]{64}$/;
const commitPattern = /^[0-9a-f]{40}$/;
const versionPattern = /^[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9]+(?:[.-][a-z0-9]+)*)?$/;
const idPattern = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const pathPattern = /^[A-Za-z0-9][A-Za-z0-9._/-]*$/;
const attemptPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
const trustedRepository = "decentespresso/openscale";
const trustedRepositoryName = "openscale";
const archive = "HDS_FW_custom.zip";
const files = new Set([
  archive,
  "build-manifest.json",
  "dependencies.txt",
]);
const binaries = ["firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin"];
const buildStates = new Set(["building", "ready", "failed"]);
const failureCodes = new Set(["dispatch_failed", "build_failed", "build_timeout"]);
const maxEntryBytes = 4 * 1024 * 1024;
const maxApiBytes = 4096;
const dayMs = 86400000;
const weekMs = 7 * dayMs;
const utcWeekOffsetMs = 3 * dayMs;
const clientBuildsPerWeek = 21;
const globalBuildsPerWeek = 140;
const maxAttempts = 2;
const buildLeaseMs = 2 * 60 * 60 * 1000;

class ApiError extends Error {
  constructor(status, code) {
    super(code);
    this.status = status;
    this.code = code;
  }
}

function utcWeekWindow(now) {
  const week = Math.floor((now + utcWeekOffsetMs) / weekMs);
  return {week, expiresAt: (week + 1) * weekMs - utcWeekOffsetMs};
}

function route(pathname, prefix) {
  const parts = pathname.split("/");
  if (parts.length !== 4 || parts[1] !== prefix) return null;
  const [, , hash, filename] = parts;
  return hashPattern.test(hash) && files.has(filename) ? { hash, filename } : null;
}

function statusHash(pathname) {
  const parts = pathname.split("/");
  return parts.length === 5 && parts[1] === "api" && parts[2] === "v1" &&
    parts[3] === "status" && hashPattern.test(parts[4]) ? parts[4] : null;
}

function internalStatusHash(pathname) {
  const parts = pathname.split("/");
  return parts.length === 5 && parts[1] === "internal" && parts[2] === "v1" &&
    parts[3] === "status" && hashPattern.test(parts[4]) ? parts[4] : null;
}

function hex(bytes) {
  return Array.from(new Uint8Array(bytes), byte => byte.toString(16).padStart(2, "0")).join("");
}

function base64Url(bytes) {
  const binary = typeof bytes === "string" ? bytes : String.fromCharCode(...new Uint8Array(bytes));
  return btoa(binary).replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/, "");
}

function jsonScalar(value) {
  return JSON.stringify(value).replace(/[\u007f-\uffff]/g, character =>
    `\\u${character.charCodeAt(0).toString(16).padStart(4, "0")}`);
}

function canonicalJson(value) {
  if (Array.isArray(value)) return `[${value.map(canonicalJson).join(",")}]`;
  if (value && typeof value === "object") {
    return `{${Object.keys(value).sort().map(key => `${jsonScalar(key)}:${canonicalJson(value[key])}`).join(",")}}`;
  }
  return jsonScalar(value);
}

async function sha256(value) {
  return hex(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value)));
}

async function authorized(request, expected) {
  const header = request.headers.get("Authorization") || "";
  const provided = header.startsWith("Bearer ") ? header.slice(7) : "";
  if (!expected || !provided) return false;
  const [left, right] = await Promise.all([sha256(provided), sha256(expected)]);
  return left === right;
}

function cors(request, env) {
  const allowedOrigin = env.ALLOWED_ORIGIN || "https://decentespresso.github.io";
  return request.headers.get("Origin") === allowedOrigin
    ? {
        "Access-Control-Allow-Origin": allowedOrigin,
        "Access-Control-Allow-Headers": "Content-Type",
        "Access-Control-Allow-Methods": "GET, HEAD, POST, OPTIONS",
        "Access-Control-Max-Age": "86400",
        Vary: "Origin",
      }
    : {};
}

function json(request, env, value, status = 200, extraHeaders = {}) {
  return new Response(JSON.stringify(value), {
    status,
    headers: {
      ...cors(request, env),
      ...extraHeaders,
      "Content-Type": "application/json",
      "Cache-Control": "no-store",
    },
  });
}

async function readJson(request, maximum = maxApiBytes) {
  if (!request.headers.get("Content-Type")?.toLowerCase().startsWith("application/json")) {
    throw new ApiError(415, "json_required");
  }
  const declaredLength = Number(request.headers.get("Content-Length") || 0);
  if (declaredLength > maximum) throw new ApiError(413, "request_too_large");
  const payload = await request.arrayBuffer();
  if (!payload.byteLength || payload.byteLength > maximum) {
    throw new ApiError(413, "request_too_large");
  }
  try {
    return JSON.parse(new TextDecoder().decode(payload));
  } catch {
    throw new ApiError(400, "invalid_json");
  }
}

function requireSelection(value) {
  const keys = value && typeof value === "object" && !Array.isArray(value)
    ? Object.keys(value).sort().join(",") : "";
  if (!["features,firmware_ref,plugins", "catalog_revision,features,firmware_ref,plugins"].includes(keys)) {
    throw new ApiError(400, "invalid_selection");
  }
  if (typeof value.firmware_ref !== "string") throw new ApiError(400, "invalid_firmware_ref");
  if (value.catalog_revision !== undefined && !hashPattern.test(value.catalog_revision)) {
    throw new ApiError(400, "invalid_catalog_revision");
  }
  for (const field of ["features", "plugins"]) {
    const values = value[field];
    if (!Array.isArray(values) || values.some(item => typeof item !== "string" || !idPattern.test(item)) ||
        values.length !== new Set(values).size) {
      throw new ApiError(400, `invalid_${field}`);
    }
  }
  return value;
}

function repository(env) {
  const configured = env.GITHUB_REPOSITORY || trustedRepository;
  if (configured !== trustedRepository) throw new ApiError(503, "github_repository_misconfigured");
  return trustedRepository;
}

function githubHeaders(extra = {}) {
  return {
    Accept: "application/vnd.github+json",
    "User-Agent": "openscale-custom-builds",
    "X-GitHub-Api-Version": "2022-11-28",
    ...extra,
  };
}

async function resolveGithubCommit(env, ref, unavailableCode) {
  const response = await fetch(`https://api.github.com/repos/${repository(env)}/commits/${encodeURIComponent(ref)}`, {
    headers: githubHeaders(),
    cf: {cacheEverything: true, cacheTtl: 60},
  });
  if (!response.ok) throw new ApiError(503, unavailableCode);
  const commit = await response.json();
  if (!commitPattern.test(commit.sha || "")) throw new ApiError(503, "invalid_github_commit");
  return commit.sha;
}

function requireAllowedFirmwareRef(env, firmwareRef) {
  const allowed = new Set((env.ALLOWED_FIRMWARE_REFS || "").split(",").map(value => value.trim()).filter(Boolean));
  if (!allowed.has(firmwareRef)) throw new ApiError(400, "unsupported_firmware_ref");
}

async function serviceCatalog(env, commit) {
  const response = await fetch(`https://raw.githubusercontent.com/${repository(env)}/${commit}/docs/custom-build/service-catalog.json`, {
    cf: {cacheEverything: true, cacheTtl: 86400},
  });
  if (!response.ok) throw new ApiError(503, "service_catalog_unavailable");
  const catalog = await response.json();
  if (!Number.isSafeInteger(catalog.schema) || !Array.isArray(catalog.firmware_refs) ||
      !catalog.features || !catalog.plugins || !catalog.firmware ||
      typeof catalog.platformio_environment !== "string" ||
      (catalog.catalog_revision !== undefined && !hashPattern.test(catalog.catalog_revision))) {
    throw new ApiError(503, "invalid_service_catalog");
  }
  return catalog;
}

function featureDefinition(catalog, id) {
  const stored = catalog.features[id];
  const feature = Array.isArray(stored)
    ? {requires: stored, firmware_refs: catalog.firmware_refs}
    : stored;
  if (!feature || !Array.isArray(feature.requires) || !Array.isArray(feature.firmware_refs) ||
      feature.requires.some(required => !idPattern.test(required)) ||
      feature.firmware_refs.some(ref => typeof ref !== "string")) {
    throw new ApiError(503, "invalid_service_catalog");
  }
  return feature;
}

function resolveFeatures(catalog, firmwareRef, requested, plugins) {
  const resolved = new Set(requested);
  for (const id of requested) {
    if (!Object.hasOwn(catalog.features, id)) throw new ApiError(400, "unknown_feature");
  }
  for (const id of plugins) {
    const plugin = catalog.plugins[id];
    if (!plugin) throw new ApiError(400, "unknown_plugin");
    for (const feature of plugin.requires) resolved.add(feature);
  }
  let previousSize = -1;
  while (resolved.size !== previousSize) {
    previousSize = resolved.size;
    for (const id of resolved) {
      for (const dependency of featureDefinition(catalog, id).requires) resolved.add(dependency);
    }
  }
  for (const id of resolved) {
    if (!featureDefinition(catalog, id).firmware_refs.includes(firmwareRef)) {
      throw new ApiError(400, "unsupported_feature_ref");
    }
  }
  return [...resolved].sort();
}

function resolvePlugins(catalog, firmwareRef, selected) {
  const closure = new Set();
  const pending = [...selected];
  while (pending.length) {
    const id = pending.pop();
    if (closure.has(id)) continue;
    const plugin = catalog.plugins[id];
    if (!plugin) throw new ApiError(400, "unknown_plugin");
    const conflictsFeatures = plugin.conflicts_features || [];
    const recommendedPlugins = plugin.recommends?.plugins || [];
    if (!Array.isArray(plugin.firmware_refs) || !Array.isArray(plugin.depends_on) ||
        !Array.isArray(plugin.conflicts) || !Array.isArray(conflictsFeatures) ||
        !Array.isArray(recommendedPlugins) ||
        !Array.isArray(plugin.requires) || !Array.isArray(plugin.assets) ||
        !plugin.patches || typeof plugin.patches !== "object" || Array.isArray(plugin.patches) ||
        plugin.firmware_refs.some(ref => typeof ref !== "string") ||
        plugin.requires.some(feature => !idPattern.test(feature)) ||
        plugin.depends_on.some(dependency => !idPattern.test(dependency)) ||
        plugin.conflicts.some(conflict => !idPattern.test(conflict)) ||
        recommendedPlugins.some(recommended => !idPattern.test(recommended)) ||
        conflictsFeatures.some(feature => !idPattern.test(feature)) ||
        plugin.depends_on.length !== new Set(plugin.depends_on).size ||
        plugin.conflicts.length !== new Set(plugin.conflicts).size ||
        recommendedPlugins.length !== new Set(recommendedPlugins).size ||
        conflictsFeatures.length !== new Set(conflictsFeatures).size) {
      throw new ApiError(503, "invalid_service_catalog");
    }
    closure.add(id);
    pending.push(...plugin.depends_on);
  }
  const orderedIds = [];
  const visiting = new Set();
  const visited = new Set();
  const visit = id => {
    const plugin = catalog.plugins[id];
    if (visiting.has(id)) throw new ApiError(503, "invalid_service_catalog");
    if (visited.has(id)) return;
    if (!plugin.firmware_refs.includes(firmwareRef)) throw new ApiError(400, "unsupported_plugin_ref");
    visiting.add(id);
    [...plugin.depends_on].sort().forEach(visit);
    visiting.delete(id);
    visited.add(id);
    orderedIds.push(id);
  };
  [...closure].sort().forEach(visit);
  return orderedIds.map(id => {
    const plugin = catalog.plugins[id];
    const patch = plugin.patches[firmwareRef];
    if (plugin.assets.some(asset => !pathPattern.test(asset.target) || !hashPattern.test(asset.sha256))) {
      throw new ApiError(503, "invalid_service_catalog");
    }
    return {
      id,
      version: plugin.version,
      patches: patch ? [{sha256: patch.sha256}] : [],
      assets: [...plugin.assets].sort((left, right) =>
        (left.target < right.target ? -1 : left.target > right.target ? 1 : 0) ||
        (left.sha256 < right.sha256 ? -1 : left.sha256 > right.sha256 ? 1 : 0)),
    };
  });
}

function compatibilityPackages(catalog, pluginIds) {
  const selected = new Set(pluginIds);
  return pluginIds.map(ownerId => {
    const owner = catalog.plugins[ownerId];
    const pending = [ownerId, ...(owner.recommends?.plugins || []).filter(id => selected.has(id))];
    const compatible = new Set();
    while (pending.length) {
      const id = pending.pop();
      if (compatible.has(id)) continue;
      compatible.add(id);
      pending.push(...catalog.plugins[id].depends_on);
    }
    return compatible;
  });
}

function validateConflicts(catalog, pluginIds, featureIds) {
  const selectedPlugins = new Set(pluginIds);
  const selectedFeatures = new Set(featureIds);
  const compatible = compatibilityPackages(catalog, pluginIds);
  for (const id of pluginIds) {
    const plugin = catalog.plugins[id];
    if (plugin.conflicts.some(conflict => selectedPlugins.has(conflict) &&
        !compatible.some(group => group.has(id) && group.has(conflict)))) {
      throw new ApiError(400, "plugin_conflict");
    }
    if ((plugin.conflicts_features || []).some(feature => selectedFeatures.has(feature))) {
      throw new ApiError(400, "feature_conflict");
    }
  }
}

async function resolveSelection(env, selection) {
  const requested = requireSelection(selection);
  const builderRef = env.BUILDER_REF || "main";
  requireAllowedFirmwareRef(env, requested.firmware_ref);
  const builderCommit = await resolveGithubCommit(env, builderRef, "builder_ref_unavailable");
  const sourceCommit = requested.firmware_ref === builderRef
    ? builderCommit
    : await resolveGithubCommit(env, requested.firmware_ref, "firmware_ref_unavailable");
  const catalog = await serviceCatalog(env, builderCommit);
  if (requested.catalog_revision && requested.catalog_revision !== catalog.catalog_revision) {
    throw new ApiError(409, "catalog_stale");
  }
  if (!catalog.firmware_refs.includes(requested.firmware_ref)) {
    throw new ApiError(400, "unsupported_firmware_ref");
  }
  const firmware = catalog.firmware[requested.firmware_ref];
  if (!firmware || !versionPattern.test(firmware.custom_version || "") ||
      !pathPattern.test(firmware.partition_schema?.path || "") ||
      !hashPattern.test(firmware.partition_schema?.sha256 || "")) {
    throw new ApiError(503, "invalid_service_catalog");
  }
  const plugins = resolvePlugins(catalog, requested.firmware_ref, requested.plugins);
  const pluginIds = plugins.map(plugin => plugin.id);
  const features = resolveFeatures(
    catalog, requested.firmware_ref, requested.features, pluginIds,
  );
  validateConflicts(catalog, pluginIds, features);
  const platformioEnvironment = features.includes("energy-menu")
    ? "esp32s3-energy-menu-custom"
    : catalog.platformio_environment;
  const identity = {
    schema: catalog.schema,
    firmware_ref: requested.firmware_ref,
    firmware_version: firmware.custom_version,
    base_source: sourceCommit,
    builder_source: builderCommit,
    features,
    plugins,
    platformio_environment: platformioEnvironment,
    partition_schema: {
      path: firmware.partition_schema.path,
      sha256: firmware.partition_schema.sha256,
    },
  };
  return {
    combinationHash: await sha256(canonicalJson(identity)),
    builderCommit,
    builderRef,
    sourceCommit,
    configuration: {
      firmware_ref: requested.firmware_ref,
      features,
      plugins: plugins.map(plugin => plugin.id),
    },
  };
}

function coordinator(env) {
  if (!env.COORDINATOR) throw new ApiError(503, "coordinator_unavailable");
  return env.COORDINATOR.get(env.COORDINATOR.idFromName("global"));
}

async function coordinatorStatus(env, hash) {
  const response = await coordinator(env).fetch(`https://coordinator/status/${hash}`);
  return response.json();
}

function readyStatus(env, hash) {
  const baseUrl = env.PUBLIC_BASE_URL || "https://openscale-custom-builds.odevstudio.workers.dev";
  return {
    state: "ready",
    combination_hash: hash,
    manifest_url: `${baseUrl}/v1/${hash}/build-manifest.json`,
    downloads: {[archive]: `${baseUrl}/v1/${hash}/${archive}`},
  };
}

function buildStatus(record) {
  const {attempt_id, lease_expires_at, ...status} = record;
  return {
    ...status,
    max_attempts: maxAttempts,
    retryable: status.state === "failed" && (status.attempts || 0) < maxAttempts,
  };
}

async function publicStatus(env, hash) {
  if (await env.BUILDS.head(`v1/${hash}/build-manifest.json`)) return readyStatus(env, hash);
  const record = await coordinatorStatus(env, hash);
  return record.state === "ready" ? {state: "missing", combination_hash: hash} : buildStatus(record);
}

async function clientKey(request, env) {
  if (!env.RATE_LIMIT_SALT || env.RATE_LIMIT_SALT.length < 32) {
    throw new ApiError(503, "rate_limit_unavailable");
  }
  return sha256(`${env.RATE_LIMIT_SALT}:${request.headers.get("CF-Connecting-IP") || "unknown"}`);
}

async function createAppToken(env) {
  if (!env.GITHUB_APP_ID || !env.GITHUB_APP_INSTALLATION_ID || !env.GITHUB_APP_PRIVATE_KEY_PKCS8) {
    throw new ApiError(503, "github_app_unavailable");
  }
  const now = Math.floor(Date.now() / 1000);
  const header = base64Url(JSON.stringify({alg: "RS256", typ: "JWT"}));
  const payload = base64Url(JSON.stringify({iat: now - 60, exp: now + 540, iss: env.GITHUB_APP_ID}));
  const unsigned = `${header}.${payload}`;
  let keyBytes;
  try {
    keyBytes = Uint8Array.from(atob(env.GITHUB_APP_PRIVATE_KEY_PKCS8), character => character.charCodeAt(0));
  } catch {
    throw new ApiError(503, "github_app_unavailable");
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
  if (!response.ok) throw new ApiError(503, "github_app_unavailable");
  const result = await response.json();
  if (!result.token) throw new ApiError(503, "github_app_unavailable");
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
  if (response.status !== 204) throw new ApiError(503, "dispatch_failed");
}

async function enqueueBuild(request, env, build) {
  const response = await coordinator(env).fetch(`https://coordinator/build/${build.combinationHash}`, {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify({...build, clientKey: await clientKey(request, env)}),
  });
  const result = await response.json();
  if (!response.ok && response.status !== 409) {
    if (result.state === "failed") return {result: buildStatus(result), status: response.status};
    throw new ApiError(response.status, result.error || "build_request_failed");
  }
  return {result: buildStatus(result), status: response.status === 409 ? 409 : 202};
}

function metadata(manifest, filename) {
  if (filename === "dependencies.txt") return manifest.dependencies;
  if (filename === archive) return manifest.archive;
  return manifest.binaries?.[filename];
}

function validMetadata(value) {
  return Number.isSafeInteger(value?.bytes) && value.bytes > 0 && hashPattern.test(value?.sha256 || "");
}

async function validManifest(env, hash, payload) {
  let manifest;
  try {
    manifest = JSON.parse(new TextDecoder().decode(payload));
  } catch {
    return false;
  }
  if (manifest.combination_hash !== hash) return false;
  if (Object.keys(manifest.binaries || {}).sort().join(",") !== [...binaries].sort().join(",") ||
      binaries.some(filename => !validMetadata(manifest.binaries[filename]))) return false;
  let total = payload.byteLength;
  for (const filename of [archive, "dependencies.txt"]) {
    const expected = metadata(manifest, filename);
    const object = await env.BUILDS.head(`v1/${hash}/${filename}`);
    if (!validMetadata(expected) || !object) return false;
    if (object.size !== expected.bytes || object.customMetadata?.sha256 !== expected.sha256) return false;
    total += object.size;
  }
  return total <= maxEntryBytes;
}

async function upload(request, env, target) {
  if (!(await authorized(request, env.UPLOAD_TOKEN))) return new Response("Unauthorized", {status: 401});
  const readyKey = `v1/${target.hash}/build-manifest.json`;
  if (await env.BUILDS.head(readyKey)) return new Response("Immutable cache entry", {status: 409});
  const declaredLength = Number(request.headers.get("Content-Length") || 0);
  if (declaredLength > maxEntryBytes) return new Response("Payload too large", {status: 413});
  const payload = await request.arrayBuffer();
  if (!payload.byteLength || payload.byteLength > maxEntryBytes) return new Response("Payload too large", {status: 413});
  const expectedSha = request.headers.get("X-OpenScale-SHA256") || "";
  if (!hashPattern.test(expectedSha) || hex(await crypto.subtle.digest("SHA-256", payload)) !== expectedSha) {
    return new Response("Invalid digest", {status: 400});
  }
  if (target.filename === "build-manifest.json" && !(await validManifest(env, target.hash, payload))) {
    return new Response("Incomplete cache entry", {status: 400});
  }
  await env.BUILDS.put(`v1/${target.hash}/${target.filename}`, payload, {
    customMetadata: {sha256: expectedSha},
    httpMetadata: {contentType: target.filename.endsWith(".json") ? "application/json" :
      target.filename.endsWith(".zip") ? "application/zip" : "application/octet-stream"},
  });
  return new Response(null, {status: 201});
}

async function download(request, env, target) {
  const key = `v1/${target.hash}/${target.filename}`;
  if (!(await env.BUILDS.head(`v1/${target.hash}/build-manifest.json`))) return new Response("Not found", {status: 404});
  const object = request.method === "HEAD" ? await env.BUILDS.head(key) : await env.BUILDS.get(key);
  if (!object) return new Response("Not found", {status: 404});
  const headers = new Headers(cors(request, env));
  headers.set("Cache-Control", "public, max-age=86400, immutable");
  headers.set("Content-Length", String(object.size));
  headers.set("Content-Type", object.httpMetadata?.contentType || "application/octet-stream");
  if (object.httpEtag) headers.set("ETag", object.httpEtag);
  if (target.filename !== "build-manifest.json") headers.set("Content-Disposition", `attachment; filename="${target.filename}"`);
  return new Response(request.method === "HEAD" ? null : object.body, {status: 200, headers});
}

async function updateStatus(request, env, hash) {
  if (!(await authorized(request, env.UPLOAD_TOKEN))) throw new ApiError(401, "unauthorized");
  const update = await readJson(request, 1024);
  const keys = update && typeof update === "object" && !Array.isArray(update)
    ? Object.keys(update).sort().join(",") : "";
  const normalizedUpdate = update?.state === "failed" && keys === "attempt_id,state"
    ? {...update, failure_code: "build_failed"} : update;
  const normalizedKeys = normalizedUpdate && typeof normalizedUpdate === "object"
    ? Object.keys(normalizedUpdate).sort().join(",") : "";
  const expectedKeys = normalizedUpdate?.state === "failed"
    ? "attempt_id,failure_code,state" : "attempt_id,state";
  if (normalizedKeys !== expectedKeys || !buildStates.has(normalizedUpdate.state) ||
      !attemptPattern.test(normalizedUpdate.attempt_id) ||
      normalizedUpdate.state === "failed" && !failureCodes.has(normalizedUpdate.failure_code)) {
    throw new ApiError(400, "invalid_build_state");
  }
  if (normalizedUpdate.state === "ready" && !(await env.BUILDS.head(`v1/${hash}/build-manifest.json`))) {
    throw new ApiError(409, "cache_entry_missing");
  }
  const response = await coordinator(env).fetch(`https://coordinator/status/${hash}`, {
    method: "PUT",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(normalizedUpdate),
  });
  if (!response.ok) throw new ApiError(response.status, "status_update_failed");
  return new Response(null, {status: 204});
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

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    try {
      if (request.method === "OPTIONS") {
        const headers = cors(request, env);
        return Object.keys(headers).length ? new Response(null, {status: 204, headers}) : new Response(null, {status: 403});
      }
      const publicTarget = route(url.pathname, "v1");
      if (publicTarget && (request.method === "GET" || request.method === "HEAD")) return await download(request, env, publicTarget);
      const internalPath = url.pathname.startsWith("/internal/") ? url.pathname.slice(9) : "";
      const internalTarget = route(internalPath, "v1");
      if (internalTarget && request.method === "PUT") return await upload(request, env, internalTarget);
      const internalHash = internalStatusHash(url.pathname);
      if (internalHash && request.method === "PUT") return await updateStatus(request, env, internalHash);
      if (url.pathname.startsWith("/api/") && !Object.keys(cors(request, env)).length) {
        throw new ApiError(403, "origin_not_allowed");
      }
      const hash = statusHash(url.pathname);
      if (hash && request.method === "GET") return json(request, env, await publicStatus(env, hash));
      if (["/api/v1/status", "/api/v1/build"].includes(url.pathname) && request.method === "POST") {
        const build = await resolveSelection(env, await readJson(request));
        const result = await publicStatus(env, build.combinationHash);
        if (url.pathname.endsWith("/build") && result.state !== "ready") {
          const queued = await enqueueBuild(request, env, build);
          return json(request, env, queued.result, queued.status);
        }
        return json(request, env, result);
      }
      throw new ApiError(404, "not_found");
    } catch (error) {
      const failure = error instanceof ApiError ? error : new ApiError(500, "internal_error");
      return json(request, env, {error: failure.code}, failure.status);
    }
  },
};
