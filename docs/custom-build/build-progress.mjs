export const elapsedTime = milliseconds => {
  const seconds = Math.max(0, Math.floor(milliseconds / 1000));
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
};

export const initBuildProgress = root => {
  const steps = [...root.querySelectorAll("[data-build-step]")];
  const elapsed = root.querySelector("[data-build-elapsed]");
  const checked = root.querySelector("[data-build-checked]");
  let latest = null;
  let checkedAt = 0;
  let timer;
  const renderTime = () => {
    if (!latest) return;
    const startedAt = Date.parse(latest.updated_at);
    elapsed.textContent = Number.isFinite(startedAt)
      ? `${latest.state === "queued" ? "Queued" : "Elapsed"} ${elapsedTime(Date.now() - startedAt)}` : "";
    checked.textContent = latest.connectionLost ? "Connection interrupted; retrying"
      : latest.pollingPaused ? "Automatic status checks paused"
      : `Status checked ${Math.max(0, Math.floor((Date.now() - checkedAt) / 1000))}s ago`;
  };
  return result => {
    clearInterval(timer);
    latest = result;
    if (!result.connectionLost) checkedAt = Date.now();
    const active = ["queued", "building"].includes(result.state);
    root.hidden = !active && result.state !== "ready";
    root.dataset.active = String(active && !result.connectionLost && !result.pollingPaused);
    root.querySelector(".build-activity").hidden = !active;
    const current = ["queued", "building", "ready"].indexOf(result.state);
    steps.forEach((step, index) => {
      step.dataset.state = index < current ? "complete" : index === current ? "current" : "pending";
      if (index === current) step.setAttribute("aria-current", "step");
      else step.removeAttribute("aria-current");
    });
    root.querySelector(".build-live-details").hidden = !active;
    if (active) {
      renderTime();
      timer = setInterval(renderTime, 1000);
    }
  };
};
