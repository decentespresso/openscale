export const buildTimeRange = selection => {
  if (selection?.features?.includes("energy-menu")) return [10, 12];
  if (selection?.plugins?.length) return [6, 9];
  return [5, 7];
};

export const buildEstimate = (result, selection, now = Date.now()) => {
  const [minMinutes, maxMinutes] = buildTimeRange(selection);
  if (result.state === "queued") {
    return `Waiting for a build runner. Once started, this build usually takes ${minMinutes}-${maxMinutes} minutes.`;
  }
  if (result.state !== "building" || !result.updated_at) return "";
  const startedAt = Date.parse(result.updated_at);
  if (!Number.isFinite(startedAt)) return "";
  const elapsedMinutes = Math.max(0, Math.floor((now - startedAt) / 60000));
  if (elapsedMinutes < minMinutes) {
    return `${elapsedMinutes} min elapsed. Typical build time: ${minMinutes}-${maxMinutes} min.`;
  }
  if (elapsedMinutes <= maxMinutes) {
    return `${elapsedMinutes} min elapsed. Expected to finish soon.`;
  }
  return `${elapsedMinutes} min elapsed. Taking longer than usual, but the build is still running.`;
};
