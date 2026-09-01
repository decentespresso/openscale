const storageKey = "hds-custom-build-fleet-v1";
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

const loadStoredKey = () => {
  try {
    const stored = JSON.parse(localStorage.getItem(storageKey));
    const key = normalizedFleetKey(stored?.key);
    return stored?.version === 1 && fleetPattern.test(key) ? key : "";
  } catch {
    return "";
  }
};

const saveKey = key => localStorage.setItem(storageKey, JSON.stringify({version: 1, key}));

export function initFleet({apiBase, getReadyHash, showToast}) {
  const onboarding = document.querySelector("#fleet-onboarding");
  const consoleRoot = document.querySelector("#fleet-console");
  const settings = document.querySelector("#fleet-settings");
  const settingsToggle = document.querySelector("#fleet-settings-toggle");
  const useFleet = document.querySelector("#use-fleet");
  const recovery = document.querySelector("#fleet-recovery");
  const recoveryKey = document.querySelector("#fleet-recovery-key");
  const scaleList = document.querySelector("#fleet-scales");
  const fleetStatus = document.querySelector("#fleet-status");
  const pairInput = document.querySelector("#pair-code");
  const existingInput = document.querySelector("#existing-fleet-key");
  let fleetKey = loadStoredKey();
  let generation = 0;

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
    if (!response.ok) throw new Error(result.error || `request failed: ${response.status}`);
    return result;
  };

  const setMode = linked => {
    onboarding.hidden = linked;
    consoleRoot.hidden = !linked;
    useFleet.hidden = linked;
    recovery.hidden = !linked;
    recoveryKey.value = formattedFleetKey(fleetKey);
  };

  const statusLabel = state => ({
    ready: "Ready",
    queued: "Waiting to build",
    building: "Building",
    failed: "Build failed",
    missing: "Build unavailable",
  })[state] || "Not assigned";

  const scaleRow = scale => {
    const row = document.createElement("li");
    row.className = "scale-row";
    const assignment = scale.assignment;
    const readyHash = getReadyHash();
    row.innerHTML = `
      <div class="scale-identity">
        <input class="scale-name" aria-label="Scale name" maxlength="40" value="">
        <span class="scale-hint"></span>
      </div>
      <div class="scale-assignment">
        <span class="scale-build-state"></span>
        <code></code>
      </div>
      <div class="scale-actions">
        <button class="ghost-button save-scale" type="button">Save name</button>
        <button class="primary-button assign-scale" type="button">Use this build</button>
      </div>`;
    row.querySelector(".scale-name").value = scale.name;
    row.querySelector(".scale-hint").textContent = scale.serial_hint;
    row.querySelector(".scale-build-state").textContent = statusLabel(assignment?.state);
    row.querySelector("code").textContent = scale.desired_combination?.slice(0, 8).toUpperCase() || "";
    const assign = row.querySelector(".assign-scale");
    row.dataset.assignedCombination = scale.desired_combination || "";
    assign.disabled = !readyHash || readyHash === scale.desired_combination;
    assign.textContent = readyHash === scale.desired_combination ? "Build assigned" : "Use this build";
    row.querySelector(".save-scale").addEventListener("click", async () => {
      const name = row.querySelector(".scale-name").value.trim();
      try {
        await api(`/api/v1/fleet/scales/${scale.device_id}`, {
          method: "PATCH",
          body: JSON.stringify({name}),
        });
        showToast("Scale name saved");
        await loadScales();
      } catch {
        showToast("Scale name could not be saved");
      }
    });
    assign.addEventListener("click", async () => {
      const hash = getReadyHash();
      if (!hash) return;
      assign.disabled = true;
      try {
        await api(`/api/v1/fleet/scales/${scale.device_id}`, {
          method: "PATCH",
          body: JSON.stringify({desired_combination: hash}),
        });
        showToast("Build assigned to scale");
        await loadScales();
      } catch {
        assign.disabled = false;
        showToast("Build could not be assigned");
      }
    });
    return row;
  };

  const loadScales = async () => {
    const currentGeneration = ++generation;
    fleetStatus.textContent = "Loading scales";
    try {
      const result = await api("/api/v1/fleet/scales");
      const scales = await Promise.all(result.scales.map(async scale => {
        if (!scale.desired_combination) return {...scale, assignment: null};
        try {
          const response = await fetch(`${apiBase}/api/v1/status/${scale.desired_combination}`);
          return {...scale, assignment: response.ok ? await response.json() : null};
        } catch {
          return {...scale, assignment: null};
        }
      }));
      if (currentGeneration !== generation) return;
      scaleList.replaceChildren(...scales.map(scaleRow));
      fleetStatus.textContent = scales.length ? "" : "No scales linked yet.";
    } catch {
      if (currentGeneration !== generation) return;
      fleetStatus.textContent = "Scales could not be loaded.";
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
    loadScales();
    return true;
  };

  document.querySelector("#start-fleet").addEventListener("click", () => {
    const key = encodeBase32(crypto.getRandomValues(new Uint8Array(20)));
    if (activateKey(key)) showToast("Recovery key created");
  });
  settingsToggle.addEventListener("click", () => setSettingsOpen(settings.hidden));
  document.querySelector("#use-fleet").addEventListener("submit", event => {
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
  document.querySelector("#forget-fleet").addEventListener("click", () => {
    if (!confirm("Forget this fleet key in this browser? Linked scales will not be deleted.")) return;
    localStorage.removeItem(storageKey);
    fleetKey = "";
    generation += 1;
    scaleList.replaceChildren();
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
      await loadScales();
    } catch {
      showToast("Pairing code is invalid or expired");
    }
  });
  document.addEventListener("openscale-build-status", () => {
    const readyHash = getReadyHash();
    scaleList.querySelectorAll(".scale-row").forEach(row => {
      const assign = row.querySelector(".assign-scale");
      assign.disabled = !readyHash || readyHash === row.dataset.assignedCombination;
      assign.textContent = readyHash === row.dataset.assignedCombination ? "Build assigned" : "Use this build";
    });
  });

  setMode(Boolean(fleetKey));
  if (fleetKey) loadScales();
}
