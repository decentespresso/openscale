from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CHECK = r'''
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const storage = new Map();
let failRead = false;
let failWrite = false;
let denyAccess = false;
let deniedKey = null;
const assets = process.argv[1];

function page(relativeHtml) {
  const html = fs.readFileSync(path.join(assets, relativeHtml), 'utf8');
  let now = Date.now();
  const warnings = [];
  const downloads = [];
  const sockets = [];
  const timers = new Map();
  let timerId = 0;
  const blobs = new Map();
  function element(tag, attributes = '') {
    const handlers = new Map();
    return {
      value: attributes.match(/\bvalue="([^"]*)"/)?.[1] ?? '',
      checked: /\bchecked\b/.test(attributes),
      disabled: /\bdisabled\b/.test(attributes),
      hidden: /\bhidden\b/.test(attributes),
      style: {}, children: [], textContent: '',
      classList: {add() {}, remove() {}, replace() {}},
      set innerHTML(value) { this.children = []; },
      get options() { return this.children; },
      addEventListener(name, callback) { handlers.set(name, callback); },
      dispatch(name) { handlers.get(name)?.({target: this}); },
      click() {
        this.dispatch('click');
        if (tag === 'a') downloads.push({blob: blobs.get(this.href), filename: this.download});
      },
      appendChild(child) { this.children.push(child); },
      removeChild(child) { this.children = this.children.filter(item => item !== child); },
      remove(index) { if (index !== undefined) this.children.splice(index, 1); },
      querySelector() { return this.children.find(child => child.value === 'save_preset'); },
      setAttribute(name) { if (name === 'disabled') this.disabled = true; },
      removeAttribute(name) { if (name === 'disabled') this.disabled = false; }
    };
  }
  const elements = new Map(Array.from(html.matchAll(/<(\w+)\b([^>]*\bid="([^"]+)"[^>]*)>/g),
    match => [match[3], element(match[1], match[2])]));
  const presetSelect = elements.get('presetSelect');
  presetSelect.appendChild(element('option'));
  presetSelect.appendChild(element('option', 'value="save_preset"'));
  const document = Object.assign(element('document'), {
    getElementById(id) { return elements.get(id) ?? null; },
    createElement: element,
    body: element('body')
  });
  const storageApi = {
    get length() { if (failRead) throw new Error('Denied'); return storage.size; },
    key(index) { if (failRead) throw new Error('Denied'); return [...storage.keys()][index]; },
    getItem(key) {
      if (failRead || key === deniedKey) throw new Error('Denied');
      return storage.get(key) ?? null;
    },
    setItem(key, value) { if (failWrite) throw new Error('Quota'); storage.set(key, String(value)); }
  };
  const context = vm.createContext({
    Date: class extends Date { static now() { return now; } },
    console: {log() {}, error() {}, warn() {}},
    crypto: require('node:crypto').webcrypto,
    alert(message) { warnings.push(message); },
    document, Blob,
    URL: {
      createObjectURL(blob) { const id = `blob:${blobs.size}`; blobs.set(id, blob); return id; },
      revokeObjectURL() {}
    },
    requestAnimationFrame(callback) { callback(); },
    setTimeout(callback) { const id = ++timerId; timers.set(id, callback); return id; },
    clearTimeout(id) { timers.delete(id); },
    window: {location: {host: 'scale.test'}},
    WebSocket: {OPEN: 1},
    ReconnectingWebSocket: class {
      constructor() { this.handlers = new Map(); this.sent = []; this.readyState = 1; sockets.push(this); }
      addEventListener(name, callback) { this.handlers.set(name, callback); }
      send(value) { this.sent.push(value); }
      emit(name) { this.handlers.get(name)?.(); }
      weight(grams, advance = 500) { now += advance; this.handlers.get('message')({data: JSON.stringify({grams})}); }
    }
  });
  Object.defineProperty(context, 'localStorage', {
    get() { if (denyAccess) throw new Error('Denied'); return storageApi; }
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
  return {html, context, document, elements, warnings, downloads, sockets, timers, load,
    advanceTime(amount) { now += amount; }};
}

async function dosing() {
  const app = page('dosing_assistant/dosing_assistant.html');
  const {DecentScale} = (await app.load(path.join(assets, 'dosing_assistant/modules/scale.js'))).namespace;
  const {DataExport} = (await app.load(path.join(assets, 'dosing_assistant/modules/export.js'))).namespace;
  const {UIController} = (await app.load(path.join(assets, 'dosing_assistant/modules/ui-controller.js'))).namespace;
  const scale = new DecentScale(new UIController(), {});
  scale.uiController.setScale(scale);
  scale.dosingSettings = {targetWeight: 10, lowThreshold: 9, highThreshold: 11};
  return {...app, scale, DataExport};
}

function qc() {
  const app = page('Quality_Control_Assistant/quality_control.html');
  const historyPosition = app.html.indexOf('src="../shared/measurement-history.js"');
  const qcPosition = app.html.indexOf('src="quality_control.js"');
  assert(historyPosition >= 0, 'QC must load measurement history');
  assert(qcPosition >= 0, 'QC must load its application script');
  assert(historyPosition < qcPosition, 'QC must load history before the application');
  vm.runInContext(fs.readFileSync(path.join(assets, 'shared/measurement-history.js'), 'utf8'), app.context);
  vm.runInContext(fs.readFileSync(path.join(assets, 'Quality_Control_Assistant/quality_control.js'), 'utf8'), app.context);
  const scale = vm.runInContext('new DecentScale()', app.context);
  scale.qcSettings = {goalWeight: 10, lowThreshold: 9, highThreshold: 11, minWeight: 1, enableSounds: false};
  return {...app, scale};
}

function checkRecovery(app, save) {
  const size = storage.size;
  const warning = app.elements.get('historyWarning');
  assert(warning, 'History must have a persistent storage warning');
  assert.equal(warning.hidden, true);
  const alerts = app.warnings.length;
  failWrite = true;
  save();
  assert.equal(storage.size, size);
  assert.equal(app.warnings.length, alerts + 1);
  assert.equal(warning.hidden, false);
  assert(warning.textContent.includes('Export readings'));
  failWrite = false;
  save();
  assert.equal(storage.size, size + 1);
  assert.equal(warning.hidden, false);
  assert(warning.textContent.includes('Export readings'));
  failWrite = true;
  save();
  assert.equal(app.warnings.length, alerts + 2);
  save();
  assert.equal(app.warnings.length, alerts + 2);
  assert.equal(storage.size, size + 1);
  failWrite = false;
}

async function checkExports(app, expected) {
  assert.equal(app.elements.get('weightReadings').children.length, expected);
  for (const format of ['CSV', 'JSON']) {
    const button = app.elements.get(`export${format}`);
    assert.equal(button.disabled, false);
    button.click();
    const download = app.downloads.at(-1);
    assert(download.filename.endsWith(`.${format.toLowerCase()}`));
    const content = await download.blob.text();
    if (format === 'JSON') assert.equal(JSON.parse(content).readings.length, expected);
    else assert.equal(content.trim().split('\n').length, expected + 1);
  }
}

function checkQCLifecycle() {
  storage.clear();
  const invalid = qc();
  const button = invalid.elements.get('toggleQC');
  button.textContent = 'Start QC Mode';
  const valid = {goalWeight: '10', lowThreshold: '9', highThreshold: '11', minWeight: '1'};
  for (const values of [{}, {...valid, goalWeight: ''}, {...valid, lowThreshold: '12'},
    {...valid, minWeight: '0.2'}]) {
    for (const [key, value] of Object.entries(values)) invalid.elements.get(key).value = value;
    button.click();
    for (let index = 0; index < 8; ++index) invalid.sockets[0].weight(0);
    assert.equal(invalid.scale.qcMode, false);
    assert.equal(button.textContent, 'Start QC Mode');
    assert.equal(invalid.scale.weightData.length, 0);
    assert.equal(invalid.sockets[0].sent.length, 0);
  }
  function active() {
    storage.clear();
    const app = qc();
    for (const [key, value] of Object.entries(valid)) app.elements.get(key).value = value;
    app.elements.get('toggleQC').click();
    assert.equal(app.scale.qcMode, true);
    return app;
  }
  for (const interval of [100, 200, 500]) {
    const app = active();
    const socket = app.sockets[0];
    for (let index = 0; index < 40; ++index) socket.weight(10 + index * 0.1, interval);
    assert.equal(app.scale.weightData.length, 0);
    for (let index = 0; index < 2500 / interval; ++index) socket.weight(10, interval);
    socket.weight(10.1, interval);
    assert.equal(app.scale.weightData.length, 1);
    assert.equal(app.scale.weightData[0].weight, 10.1);
    socket.emit('close');
    socket.emit('open');
    for (let index = 0; index < 8; ++index) socket.weight(10.1);
    assert.equal(app.scale.weightData.length, 1);
  }
  for (const interruption of ['gap', 'close', 'error']) {
    const app = active();
    const socket = app.sockets[0];
    socket.weight(10);
    socket.weight(10);
    if (interruption !== 'gap') socket.emit(interruption);
    socket.weight(10, interruption === 'gap' ? 10000 : 500);
    assert.equal(app.scale.weightData.length, 0);
    for (let index = 0; index < 5; ++index) socket.weight(10);
    assert.equal(app.scale.weightData.length, 1);
  }
  for (const removedWeight of [0, -10]) {
    const app = active();
    app.sockets[0].weight(10);
    for (let index = 0; index < 8; ++index) app.sockets[0].weight(removedWeight);
    assert.equal(app.scale.weightData.length, 0);
    assert.equal(app.scale.currentQCState, app.scale.QC_STATES.WAITING_FOR_NEXT);
    for (let index = 0; index < 7; ++index) app.sockets[0].weight(10);
    assert.equal(app.scale.weightData.length, 1);
    app.sockets[0].weight(0);
    assert.equal(app.timers.size, 0);
    app.sockets[0].weight(10);
    for (let index = 0; index < 6; ++index) app.sockets[0].weight(0.5);
    assert.equal(app.scale.weightData.length, 2);
    assert.equal(app.scale.weightData[1].result, 'fail');
    app.elements.get('toggleQC').click();
    const status = app.elements.get('qcStatus').textContent;
    assert.equal(status, 'QC Mode: Inactive');
    assert.equal(app.scale.currentQCState, app.scale.QC_STATES.WAITING_FOR_NEXT);
    assert.equal(app.timers.size, 0);
    app.advanceTime(6000);
    for (const callback of app.timers.values()) callback();
    assert.equal(app.elements.get('qcStatus').textContent, status);
    app.elements.get('toggleQC').click();
    assert.equal(app.timers.size, 0);
  }
}

(async () => {
  const first = await dosing();
  const concurrent = await dosing();
  assert.notEqual(first.context, concurrent.context);
  first.scale.saveDosing(10);
  concurrent.scale.saveDosing(11);
  const restored = await dosing();
  assert.equal(restored.scale.weightData.length, 2);
  assert.deepEqual(Array.from(restored.scale.weightData, row => row.weight).sort(), ['10.0', '11.0']);
  assert.equal(restored.scale.readingCount, 2);
  assert.equal(restored.scale.weightReadings.length, 2);
  assert.equal(JSON.parse(restored.DataExport.exportToJSON(restored.scale.weightData).content).total_readings, 2);
  restored.scale.saveDosing(12);
  assert.equal((await dosing()).scale.weightData.length, 3);
  const quality = qc();
  assert.notEqual(quality.context, first.context);
  quality.scale.saveMeasurement(10, 'pass');
  const qcReloaded = qc();
  assert.equal(qcReloaded.scale.weightData.length, 1);
  assert.equal(qcReloaded.scale.weightReadings.length, 1);
  assert.equal(qcReloaded.scale.weightData[0].weight, 10);
  assert.equal(qcReloaded.scale.weightData[0].qcSettings.goalWeight, 10);
  assert.equal((await dosing()).scale.weightData.length, 3);
  checkRecovery(restored, () => restored.scale.saveDosing(13));
  assert.equal(JSON.parse(restored.DataExport.exportToJSON(restored.scale.weightData).content).total_readings, 7);
  checkRecovery(quality, () => quality.scale.saveMeasurement(10, 'pass'));
  await checkExports(quality, 5);
  const validSize = storage.size;
  storage.set('hds.dosing.history.v1.broken', '{');
  storage.set('hds.dosing.history.v1.wrong-schema', '{"weight":null}');
  assert.equal((await dosing()).scale.weightData.length, 4);
  assert.equal(storage.get('hds.dosing.history.v1.broken'), '{');
  assert.equal(storage.get('hds.dosing.history.v1.wrong-schema'), '{"weight":null}');
  for (const mode of ['reads', 'access', 'lastUsedPreset', 'lastUsedDosingPreset', 'lastUsedQCPreset']) {
    failRead = mode === 'reads';
    denyAccess = mode === 'access';
    deniedKey = mode.startsWith('lastUsed') ? mode : null;
    failWrite = true;
    const app = page('dosing_assistant/dosing_assistant.html');
    await app.load(path.join(assets, 'dosing_assistant/main.js'));
    app.document.dispatch('DOMContentLoaded');
    assert.equal(app.sockets.length, 1);
    const previous = app.elements.get('weightReadings').children.length;
    if (mode === 'reads' || mode === 'access') assert.equal(previous, 0);
    const sound = app.elements.get('soundEnabled');
    sound.checked = false;
    sound.dispatch('change');
    app.sockets[0].weight(0);
    app.elements.get('dosingToggleButton').click();
    app.elements.get('setContainerWeightButton').click();
    for (let index = 0; index < 7; ++index) app.sockets[0].weight(30);
    for (const callback of app.timers.values()) callback();
    assert(app.elements.get('weight').textContent.includes('30.0'));
    await checkExports(app, previous + 1);
    const qcApp = qc();
    assert.equal(qcApp.sockets.length, 1);
    const qcPrevious = qcApp.scale.weightData.length;
    if (mode === 'reads' || mode === 'access') assert.equal(qcPrevious, 0);
    qcApp.scale.qcMode = true;
    for (let index = 0; index < 7; ++index) qcApp.sockets[0].weight(10);
    await checkExports(qcApp, qcPrevious + 1);
    for (const instance of [app, qcApp]) {
      instance.elements.get('objectName').value = 'Unsaved preset';
      const select = instance.elements.get('presetSelect');
      select.value = 'save_preset';
      select.dispatch('change');
      assert(instance.warnings.some(message => message.includes('Preset could not be saved')));
      assert(!instance.warnings.some(message => message.includes('saved successfully')));
    }
    assert.equal(storage.size, validSize + 2);
  }
  failRead = false;
  denyAccess = false;
  deniedKey = null;
  failWrite = false;
  const {PresetManager} = (await first.load(path.join(assets, 'shared/modules/presets.js'))).namespace;
  const presets = new PresetManager();
  const dosingPreset = {name: 'Dosing probe', settings: first.scale.dosingSettings};
  assert.equal(presets.savePreset(dosingPreset), true);
  assert.equal(presets.getPreset(dosingPreset.name).settings.targetWeight, 10);
  const qcPreset = {name: 'QC probe', settings: quality.scale.qcSettings};
  assert.equal(quality.scale.savePreset(qcPreset), true);
  assert.equal(quality.scale.getPreset(qcPreset.name).goalWeight, 10);
  failWrite = true;
  assert.doesNotThrow(() => presets.loadPreset(dosingPreset.name));
  assert.doesNotThrow(() => quality.scale.loadPreset(qcPreset.name));
  const sound = first.elements.get('soundEnabled');
  sound.checked = false;
  sound.dispatch('change');
  assert.equal(first.scale.soundEnabled, false);
  failWrite = false;
  const savedDosingPresets = storage.get('decentScaleDosingPresets');
  const savedQCPresets = storage.get('decentScaleQCPresets');
  failRead = true;
  assert.equal(presets.savePreset(dosingPreset), false);
  assert.equal(quality.scale.savePreset(qcPreset), false);
  assert.equal(storage.get('decentScaleDosingPresets'), savedDosingPresets);
  assert.equal(storage.get('decentScaleQCPresets'), savedQCPresets);
  failRead = false;
  for (const [manager, preset, key] of [
    [presets, dosingPreset, 'decentScaleDosingPresets'],
    [quality.scale, qcPreset, 'decentScaleQCPresets']
  ]) {
    for (const invalid of ['{', 'null', '[]']) {
      storage.set(key, invalid);
      assert.doesNotThrow(() => manager.loadPresets());
      assert.equal(manager.savePreset(preset), false);
      assert.equal(storage.get(key), invalid);
    }
    storage.delete(key);
  }
  for (const invalid of ['{', 'null', '[]']) {
    storage.set('decentScalePresets', invalid);
    assert.doesNotThrow(() => presets.loadPresets());
    assert.doesNotThrow(() => quality.scale.loadPresets());
    assert.equal(presets.savePreset(dosingPreset), false);
    assert.equal(quality.scale.savePreset(qcPreset), false);
    assert.equal(storage.get('decentScalePresets'), invalid);
  }
  checkQCLifecycle();
  console.log('History recovery, exports, storage failures and QC lifecycle passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
'''


if __name__ == "__main__":
    subprocess.run(["node", "--experimental-vm-modules", "-e", CHECK,
                    str(ROOT / "plugins/default-web-apps/assets")], check=True)
