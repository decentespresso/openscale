(async () => {
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
  let toastTimer;

  const escapeHtml = value => String(value).replace(/[&<>'"]/g, character => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;"
  })[character]);

  const makeOption = (item, kind) => {
    const wrapper = document.createElement("div");
    const inputId = `${kind}-${item.id}`;
    const tooltipId = `${inputId}-tooltip`;
    wrapper.className = "option-card";
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
      <button class="info-button" type="button" aria-label="More information about ${escapeHtml(item.name)}" aria-describedby="${tooltipId}">${icons.info}</button>
      <span class="tooltip" id="${tooltipId}" role="tooltip">${escapeHtml(item.tooltip || item.description || "More information")}</span>`;
    return wrapper;
  };

  catalog.firmware_refs.forEach(ref => refSelect.add(new Option(ref, ref)));
  catalog.features.forEach(item => featureRoot.append(makeOption(item, "feature")));
  catalog.plugins.forEach(item => pluginRoot.append(makeOption(item, "plugin")));

  const resolveSelection = () => {
    const resolved = new Set(state.requested);
    state.plugins.forEach(id => {
      const plugin = pluginById.get(id);
      if (plugin) plugin.requires.forEach(feature => resolved.add(feature));
    });
    let changed = true;
    while (changed) {
      const previousSize = resolved.size;
      [...resolved].forEach(id => {
        const feature = featureById.get(id);
        if (feature) feature.requires.forEach(required => resolved.add(required));
      });
      changed = previousSize !== resolved.size;
    }
    return resolved;
  };

  const requirementReason = (featureId, resolved) => {
    const reasons = [];
    state.plugins.forEach(id => {
      const plugin = pluginById.get(id);
      if (plugin && plugin.requires.includes(featureId)) reasons.push(plugin.name);
    });
    resolved.forEach(id => {
      if (id === featureId) return;
      const feature = featureById.get(id);
      if (feature && feature.requires.includes(featureId)) reasons.push(feature.name);
    });
    const unique = [...new Set(reasons)];
    return unique.length ? `Required by ${unique.join(", ")}` : "Included as a dependency";
  };

  const pluginAvailability = plugin => {
    if (!plugin.firmware_refs.includes(refSelect.value)) return `Unavailable for ${refSelect.value}`;
    const conflicting = [...state.plugins].find(selectedId => {
      if (selectedId === plugin.id) return false;
      const selected = pluginById.get(selectedId);
      return plugin.conflicts.includes(selectedId) || (selected && selected.conflicts.includes(plugin.id));
    });
    return conflicting ? `Conflicts with ${pluginById.get(conflicting).name}` : "";
  };

  const render = () => {
    catalog.plugins.forEach(plugin => {
      if (!plugin.firmware_refs.includes(refSelect.value)) state.plugins.delete(plugin.id);
    });
    const resolved = resolveSelection();

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
      card.querySelector(".status-badge").textContent = isRequired ? requirementReason(id, resolved) : "";
    });

    document.querySelectorAll('[data-kind="plugin"]').forEach(input => {
      const plugin = pluginById.get(input.value);
      const card = input.closest(".option-card");
      const unavailableReason = pluginAvailability(plugin);
      input.checked = state.plugins.has(plugin.id);
      input.disabled = Boolean(unavailableReason) && !input.checked;
      card.classList.toggle("is-checked", input.checked);
      card.classList.toggle("is-disabled", input.disabled);
      card.classList.toggle("is-unavailable", input.disabled);
      card.querySelector(".status-badge").textContent = input.disabled ? unavailableReason : "";
    });

    const features = [...resolved].sort();
    const plugins = [...state.plugins].sort();
    const resolvedRoot = document.querySelector("#resolved");
    resolvedRoot.replaceChildren();
    if (features.length) {
      features.forEach(id => {
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

    document.querySelector("#feature-count").textContent = `${features.length} selected`;
    document.querySelector("#plugin-count").textContent = `${plugins.length} selected`;
    document.querySelector("#summary-ref").textContent = refSelect.value;
    document.querySelector("#summary-plugins").textContent = plugins.length || "None";
    document.querySelector("#command").textContent = `gh workflow run custom-build.yml -f firmware_ref=${refSelect.value} -f features=${features.join(",")} -f plugins=${plugins.join(",")}`;
  };

  const showToast = message => {
    const toast = document.querySelector("#toast");
    document.querySelector("#toast-message").textContent = message;
    clearTimeout(toastTimer);
    toast.classList.add("is-visible");
    toastTimer = setTimeout(() => toast.classList.remove("is-visible"), 2200);
  };

  const copyCommand = async () => {
    const command = document.querySelector("#command").textContent;
    try {
      await navigator.clipboard.writeText(command);
      showToast("Command copied to clipboard");
    } catch (error) {
      showToast("Select the command and copy it manually");
    }
  };

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
  document.querySelector("#copy").addEventListener("click", copyCommand);
  document.querySelector("#icon-copy").addEventListener("click", copyCommand);
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
  document.querySelector("#command").textContent = "Catalog unavailable";
  const resolved = document.querySelector("#resolved");
  resolved.textContent = "The build catalog could not be loaded.";
  resolved.classList.add("empty-state");
  document.querySelectorAll("button, select").forEach(control => { control.disabled = true; });
});
