const state = {
  catalog: null,
  requestedFeatures: new Set(),
  selectedPlugins: new Set(),
};

function featureMap() {
  return new Map(state.catalog.features.map(feature => [feature.id, feature]));
}

function pluginMap() {
  return new Map(state.catalog.plugins.map(plugin => [plugin.id, plugin]));
}

function resolveFeatures() {
  const features = featureMap();
  const plugins = pluginMap();
  const resolved = new Set(state.requestedFeatures);
  if (state.requestedFeatures.has("wifi")) {
    resolved.add("webserver");
  }
  for (const pluginId of state.selectedPlugins) {
    for (const dependency of plugins.get(pluginId).requires) {
      resolved.add(dependency);
    }
  }
  let changed = true;
  while (changed) {
    changed = false;
    for (const featureId of [...resolved]) {
      for (const dependency of features.get(featureId).requires) {
        if (!resolved.has(dependency)) {
          resolved.add(dependency);
          changed = true;
        }
      }
    }
  }
  return resolved;
}

function incompatiblePlugin(plugin, firmwareRef) {
  if (!plugin.firmware_refs.includes(firmwareRef)) {
    return "unsupported by this firmware reference";
  }
  const conflict = plugin.conflicts.find(id => state.selectedPlugins.has(id));
  return conflict ? `conflicts with ${conflict}` : "";
}

function renderFeatures(resolved) {
  const root = document.querySelector("#features");
  root.replaceChildren();
  for (const feature of state.catalog.features) {
    const card = document.createElement("div");
    card.className = "card";
    const label = document.createElement("label");
    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = state.requestedFeatures.has(feature.id);
    input.addEventListener("change", () => {
      if (input.checked) state.requestedFeatures.add(feature.id);
      else state.requestedFeatures.delete(feature.id);
      render();
    });
    const title = document.createElement("span");
    title.textContent = feature.name;
    label.append(input, title);
    const description = document.createElement("p");
    description.textContent = feature.description;
    const status = document.createElement("small");
    status.textContent = resolved.has(feature.id)
      ? state.requestedFeatures.has(feature.id) ? "Selected directly" : "Enabled by dependency"
      : "Not included";
    card.append(label, description, status);
    root.append(card);
  }
}

function renderPlugins() {
  const root = document.querySelector("#plugins");
  const firmwareRef = document.querySelector("#firmware-ref").value;
  root.replaceChildren();
  for (const plugin of state.catalog.plugins) {
    const card = document.createElement("div");
    card.className = "card";
    const reason = incompatiblePlugin(plugin, firmwareRef);
    const label = document.createElement("label");
    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = state.selectedPlugins.has(plugin.id);
    input.disabled = Boolean(reason);
    input.addEventListener("change", () => {
      if (input.checked) state.selectedPlugins.add(plugin.id);
      else state.selectedPlugins.delete(plugin.id);
      render();
    });
    const title = document.createElement("span");
    title.textContent = `${plugin.name} ${plugin.version}`;
    label.append(input, title);
    const description = document.createElement("p");
    description.textContent = plugin.description;
    const requirements = document.createElement("small");
    requirements.textContent = reason || `Requires: ${plugin.requires.join(", ")}`;
    card.append(label, description, requirements);
    root.append(card);
  }
}

function commandText() {
  const orderedFeatures = state.catalog.features
    .map(feature => feature.id)
    .filter(id => state.requestedFeatures.has(id));
  const orderedPlugins = state.catalog.plugins
    .map(plugin => plugin.id)
    .filter(id => state.selectedPlugins.has(id));
  const firmwareRef = document.querySelector("#firmware-ref").value;
  return `gh workflow run custom-build.yml \\\n  -f firmware_ref=${firmwareRef} \\\n  -f features=${orderedFeatures.join(",")} \\\n  -f plugins=${orderedPlugins.join(",")}`;
}

function render() {
  const resolved = resolveFeatures();
  renderFeatures(resolved);
  renderPlugins();
  const ordered = state.catalog.features
    .map(feature => feature.id)
    .filter(id => resolved.has(id));
  document.querySelector("#resolved").textContent = ordered.length
    ? `Features: ${ordered.join(", ")}`
    : "Features: none";
  const automatic = ordered.filter(id => !state.requestedFeatures.has(id));
  document.querySelector("#dependency-note").textContent = automatic.length
    ? `Automatically resolved: ${automatic.join(", ")}`
    : "No transitive feature dependencies were added.";
  document.querySelector("#command").textContent = commandText();
}

async function start() {
  const response = await fetch("catalog.json", {cache: "no-store"});
  if (!response.ok) throw new Error(`catalog load failed: ${response.status}`);
  state.catalog = await response.json();
  state.requestedFeatures = new Set(state.catalog.defaults.features);
  state.selectedPlugins = new Set(state.catalog.defaults.plugins);
  const firmware = document.querySelector("#firmware-ref");
  for (const ref of state.catalog.firmware_refs) {
    const option = document.createElement("option");
    option.value = ref;
    option.textContent = ref;
    firmware.append(option);
  }
  firmware.addEventListener("change", render);
  document.querySelector("#copy").addEventListener("click", async () => {
    await navigator.clipboard.writeText(document.querySelector("#command").textContent);
    document.querySelector("#copy-status").textContent = "Command copied.";
  });
  render();
}

start().catch(error => {
  document.querySelector("#resolved").textContent = error.message;
});
