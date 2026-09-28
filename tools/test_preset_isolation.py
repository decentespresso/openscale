from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CHECK = r'''
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const storage = new Map();
const elements = new Map();
for (const id of ['objectName', 'targetWeight', 'goalWeight', 'lowThreshold', 'highThreshold', 'minWeight']) {
  elements.set(id, {value: 'unchanged'});
}
const context = vm.createContext({
  console: {log() {}, error() {}, warn() {}}, alert() {},
  document: {getElementById(id) { return elements.get(id); }},
  localStorage: {getItem(key) { return storage.get(key) ?? null; }, setItem(key, value) { storage.set(key, value); }}
});
(async () => {
  const assets = process.argv[1];
  const module = new vm.SourceTextModule(fs.readFileSync(path.join(assets, 'shared/modules/presets.js'), 'utf8'), {context});
  await module.link(() => { throw new Error('Unexpected import'); });
  await module.evaluate();
  const dosing = new module.namespace.PresetManager();
  vm.runInContext(fs.readFileSync(path.join(assets, 'Quality_Control_Assistant/quality_control.js'), 'utf8') + '\nglobalThis.QC = DecentScale;', context);
  const qc = Object.create(context.QC.prototype);
  const dose = {name: 'Dose', settings: {targetWeight: 10, lowThreshold: 9, highThreshold: 11}};
  const quality = {name: 'Quality', settings: {goalWeight: 20, lowThreshold: 19, highThreshold: 21, minWeight: 2}};
  const legacy = JSON.stringify({Dose: dose, Quality: quality.settings, Broken: {settings: {targetWeight: null}}});
  storage.set('decentScalePresets', legacy);
  storage.set('lastUsedPreset', 'Quality');
  assert.deepEqual(Object.keys(dosing.getPresets()), ['Dose']);
  assert.deepEqual(Object.keys(qc.getPresets()), ['Quality']);
  qc.loadPreset('Dose');
  assert.equal(elements.get('goalWeight').value, 'unchanged');
  assert.equal(dosing.savePreset({...dose, name: 'Coffee'}), true);
  assert.equal(qc.savePreset({...quality, name: 'Coffee'}), true);
  dosing.loadPreset('Coffee');
  assert.equal(elements.get('targetWeight').value, 10);
  qc.loadPreset('Coffee');
  assert.equal(elements.get('goalWeight').value, 20);
  assert.equal(storage.get('decentScalePresets'), legacy);
  assert.equal(storage.get('lastUsedPreset'), 'Quality');
  assert.equal(storage.get('lastUsedDosingPreset'), 'Coffee');
  assert.equal(storage.get('lastUsedQCPreset'), 'Coffee');
  assert.equal(dosing.getPresets().Dose.settings.targetWeight, 10);
  assert.equal(qc.getPresets().Quality.goalWeight, 20);
  assert.equal(dosing.savePreset({name: 'bad', settings: {targetWeight: NaN}}), false);
  assert.equal(qc.savePreset(dose), false);
  const reloadedDosing = new module.namespace.PresetManager();
  const reloadedQC = Object.create(context.QC.prototype);
  assert.equal(reloadedDosing.getPreset('Coffee').settings.targetWeight, 10);
  assert.equal(reloadedQC.getPreset('Coffee').goalWeight, 20);
  for (const invalid of ['null', '[]', '{', '{"broken":null}']) {
    storage.set('decentScaleDosingPresets', invalid);
    storage.set('decentScaleQCPresets', invalid);
    assert.equal(Object.keys(dosing.getPresets()).length, 0);
    assert.equal(Object.keys(qc.getPresets()).length, 0);
  }
  console.log('Preset schemas stay isolated, legacy valid entries survive, and invalid settings do not load');
})().catch(error => { console.error(error); process.exitCode = 1; });
'''


if __name__ == "__main__":
    subprocess.run(["node", "--experimental-vm-modules", "-e", CHECK,
                    str(ROOT / "plugins/default-web-apps/assets")], check=True)
