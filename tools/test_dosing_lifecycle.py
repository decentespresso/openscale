from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CHECK = r'''
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
let now = 0;
const timers = new Map();
let timerId = 0;
const ui = new Proxy({}, {get() { return () => {}; }});
const context = vm.createContext({
  Date: class extends Date { static now() { return now; } },
  console: {log() {}, warn() {}, error() {}},
  setTimeout(callback) { const id = ++timerId; timers.set(id, callback); return id; },
  clearTimeout(id) { timers.delete(id); },
  WebSocket: {OPEN: 1}
});
const modules = new Map();
async function load(file) {
  if (modules.has(file)) return modules.get(file);
  const module = new vm.SourceTextModule(fs.readFileSync(file, 'utf8'), {context, identifier: file});
  modules.set(file, module);
  await module.link((specifier, parent) => load(path.resolve(path.dirname(parent.identifier), specifier)));
  await module.evaluate();
  return module;
}
(async () => {
  const app = process.argv[1];
  const {StateMachine} = (await load(path.join(app, 'modules/state-machine.js'))).namespace;
  const {DecentScale} = (await load(path.join(app, 'modules/scale.js'))).namespace;
  function scale() {
    now = 0;
    const result = new DecentScale(ui, new StateMachine(ui));
    result.dosingSettings = {targetWeight: 10, lowThreshold: 9, highThreshold: 11};
    result.dosingMode = true;
    result.soundEnabled = false;
    result.ws = {readyState: 1, send() {}};
    return result;
  }
  function sample(target, weight, advance = 500) {
    now += advance;
    target.handleWebSocketWeight(weight);
  }
  const ramp = scale();
  for (let index = 0; index < 20; ++index) sample(ramp, 1 + index * 0.2);
  assert.equal(ramp.weightData.length, 0);
  const settled = scale();
  for (let index = 0; index < 7; ++index) sample(settled, index === 6 ? 10.1 : 10);
  assert.equal(settled.weightData.length, 1);
  assert.equal(settled.weightData[0].weight, '10.1');
  sample(settled, 10.1);
  assert.equal(settled.weightData.length, 1);
  for (const event of ['stop', 'close', 'error']) {
    const stopped = scale();
    sample(stopped, 10);
    sample(stopped, 10);
    if (event === 'stop') stopped.toggleDosingMode();
    else {
      const main = fs.readFileSync(path.join(app, 'main.js'), 'utf8');
      const handler = main.split(`ws.addEventListener('${event}', () => {`)[1].split('});')[0];
      context.scale = stopped;
      context.ui = ui;
      vm.runInContext(handler, context);
    }
    now += 10000;
    for (const callback of timers.values()) callback();
    timers.clear();
    sample(stopped, 10);
    assert.equal(stopped.weightData.length, 0);
    assert.equal(stopped.dosingMode, false);
    stopped.startDosingAutomatically();
    sample(stopped, 10);
    sample(stopped, 10);
    assert.equal(stopped.weightData.length, 0);
  }
  const gap = scale();
  sample(gap, 10);
  sample(gap, 10);
  sample(gap, 10, 10000);
  assert.equal(gap.weightData.length, 0);
  for (const removedWeight of [0, -10]) {
    const removed = scale();
    for (let index = 0; index < 6; ++index) sample(removed, 10);
    sample(removed, removedWeight);
    now += 5000;
    for (const callback of timers.values()) callback();
    assert.equal(removed.weightData.length, 0);
    assert.equal(removed.stateMachine.stabilityWindow, null);
  }
  console.log('Dosing rejects ramps/gaps and saves current stable samples only in active sessions');
})().catch(error => { console.error(error); process.exitCode = 1; });
'''


if __name__ == "__main__":
    subprocess.run(["node", "--experimental-vm-modules", "-e", CHECK,
                    str(ROOT / "plugins/default-web-apps/assets/dosing_assistant")], check=True)
