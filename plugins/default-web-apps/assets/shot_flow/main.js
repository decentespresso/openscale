import { createGhostSeries, ghostAt, SHOT_PRESETS, ShotEngine } from './modules/shot-model.js';
import { ShotChart } from './modules/shot-chart.js';
import { createShotRecord, recommendGrind, validSavedProfiles } from './modules/profile-tools.js';
import { ShotStorage } from './modules/storage-client.js';

const profileSource = document.createElement('a');
profileSource.className = 'pace-label';
profileSource.target = '_blank';
profileSource.rel = 'noopener noreferrer';
profileSource.hidden = true;
document.querySelector('.legend').append(profileSource);

const elements = {
    connectionDot: document.querySelector('#connection-dot'),
    connectionStatus: document.querySelector('#connection-status'),
    stateLabel: document.querySelector('#state-label'),
    stateDetail: document.querySelector('#state-detail'),
    yieldValue: document.querySelector('#yield-value'),
    yieldTarget: document.querySelector('#yield-target'),
    timeValue: document.querySelector('#time-value'),
    timeTarget: document.querySelector('#time-target'),
    flowValue: document.querySelector('#flow-value'),
    flowTarget: document.querySelector('#flow-target'),
    paceLabel: document.querySelector('#pace-label'),
    profileSource,
    targetLegend: document.querySelectorAll('.target-legend'),
    recommendation: document.querySelector('#grind-recommendation'),
    recommendationTitle: document.querySelector('#recommendation-title'),
    recommendationDetail: document.querySelector('#recommendation-detail'),
    doseInput: document.querySelector('#dose-input'),
    autoToggle: document.querySelector('#auto-toggle'),
    grindToggle: document.querySelector('#grind-toggle'),
    profileSelect: document.querySelector('#profile-select'),
    saveShotButton: document.querySelector('#save-shot-button'),
    saveShotDialog: document.querySelector('#save-shot-dialog'),
    saveShotForm: document.querySelector('#save-shot-form'),
    shotNameInput: document.querySelector('#shot-name-input'),
    saveShotSummary: document.querySelector('#save-shot-summary'),
    cancelShotButton: document.querySelector('#cancel-shot-button'),
    tareButton: document.querySelector('#tare-button'),
    resetButton: document.querySelector('#reset-button'),
    shotButton: document.querySelector('#shot-button')
};

const engine = new ShotEngine();
const chart = new ShotChart(document.querySelector('#shot-chart'));
const storage = new ShotStorage();
let preset = SHOT_PRESETS.espresso;
let savedProfiles = [];
let dose = 18;
let currentWeight = 0;
let currentTime = 0;
let lastDeviceTime = null;
let deviceTimeEpoch = 0;

const socketProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
const socket = new ReconnectingWebSocket(`${socketProtocol}//${window.location.host}/snapshot`);

function normalizeDeviceTime(milliseconds) {
    if (lastDeviceTime !== null && milliseconds < lastDeviceTime && lastDeviceTime - milliseconds > 2147483648) {
        deviceTimeEpoch += 4294967296;
    }
    lastDeviceTime = milliseconds;
    return deviceTimeEpoch + milliseconds;
}

function targetYield() {
    return preset ? dose * preset.ratio : 0;
}

function selectedPreset(id) {
    if (id === 'none') return null;
    return SHOT_PRESETS[id] || savedProfiles.find(candidate => candidate.id === id) || null;
}

function populateProfileSelect(selectedId = preset?.id || 'none') {
    elements.profileSelect.querySelectorAll('[data-custom]').forEach(option => option.remove());
    savedProfiles.forEach(saved => {
        const option = document.createElement('option');
        option.value = saved.id;
        option.dataset.custom = 'true';
        option.textContent = `${saved.name} / 1:${saved.ratio.toFixed(1)} / ${Math.round(saved.duration)}s`;
        elements.profileSelect.append(option);
    });
    elements.profileSelect.value = selectedId;
}

function currentGhostSeries() {
    return preset ? createGhostSeries(dose, preset) : [];
}

function refreshChart() {
    const duration = preset?.duration || Math.max(25, Math.ceil(engine.latest.elapsed / 5) * 5);
    chart.update(
        engine.points,
        currentGhostSeries(),
        duration,
        targetYield(),
        engine.status === 'complete'
    );
}

function updateConnection(label, className) {
    elements.connectionStatus.textContent = label;
    elements.connectionDot.className = `connection-dot ${className}`;
}

function updateState() {
    const states = {
        ready: ['Ready', elements.autoToggle.checked ? 'Waiting for flow' : 'Manual start'],
        recording: ['Recording', 'Extraction in progress'],
        complete: ['Complete', 'Shot captured']
    };
    const [label, detail] = states[engine.status];
    elements.stateLabel.textContent = label;
    elements.stateDetail.textContent = detail;
    elements.shotButton.textContent = engine.status === 'recording' ? 'Stop shot' : 'Start shot';
    elements.shotButton.classList.toggle('stop', engine.status === 'recording');
    elements.saveShotButton.disabled = engine.status !== 'complete';
}

function updateMeasurements() {
    const { elapsed, weight, flow } = engine.latest;
    elements.yieldValue.textContent = weight.toFixed(1);
    elements.timeValue.textContent = elapsed.toFixed(1);
    elements.flowValue.textContent = flow.toFixed(1);

    if (!preset) {
        elements.yieldTarget.textContent = 'No target';
        elements.timeTarget.textContent = 'Free pour';
        elements.flowTarget.textContent = 'No target';
        elements.paceLabel.textContent = 'Free pour';
        elements.targetLegend.forEach(item => { item.hidden = true; });
        elements.profileSource.hidden = true;
        return;
    }

    const ghost = ghostAt(elapsed, dose, preset);
    elements.yieldTarget.textContent = `of ${targetYield().toFixed(1)}g`;
    elements.timeTarget.textContent = `of ${Math.round(preset.duration)}s`;
    elements.flowTarget.textContent = `target ${ghost.flow.toFixed(1)}g/s`;
    elements.targetLegend.forEach(item => { item.hidden = false; });
    elements.profileSource.hidden = !preset.source;
    if (preset.source) {
        elements.profileSource.href = preset.source.url;
        elements.profileSource.textContent = 'Source: Decent';
        elements.profileSource.title = `${preset.source.label}. ${preset.source.note}`;
    }

    const difference = weight - ghost.weight;
    if (elapsed === 0 || Math.abs(difference) < 0.5) {
        elements.paceLabel.textContent = 'On target';
    } else {
        elements.paceLabel.textContent = `${Math.abs(difference).toFixed(1)}g ${difference > 0 ? 'ahead' : 'behind'}`;
    }
}

function updateRecommendation() {
    const shouldShow = elements.grindToggle.checked && preset && engine.status === 'complete';
    if (!shouldShow) {
        elements.recommendation.hidden = true;
        return;
    }

    const recommendation = recommendGrind(
        engine.points,
        currentGhostSeries(),
        preset.duration
    );
    if (!recommendation) {
        elements.recommendation.hidden = true;
        return;
    }

    elements.recommendation.dataset.direction = recommendation.direction;
    elements.recommendationTitle.textContent = recommendation.title;
    elements.recommendationDetail.textContent = recommendation.detail;
    elements.recommendation.hidden = false;
}

function render() {
    updateState();
    updateMeasurements();
    updateRecommendation();
    refreshChart();
}

function resetShot() {
    engine.reset();
    elements.saveShotButton.textContent = 'Save shot';
    render();
}

socket.addEventListener('open', () => {
    updateConnection('Connected / 10 Hz', 'connected');
    socket.send(JSON.stringify({ rate_hz: 10 }));
});

socket.addEventListener('close', () => updateConnection('Reconnecting', 'disconnected'));
socket.addEventListener('error', () => updateConnection('Connection error', 'disconnected'));
socket.addEventListener('message', event => {
    try {
        const message = JSON.parse(event.data);
        if (!Number.isFinite(Number(message.grams))) return;
        currentWeight = Number(message.grams);
        currentTime = Number.isFinite(Number(message.ms))
            ? normalizeDeviceTime(Number(message.ms))
            : performance.now();
        engine.ingest(currentWeight, currentTime, elements.autoToggle.checked);
        render();
    } catch (error) {
        console.error('Invalid scale message', error);
    }
});

elements.profileSelect.addEventListener('change', () => {
    preset = selectedPreset(elements.profileSelect.value);
    render();
});

elements.doseInput.addEventListener('change', () => {
    const parsedDose = Number(elements.doseInput.value);
    dose = Number.isFinite(parsedDose) ? Math.min(40, Math.max(5, parsedDose)) : 18;
    elements.doseInput.value = dose.toFixed(1);
    render();
});

elements.autoToggle.addEventListener('change', updateState);
elements.grindToggle.addEventListener('change', updateRecommendation);

elements.saveShotButton.addEventListener('click', () => {
    if (engine.status !== 'complete') return;
    elements.shotNameInput.value = `Shot ${new Date().toLocaleDateString()}`;
    elements.saveShotSummary.textContent = `${dose.toFixed(1)}g in / ${engine.latest.weight.toFixed(1)}g out / ${engine.latest.elapsed.toFixed(1)}s`;
    elements.saveShotDialog.showModal();
    elements.shotNameInput.select();
});

elements.cancelShotButton.addEventListener('click', () => elements.saveShotDialog.close());

elements.saveShotForm.addEventListener('submit', async event => {
    event.preventDefault();
    const recommendation = preset ? recommendGrind(
        engine.points,
        currentGhostSeries(),
        preset.duration
    ) : null;
    const shot = createShotRecord({
        name: elements.shotNameInput.value,
        dose,
        points: engine.points,
        targetPreset: preset,
        recommendation,
        id: `shot-${Date.now()}`
    });
    if (!shot) return;
    try {
        await storage.saveShot(shot);
        elements.saveShotDialog.close();
        elements.saveShotButton.textContent = 'Saved';
    } catch (error) {
        console.error('Could not save shot', error);
        elements.saveShotSummary.textContent = 'Could not save this shot.';
    }
});

elements.tareButton.addEventListener('click', () => {
    if (socket.readyState === WebSocket.OPEN) socket.send('tare');
    resetShot();
});

elements.resetButton.addEventListener('click', resetShot);

elements.shotButton.addEventListener('click', () => {
    if (engine.status === 'recording') engine.stop();
    else {
        engine.reset();
        engine.start(currentWeight, currentTime || performance.now());
    }
    render();
});

async function initialize() {
    try {
        savedProfiles = validSavedProfiles(await storage.listProfiles());
        const settings = await storage.getSettings();
        dose = Number.isFinite(settings.defaultDose) ? settings.defaultDose : dose;
        elements.doseInput.value = dose.toFixed(1);
        elements.autoToggle.checked = settings.autoDetect ?? true;
        elements.grindToggle.checked = settings.grindGuidance ?? false;
        const defaultProfileId = settings.defaultProfileId ?? 'espresso';
        preset = selectedPreset(defaultProfileId);
        if (defaultProfileId !== 'none' && !preset) preset = SHOT_PRESETS.espresso;
    } catch (error) {
        console.error('Could not load Shot Flow data', error);
    }
    populateProfileSelect();
    render();
}

initialize();