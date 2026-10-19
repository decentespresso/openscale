(() => {
  const storageKey = "hds-web-theme-v1";
  const legacyStorageKey = "hds-custom-build-theme-v1";
  const root = document.documentElement;
  const systemTheme = matchMedia("(prefers-color-scheme: dark)");
  const toggles = [];

  const readPreference = key => {
    try {
      const stored = JSON.parse(localStorage.getItem(key));
      return stored?.version === 1 && ["system", "light", "dark"].includes(stored.preference)
        ? stored.preference : null;
    } catch {
      return null;
    }
  };

  const writePreference = preference => {
    try {
      localStorage.setItem(storageKey, JSON.stringify({version: 1, preference}));
    } catch {
    }
  };

  const currentPreference = readPreference(storageKey);
  const legacyPreference = currentPreference === null ? readPreference(legacyStorageKey) : null;
  let preference = currentPreference ?? legacyPreference ?? "system";
  if (legacyPreference !== null) writePreference(legacyPreference);

  const applyTheme = () => {
    const dark = preference === "dark" || preference === "system" && systemTheme.matches;
    root.dataset.themePreference = preference;
    root.dataset.theme = dark ? "dark" : "light";
    const themeColor = document.querySelector('meta[name="theme-color"]');
    if (themeColor) themeColor.content = dark ? "#111413" : "#0d6b4f";
    const current = preference === "system" ? "System" : dark ? "Dark" : "Light";
    const label = `${current} theme; switch to ${dark ? "light" : "dark"} theme`;
    toggles.forEach(toggle => {
      toggle.setAttribute("aria-pressed", String(dark));
      toggle.setAttribute("aria-label", label);
      toggle.title = label;
    });
  };

  const toggleSvg = `<svg aria-hidden="true" viewBox="0 0 68 32">
    <rect class="theme-toggle-track" x="1" y="1" width="66" height="30" rx="15"/>
    <g class="theme-toggle-stars">
      <circle cx="10" cy="9" r="1.2"/>
      <circle cx="24" cy="7" r="1"/>
      <circle cx="29" cy="21" r="1.3"/>
      <path d="M17 18h4M19 16v4"/>
    </g>
    <path class="theme-toggle-cloud" d="M43 21h14.5a3.5 3.5 0 0 0 .3-7 6 6 0 0 0-11.5-1.3A4.2 4.2 0 0 0 43 21Z"/>
    <g class="theme-toggle-knob">
      <circle class="theme-toggle-thumb" cx="16" cy="16" r="12"/>
      <g class="theme-toggle-sun">
        <circle cx="16" cy="16" r="4.5"/>
        <path d="M16 7.5v2M16 22.5v2M7.5 16h2M22.5 16h2M10 10l1.4 1.4M20.6 20.6 22 22M10 22l1.4-1.4M20.6 11.4 22 10"/>
      </g>
      <path class="theme-toggle-moon" d="M20.8 19.8A7 7 0 0 1 12.2 11a6.3 6.3 0 1 0 8.6 8.8Z"/>
    </g>
  </svg>`;

  const initToggles = () => {
    document.querySelectorAll("[data-hds-theme-toggle]").forEach(slot => {
      const toggle = document.createElement("button");
      toggle.type = "button";
      toggle.className = "theme-toggle";
      toggle.innerHTML = toggleSvg;
      toggle.addEventListener("click", () => {
        preference = root.dataset.theme === "dark" ? "light" : "dark";
        writePreference(preference);
        applyTheme();
      });
      slot.replaceChildren(toggle);
      toggles.push(toggle);
    });
    applyTheme();
  };

  applyTheme();
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initToggles, {once: true});
  } else {
    initToggles();
  }
  systemTheme.addEventListener("change", () => {
    if (preference === "system") applyTheme();
  });
  window.addEventListener("storage", event => {
    if (event.key === storageKey) {
      preference = readPreference(storageKey) ?? "system";
      applyTheme();
    }
  });
})();
