from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CHECK = r'''
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
let now = 100000;
let nextId = 0;
const ui = new Proxy({}, {get() { return () => {}; }});
const context = vm.createContext({
  Date: class extends Date { static now() { return now; } },
  console: {log() {}, error() {}},
  setInterval() { return ++nextId; }, clearInterval() {}
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
  const {DecentScale} = (await load(path.join(app, 'modules/scale.js'))).namespace;
  const {TimerManager} = (await load(path.join(app, 'modules/timer.js'))).namespace;
  const timer = new TimerManager();
  timer.setUIController(ui);
  const scale = new DecentScale(ui, timer);
  timer.startTimer(1);
  scale.processWeight(10);
  now += 1000;
  scale.processWeight(10);
  assert.equal(scale.weightData.length, 1);
  now += 100;
  scale.processWeight(0);
  now += 1000;
  scale.processWeight(10);
  assert.equal(scale.weightData.length, 2);
  assert.equal(scale.weightData[1].rate, '0.0');
  timer.stopTimer();
  const main = fs.readFileSync(path.join(app, 'main.js'), 'utf8');
  const handler = main.split("document.getElementById('toggleTimer')?.addEventListener('click', () => {")[1].split('});')[0];
  context.scale = scale;
  context.ui = {toggleTimer() { timer.startTimer(1); }};
  vm.runInContext(handler, context);
  now += 1000;
  scale.processWeight(10);
  assert.equal(scale.weightData.length, 3);
  assert.equal(scale.weightData[2].rate, '0.0');
  console.log('Weigh Save preserves duplicate suppression but records equal-weight new objects/runs');
})().catch(error => { console.error(error); process.exitCode = 1; });
'''


if __name__ == "__main__":
    subprocess.run(["node", "--experimental-vm-modules", "-e", CHECK,
                    str(ROOT / "plugins/default-web-apps/assets/Weigh_Save")], check=True)
