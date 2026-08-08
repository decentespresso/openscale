const hashPattern = /^[0-9a-f]{64}$/;
const shaPattern = /^[0-9a-f]{64}$/;
const files = new Set([
  "firmware.bin",
  "bootloader.bin",
  "partitions.bin",
  "littlefs.bin",
  "build-manifest.json",
  "dependencies.txt",
]);
const binaries = ["firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin"];
const maxEntryBytes = 5 * 1024 * 1024;

function route(pathname, prefix) {
  const parts = pathname.split("/");
  if (parts.length !== 4 || parts[1] !== prefix) return null;
  const [, , hash, filename] = parts;
  return hashPattern.test(hash) && files.has(filename) ? { hash, filename } : null;
}

function hex(bytes) {
  return Array.from(new Uint8Array(bytes), byte => byte.toString(16).padStart(2, "0")).join("");
}

async function authorized(request, expected) {
  const header = request.headers.get("Authorization") || "";
  const provided = header.startsWith("Bearer ") ? header.slice(7) : "";
  if (!expected || !provided) return false;
  const encoder = new TextEncoder();
  const [left, right] = await Promise.all([
    crypto.subtle.digest("SHA-256", encoder.encode(provided)),
    crypto.subtle.digest("SHA-256", encoder.encode(expected)),
  ]);
  return hex(left) === hex(right);
}

function metadata(manifest, filename) {
  return filename === "dependencies.txt" ? manifest.dependencies : manifest.binaries?.[filename];
}

function validMetadata(value) {
  return Number.isSafeInteger(value?.bytes) && value.bytes > 0 && shaPattern.test(value?.sha256 || "");
}

async function validManifest(env, hash, payload) {
  let manifest;
  try {
    manifest = JSON.parse(new TextDecoder().decode(payload));
  } catch {
    return false;
  }
  if (manifest.combination_hash !== hash) return false;
  let total = payload.byteLength;
  for (const filename of [...binaries, "dependencies.txt"]) {
    const expected = metadata(manifest, filename);
    const object = await env.BUILDS.head(`v1/${hash}/${filename}`);
    if (!validMetadata(expected) || !object) return false;
    if (object.size !== expected.bytes || object.customMetadata?.sha256 !== expected.sha256) return false;
    total += object.size;
  }
  return total <= maxEntryBytes;
}

async function upload(request, env, target) {
  if (!(await authorized(request, env.UPLOAD_TOKEN))) return new Response("Unauthorized", { status: 401 });
  const readyKey = `v1/${target.hash}/build-manifest.json`;
  if (await env.BUILDS.head(readyKey)) return new Response("Immutable cache entry", { status: 409 });
  const declaredLength = Number(request.headers.get("Content-Length") || 0);
  if (declaredLength > maxEntryBytes) return new Response("Payload too large", { status: 413 });
  const payload = await request.arrayBuffer();
  if (!payload.byteLength || payload.byteLength > maxEntryBytes) {
    return new Response("Payload too large", { status: 413 });
  }
  const expectedSha = request.headers.get("X-OpenScale-SHA256") || "";
  if (!shaPattern.test(expectedSha) || hex(await crypto.subtle.digest("SHA-256", payload)) !== expectedSha) {
    return new Response("Invalid digest", { status: 400 });
  }
  if (target.filename === "build-manifest.json" && !(await validManifest(env, target.hash, payload))) {
    return new Response("Incomplete cache entry", { status: 400 });
  }
  await env.BUILDS.put(`v1/${target.hash}/${target.filename}`, payload, {
    customMetadata: { sha256: expectedSha },
    httpMetadata: {
      contentType: target.filename.endsWith(".json") ? "application/json" : "application/octet-stream",
    },
  });
  return new Response(null, { status: 201 });
}

function cors(request, env) {
  const allowedOrigin = env.ALLOWED_ORIGIN || "https://decentespresso.github.io";
  return request.headers.get("Origin") === allowedOrigin
    ? {
        "Access-Control-Allow-Origin": allowedOrigin,
        "Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
        Vary: "Origin",
      }
    : {};
}

async function download(request, env, target) {
  const key = `v1/${target.hash}/${target.filename}`;
  if (!(await env.BUILDS.head(`v1/${target.hash}/build-manifest.json`))) {
    return new Response("Not found", { status: 404 });
  }
  const object = request.method === "HEAD" ? await env.BUILDS.head(key) : await env.BUILDS.get(key);
  if (!object) return new Response("Not found", { status: 404 });
  const headers = new Headers(cors(request, env));
  headers.set("Cache-Control", "public, max-age=86400, immutable");
  headers.set("Content-Length", String(object.size));
  headers.set("Content-Type", object.httpMetadata?.contentType || "application/octet-stream");
  if (object.httpEtag) headers.set("ETag", object.httpEtag);
  if (target.filename !== "build-manifest.json") {
    headers.set("Content-Disposition", `attachment; filename="${target.filename}"`);
  }
  return new Response(request.method === "HEAD" ? null : object.body, { status: 200, headers });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "OPTIONS") {
      const headers = cors(request, env);
      return Object.keys(headers).length ? new Response(null, { status: 204, headers }) : new Response(null, { status: 403 });
    }
    const publicTarget = route(url.pathname, "v1");
    if (publicTarget && (request.method === "GET" || request.method === "HEAD")) {
      return download(request, env, publicTarget);
    }
    const internalPath = url.pathname.startsWith("/internal/") ? url.pathname.slice(9) : "";
    const internalTarget = route(internalPath, "v1");
    if (internalTarget && request.method === "PUT") return upload(request, env, internalTarget);
    return new Response("Not found", { status: 404 });
  },
};
