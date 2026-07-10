import { DecentScale } from './modules/scale.js';
import { StateMachine } from './modules/state-machine.js';
import { UIController } from './modules/ui-controller.js';
import { PresetManager } from '../shared/modules/presets.js';

document.addEventListener('DOMContentLoaded', () => {
    const ui = new UIController();
    const stateMachine = new StateMachine(ui);
    const presetManager = new PresetManager();
    const scale = new DecentScale(ui, stateMachine, presetManager);
    ui.setScale(scale);

    const ws = new ReconnectingWebSocket(`ws://${window.location.host}/snapshot`);
    ws.debug = true;
    scale.ws = ws;

    ws.addEventListener('open', () => {
        ui.updateStatus('WebSocket Connected');
        console.log('WebSocket connected');
    });

    ws.addEventListener('close', () => {
        ui.updateStatus('WebSocket Disconnected');
        console.log('WebSocket disconnected');
    });

    ws.addEventListener('error', () => {
        ui.updateStatus('WebSocket Error');
        console.log('WebSocket error');
    });

    ws.addEventListener('message', (event) => {
        let jsondata;
        try {
            jsondata = JSON.parse(event.data);
        } catch (error) {
            console.error('Failed to parse WebSocket message as JSON:', event.data, error);
            return;
        }

        if (jsondata.grams !== undefined) {
            const weight = Number(jsondata.grams);
            if (Number.isFinite(weight)) {
                scale.handleWebSocketWeight(weight);
            }
        }
    });

    document.getElementById('tareButton')?.addEventListener('click', () => 
        scale.tare()
    );

    document.getElementById('setContainerWeightButton')?.addEventListener('click', () => 
        scale.setContainerWeight()
    );

    document.getElementById('dosingToggleButton')?.addEventListener('click', () => 
        scale.toggleDosingMode()
    );

    presetManager.init(scale);
    presetManager.loadPresets();
    presetManager.setupPresetHandlers();

    const exportCSVButton = document.getElementById('exportCSV');
    const exportJSONButton = document.getElementById('exportJSON');
    
    if (exportCSVButton) {
        exportCSVButton.addEventListener('click', () => {
            scale.exportToCSV();
        });
    }
    
    if (exportJSONButton) {
        exportJSONButton.addEventListener('click', () => {
            scale.exportToJSON();
        });
    }

    if (ui.setContainerWeightButton) {
        ui.setContainerWeightButton.classList.add('invisible');
        ui.setContainerWeightButton.setAttribute('disabled', '');
    }
    
    if (ui.guidanceElement) {
        ui.guidanceElement.textContent = 'Connect to scale to begin';
    }
});
