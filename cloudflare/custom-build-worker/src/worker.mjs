import {isDeviceApi, isFleetApi} from "./fleet.mjs";
import {BuildCoordinator, maxAttempts} from "./build-coordinator.mjs";

export {BuildCoordinator};

const hashPattern = /^[0-9a-f]{64}$/;
const commitPattern = /^[0-9a-f]{40}$/;
const versionPattern = /^[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9]+(?:[.-][a-z0-9]+)*)?$/;
const idPattern = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const pathPattern = /^[A-Za-z0-9][A-Za-z0-9._/-]*$/;
const attemptPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
const trustedRepository = "decentespresso/openscale";
const archive = "HDS_FW_custom.zip";
const files = new Set([
  archive,
  "build-manifest.json",
  "dependencies.txt",
  "firmware.bin",
  "littlefs.bin",
  "ota-manifest.json",
  "ota-manifest.sig",
]);
const binaries = ["firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin"];
const buildStates = new Set(["building", "ready", "failed"]);
const failureCodes = new Set(["dispatch_failed", "build_failed", "build_timeout"]);
const maxEntryBytes = 12 * 1024 * 1024;
const maxApiBytes = 4096;

class ApiError extends Error {
  constructor(status, code) {
    super(code);
    this.status = status;
    this.code = code;
  }
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
        "Access-Control-Allow-Headers": "Authorization, Content-Type",
        "Access-Control-Allow-Methods": "DELETE, GET, HEAD, PATCH, POST, OPTIONS",
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
      !Number.isSafeInteger(catalog.custom_ota_signing_key_generation) ||
      catalog.custom_ota_signing_key_generation < 1 ||
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
    custom_ota_signing_key_generation: catalog.custom_ota_signing_key_generation,
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
  if (filename === "ota-manifest.json") return manifest.custom_ota?.manifest;
  if (filename === "ota-manifest.sig") return manifest.custom_ota?.signature;
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
  for (const filename of [
    archive,
    "dependencies.txt",
    "firmware.bin",
    "littlefs.bin",
    "ota-manifest.json",
    "ota-manifest.sig",
  ]) {
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

async function fleetCoordinatorResponse(request, env, client = null) {
  const url = new URL(request.url);
  const headers = new Headers(request.headers);
  if (client) headers.set("X-OpenScale-Client-Key", client);
  const body = ["GET", "HEAD"].includes(request.method) ? undefined : await request.arrayBuffer();
  return coordinator(env).fetch(`https://coordinator${url.pathname.slice(7)}`, {
    method: request.method,
    headers,
    body,
  });
}

async function fleetApi(request, env) {
  const url = new URL(request.url);
  const response = await fleetCoordinatorResponse(
    request,
    env,
    ["/api/v1/device/pair", "/api/v1/fleet/claim"].includes(url.pathname)
      ? await clientKey(request, env) : null,
  );
  const result = await response.json();
  if (!response.ok) {
    return json(request, env, result, response.status, response.headers.get("Retry-After")
      ? {"Retry-After": response.headers.get("Retry-After")} : {});
  }
  if (!["/api/v1/device/assignment", "/api/v1/device/check-in"].includes(url.pathname) ||
      !result.desired_combination) {
    return json(request, env, result);
  }
  const status = await publicStatus(env, result.desired_combination);
  return json(request, env, {
    ...result,
    state: status.state,
  });
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
      if (isDeviceApi(url.pathname)) return await fleetApi(request, env);
      if (url.pathname.startsWith("/api/") && !Object.keys(cors(request, env)).length) {
        throw new ApiError(403, "origin_not_allowed");
      }
      if (isFleetApi(url.pathname)) return await fleetApi(request, env);
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
