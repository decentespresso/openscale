const DATABASE_NAME = 'shot-flow-data';
const DATABASE_VERSION = 1;
const STORES = Object.freeze({ shots: 'shots', profiles: 'profiles', settings: 'settings' });

function openDatabase() {
    return new Promise((resolve, reject) => {
        const request = indexedDB.open(DATABASE_NAME, DATABASE_VERSION);
        request.addEventListener('upgradeneeded', () => {
            const database = request.result;
            if (!database.objectStoreNames.contains(STORES.shots)) {
                database.createObjectStore(STORES.shots, { keyPath: 'id' });
            }
            if (!database.objectStoreNames.contains(STORES.profiles)) {
                database.createObjectStore(STORES.profiles, { keyPath: 'id' });
            }
            if (!database.objectStoreNames.contains(STORES.settings)) {
                database.createObjectStore(STORES.settings, { keyPath: 'key' });
            }
        });
        request.addEventListener('success', () => resolve(request.result));
        request.addEventListener('error', () => reject(request.error));
    });
}

function transactionRequest(storeName, mode, operation) {
    return openDatabase().then(database => new Promise((resolve, reject) => {
        const transaction = database.transaction(storeName, mode);
        const request = operation(transaction.objectStore(storeName));
        request.addEventListener('success', () => resolve(request.result));
        request.addEventListener('error', () => reject(request.error));
        transaction.addEventListener('complete', () => database.close());
        transaction.addEventListener('abort', () => reject(transaction.error));
    }));
}

function list(storeName) {
    return transactionRequest(storeName, 'readonly', store => store.getAll());
}

function put(storeName, value) {
    return transactionRequest(storeName, 'readwrite', store => store.put(value));
}

function remove(storeName, id) {
    return transactionRequest(storeName, 'readwrite', store => store.delete(id));
}

async function getSettings() {
    const rows = await list(STORES.settings);
    return Object.fromEntries(rows.map(row => [row.key, row.value]));
}

async function saveSettings(settings) {
    for (const [key, value] of Object.entries(settings)) {
        await put(STORES.settings, { key, value });
    }
    return getSettings();
}

async function handle(action, payload) {
    if (action === 'listShots') return list(STORES.shots);
    if (action === 'saveShot') return put(STORES.shots, payload);
    if (action === 'deleteShot') return remove(STORES.shots, payload.id);
    if (action === 'listProfiles') return list(STORES.profiles);
    if (action === 'saveProfile') return put(STORES.profiles, payload);
    if (action === 'deleteProfile') return remove(STORES.profiles, payload.id);
    if (action === 'getSettings') return getSettings();
    if (action === 'saveSettings') return saveSettings(payload);
    throw new Error(`Unknown storage action: ${action}`);
}

self.addEventListener('message', async event => {
    const { id, action, payload } = event.data;
    try {
        self.postMessage({ id, result: await handle(action, payload) });
    } catch (error) {
        self.postMessage({ id, error: error.message || 'Storage operation failed' });
    }
});