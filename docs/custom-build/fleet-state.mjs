const hashPattern = /^[0-9a-f]{64}$/;
const offlineAfterMs = 7 * 24 * 60 * 60 * 1000;

export const shortHash = (value, length = 12) =>
  hashPattern.test(value || "") ? value.slice(0, length).toUpperCase() : "";

export const buildLabel = (build, index) => /^Build [0-9A-F]{12}$/i.test(build.label || "")
  ? `Build ${index + 1}` : build.label;

export function buildSummary(build) {
  const features = Array.isArray(build?.features) ? build.features : [];
  const plugins = Array.isArray(build?.plugins)
    ? build.plugins.map(plugin => plugin.version ? `${plugin.id} ${plugin.version}` : plugin.id)
    : [];
  const entries = [...features, ...plugins];
  if (!entries.length) return "Base firmware";
  return entries.join(", ");
}

export function deploymentState(scale, buildStates, now = Date.now()) {
  const lastSeen = Date.parse(scale.last_seen_at || "");
  if (Number.isFinite(lastSeen) && now - lastSeen > offlineAfterMs) return "Offline";
  const desired = scale.desired_combination;
  if (desired && desired === scale.installed_combination) return "Up to date";
  const buildState = buildStates[desired];
  if (["queued", "building"].includes(buildState)) return "Build preparing";
  if (desired && ["failed", "missing"].includes(buildState)) return "Build unavailable";
  if (desired) {
    const assigned = Date.parse(scale.desired_updated_at || "");
    return !Number.isFinite(lastSeen) || !Number.isFinite(assigned) || lastSeen < assigned
      ? "Update assigned" : "Install pending";
  }
  return Number.isFinite(lastSeen) || scale.installed_combination ? "No update assigned" : "Unknown";
}

export function lastSeenLabel(value, now = Date.now()) {
  if (!value) return "Never";
  const timestamp = Date.parse(value);
  if (!Number.isFinite(timestamp)) return "Unknown";
  const elapsedMinutes = Math.max(0, Math.floor((now - timestamp) / 60000));
  if (elapsedMinutes < 1) return "Just now";
  if (elapsedMinutes < 60) return `${elapsedMinutes}m ago`;
  const elapsedHours = Math.floor(elapsedMinutes / 60);
  if (elapsedHours < 24) return `${elapsedHours}h ago`;
  return `${Math.floor(elapsedHours / 24)}d ago`;
}
