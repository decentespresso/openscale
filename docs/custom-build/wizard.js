const start = document.querySelector("#start-wizard");
const steps = [
  ["firmware-heading", "Choose the source version for your custom firmware, then choose WiFi or USB installation. Your build includes the features and plugins you select, not necessarily everything in the official firmware. For everyday use, choose a stable release as the starting point. main is unfinished development code: it may contain untested changes or bugs and is intended for testing only."],
  ["features-heading", "Choose the features you need. Required dependencies are selected automatically and cannot be removed while another selection needs them."],
  ["plugins-heading", "Choose optional approved plugins. Only plugins compatible with your firmware version are available."],
  ["summary-heading", "Review your selection and request the build when ready. Wait for it to finish, then download the ZIP for USB or save the build for your scales."],
  ["fleet-heading", "Add your scale using its pairing code and keep your recovery key safe. Save the ready build, select your scale and assign the build. On the scale, open Custom Build to confirm installation."]
].map(([id, description], index) => {
  const heading = document.getElementById(id);
  const number = document.createElement("span");
  number.className = "wizard-step-number";
  number.textContent = `Step ${index + 1}`;
  heading.prepend(number);
  return {heading, description, panel: heading.closest("section.panel")};
});
const guide = document.createElement("div");
guide.id = "build-wizard";
guide.className = "wizard-guide";
guide.hidden = true;
guide.setAttribute("role", "region");
guide.setAttribute("aria-label", "Build wizard");
guide.innerHTML = `<p class="wizard-progress" tabindex="-1"></p>
  <p class="wizard-explanation"></p>
  <div class="wizard-actions">
    <button type="button" class="ghost-bordered-button" data-action="back">Back</button>
    <button type="button" class="primary-button" data-action="next">Next</button>
    <button type="button" class="ghost-button" data-action="close">Close</button>
  </div>`;
document.body.append(guide);
const progress = guide.querySelector(".wizard-progress");
const explanation = guide.querySelector(".wizard-explanation");
const back = guide.querySelector('[data-action="back"]');
const next = guide.querySelector('[data-action="next"]');
let current = -1;
const stepCount = () => document.querySelector('[name="install-method"]:checked')?.value === "usb" ? 4 : 5;
const show = index => {
  steps.forEach(step => step.panel.classList.remove("wizard-active"));
  current = Math.min(index, stepCount() - 1);
  const step = steps[current];
  step.panel.classList.add("wizard-active");
  step.panel.prepend(guide);
  guide.hidden = false;
  start.setAttribute("aria-expanded", "true");
  progress.textContent = `Step ${current + 1} of ${stepCount()}`;
  explanation.textContent = step.description;
  back.disabled = current === 0;
  next.textContent = current === stepCount() - 1 ? "Finish" : "Next";
  progress.focus({preventScroll: true});
  guide.scrollIntoView({block: "start", behavior: "instant"});
};
const close = () => {
  current = -1;
  guide.hidden = true;
  steps.forEach(step => step.panel.classList.remove("wizard-active"));
  start.setAttribute("aria-expanded", "false");
  start.focus();
};
start.addEventListener("click", () => current < 0 ? show(0) : close());
back.addEventListener("click", () => show(current - 1));
next.addEventListener("click", () => current === stepCount() - 1 ? close() : show(current + 1));
guide.querySelector('[data-action="close"]').addEventListener("click", close);
document.addEventListener("keydown", event => {
  if (event.key === "Escape" && current >= 0) close();
});
document.querySelectorAll('[name="install-method"]').forEach(input => {
  input.addEventListener("change", () => {
    if (current >= 0) show(current);
  });
});
