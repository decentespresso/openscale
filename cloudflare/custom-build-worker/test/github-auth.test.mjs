import assert from "node:assert/strict";
import {Buffer} from "node:buffer";
import {readFile} from "node:fs/promises";
import {test} from "node:test";

import worker from "../src/worker.mjs";
import {BuildCoordinator, createAppToken, invalidateAppToken} from "../src/build-coordinator.mjs";

const origin = "https://decentespresso.github.io";
const catalog = JSON.parse(await readFile(new URL("../../../docs/custom-build/service-catalog.json", import.meta.url), "utf8"));
const keys = await Promise.all([0, 1].map(async () => {
  const pair = await crypto.subtle.generateKey({
    name: "RSASSA-PKCS1-v1_5", modulusLength: 2048,
    publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256",
  }, true, ["sign", "verify"]);
  return Buffer.from(await crypto.subtle.exportKey("pkcs8", pair.privateKey)).toString("base64");
}));

function environment(appId) {
  return {
    GITHUB_APP_ID: appId,
    GITHUB_APP_INSTALLATION_ID: "456",
    GITHUB_APP_PRIVATE_KEY_PKCS8: keys[0],
    GITHUB_REPOSITORY: "decentespresso/openscale",
    ALLOWED_FIRMWARE_REFS: "main,v3.1.16",
    BUILDS: {head: async () => null},
    COORDINATOR: {
      idFromName: name => name,
      get: () => ({fetch: async url => Response.json({state: "missing", combination_hash: url.split("/").at(-1)})}),
    },
  };
}

function tokenResponse(token = "installation-token", expiresAt = Date.now() + 3600000) {
  return Response.json({token, expires_at: new Date(expiresAt).toISOString()});
}

function assertTokenRequest(url, options) {
  assert.equal(String(url), "https://api.github.com/app/installations/456/access_tokens");
  assert.equal(options.method, "POST");
  assert.equal(options.redirect, "manual");
  assert.deepEqual(JSON.parse(options.body), {repositories: ["openscale"], permissions: {actions: "write"}});
}

function status(env, ref = "v3.1.16") {
  return worker.fetch(new Request("https://example.test/api/v1/status", {
    method: "POST",
    headers: {Origin: origin, "Content-Type": "application/json", Authorization: "Bearer untrusted-client-token"},
    body: JSON.stringify({firmware_ref: ref, features: [], plugins: []}),
  }), env);
}

test("installation token issuance is shared concurrently and cached before expiry", async t => {
  const env = environment("101");
  let issued;
  let finish;
  let calls = 0;
  const started = new Promise(resolve => { issued = resolve; });
  t.mock.method(globalThis, "fetch", (url, options) => {
    calls += 1;
    assertTokenRequest(url, options);
    issued();
    return new Promise(resolve => { finish = () => resolve(tokenResponse()); });
  });
  const pending = Promise.all([createAppToken(env), createAppToken({...env}), createAppToken(env)]);
  await started;
  assert.equal(calls, 1);
  finish();
  assert.deepEqual(await pending, ["installation-token", "installation-token", "installation-token"]);
  assert.equal(await createAppToken(env), "installation-token");
  assert.equal(calls, 1);
});

test("cached installation tokens refresh within the safety margin and never outlive one hour", async t => {
  const env = environment("102");
  let now = Date.now();
  let calls = 0;
  t.mock.method(Date, "now", () => now);
  t.mock.method(globalThis, "fetch", async () => tokenResponse(`token-${++calls}`, now + 86400000));
  assert.equal(await createAppToken(env), "token-1");
  now += 3540000 - 1;
  assert.equal(await createAppToken(env), "token-1");
  now += 2;
  assert.equal(await createAppToken(env), "token-2");
  assert.equal(calls, 2);
});

test("credential rotation cannot reuse another app, installation, or key's token", async t => {
  const env = environment("103");
  let calls = 0;
  t.mock.method(globalThis, "fetch", async () => tokenResponse(`token-${++calls}`));
  assert.equal(await createAppToken(env), "token-1");
  assert.equal(await createAppToken({...env, GITHUB_APP_ID: "104"}), "token-2");
  assert.equal(await createAppToken({...env, GITHUB_APP_INSTALLATION_ID: "457"}), "token-3");
  assert.equal(await createAppToken({...env, GITHUB_APP_PRIVATE_KEY_PKCS8: keys[1]}), "token-4");
  assert.equal(calls, 4);
});

test("failed or invalid token issuance is not cached", async t => {
  let calls = 0;
  let fail = true;
  let failure;
  t.mock.method(globalThis, "fetch", async () => {
    calls += 1;
    return fail ? failure() : tokenResponse();
  });
  for (const [index, response] of [
    () => Response.json({message: "denied"}, {status: 403}),
    () => Response.json({token: "token", expires_at: "invalid"}),
    () => tokenResponse("token", Date.now() + 30000),
    () => Response.json({expires_at: new Date(Date.now() + 3600000).toISOString()}),
  ].entries()) {
    const env = environment(String(110 + index));
    failure = response;
    fail = true;
    await assert.rejects(createAppToken(env), /github_app_unavailable/);
    fail = false;
    assert.equal(await createAppToken(env), "installation-token");
  }
  assert.equal(calls, 8);
});

test("token invalidation affects only the matching current token", async t => {
  const env = environment("120");
  let calls = 0;
  t.mock.method(globalThis, "fetch", async () => tokenResponse(`token-${++calls}`));
  assert.equal(await createAppToken(env), "token-1");
  invalidateAppToken("unrelated-token");
  assert.equal(await createAppToken(env), "token-1");
  invalidateAppToken("token-1");
  assert.equal(await createAppToken(env), "token-2");
  invalidateAppToken("token-1");
  assert.equal(await createAppToken(env), "token-2");
  assert.equal(calls, 2);
});

test("status resolves refs with scoped authentication and does not cache upstream errors", async t => {
  const env = environment("121");
  const requests = [];
  const warnings = [];
  let rejectTag = true;
  t.mock.method(console, "warn", message => warnings.push(JSON.parse(message)));
  t.mock.method(globalThis, "fetch", async (url, options = {}) => {
    requests.push({url: String(url), options});
    if (String(url).endsWith("/access_tokens")) {
      assertTokenRequest(url, options);
      return tokenResponse();
    }
    if (String(url).includes("/commits/")) {
      assert.equal(new URL(url).origin, "https://api.github.com");
      assert.equal(options.headers.Authorization, "Bearer installation-token");
      assert.equal(options.redirect, "manual");
      assert.deepEqual(options.cf, {cacheEverything: true, cacheTtlByStatus: {"200-299": 60, "300-599": -1}});
      if (String(url).endsWith("/v3.1.16") && rejectTag) return Response.json({message: "sensitive-upstream-body"}, {
        status: 403,
        headers: {"X-RateLimit-Remaining": "0", "X-RateLimit-Reset": "1794400000", "Retry-After": "60"},
      });
      return Response.json({sha: "1".repeat(40)});
    }
    assert.equal(options.headers, undefined);
    assert.match(String(url), /^https:\/\/raw\.githubusercontent\.com\/decentespresso\/openscale\//);
    return Response.json(catalog);
  });
  for (let attempt = 0; attempt < 2; attempt += 1) {
    const response = await status(env);
    assert.equal(response.status, 503);
    assert.deepEqual(await response.json(), {error: "firmware_ref_unavailable"});
  }
  assert.equal(requests.filter(request => request.url.endsWith("/access_tokens")).length, 1);
  assert.equal(requests.filter(request => request.url.endsWith("/commits/v3.1.16")).length, 2);
  assert.equal(warnings.length, 2);
  assert.deepEqual(warnings[0], {
    event: "github-ref-unavailable", ref: "v3.1.16", status: 403,
    rateLimitRemaining: "0", rateLimitReset: "1794400000", retryAfter: "60",
  });
  for (const sensitive of ["installation-token", "untrusted-client-token", keys[0], "sensitive-upstream-body"]) {
    assert.equal(JSON.stringify(warnings).includes(sensitive), false);
  }
  rejectTag = false;
  const recovered = await status(env);
  assert.equal(recovered.status, 200);
  const result = await recovered.json();
  assert.equal(result.state, "missing");
  assert.equal(JSON.stringify(result).includes("installation-token"), false);
});

test("a GitHub 401 invalidates the token for a later status check without replaying it", async t => {
  const env = environment("122");
  let tokens = 0;
  let commits = 0;
  t.mock.method(console, "warn", () => {});
  t.mock.method(globalThis, "fetch", async (url, options) => {
    if (String(url).endsWith("/access_tokens")) return tokenResponse(`token-${++tokens}`);
    if (String(url).includes("/commits/")) {
      commits += 1;
      assert.equal(options.headers.Authorization, `Bearer token-${tokens}`);
      return commits === 1 ? Response.json({}, {status: 401}) : Response.json({sha: "1".repeat(40)});
    }
    return Response.json(catalog);
  });
  assert.equal((await status(env, "main")).status, 503);
  assert.equal(tokens, 1);
  assert.equal(commits, 1);
  assert.equal((await status(env, "main")).status, 200);
  assert.equal(tokens, 2);
  assert.equal(commits, 2);
});

test("repository and ref guards reject requests before issuing credentials", async t => {
  const fetch = t.mock.method(globalThis, "fetch", async () => { throw new Error("unexpected fetch"); });
  const invalidRepository = await status({...environment("123"), GITHUB_REPOSITORY: "another/repository"});
  assert.equal(invalidRepository.status, 503);
  assert.deepEqual(await invalidRepository.json(), {error: "github_repository_misconfigured"});
  assert.equal((await status(environment("124"), "unsupported")).status, 400);
  const missingKey = await status({...environment("125"), GITHUB_APP_PRIVATE_KEY_PKCS8: ""});
  assert.equal(missingKey.status, 503);
  assert.deepEqual(await missingKey.json(), {error: "github_app_unavailable"});
  assert.equal(fetch.mock.callCount(), 0);
});

test("dispatch rejection invalidates a revoked token without repeating the dispatch", async t => {
  const env = environment("126");
  const values = new Map();
  const storage = {
    get: async key => values.get(key),
    put: async (key, value) => {
      if (typeof key === "object") Object.entries(key).forEach(([name, entry]) => values.set(name, entry));
      else values.set(key, value);
    },
    list: async ({prefix}) => new Map([...values].filter(([key]) => key.startsWith(prefix))),
    setAlarm: async () => {},
    deleteAlarm: async () => {},
    transaction: action => action(storage),
  };
  let tokens = 0;
  let dispatches = 0;
  t.mock.method(globalThis, "fetch", async (url, options) => {
    if (String(url).endsWith("/access_tokens")) return tokenResponse(`token-${++tokens}`);
    assert.equal(String(url), "https://api.github.com/repos/decentespresso/openscale/actions/workflows/custom-build.yml/dispatches");
    assert.equal(options.redirect, "manual");
    dispatches += 1;
    return Response.json({}, {status: 401});
  });
  const coordinator = new BuildCoordinator({storage}, env);
  const response = await coordinator.fetch(new Request(`https://coordinator/build/${"a".repeat(64)}`, {
    method: "POST",
    body: JSON.stringify({
      configuration: {firmware_ref: "main", features: [], plugins: []},
      builderRef: "main", builderCommit: "1".repeat(40), sourceCommit: "1".repeat(40),
      combinationHash: "a".repeat(64), clientKey: "client",
    }),
  }));
  assert.equal(response.status, 503);
  assert.equal((await response.json()).failure_code, "dispatch_failed");
  assert.equal(dispatches, 1);
  assert.equal(await createAppToken(env), "token-2");
  assert.equal(tokens, 2);
  assert.equal(dispatches, 1);
});
