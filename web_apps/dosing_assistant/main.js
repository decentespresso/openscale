import { DecentScale } from './modules/scale.js';
import { StateMachine } from './modules/state-machine.js';
import { UIController } from './modules/ui-controller.js';
import { DataExport } from './modules/export.js';
import { PresetManager } from './modules/presets.js';
import { SCALE_CONSTANTS } from './modules/constants.js';
document.addEventListener('DOMContentLoaded', () => {
    // Initialize components
    const ui = new UIController();
    const stateMachine = new StateMachine(ui);
    const presetManager = new PresetManager();
    
    // Pass components to DecentScale
    const scale = new DecentScale(ui, stateMachine, presetManager);
    ui.setScale(scale); // Add this line
    
    // Connect button event listener - using correct ID
    document.getElementById('connectButton').addEventListener('click', async () => {
        if (scale.device) {
            await scale.disconnect();
        } else {
            await scale.connect();
            await scale.tare();
        }
    });

    // Other button listeners - using correct IDs
    document.getElementById('tareButton')?.addEventListener('click', () => 
        scale.tare()
    );

    document.getElementById('setContainerWeightButton')?.addEventListener('click', () => 
        scale.setContainerWeight()
    );

    document.getElementById('dosingToggleButton')?.addEventListener('click', () => 
        scale.toggleDosingMode()
    );

    // Setup preset manager with reference to scale
    presetManager.init(scale);
    presetManager.loadPresets();
    presetManager.setupPresetHandlers();
    
    // Add export functionality
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
    
    // Initialize UI
    if (ui.setContainerWeightButton) {
        ui.setContainerWeightButton.classList.add('invisible');
        ui.setContainerWeightButton.setAttribute('disabled', '');
    }
    
    if (ui.guidanceElement) {
        ui.guidanceElement.textContent = 'Connect to scale to begin';
    }
});
