export class ShotStorage {
    constructor(workerUrl = '../storage-worker.js') {
        this.worker = new Worker(new URL(workerUrl, import.meta.url));
        this.requests = new Map();
        this.nextId = 1;
        this.worker.addEventListener('message', event => {
            const request = this.requests.get(event.data.id);
            if (!request) return;
            this.requests.delete(event.data.id);
            if (event.data.error) request.reject(new Error(event.data.error));
            else request.resolve(event.data.result);
        });
        this.worker.addEventListener('error', error => {
            this.requests.forEach(request => request.reject(error));
            this.requests.clear();
        });
    }

    request(action, payload = null) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            this.requests.set(id, { resolve, reject });
            this.worker.postMessage({ id, action, payload });
        });
    }

    listShots() { return this.request('listShots'); }
    saveShot(shot) { return this.request('saveShot', shot); }
    deleteShot(id) { return this.request('deleteShot', { id }); }
    listProfiles() { return this.request('listProfiles'); }
    saveProfile(profile) { return this.request('saveProfile', profile); }
    deleteProfile(id) { return this.request('deleteProfile', { id }); }
    getSettings() { return this.request('getSettings'); }
    saveSettings(settings) { return this.request('saveSettings', settings); }
}