import { DecentScale } from './modules/scale.js';
import { UIController } from './modules/ui-controller.js';
import { TimerManager } from './modules/timer.js';
import { Led } from './modules/led.js';

document.addEventListener('DOMContentLoaded', () => {
    // Initialize components in correct order
    const timerManager = new TimerManager();
    const ui = new UIController(timerManager);
    timerManager.setUIController(ui);
    const scale = new DecentScale(ui, timerManager);
    const led = new Led(scale);

    // Connect button
    document.getElementById('connect')?.addEventListener('click', async () => {
        if (scale.activeConnection) {
            await scale.disconnect();
        } else {
            const connectionType = document.getElementById('connectionType').value;
            await scale.connect(connectionType);
        }
    });

    // Timer control
    document.getElementById('toggleTimer')?.addEventListener('click', () => {
        ui.toggleTimer();
    });

    // Other controls
    document.getElementById('tareButton')?.addEventListener('click', () => scale.tare());
    document.getElementById('toggleLed')?.addEventListener('click', () => led.toggleLed());
    document.getElementById('exportCSV')?.addEventListener('click', () => scale.exportToCSV());
    document.getElementById('exportJSON')?.addEventListener('click', () => scale.exportToJSON());
});
