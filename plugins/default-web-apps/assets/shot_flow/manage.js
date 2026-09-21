import { createShotPreset, validSavedProfiles } from './modules/profile-tools.js';
import { ShotStorage } from './modules/storage-client.js';

const storage = new ShotStorage();
const elements = {
    status: document.querySelector('#storage-status'),
    tabs: document.querySelectorAll('[data-view]'),
    views: document.querySelectorAll('.manager-view'),
    shotCount: document.querySelector('#shot-count'),
    profileCount: document.querySelector('#profile-count'),
    shotList: document.querySelector('#shot-list'),
    shotEmpty: document.querySelector('#shot-empty'),
    profileList: document.querySelector('#profile-list'),
    profileEmpty: document.querySelector('#profile-empty'),
    settingsForm: document.querySelector('#settings-form'),
    defaultDose: document.querySelector('#default-dose'),
    defaultProfile: document.querySelector('#default-profile'),
    defaultAuto: document.querySelector('#default-auto'),
    defaultGuidance: document.querySelector('#default-guidance')
};

let shots = [];
let profiles = [];

const button = (label, className, action, id) => {
    const element = document.createElement('button');
    element.type = 'button';
    element.className = `button secondary ${className}`;
    element.dataset.action = action;
    element.dataset.id = id;
    element.textContent = label;
    return element;
};

function row(title, meta, actions) {
    const element = document.createElement('article');
    element.className = 'data-row';
    const detail = document.createElement('div');
    const heading = document.createElement('h3');
    heading.textContent = title;
    const metadata = document.createElement('span');
    metadata.className = 'data-meta';
    metadata.textContent = meta;
    detail.append(heading, metadata);
    const actionGroup = document.createElement('div');
    actionGroup.className = 'data-actions';
    actionGroup.append(...actions);
    element.append(detail, actionGroup);
    return element;
}

function renderShots() {
    elements.shotList.replaceChildren();
    shots.sort((left, right) => right.createdAt - left.createdAt).forEach(shot => {
        const meta = `${shot.dose.toFixed(1)}g in / ${shot.yieldWeight.toFixed(1)}g out / ${shot.duration.toFixed(1)}s / ${shot.points.length} samples`;
        elements.shotList.append(row(shot.name, meta, [
            button('Use as profile', '', 'profile', shot.id),
            button('Delete', 'danger', 'delete-shot', shot.id)
        ]));
    });
    elements.shotCount.textContent = shots.length;
    elements.shotEmpty.hidden = shots.length > 0;
}

function renderProfiles() {
    elements.profileList.replaceChildren();
    profiles.forEach(profile => {
        const meta = `1:${profile.ratio.toFixed(1)} / ${profile.duration.toFixed(1)}s`;
        elements.profileList.append(row(profile.name, meta, [
            button('Delete', 'danger', 'delete-profile', profile.id)
        ]));
    });
    elements.profileCount.textContent = profiles.length;
    elements.profileEmpty.hidden = profiles.length > 0;
    elements.defaultProfile.querySelectorAll('[data-custom]').forEach(option => option.remove());
    profiles.forEach(profile => {
        const option = document.createElement('option');
        option.value = profile.id;
        option.dataset.custom = 'true';
        option.textContent = profile.name;
        elements.defaultProfile.append(option);
    });
}

async function refresh() {
    [shots, profiles] = await Promise.all([
        storage.listShots(),
        storage.listProfiles().then(validSavedProfiles)
    ]);
    renderShots();
    renderProfiles();
}

elements.tabs.forEach(tab => tab.addEventListener('click', () => {
    elements.tabs.forEach(candidate => candidate.classList.toggle('selected', candidate === tab));
    elements.views.forEach(view => { view.hidden = view.id !== `${tab.dataset.view}-view`; });
}));

elements.shotList.addEventListener('click', async event => {
    const action = event.target.closest('[data-action]');
    if (!action) return;
    const shot = shots.find(candidate => candidate.id === action.dataset.id);
    if (!shot) return;
    if (action.dataset.action === 'delete-shot') await storage.deleteShot(shot.id);
    if (action.dataset.action === 'profile') {
        const profile = createShotPreset(shot.name, shot.dose, shot.points, `profile-${Date.now()}`);
        if (profile) await storage.saveProfile(profile);
    }
    await refresh();
});

elements.profileList.addEventListener('click', async event => {
    const action = event.target.closest('[data-action="delete-profile"]');
    if (!action) return;
    await storage.deleteProfile(action.dataset.id);
    await refresh();
});

elements.settingsForm.addEventListener('submit', async event => {
    event.preventDefault();
    await storage.saveSettings({
        defaultDose: Number(elements.defaultDose.value),
        defaultProfileId: elements.defaultProfile.value,
        autoDetect: elements.defaultAuto.checked,
        grindGuidance: elements.defaultGuidance.checked
    });
    elements.status.textContent = 'Settings saved';
});

async function initialize() {
    try {
        await refresh();
        const settings = await storage.getSettings();
        elements.defaultDose.value = Number.isFinite(settings.defaultDose) ? settings.defaultDose.toFixed(1) : '18.0';
        elements.defaultAuto.checked = settings.autoDetect ?? true;
        elements.defaultGuidance.checked = settings.grindGuidance ?? false;
        elements.defaultProfile.value = settings.defaultProfileId || 'espresso';
        if (!elements.defaultProfile.value) elements.defaultProfile.value = 'none';
        elements.status.textContent = 'Stored in this browser';
    } catch (error) {
        console.error('Could not load saved data', error);
        elements.status.textContent = 'Storage unavailable';
    }
}

initialize();