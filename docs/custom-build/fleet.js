import {buildSummary, deploymentState, lastSeenLabel, shortHash} from "./fleet-state.mjs?v=1";

const storageKey = "hds-custom-build-fleet-v2";
const legacyStorageKey = "hds-custom-build-fleet-v1";
const fleetPattern = /^[A-Z2-7]{32}$/;
const pairPattern = /^[0-9A-F]{6}-[0-9]{6}$/;

const normalizedFleetKey = value => String(value || "").replaceAll("-", "").trim().toUpperCase();
const formattedFleetKey = value => normalizedFleetKey(value).match(/.{1,4}/g)?.join("-") || "";

const encodeBase32 = bytes => {
  const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  let bits = 0;
  let value = 0;
  let result = "";
  bytes.forEach(byte => {
    value = (value << 8) | byte;
    bits += 8;
    while (bits >= 5) {
      result += alphabet[(value >>> (bits - 5)) & 31];
      bits -= 5;
    }
  });
  if (bits) result += alphabet[(value << (5 - bits)) & 31];
  return result;
};

const readStoredKey = (storage, key, version) => {
  try {
    const stored = JSON.parse(storage.getItem(key));
    const fleetKey = normalizedFleetKey(stored?.key);
    return stored?.version === version && fleetPattern.test(fleetKey) ? fleetKey : "";
  } catch {
    return "";
  }
};

const removeStoredKey = (storage, key) => {
  try {
    storage.removeItem(key);
  } catch {
  }
};

const saveKey = key => {
  try {
    sessionStorage.setItem(storageKey, JSON.stringify({version: 2, key}));
  } catch {
  }
};

const loadStoredKey = () => {
  const current = readStoredKey(sessionStorage, storageKey, 2);
  if (current) return current;
  const legacy = readStoredKey(localStorage, legacyStorageKey, 1);
  removeStoredKey(localStorage, legacyStorageKey);
  if (legacy) saveKey(legacy);
  return legacy;
};

export function initFleet({apiBase, getReadyHash, showToast}) {
  const onboarding = document.querySelector("#fleet-onboarding");
  const consoleRoot = document.querySelector("#fleet-console");
  const settings = document.querySelector("#fleet-settings");
  const settingsToggle = document.querySelector("#fleet-settings-toggle");
  const useFleet = document.querySelector("#use-fleet");
  const recovery = document.querySelector("#fleet-recovery");
  const recoveryKey = document.querySelector("#fleet-recovery-key");
  const fleetStatus = document.querySelector("#fleet-status");
  const buildStatus = document.querySelector("#fleet-build-status");
  const buildList = document.querySelector("#fleet-builds");
  const scaleRows = document.querySelector("#fleet-scale-rows");
  const selectAll = document.querySelector("#select-all-scales");
  const buildSelect = document.querySelector("#fleet-build-select");
  const selectedCount = document.querySelector("#selected-scale-count");
  const addBuild = document.querySelector("#add-fleet-build");
  const assignSelected = document.querySelector("#assign-selected");
  const assignAll = document.querySelector("#assign-all");
  const clearSelected = document.querySelector("#clear-selected");
  const pairInput = document.querySelector("#pair-code");
  const existingInput = document.querySelector("#existing-fleet-key");
  const forgetDialog = document.querySelector("#forget-fleet-dialog");
  const confirmDialog = document.querySelector("#fleet-confirm-dialog");
  let fleetKey = loadStoredKey();
  let generation = 0;
  let scales = [];
  let builds = [];
  let buildStates = {};
  let selectedDeviceIds = new Set();

  const setSettingsOpen = open => {
    settings.hidden = !open;
    settingsToggle.setAttribute("aria-expanded", String(open));
  };

  const api = async (path, options = {}) => {
    const response = await fetch(`${apiBase}${path}`, {
      ...options,
      headers: {
        Authorization: `Bearer ${fleetKey}`,
        ...(options.body ? {"Content-Type": "application/json"} : {}),
      },
    });
    const result = await response.json().catch(() => ({}));
    if (!response.ok) {
      const error = new Error(result.error || `request failed: ${response.status}`);
      error.code = result.error;
      throw error;
    }
    return result;
  };

  const setMode = linked => {
    onboarding.hidden = linked;
    consoleRoot.hidden = !linked;
    useFleet.hidden = linked;
    recovery.hidden = !linked;
    recoveryKey.value = formattedFleetKey(fleetKey);
  };

  const confirm = (title, description, action) => new Promise(resolve => {
    document.querySelector("#fleet-confirm-heading").textContent = title;
    document.querySelector("#fleet-confirm-description").textContent = description;
    document.querySelector("#fleet-confirm-action").textContent = action;
    const closed = () => {
      confirmDialog.removeEventListener("close", closed);
      resolve(confirmDialog.returnValue === "confirm");
    };
    confirmDialog.addEventListener("close", closed);
    confirmDialog.showModal();
  });

  const copyHash = async hash => {
    try {
      await navigator.clipboard.writeText(hash);
      showToast("Combination hash copied");
    } catch {
      showToast("Combination hash could not be copied");
    }
  };

  const showApiError = error => {
    const messages = {
      build_assigned: "Clear this build from assigned scales before removing it",
      build_not_ready: "Only ready custom builds can be added or assigned",
      cross_fleet_device: "One or more scales belong to another fleet",
      device_not_found: "One or more scales are no longer linked",
      empty_target_set: "Select at least one scale",
    };
    showToast(messages[error.code] || "Fleet change could not be saved");
  };

  const renderControls = () => {
    const selected = selectedDeviceIds.size;
    const readyBuilds = builds.filter(build => build.state === "ready");
    const previousHash = buildSelect.value;
    buildSelect.replaceChildren(...readyBuilds.map(build =>
      new Option(`${build.label} · ${shortHash(build.combination_hash)}`, build.combination_hash)));
    if (readyBuilds.some(build => build.combination_hash === previousHash)) {
      buildSelect.value = previousHash;
    }
    buildSelect.disabled = !readyBuilds.length;
    selectedCount.textContent = `${selected} selected`;
    assignSelected.disabled = !selected || !readyBuilds.length;
    assignAll.disabled = !scales.length || !readyBuilds.length;
    clearSelected.disabled = !selected;
    selectAll.checked = Boolean(scales.length) && selected === scales.length;
    selectAll.indeterminate = selected > 0 && selected < scales.length;
    const readyHash = getReadyHash();
    const alreadyAdded = builds.some(build => build.combination_hash === readyHash);
    addBuild.disabled = !scales.length || !readyHash || alreadyAdded;
    addBuild.textContent = alreadyAdded ? "Build added" : "Add current build";
  };

  const loadFleet = async () => {
    const currentGeneration = ++generation;
    fleetStatus.textContent = "Loading fleet";
    buildStatus.textContent = "";
    try {
      const result = await api("/api/v1/fleet/overview");
      if (currentGeneration !== generation) return;
      scales = Array.isArray(result.scales) ? result.scales : [];
      builds = Array.isArray(result.builds) ? result.builds : [];
      buildStates = result.build_states || {};
      selectedDeviceIds = new Set([...selectedDeviceIds].filter(deviceId =>
        scales.some(scale => scale.device_id === deviceId)));
      renderBuilds();
      renderScales();
      fleetStatus.textContent = scales.length
        ? `${scales.length} scale${scales.length === 1 ? "" : "s"}` : "No scales linked yet";
      buildStatus.textContent = builds.length
        ? `${builds.length} saved build${builds.length === 1 ? "" : "s"}` : "No saved builds";
    } catch {
      if (currentGeneration !== generation) return;
      fleetStatus.textContent = "Fleet could not be loaded";
    }
  };

  const renderBuilds = () => {
    if (!builds.length) {
      const empty = document.createElement("li");
      empty.className = "fleet-empty-row";
      empty.textContent = "Add the ready build shown above to use it for deployments.";
      buildList.replaceChildren(empty);
      renderControls();
      return;
    }
    buildList.replaceChildren(...builds.map(build => {
      const row = document.createElement("li");
      row.className = "fleet-build-row";
      row.innerHTML = `
        <div class="fleet-build-name"><input class="build-label" maxlength="40" aria-label="Build label"></div>
        <div class="fleet-build-version"><strong></strong><span></span></div>
        <button class="fleet-hash" type="button" title="Copy full combination hash"><code></code></button>
        <span class="deployment-state"></span>
        <div class="fleet-row-actions">
          <button class="ghost-bordered-button save-build" type="button">Save</button>
          <button class="ghost-button remove-build" type="button">Remove</button>
        </div>`;
      row.querySelector(".build-label").value = build.label;
      row.querySelector(".fleet-build-version strong").textContent = build.firmware_version;
      row.querySelector(".fleet-build-version span").textContent = buildSummary(build);
      row.querySelector("code").textContent = shortHash(build.combination_hash);
      const state = row.querySelector(".deployment-state");
      state.textContent = build.state === "ready" ? "Ready" : "Unavailable";
      state.dataset.state = build.state;
      row.querySelector(".fleet-hash").addEventListener("click", () => copyHash(build.combination_hash));
      row.querySelector(".save-build").addEventListener("click", async () => {
        try {
          await api(`/api/v1/fleet/builds/${build.combination_hash}`, {
            method: "PATCH",
            body: JSON.stringify({label: row.querySelector(".build-label").value.trim()}),
          });
          showToast("Build label saved");
          await loadFleet();
        } catch (error) {
          showApiError(error);
        }
      });
      row.querySelector(".remove-build").addEventListener("click", async () => {
        if (!(await confirm("Remove saved build?", `${build.label} will remain available to existing installations.`, "Remove"))) return;
        try {
          await api(`/api/v1/fleet/builds/${build.combination_hash}`, {method: "DELETE"});
          showToast("Build removed from fleet");
          await loadFleet();
        } catch (error) {
          showApiError(error);
        }
      });
      return row;
    }));
    renderControls();
  };

  const renderScales = () => {
    scaleRows.replaceChildren(...scales.map(scale => {
      const row = document.createElement("tr");
      row.innerHTML = `
        <td class="scale-select"><input type="checkbox"></td>
        <td class="scale-identity"><input class="scale-name" maxlength="40"><span class="scale-hint"></span></td>
        <td class="scale-build" data-label="Installed"><strong></strong><code></code></td>
        <td class="scale-build desired-build" data-label="Desired"><strong></strong><code></code></td>
        <td data-label="Deployment"><span class="deployment-state"></span></td>
        <td class="last-seen" data-label="Last seen"></td>
        <td><button class="ghost-bordered-button save-scale" type="button">Save</button></td>`;
      const checkbox = row.querySelector('[type="checkbox"]');
      checkbox.checked = selectedDeviceIds.has(scale.device_id);
      checkbox.setAttribute("aria-label", `Select ${scale.name}`);
      checkbox.addEventListener("change", () => {
        selectedDeviceIds = checkbox.checked
          ? new Set([...selectedDeviceIds, scale.device_id])
          : new Set([...selectedDeviceIds].filter(deviceId => deviceId !== scale.device_id));
        renderControls();
      });
      const name = row.querySelector(".scale-name");
      name.value = scale.name;
      row.querySelector(".scale-hint").textContent = scale.serial_hint;
      const installed = row.querySelector(".scale-build");
      installed.querySelector("strong").textContent = scale.firmware_version || "Unknown";
      installed.querySelector("code").textContent = shortHash(scale.installed_combination) ||
        (scale.last_seen_at ? "Official" : "Unknown");
      const desired = row.querySelector(".desired-build");
      const desiredBuild = builds.find(build => build.combination_hash === scale.desired_combination);
      desired.querySelector("strong").textContent = desiredBuild?.label ||
        (scale.desired_combination ? "Custom build" : "None");
      desired.querySelector("code").textContent = shortHash(scale.desired_combination);
      const state = row.querySelector(".deployment-state");
      state.textContent = deploymentState(scale, buildStates);
      state.dataset.state = state.textContent.toLowerCase().replaceAll(" ", "-");
      row.querySelector(".last-seen").textContent = lastSeenLabel(scale.last_seen_at);
      row.querySelector(".save-scale").addEventListener("click", async () => {
        try {
          await api(`/api/v1/fleet/scales/${scale.device_id}`, {
            method: "PATCH",
            body: JSON.stringify({name: name.value.trim()}),
          });
          showToast("Scale name saved");
          await loadFleet();
        } catch (error) {
          showApiError(error);
        }
      });
      return row;
    }));
    renderControls();
  };

  const assign = async (targets, combinationHash, label) => {
    const count = targets.all ? scales.length : targets.device_ids.length;
    if (count > 1 && !(await confirm(
      `${label} ${count} scales?`,
      combinationHash ? "The selected build will become their desired deployment." : "Their desired deployment will be cleared.",
      label,
    ))) return;
    try {
      await api("/api/v1/fleet/assignments", {
        method: "POST",
        body: JSON.stringify({combination_hash: combinationHash, ...targets}),
      });
      selectedDeviceIds = new Set();
      showToast(`${label} complete`);
      await loadFleet();
    } catch (error) {
      showApiError(error);
    }
  };

  const activateKey = key => {
    fleetKey = normalizedFleetKey(key);
    if (!fleetPattern.test(fleetKey)) {
      showToast("Enter a valid fleet recovery key");
      return false;
    }
    saveKey(fleetKey);
    setMode(true);
    setSettingsOpen(false);
    loadFleet();
    return true;
  };

  document.querySelector("#start-fleet").addEventListener("click", () => {
    const key = encodeBase32(crypto.getRandomValues(new Uint8Array(20)));
    if (activateKey(key)) showToast("Recovery key created");
  });
  settingsToggle.addEventListener("click", () => setSettingsOpen(settings.hidden));
  useFleet.addEventListener("submit", event => {
    event.preventDefault();
    activateKey(existingInput.value);
  });
  document.querySelector("#copy-fleet-key").addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(formattedFleetKey(fleetKey));
      showToast("Fleet recovery key copied");
    } catch {
      showToast("Fleet key could not be copied");
    }
  });
  document.querySelector("#forget-fleet").addEventListener("click", () => forgetDialog.showModal());
  forgetDialog.addEventListener("close", () => {
    if (forgetDialog.returnValue !== "confirm") return;
    removeStoredKey(sessionStorage, storageKey);
    removeStoredKey(localStorage, legacyStorageKey);
    fleetKey = "";
    generation += 1;
    scales = [];
    builds = [];
    selectedDeviceIds = new Set();
    buildList.replaceChildren();
    scaleRows.replaceChildren();
    setMode(false);
  });
  document.querySelector("#pair-scale").addEventListener("submit", async event => {
    event.preventDefault();
    const pairCode = pairInput.value.trim().toUpperCase();
    if (!pairPattern.test(pairCode)) {
      showToast("Enter the pairing code shown on the scale");
      return;
    }
    try {
      await api("/api/v1/fleet/claim", {
        method: "POST",
        body: JSON.stringify({pair_code: pairCode}),
      });
      pairInput.value = "";
      showToast("Scale linked");
      await loadFleet();
    } catch {
      showToast("Pairing code is invalid or expired");
    }
  });
  addBuild.addEventListener("click", async () => {
    const combinationHash = getReadyHash();
    if (!combinationHash) return;
    try {
      await api("/api/v1/fleet/builds", {
        method: "POST",
        body: JSON.stringify({combination_hash: combinationHash}),
      });
      showToast("Build added to fleet");
      await loadFleet();
    } catch (error) {
      showApiError(error);
    }
  });
  selectAll.addEventListener("change", () => {
    selectedDeviceIds = selectAll.checked ? new Set(scales.map(scale => scale.device_id)) : new Set();
    renderScales();
  });
  buildSelect.addEventListener("change", renderControls);
  assignSelected.addEventListener("click", () => assign(
    {device_ids: [...selectedDeviceIds]}, buildSelect.value, "Assign",
  ));
  assignAll.addEventListener("click", () => assign({all: true}, buildSelect.value, "Assign to"));
  clearSelected.addEventListener("click", () => assign(
    {device_ids: [...selectedDeviceIds]}, null, "Clear assignment for",
  ));
  document.addEventListener("openscale-build-status", renderControls);

  setMode(Boolean(fleetKey));
  if (fleetKey) loadFleet();
}
