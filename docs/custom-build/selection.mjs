const idPattern = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;

export class SelectionError extends Error {
  constructor(code, details = {}) {
    super(code);
    this.code = code;
    this.details = details;
  }
}

function catalogMaps(catalog) {
  return {
    features: new Map(catalog.features.map(item => [item.id, item])),
    plugins: new Map(catalog.plugins.map(item => [item.id, item])),
  };
}

function requireIds(values, field) {
  if (!Array.isArray(values) || values.some(value => typeof value !== "string" || !idPattern.test(value)) ||
      values.length !== new Set(values).size) {
    throw new SelectionError(`invalid_${field}`);
  }
  return [...values].sort();
}

export function defaultSelection(catalog) {
  return {
    firmware_ref: catalog.firmware_refs[0],
    features: catalog.features.filter(item => item.default).map(item => item.id).sort(),
    plugins: catalog.plugins.filter(item => item.default).map(item => item.id).sort(),
  };
}

export function resolveSelection(catalog, selection) {
  const maps = catalogMaps(catalog);
  const firmwareRef = selection.firmware_ref;
  if (!catalog.firmware_refs.includes(firmwareRef)) {
    throw new SelectionError("unsupported_firmware_ref", {firmwareRef});
  }
  const requestedFeatures = requireIds(selection.features, "features");
  const requestedPlugins = requireIds(selection.plugins, "plugins");
  const resolvedPlugins = new Set();
  const visiting = new Set();
  const visitPlugin = id => {
    const plugin = maps.plugins.get(id);
    if (!plugin) throw new SelectionError("unknown_plugin", {id});
    if (visiting.has(id)) throw new SelectionError("invalid_catalog");
    if (resolvedPlugins.has(id)) return;
    if (!plugin.firmware_refs.includes(firmwareRef)) {
      throw new SelectionError("unsupported_plugin_ref", {id, firmwareRef});
    }
    visiting.add(id);
    plugin.depends_on.forEach(visitPlugin);
    visiting.delete(id);
    resolvedPlugins.add(id);
  };
  requestedPlugins.forEach(visitPlugin);

  const resolvedFeatures = new Set(requestedFeatures);
  resolvedPlugins.forEach(id => maps.plugins.get(id).requires.forEach(feature => resolvedFeatures.add(feature)));
  let previousSize = -1;
  while (resolvedFeatures.size !== previousSize) {
    previousSize = resolvedFeatures.size;
    [...resolvedFeatures].forEach(id => {
      const feature = maps.features.get(id);
      if (!feature) throw new SelectionError("unknown_feature", {id});
      feature.requires.forEach(required => resolvedFeatures.add(required));
    });
  }
  resolvedFeatures.forEach(id => {
    const feature = maps.features.get(id);
    if (!feature.firmware_refs.includes(firmwareRef)) {
      throw new SelectionError("unsupported_feature_ref", {id, firmwareRef});
    }
  });

  const pluginIds = [...resolvedPlugins].sort();
  const featureIds = [...resolvedFeatures].sort();
  for (const id of pluginIds) {
    const plugin = maps.plugins.get(id);
    const otherPluginId = plugin.conflicts.find(conflict => resolvedPlugins.has(conflict));
    if (otherPluginId) {
      throw new SelectionError("plugin_conflict", {pluginId: id, otherPluginId});
    }
    const featureId = (plugin.conflicts_features || []).find(feature => resolvedFeatures.has(feature));
    if (featureId) throw new SelectionError("feature_conflict", {pluginId: id, featureId});
  }
  return {firmware_ref: firmwareRef, features: featureIds, plugins: pluginIds};
}

function itemName(catalog, kind, id) {
  const items = kind === "plugin" ? catalog.plugins : catalog.features;
  return items.find(item => item.id === id)?.name || id;
}

function conflictReason(catalog, error, kind, id, current) {
  if (error.code === "unsupported_plugin_ref") {
    const name = itemName(catalog, "plugin", error.details.id);
    return error.details.id === id ? `Unavailable for ${current.firmware_ref}` :
      `Requires ${name}, unavailable for ${current.firmware_ref}`;
  }
  if (error.code === "unsupported_feature_ref") {
    const name = itemName(catalog, "feature", error.details.id);
    return error.details.id === id ? `Unavailable for ${current.firmware_ref}` :
      `Requires ${name}, unavailable for ${current.firmware_ref}`;
  }
  const resolved = resolveSelection(catalog, current);
  if (error.code === "plugin_conflict") {
    const pair = [error.details.pluginId, error.details.otherPluginId];
    const introduced = pair.find(pluginId => !resolved.plugins.includes(pluginId));
    const existing = pair.find(pluginId => pluginId !== introduced);
    if (kind === "plugin" && introduced === id) {
      return `Conflicts with ${itemName(catalog, "plugin", existing)}`;
    }
    if (introduced) {
      return `Requires ${itemName(catalog, "plugin", introduced)}, which conflicts with ${itemName(catalog, "plugin", existing)}`;
    }
  }
  if (error.code === "feature_conflict") {
    const {pluginId, featureId} = error.details;
    const pluginIsNew = !resolved.plugins.includes(pluginId);
    const featureIsNew = !resolved.features.includes(featureId);
    if (featureIsNew && !pluginIsNew) {
      return kind === "feature" && featureId === id
        ? `Conflicts with ${itemName(catalog, "plugin", pluginId)}`
        : `Requires ${itemName(catalog, "feature", featureId)}, which conflicts with ${itemName(catalog, "plugin", pluginId)}`;
    }
    if (pluginIsNew && !featureIsNew) {
      return kind === "plugin" && pluginId === id
        ? `Conflicts with ${itemName(catalog, "feature", featureId)}`
        : `Requires ${itemName(catalog, "plugin", pluginId)}, which conflicts with ${itemName(catalog, "feature", featureId)}`;
    }
  }
  return "Unavailable with the current selection";
}

export function optionReason(catalog, current, kind, id) {
  const resolved = resolveSelection(catalog, current);
  if ((kind === "plugin" ? resolved.plugins : resolved.features).includes(id)) return "";
  const candidate = {
    ...current,
    features: kind === "feature" ? [...current.features, id] : current.features,
    plugins: kind === "plugin" ? [...current.plugins, id] : current.plugins,
  };
  try {
    resolveSelection(catalog, candidate);
    return "";
  } catch (error) {
    if (!(error instanceof SelectionError)) throw error;
    return conflictReason(catalog, error, kind, id, current);
  }
}

function parseList(value) {
  if (typeof value !== "string") throw new SelectionError("invalid_url");
  if (value === "") return [];
  const items = value.split(",");
  if (items.some(item => !idPattern.test(item)) || items.length !== new Set(items).size) {
    throw new SelectionError("invalid_url");
  }
  return items.sort();
}

export function parseSelection(search, catalog) {
  const params = new URLSearchParams(search);
  if ([...params.keys()].sort().join(",") !== "features,plugins,ref") {
    throw new SelectionError("invalid_url");
  }
  const selection = {
    firmware_ref: params.get("ref"),
    features: parseList(params.get("features")),
    plugins: parseList(params.get("plugins")),
  };
  const maps = catalogMaps(catalog);
  if (selection.features.some(id => !maps.features.has(id) || maps.features.get(id).hidden) ||
      selection.plugins.some(id => !maps.plugins.has(id))) {
    throw new SelectionError("invalid_url");
  }
  resolveSelection(catalog, selection);
  return selection;
}

export function selectionQuery(selection) {
  const params = new URLSearchParams();
  params.set("ref", selection.firmware_ref);
  params.set("features", [...selection.features].sort().join(","));
  params.set("plugins", [...selection.plugins].sort().join(","));
  return `?${params}`;
}
