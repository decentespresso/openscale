(async () => {
  const apiBase = "https://openscale-custom-builds.odevstudio.workers.dev";
  const response = await fetch("catalog.json", {cache: "no-cache"});
  if (!response.ok) throw new Error(`catalog request failed: ${response.status}`);
  const catalog = await response.json();
  const icons = {
    check: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="m5 12 4 4L19 6"/></svg>',
    info: '<svg aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 8h.01"/></svg>'
  };
  const state = {
    requested: new Set(catalog.features.filter(item => item.default).map(item => item.id)),
    plugins: new Set(catalog.plugins.filter(item => item.default).map(item => item.id))
  };
  const refSelect = document.querySelector("#firmware-ref");
  const featureRoot = document.querySelector("#features");
  const pluginRoot = document.querySelector("#plugins");
  const featureById = new Map(catalog.features.map(item => [item.id, item]));
  const pluginById = new Map(catalog.plugins.map(item => [item.id, item]));
  const buildButton = document.querySelector("#request-build");
  let toastTimer;
  let updaterTimer;
  let statusTimer;
  let pollTimer;
  let pollRemaining = 0;
  let selectionGeneration = 0;
  let selectionController;
  let currentSelection;

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

  const refLabel = ref => /^v?\d+\.\d+\.\d+$/.test(ref) ?
    `${ref.replace(/^v/, "")} (stable)` : `${ref} (development)`;
  catalog.firmware_refs.forEach(ref => refSelect.add(new Option(refLabel(ref), ref)));
  catalog.features.filter(item => !item.hidden).forEach(item => featureRoot.append(makeOption(item, "feature")));
  catalog.plugins.forEach(item => pluginRoot.append(makeOption(item, "plugin")));

  const resolvePlugins = () => {
    const resolved = new Set();
    const visit = id => {
      if (resolved.has(id)) return;
      pluginById.get(id)?.depends_on.forEach(visit);
      resolved.add(id);
    };
    [...state.plugins].sort().forEach(visit);
    return resolved;
  };

  const resolveSelection = resolvedPlugins => {
    const resolved = new Set(state.requested);
    resolvedPlugins.forEach(id => pluginById.get(id)?.requires.forEach(feature => resolved.add(feature)));
    let previousSize = -1;
    while (resolved.size !== previousSize) {
      previousSize = resolved.size;
      [...resolved].forEach(id => featureById.get(id)?.requires.forEach(required => resolved.add(required)));
    }
    return resolved;
  };

  const requirementReason = (featureId, resolved, resolvedPlugins) => {
    const reasons = [];
    resolvedPlugins.forEach(id => {
      const plugin = pluginById.get(id);
      if (plugin?.requires.includes(featureId)) reasons.push(plugin.name);
    });
    resolved.forEach(id => {
      if (id === featureId) return;
      const feature = featureById.get(id);
      if (feature?.requires.includes(featureId)) reasons.push(feature.name);
    });
    const unique = [...new Set(reasons)];
    return unique.length ? `Required by ${unique.join(", ")}` : "Included as a dependency";
  };

  const pluginRequirementReason = (pluginId, resolvedPlugins) => {
    const reasons = [...resolvedPlugins]
      .map(id => pluginById.get(id))
      .filter(plugin => plugin?.depends_on.includes(pluginId))
      .map(plugin => plugin.name);
    return reasons.length ? `Required by ${reasons.join(", ")}` : "Included as a dependency";
  };

  const pluginAvailability = (plugin, resolvedPlugins) => {
    if (!plugin.firmware_refs.includes(refSelect.value)) return `Unavailable for ${refSelect.value}`;
    const conflicting = [...resolvedPlugins].find(selectedId => {
      if (selectedId === plugin.id) return false;
      const selected = pluginById.get(selectedId);
      return plugin.conflicts.includes(selectedId) || selected?.conflicts.includes(plugin.id);
    });
    return conflicting ? `Conflicts with ${pluginById.get(conflicting).name}` : "";
  };

  const showToast = message => {
    const toast = document.querySelector("#toast");
    document.querySelector("#toast-message").textContent = message;
    clearTimeout(toastTimer);
    toast.classList.add("is-visible");
    toastTimer = setTimeout(() => toast.classList.remove("is-visible"), 2200);
  };

  const buildEstimate = result => {
    if (result.state === "queued") return "Usually ready in about 5 minutes.";
    if (result.state !== "building" || !result.updated_at) return "";
    const elapsedMinutes = Math.max(0, (Date.now() - Date.parse(result.updated_at)) / 60000);
    const remaining = Math.ceil(5 - elapsedMinutes);
    return remaining > 0 ? `Usually ready in about ${remaining} minute${remaining === 1 ? "" : "s"}.` : "Finishing up.";
  };

  const setStatus = (result, generation = selectionGeneration) => {
    if (generation !== selectionGeneration) return;
    const buildState = document.querySelector("#build-state");
    const combinationHash = result.combination_hash || "";
    const messages = {
      missing: "This combination has not been built yet.",
      queued: "The build is waiting for an available runner.",
      building: "The firmware and filesystem images are being built.",
      ready: "The verified build archive is ready to download.",
      failed: result.retryable ? "The build did not complete. One retry is available." :
        "The build did not complete after two attempts.",
      unavailable: "The build service is currently unavailable.",
      "rate-limited": "The daily build limit has been reached. Existing cached builds remain available.",
      checking: "Checking the build cache."
    };
    buildState.className = `ready state-${result.state}`;
    buildState.textContent = result.state[0].toUpperCase() + result.state.slice(1);
    document.querySelector("#combination-hash").textContent = combinationHash ? combinationHash.slice(0, 12) : "";
    document.querySelector("#status-message").textContent =
      [messages[result.state] || messages.unavailable, buildEstimate(result)].filter(Boolean).join(" ");
    buildButton.disabled = result.state !== "missing" && !(result.state === "failed" && result.retryable);
    const downloads = document.querySelector("#downloads");
    downloads.replaceChildren();
    if (result.state === "ready") {
      Object.entries(result.downloads || {}).forEach(([name, href]) => {
        const link = document.createElement("a");
        link.href = href;
        link.textContent = name;
        link.addEventListener("click", () => {
          const updaterLink = document.querySelector("#hds-updater-link");
          clearTimeout(updaterTimer);
          updaterLink.classList.remove("is-next-step");
          requestAnimationFrame(() => updaterLink.classList.add("is-next-step"));
          updaterTimer = setTimeout(() => updaterLink.classList.remove("is-next-step"), 6000);
        });
        downloads.append(link);
      });
    }
    if (["queued", "building"].includes(result.state) && combinationHash) {
      schedulePoll(combinationHash, generation);
    }
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
      setStatus(result, generation);
    } catch (error) {
      if (error.name !== "AbortError") setStatus({state: "unavailable"}, generation);
    }
  };

  const render = () => {
    catalog.plugins.forEach(plugin => {
      if (!plugin.firmware_refs.includes(refSelect.value)) state.plugins.delete(plugin.id);
    });
    const resolvedPlugins = resolvePlugins();
    const resolved = resolveSelection(resolvedPlugins);
    document.querySelectorAll('[data-kind="feature"]').forEach(input => {
      const id = input.value;
      const card = input.closest(".option-card");
      const isRequested = state.requested.has(id);
      const isResolved = resolved.has(id);
      const isRequired = isResolved && !isRequested;
      input.checked = isResolved;
      input.disabled = isRequired;
      card.classList.toggle("is-checked", isResolved);
      card.classList.toggle("is-required", isRequired);
      card.classList.toggle("is-disabled", isRequired);
      card.querySelector(".status-badge").textContent = isRequired ?
        requirementReason(id, resolved, resolvedPlugins) : "";
    });
    document.querySelectorAll('[data-kind="plugin"]').forEach(input => {
      const plugin = pluginById.get(input.value);
      const card = input.closest(".option-card");
      const isRequested = state.plugins.has(plugin.id);
      const isResolved = resolvedPlugins.has(plugin.id);
      const isRequired = isResolved && !isRequested;
      const unavailableReason = pluginAvailability(plugin, resolvedPlugins);
      input.checked = isResolved;
      input.disabled = isRequired || Boolean(unavailableReason) && !isResolved;
      card.classList.toggle("is-checked", isResolved);
      card.classList.toggle("is-required", isRequired);
      card.classList.toggle("is-disabled", input.disabled);
      card.classList.toggle("is-unavailable", Boolean(unavailableReason));
      card.querySelector(".status-badge").textContent = isRequired ?
        pluginRequirementReason(plugin.id, resolvedPlugins) : unavailableReason;
      const recommendationButton = card.querySelector(".recommend-button");
      if (recommendationButton) recommendationButton.disabled = Boolean(unavailableReason);
    });
    const features = [...resolved].sort();
    const visibleFeatures = features.filter(id => !featureById.get(id)?.hidden);
    const plugins = [...resolvedPlugins].sort();
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
    document.querySelector("#plugin-count").textContent = `${plugins.length} selected`;
    document.querySelector("#summary-ref").textContent = refLabel(refSelect.value);
    document.querySelector("#summary-plugins").textContent = plugins.length || "None";
    document.querySelector("#pull-ota-warning").hidden = !resolved.has("pull-ota");
    selectionGeneration += 1;
    selectionController?.abort();
    selectionController = new AbortController();
    currentSelection = Object.freeze({
      firmware_ref: refSelect.value,
      features: Object.freeze(features),
      plugins: Object.freeze(plugins)
    });
    const generation = selectionGeneration;
    const selection = currentSelection;
    clearTimeout(statusTimer);
    clearTimeout(pollTimer);
    statusTimer = setTimeout(() => checkStatus(selection, generation), 350);
  };

  document.addEventListener("click", event => {
    const button = event.target.closest("[data-recommend-plugin]");
    if (!button) return;
    const plugin = pluginById.get(button.dataset.recommendPlugin);
    state.requested = new Set(plugin.recommends.features);
    state.plugins = new Set([plugin.id, ...plugin.recommends.plugins]);
    render();
    showToast(`Recommended setup for ${plugin.name} selected`);
  });

  document.addEventListener("change", event => {
    const input = event.target;
    if (input.matches('[data-kind="feature"]')) {
      input.checked ? state.requested.add(input.value) : state.requested.delete(input.value);
    } else if (input.matches('[data-kind="plugin"]')) {
      input.checked ? state.plugins.add(input.value) : state.plugins.delete(input.value);
    } else {
      return;
    }
    render();
  });

  refSelect.addEventListener("change", render);
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
      pollRemaining = 300;
      setStatus(result, generation);
    } catch (error) {
      if (error.name === "AbortError" || generation !== selectionGeneration) return;
      if (error.status === 429) {
        setStatus({state: "rate-limited"}, generation);
        showToast("Daily build limit reached");
      } else if (error.result?.state === "failed") {
        setStatus(error.result, generation);
        showToast("Retry limit reached");
      } else {
        setStatus({state: "unavailable"}, generation);
        showToast("Build request rejected");
      }
    }
  });
  document.querySelector("#reset").addEventListener("click", () => {
    state.requested = new Set(catalog.features.filter(item => item.default).map(item => item.id));
    state.plugins = new Set(catalog.plugins.filter(item => item.default).map(item => item.id));
    refSelect.value = catalog.firmware_refs[0];
    render();
    showToast("Defaults restored");
  });

  render();
})().catch(() => {
  document.querySelector("#feature-count").textContent = "Unavailable";
  document.querySelector("#plugin-count").textContent = "Unavailable";
  document.querySelector("#status-message").textContent = "The build catalog could not be loaded.";
  document.querySelector("#build-state").textContent = "Unavailable";
  document.querySelectorAll("button, select").forEach(control => { control.disabled = true; });
});
