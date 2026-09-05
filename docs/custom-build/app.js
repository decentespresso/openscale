import {
  SelectionError,
  catalogRevisionChanged,
  defaultSelection,
  firmwareRefLabel,
  optionReason,
  parseSelection,
  resolveSelection,
  selectionQuery,
} from "./selection.mjs?v=5";
import {initFleet} from "./fleet.js?v=7";
import {buildEstimate} from "./build-estimate.mjs?v=1";

(async () => {
  const themeStorageKey = "hds-custom-build-theme-v1";
  const themeRoot = document.documentElement;
  const themeToggle = document.querySelector("#theme-toggle");
  const systemTheme = matchMedia("(prefers-color-scheme: dark)");
  const applyTheme = preference => {
    const dark = preference === "dark" || preference === "system" && systemTheme.matches;
    themeRoot.dataset.themePreference = preference;
    themeRoot.dataset.theme = dark ? "dark" : "light";
    const current = preference === "system" ? "System" : dark ? "Dark" : "Light";
    const label = `${current} theme; switch to ${dark ? "light" : "dark"} theme`;
    themeToggle.setAttribute("aria-pressed", String(dark));
    themeToggle.setAttribute("aria-label", label);
    themeToggle.title = label;
    document.querySelector("#theme-color").content = dark ? "#111413" : "#0d6b4f";
  };
  applyTheme(themeRoot.dataset.themePreference || "system");
  themeToggle.addEventListener("click", () => {
    const preference = themeRoot.dataset.theme === "dark" ? "light" : "dark";
    applyTheme(preference);
    try {
      localStorage.setItem(themeStorageKey, JSON.stringify({version: 1, preference}));
    } catch {
    }
  });
  systemTheme.addEventListener("change", () => {
    if (themeRoot.dataset.themePreference === "system") applyTheme("system");
  });

  const apiBase = "https://openscale-custom-builds.odevstudio.workers.dev";
  const response = await fetch("catalog.json", {cache: "no-store"});
  if (!response.ok) throw new Error(`catalog request failed: ${response.status}`);
  const catalog = await response.json();
  const icons = {
    check: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="m5 12 4 4L19 6"/></svg>',
    info: '<svg aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 8h.01"/></svg>'
  };
  const defaults = defaultSelection(catalog);
  const installStorageKey = "hds-custom-build-install-v1";
  let installMethod = "wifi";
  try {
    const storedInstall = JSON.parse(localStorage.getItem(installStorageKey));
    if (storedInstall?.version === 1 && ["wifi", "usb"].includes(storedInstall.method)) {
      installMethod = storedInstall.method;
    }
  } catch {
  }
  const withInstallRequirements = candidate => installMethod === "wifi" ? {
    ...candidate,
    features: [...new Set([...candidate.features, "pull-ota"])],
  } : candidate;
  let invalidLink = false;
  let selected = defaults;
  if (location.search) {
    try {
      selected = parseSelection(location.search, catalog);
    } catch {
      invalidLink = true;
    }
  }
  selected = withInstallRequirements(selected);
  const refSelect = document.querySelector("#firmware-ref");
  const featureRoot = document.querySelector("#features");
  const pluginRoot = document.querySelector("#plugins");
  const featureById = new Map(catalog.features.map(item => [item.id, item]));
  const pluginById = new Map(catalog.plugins.map(item => [item.id, item]));
  const buildButton = document.querySelector("#request-build");
  const fleetPanel = document.querySelector("#fleet-panel");
  let toastTimer;
  let statusTimer;
  let pollTimer;
  let pollRemaining = 0;
  let selectionGeneration = 0;
  let selectionController;
  let currentSelection;
  let currentCombinationHash = "";
  let currentBuildState = "checking";
  let catalogRetryDelay = 2000;

  const escapeHtml = value => String(value).replace(/[&<>'"]/g, character => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;"
  })[character]);

  const makeOption = (item, kind) => {
    const wrapper = document.createElement("div");
    const inputId = `${kind}-${item.id}`;
    const tooltipId = `${inputId}-tooltip`;
    const hasRecommendation = kind === "plugin" &&
      (item.recommends?.features.length || item.recommends?.plugins.length);
    wrapper.className = `option-card${hasRecommendation ? " has-recommendation" : ""}`;
    wrapper.dataset.id = item.id;
    wrapper.innerHTML = `
      <input class="native-check" id="${escapeHtml(inputId)}" type="checkbox" data-kind="${kind}" value="${escapeHtml(item.id)}">
      <label class="check-target" for="${escapeHtml(inputId)}">
        <span class="check-box" aria-hidden="true">${icons.check}</span>
        <span class="option-copy">
          <span class="option-name">${escapeHtml(item.name)}${kind === "plugin" ? `<span class="version">v${escapeHtml(item.version)}</span>` : ""}</span>
          <span class="option-description">${escapeHtml(item.description || "")}</span>
          <span class="status-badge"></span>
        </span>
      </label>
      ${hasRecommendation ? `<button class="recommend-button" type="button" data-recommend-plugin="${escapeHtml(item.id)}">Recommended</button>` : ""}
      <button class="info-button" type="button" aria-label="More information about ${escapeHtml(item.name)}" aria-describedby="${tooltipId}">${icons.info}</button>
      <span class="tooltip" id="${tooltipId}" role="tooltip">${escapeHtml(item.tooltip || item.description || "More information")}</span>`;
    return wrapper;
  };

  catalog.firmware_refs.forEach(ref => refSelect.add(new Option(firmwareRefLabel(ref), ref)));
  catalog.features.filter(item => !item.hidden).forEach(item => featureRoot.append(makeOption(item, "feature")));
  catalog.plugins.forEach(item => pluginRoot.append(makeOption(item, "plugin")));

  const requirementReason = (featureId, resolved) => {
    const reasons = [];
    resolved.plugins.forEach(id => {
      const plugin = pluginById.get(id);
      if (plugin?.requires.includes(featureId)) reasons.push(plugin.name);
    });
    resolved.features.forEach(id => {
      if (id === featureId) return;
      const feature = featureById.get(id);
      if (feature?.requires.includes(featureId)) reasons.push(feature.name);
    });
    const unique = [...new Set(reasons)];
    return unique.length ? `Required by ${unique.join(", ")}` : "Included as a dependency";
  };

  const pluginRequirementReason = (pluginId, resolved) => {
    const reasons = resolved.plugins
      .map(id => pluginById.get(id))
      .filter(plugin => plugin?.depends_on.includes(pluginId))
      .map(plugin => plugin.name);
    return reasons.length ? `Required by ${reasons.join(", ")}` : "Included as a dependency";
  };

  const showToast = message => {
    const toast = document.querySelector("#toast");
    document.querySelector("#toast-message").textContent = message;
    clearTimeout(toastTimer);
    toast.classList.add("is-visible");
    toastTimer = setTimeout(() => toast.classList.remove("is-visible"), 2200);
  };

  const setStatus = (result, generation = selectionGeneration) => {
    if (generation !== selectionGeneration) return;
    const buildState = document.querySelector("#build-state");
    const combinationHash = result.combination_hash || "";
    const failureMessages = {
      dispatch_failed: "The build could not be sent to the build runner.",
      build_failed: "The firmware build failed.",
      build_timeout: "The build runner did not finish in time."
    };
    const messages = {
      missing: "This combination has not been built yet.",
      queued: "",
      building: "The firmware and filesystem images are being built.",
      ready: "The build archive is ready to download.",
      failed: failureMessages[result.failure_code] || "The build did not complete.",
      unavailable: "The build service is currently unavailable.",
      updating: "Configurator update in progress.",
      "rate-limited": "The weekly build limit has been reached. Existing cached builds remain available.",
      checking: "Checking the build cache."
    };
    const labels = {"rate-limited": "Rate limited", updating: "Updating"};
    buildState.className = `ready state-${result.state}`;
    buildState.textContent = labels[result.state] || result.state[0].toUpperCase() + result.state.slice(1);
    currentCombinationHash = combinationHash;
    currentBuildState = result.state;
    const updaterLink = document.querySelector("#hds-updater-link");
    const updaterReady = result.state === "ready" && installMethod === "usb";
    updaterLink.classList.toggle("is-ready", updaterReady);
    updaterLink.setAttribute(
      "aria-label", updaterReady ? "Open HDS Updater for this build" : "Open HDS Updater",
    );
    const retryMessage = result.state === "failed" ?
      (result.retryable ? " One retry is available." : " No retries remain.") : "";
    document.querySelector("#status-message").textContent =
      [messages[result.state] ?? messages.unavailable, retryMessage, buildEstimate(result, currentSelection)].filter(Boolean).join(" ");
    buildButton.disabled = result.state !== "missing" && !(result.state === "failed" && result.retryable);
    const downloads = document.querySelector("#downloads");
    downloads.replaceChildren();
    if (result.state === "ready") {
      Object.entries(result.downloads || {}).forEach(([name, href]) => {
        const link = document.createElement("a");
        link.href = href;
        link.textContent = name;
        downloads.append(link);
      });
    }
    if (["queued", "building"].includes(result.state) && combinationHash) {
      schedulePoll(combinationHash, generation);
    }
    document.dispatchEvent(new CustomEvent("openscale-build-status", {
      detail: {state: result.state, combinationHash},
    }));
  };

  const apiRequest = async (path, options = {}) => {
    const apiResponse = await fetch(`${apiBase}${path}`, {
      ...options,
      headers: {"Content-Type": "application/json", ...(options.headers || {})}
    });
    const result = await apiResponse.json();
    if (!apiResponse.ok) {
      const error = new Error(result.error || `request failed: ${apiResponse.status}`);
      error.result = result;
      error.status = apiResponse.status;
      throw error;
    }
    return result;
  };

  const clearCatalogReload = () => {
    catalogRetryDelay = 2000;
    try {
      sessionStorage.removeItem("hds-custom-build-catalog-reload");
    } catch {
    }
  };

  const handleCatalogStale = (error, generation, selection) => {
    if (error.result?.error !== "catalog_stale") return false;
    setStatus({state: "updating"}, generation);
    try {
      if (!sessionStorage.getItem("hds-custom-build-catalog-reload")) {
        sessionStorage.setItem("hds-custom-build-catalog-reload", "1");
        location.reload();
        return true;
      }
    } catch {
    }
    const retryDelay = catalogRetryDelay;
    catalogRetryDelay = Math.min(catalogRetryDelay * 2, 30000);
    clearTimeout(statusTimer);
    statusTimer = setTimeout(async () => {
      if (generation !== selectionGeneration) return;
      let revisionChanged = false;
      try {
        revisionChanged = await catalogRevisionChanged(fetch, catalog.catalog_revision);
      } catch {
      }
      if (generation !== selectionGeneration) return;
      if (revisionChanged) {
        location.reload();
        return;
      }
      await checkStatus(selection, generation);
    }, retryDelay);
    return true;
  };

  const schedulePoll = (combinationHash, generation) => {
    clearTimeout(pollTimer);
    if (pollRemaining <= 0 || generation !== selectionGeneration) return;
    pollTimer = setTimeout(async () => {
      try {
        pollRemaining -= 1;
        const result = await apiRequest(`/api/v1/status/${combinationHash}`, {
          signal: selectionController.signal
        });
        if (result.combination_hash === combinationHash) setStatus(result, generation);
      } catch (error) {
        if (error.name !== "AbortError") {
          setStatus({state: "unavailable", combination_hash: combinationHash}, generation);
        }
      }
    }, 10000);
  };

  const checkStatus = async (selection, generation) => {
    clearTimeout(pollTimer);
    pollRemaining = 300;
    setStatus({state: "checking"}, generation);
    try {
      const result = await apiRequest("/api/v1/status", {
        method: "POST",
        body: JSON.stringify(selection),
        signal: selectionController.signal
      });
      clearCatalogReload();
      setStatus(result, generation);
    } catch (error) {
      if (error.name !== "AbortError" && !handleCatalogStale(error, generation, selection)) {
        setStatus({state: "unavailable"}, generation);
      }
    }
  };

  const recommendationAvailable = plugin => {
    try {
      resolveSelection(catalog, {
        firmware_ref: selected.firmware_ref,
        features: plugin.recommends.features,
        plugins: [plugin.id, ...plugin.recommends.plugins]
      });
      return true;
    } catch {
      return false;
    }
  };

  const render = () => {
    const resolved = resolveSelection(catalog, selected);
    refSelect.value = selected.firmware_ref;
    document.querySelectorAll('[data-kind="feature"]').forEach(input => {
      const id = input.value;
      const card = input.closest(".option-card");
      const isRequested = selected.features.includes(id);
      const isResolved = resolved.features.includes(id);
      const installRequired = id === "pull-ota" && installMethod === "wifi";
      const isRequired = installRequired || isResolved && !isRequested;
      const unavailableReason = isResolved ? "" : optionReason(catalog, selected, "feature", id);
      input.checked = isResolved;
      input.disabled = isRequired || Boolean(unavailableReason);
      card.classList.toggle("is-checked", isResolved);
      card.classList.toggle("is-required", isRequired);
      card.classList.toggle("is-disabled", input.disabled);
      card.classList.toggle("is-unavailable", Boolean(unavailableReason));
      card.querySelector(".status-badge").textContent = installRequired ?
        "Required for scale install" : isRequired ? requirementReason(id, resolved) : unavailableReason;
    });
    document.querySelectorAll('[data-kind="plugin"]').forEach(input => {
      const plugin = pluginById.get(input.value);
      const card = input.closest(".option-card");
      const isRequested = selected.plugins.includes(plugin.id);
      const isResolved = resolved.plugins.includes(plugin.id);
      const isRequired = isResolved && !isRequested;
      const unavailableReason = isResolved ? "" : optionReason(catalog, selected, "plugin", plugin.id);
      input.checked = isResolved;
      input.disabled = isRequired || Boolean(unavailableReason);
      card.classList.toggle("is-checked", isResolved);
      card.classList.toggle("is-required", isRequired);
      card.classList.toggle("is-disabled", input.disabled);
      card.classList.toggle("is-unavailable", Boolean(unavailableReason));
      card.querySelector(".status-badge").textContent = isRequired ?
        pluginRequirementReason(plugin.id, resolved) : unavailableReason;
      const recommendationButton = card.querySelector(".recommend-button");
      if (recommendationButton) recommendationButton.disabled = !recommendationAvailable(plugin);
    });
    const visibleFeatures = resolved.features.filter(id => !featureById.get(id)?.hidden);
    const resolvedRoot = document.querySelector("#resolved");
    resolvedRoot.replaceChildren();
    if (visibleFeatures.length) {
      visibleFeatures.forEach(id => {
        const chip = document.createElement("span");
        chip.className = "resolved-chip";
        chip.textContent = id;
        resolvedRoot.append(chip);
      });
    } else {
      const empty = document.createElement("p");
      empty.className = "empty-state";
      empty.textContent = "No features selected";
      resolvedRoot.append(empty);
    }
    document.querySelector("#feature-count").textContent = `${visibleFeatures.length} selected`;
    document.querySelector("#plugin-count").textContent = `${resolved.plugins.length} selected`;
    document.querySelector("#summary-ref").textContent = firmwareRefLabel(selected.firmware_ref);
    document.querySelector("#summary-plugins").textContent = resolved.plugins.length || "None";
    document.querySelector("#pull-ota-warning").hidden = !resolved.features.includes("pull-ota");
    history.replaceState(null, "", `${location.pathname}${selectionQuery(selected)}${location.hash}`);
    selectionGeneration += 1;
    selectionController?.abort();
    selectionController = new AbortController();
    currentSelection = Object.freeze({
      firmware_ref: selected.firmware_ref,
      features: Object.freeze(resolved.features),
      plugins: Object.freeze(resolved.plugins),
      catalog_revision: catalog.catalog_revision
    });
    const generation = selectionGeneration;
    clearTimeout(statusTimer);
    clearTimeout(pollTimer);
    statusTimer = setTimeout(() => checkStatus(currentSelection, generation), 350);
  };

  const applySelection = (candidate, message) => {
    try {
      const requiredCandidate = withInstallRequirements(candidate);
      resolveSelection(catalog, requiredCandidate);
      selected = {
        firmware_ref: requiredCandidate.firmware_ref,
        features: [...requiredCandidate.features].sort(),
        plugins: [...requiredCandidate.plugins].sort()
      };
      render();
      if (message) showToast(message);
    } catch (error) {
      if (!(error instanceof SelectionError)) throw error;
      render();
      showToast("Selection conflicts with the current setup");
    }
  };

  document.addEventListener("click", event => {
    const button = event.target.closest("[data-recommend-plugin]");
    if (!button) return;
    const plugin = pluginById.get(button.dataset.recommendPlugin);
    applySelection({
      firmware_ref: selected.firmware_ref,
      features: plugin.recommends.features,
      plugins: [plugin.id, ...plugin.recommends.plugins]
    }, `Recommended setup for ${plugin.name} selected`);
  });

  document.addEventListener("change", event => {
    const input = event.target;
    if (!input.matches('[data-kind="feature"], [data-kind="plugin"]')) return;
    const field = input.dataset.kind === "feature" ? "features" : "plugins";
    const values = input.checked ? [...selected[field], input.value] :
      selected[field].filter(value => value !== input.value);
    applySelection({...selected, [field]: values});
  });

  refSelect.addEventListener("change", () => applySelection({...selected, firmware_ref: refSelect.value}));
  document.querySelectorAll('[name="install-method"]').forEach(input => {
    input.checked = input.value === installMethod;
    input.addEventListener("change", () => {
      installMethod = input.value;
      fleetPanel.hidden = installMethod !== "wifi";
      localStorage.setItem(installStorageKey, JSON.stringify({version: 1, method: installMethod}));
      const nextSelection = installMethod === "usb"
        ? {...selected, features: selected.features.filter(feature => feature !== "pull-ota")}
        : selected;
      applySelection(nextSelection, installMethod === "wifi" ? "Pull OTA included for scale install" : "USB install selected");
    });
  });
  fleetPanel.hidden = installMethod !== "wifi";
  buildButton.addEventListener("click", async () => {
    const generation = selectionGeneration;
    const selection = currentSelection;
    buildButton.disabled = true;
    setStatus({state: "checking"}, generation);
    try {
      const result = await apiRequest("/api/v1/build", {
        method: "POST",
        body: JSON.stringify(selection),
        signal: selectionController.signal
      });
      if (generation !== selectionGeneration) return;
      clearCatalogReload();
      pollRemaining = 300;
      setStatus(result, generation);
    } catch (error) {
      if (error.name === "AbortError" || generation !== selectionGeneration) return;
      if (handleCatalogStale(error, generation, selection)) return;
      if (error.status === 429) {
        setStatus({state: "rate-limited"}, generation);
        showToast("Weekly build limit reached");
      } else if (error.result?.state === "failed") {
        setStatus(error.result, generation);
        showToast("Build could not be completed");
      } else {
        setStatus({state: "unavailable"}, generation);
        showToast("Build request rejected");
      }
    }
  });
  document.querySelector("#reset").addEventListener("click", () => {
    applySelection(defaults, "Defaults restored");
  });

  initFleet({
    apiBase,
    getReadyHash: () => currentBuildState === "ready" ? currentCombinationHash : "",
    showToast,
  });
  render();
  if (invalidLink) showToast("Invalid selection link discarded");
})().catch(() => {
  document.querySelector("#feature-count").textContent = "Unavailable";
  document.querySelector("#plugin-count").textContent = "Unavailable";
  document.querySelector("#status-message").textContent = "The build catalog could not be loaded.";
  document.querySelector("#build-state").textContent = "Unavailable";
  document.querySelectorAll("button, select").forEach(control => { control.disabled = true; });
});
