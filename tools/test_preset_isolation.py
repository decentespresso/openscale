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
function resetElements() {
  for (const id of ['objectName', 'targetWeight', 'goalWeight', 'lowThreshold', 'highThreshold', 'minWeight']) {
    elements.set(id, {value: 'unchanged'});
  }
  elements.set('presetSelect', {
    value: '', options: [{value: ''}, {value: 'save_preset'}],
    remove(index) { this.options = this.options.filter((_, i) => i !== index); },
    appendChild(option) { this.options = [...this.options, option]; },
    querySelector() { return this.options.find(option => option.value === 'save_preset'); }
  });
}
resetElements();
const context = vm.createContext({
  console: {log() {}, error() {}, warn() {}}, alert() {},
  document: {getElementById(id) { return elements.get(id); }, createElement() { return {}; }},
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
  const apps = [
    {manager: dosing, preset: dose, storedPreset: dose, storeKey: 'decentScaleDosingPresets', lastKey: 'lastUsedDosingPreset', inputId: 'targetWeight', weight: 10},
    {manager: qc, preset: quality, storedPreset: quality.settings, storeKey: 'decentScaleQCPresets', lastKey: 'lastUsedQCPreset', inputId: 'goalWeight', weight: 20}
  ];
  for (const {manager, preset, storedPreset, storeKey, lastKey, inputId, weight} of apps) {
    storage.clear();
    resetElements();
    storage.set('decentScalePresets', legacy);
    storage.set('lastUsedPreset', preset.name);
    manager.loadPresets();
    assert.deepEqual(elements.get('presetSelect').options.map(option => option.value), ['', 'save_preset', preset.name]);
    assert.equal(elements.get('presetSelect').value, preset.name);
    assert.equal(elements.get(inputId).value, weight);
    assert.equal(storage.get(lastKey), preset.name);
    assert.equal(storage.has(storeKey), false);

    storage.delete(lastKey);
    const currentStore = JSON.stringify({version: 1, presets: {[preset.name]: storedPreset}});
    storage.set(storeKey, currentStore);
    resetElements();
    manager.loadPresets();
    assert.equal(elements.get(inputId).value, 'unchanged');
    assert.equal(elements.get('presetSelect').value, '');
    assert.equal(storage.has(lastKey), false);
    assert.equal(storage.get(storeKey), currentStore);

    storage.set(lastKey, preset.name);
    resetElements();
    manager.loadPresets();
    assert.equal(elements.get(inputId).value, weight);
    assert.equal(elements.get('presetSelect').value, preset.name);

    storage.delete(storeKey);
    storage.delete(lastKey);
    assert.equal(manager.savePreset({...preset, name: 'Coffee'}), true);
    assert.equal(storage.get(lastKey), preset.name);
    assert.equal(JSON.parse(storage.get(storeKey)).version, 1);
    assert.equal(storage.get('decentScalePresets'), legacy);
    assert.equal(storage.get('lastUsedPreset'), preset.name);
    resetElements();
    manager.loadPresets();
    assert.equal(elements.get('presetSelect').value, preset.name);

    storage.delete(storeKey);
    storage.delete(lastKey);
    storage.set('lastUsedPreset', preset.name === 'Dose' ? 'Quality' : 'Dose');
    resetElements();
    manager.loadPresets();
    assert.equal(elements.get(inputId).value, 'unchanged');
    assert.equal(storage.has(lastKey), false);
    assert.equal(manager.savePreset({...preset, name: 'Coffee'}), true);
    assert.equal(storage.has(lastKey), false);
  }
  storage.clear();
  resetElements();
  storage.set('decentScalePresets', legacy);
  storage.set('lastUsedPreset', 'Quality');
  assert.deepEqual(Object.keys(dosing.getPresets()), ['Dose']);
  assert.deepEqual(Object.keys(qc.getPresets()), ['Quality']);
  qc.loadPreset('Dose');
  assert.equal(elements.get('goalWeight').value, 'unchanged');
  assert.equal(dosing.savePreset({...dose, name: 'Coffee'}), true);
  assert.equal(qc.savePreset({...quality, name: 'Coffee'}), true);
  assert.equal(JSON.parse(storage.get('decentScaleDosingPresets')).presets.Coffee.settings.targetWeight, 10);
  assert.equal(JSON.parse(storage.get('decentScaleQCPresets')).presets.Coffee.goalWeight, 20);
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
  for (const {manager, preset, storedPreset, storeKey, lastKey, inputId} of apps) {
    for (const version of [undefined, 0, 2, 99, '1']) {
      const unsupported = JSON.stringify({version, presets: {[preset.name]: storedPreset}});
      storage.set(storeKey, unsupported);
      storage.delete(lastKey);
      storage.set('lastUsedPreset', preset.name);
      resetElements();
      assert.equal(Object.keys(manager.getPresets()).length, 0);
      manager.loadPresets();
      assert.equal(elements.get(inputId).value, 'unchanged');
      assert.equal(storage.has(lastKey), false);
      assert.equal(manager.savePreset(preset), false);
      assert.equal(storage.get(storeKey), unsupported);
    }
    for (const invalid of ['null', '[]', '{', '{"broken":null}', JSON.stringify({[preset.name]: storedPreset}),
      '{"version":1}', '{"version":1,"presets":null}', '{"version":1,"presets":[]}', '{"version":1,"presets":{"broken":null}}']) {
      storage.set(storeKey, invalid);
      assert.equal(Object.keys(manager.getPresets()).length, 0);
    }
    for (const key of Object.keys(preset.settings)) {
      const settings = {...preset.settings, [key]: null};
      const invalidPreset = storedPreset === preset ? {...preset, settings} : settings;
      storage.set(storeKey, JSON.stringify({version: 1, presets: {broken: invalidPreset}}));
      assert.equal(Object.keys(manager.getPresets()).length, 0);
      assert.equal(manager.savePreset({...preset, settings: {...preset.settings, [key]: Infinity}}), false);
    }
  }
  console.log('Versioned preset stores stay isolated and startup selection respects the migration boundary');
})().catch(error => { console.error(error); process.exitCode = 1; });
'''


if __name__ == "__main__":
    subprocess.run(["node", "--experimental-vm-modules", "-e", CHECK,
                    str(ROOT / "plugins/default-web-apps/assets")], check=True)
