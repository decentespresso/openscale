import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {test} from "node:test";
import {runInNewContext} from "node:vm";

test("wizard navigates WiFi and USB steps without performing build actions", async () => {
  const elements = new Map();
  const element = key => {
    if (!elements.has(key)) {
      const classes = new Set();
      elements.set(key, {
        events: {}, attributes: {},
        classList: {add: value => classes.add(value), remove: value => classes.delete(value)},
        setAttribute(name, value) { this.attributes[name] = value; },
        addEventListener(name, handler) { this.events[name] = handler; },
        prepend(child) { this.child = child; },
        append() {},
        closest() { return element(`${key}-panel`); },
        querySelector: element,
        focus() { document.activeElement = this; },
        scrollIntoView() {},
      });
    }
    return elements.get(key);
  };
  const radio = element("radio");
  radio.value = "wifi";
  const document = {
    body: element("body"),
    querySelector: selector => selector.includes(":checked") ? radio : element(selector),
    querySelectorAll: () => [radio],
    getElementById: element,
    createElement: name => element(`created-${name}`),
    addEventListener: (name, handler) => element("document").addEventListener(name, handler),
  };
  const source = await readFile(new URL("../../../docs/custom-build/wizard.js", import.meta.url), "utf8");
  runInNewContext(source, {document});
  const click = selector => element(selector).events.click();
  const progress = element(".wizard-progress");
  const guide = element("created-div");
  click("#start-wizard");
  for (let step = 1; step <= 5; step++) {
    assert.equal(progress.textContent, `Step ${step} of 5`);
    click('[data-action="next"]');
  }
  assert.equal(guide.hidden, true);
  assert.equal(document.activeElement, element("#start-wizard"));
  radio.value = "usb";
  click("#start-wizard");
  click('[data-action="next"]');
  click('[data-action="back"]');
  assert.equal(progress.textContent, "Step 1 of 4");
  radio.value = "wifi";
  radio.events.change();
  assert.equal(progress.textContent, "Step 1 of 5");
  for (let step = 1; step < 5; step++) click('[data-action="next"]');
  radio.value = "usb";
  radio.events.change();
  assert.equal(progress.textContent, "Step 4 of 4");
  assert.equal(element('[data-action="next"]').textContent, "Finish");
  element("document").events.keydown({key: "Escape"});
  assert.equal(guide.hidden, true);
  assert.equal(element("#start-wizard").attributes["aria-expanded"], "false");
});
