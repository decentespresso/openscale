from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CHECK = r'''
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const storage = new Map();
const warnings = [];
let failRead = false;
let failWrite = false;
const context = vm.createContext({
  console: {log() {}, error() {}, warn() {}},
  crypto: require('node:crypto').webcrypto,
  alert(message) { warnings.push(message); },
  localStorage: {
    get length() { if (failRead) throw new Error('Denied'); return storage.size; },
    key(index) { return [...storage.keys()][index]; },
    getItem(key) { return storage.get(key) ?? null; },
    setItem(key, value) { if (failWrite) throw new Error('Quota'); storage.set(key, value); }
  },
  window: {location: {host: 'scale.test'}},
  ReconnectingWebSocket: class { addEventListener() {} }
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
  const assets = process.argv[1];
  const {DecentScale} = (await load(path.join(assets, 'dosing_assistant/modules/scale.js'))).namespace;
  const {DataExport} = (await load(path.join(assets, 'dosing_assistant/modules/export.js'))).namespace;
  function dosing() {
    const scale = new DecentScale({displayWeightReadings() {}}, {});
    scale.dosingSettings = {targetWeight: 10, lowThreshold: 9, highThreshold: 11};
    return scale;
  }
  const first = dosing();
  const concurrent = dosing();
  first.saveDosing(10);
  concurrent.saveDosing(11);
  const restored = dosing();
  assert.equal(restored.weightData.length, 2);
  assert.deepEqual(Array.from(restored.weightData, row => row.weight).sort(), ['10.0', '11.0']);
  assert.equal(restored.readingCount, 2);
  assert.equal(restored.weightReadings.length, 2);
  assert.equal(JSON.parse(DataExport.exportToJSON(restored.weightData).content).total_readings, 2);
  restored.saveDosing(12);
  assert.equal(dosing().weightData.length, 3);
  const qcPath = path.join(assets, 'Quality_Control_Assistant/quality_control.js');
  const qcHtml = fs.readFileSync(path.join(assets, 'Quality_Control_Assistant/quality_control.html'), 'utf8');
  assert(qcHtml.indexOf('src="../shared/measurement-history.js"') < qcHtml.indexOf('src="quality_control.js"'));
  vm.runInContext(fs.readFileSync(qcPath, 'utf8') + `
    globalThis.QC = class extends DecentScale {
      initializeElements() {} addEventListeners() {} displayWeightReadings() {}
      loadPresets() {} setupPresetHandlers() {}
    };`, context);
  const qc = new context.QC();
  qc.qcSettings = {goalWeight: 10, lowThreshold: 9, highThreshold: 11, minWeight: 1, enableSounds: false};
  qc.saveMeasurement(10, 'pass');
  const qcReloaded = new context.QC();
  assert.equal(qcReloaded.weightData.length, 1);
  assert.equal(qcReloaded.weightReadings.length, 1);
  assert.equal(qcReloaded.weightData[0].weight, 10);
  assert.equal(qcReloaded.weightData[0].qcSettings.goalWeight, 10);
  assert.equal(dosing().weightData.length, 3);
  const validSize = storage.size;
  failWrite = true;
  restored.saveDosing(13);
  assert.equal(restored.weightData.length, 4);
  assert.equal(storage.size, validSize);
  assert.equal(JSON.parse(DataExport.exportToJSON(restored.weightData).content).total_readings, 4);
  assert(warnings.some(message => message.includes('Export readings')));
  failWrite = false;
  storage.set('hds.dosing.history.v1.broken', '{');
  storage.set('hds.dosing.history.v1.wrong-schema', '{"weight":null}');
  assert.equal(dosing().weightData.length, 3);
  assert.equal(storage.get('hds.dosing.history.v1.broken'), '{');
  failRead = true;
  assert.equal(dosing().weightData.length, 0);
  assert.equal(storage.size, validSize + 2);
  console.log('History reload/export, app isolation, concurrent writers, quota, denied reads and corruption passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
'''


if __name__ == "__main__":
    subprocess.run(["node", "--experimental-vm-modules", "-e", CHECK,
                    str(ROOT / "plugins/default-web-apps/assets")], check=True)
