import { DecentScale } from './modules/scale.js';
import { UIController } from './modules/ui-controller.js';
import { TimerManager } from './modules/timer.js';
import { Led } from './modules/led.js';

document.addEventListener('DOMContentLoaded', () => {
    const timerManager = new TimerManager();
    const ui = new UIController(timerManager);
    const scale = new DecentScale(ui, timerManager);

    timerManager.setUIController(ui);

    // --- WebSocket for live weight ---
    let currentWeight = 0;
    const ws = new ReconnectingWebSocket(`ws://${window.location.host}/snapshot`);
    ws.debug = true;

//mock : ws://localhost:8080/snapshot
    ws.addEventListener('message', (event) => {
        currentWeight = parseFloat(event.data);
        ui.updateWeightDisplay(currentWeight);
            scale.processWeight(currentWeight);
            
 // Make sure your UIController has this method
    });
    ws.addEventListener('open', () => {
    console.log('WebSocket connection established.');
    ui.updateStatus('Connected'); // Set status to connected when the WebSocket opens
});
    ws.addEventListener('error', () => {
        ui.updateStatus('Connection error');
    });

    ws.addEventListener('close', () => {
        ui.updateStatus('Disconnected');
    });

    // Tare button
    document.getElementById('tareButton')?.addEventListener('click', () => {
        if (ws.readyState === WebSocket.OPEN) {
            ws.send('tare');
        } else {
            alert('WebSocket not connected.');
        }
    });

    // Timer control
    document.getElementById('toggleTimer')?.addEventListener('click', () => {
        ui.toggleTimer();
    });

    
   

    // Remove BLE/USB/DecentScale/LED logic


    // Other controls
    document.getElementById('tareButton')?.addEventListener('click', () => scale.tare());
    document.getElementById('toggleLed')?.addEventListener('click', () => led.toggleLed());
    document.getElementById('exportCSV')?.addEventListener('click', () => scale.exportToCSV());
    document.getElementById('exportJSON')?.addEventListener('click', () => scale.exportToJSON());
});

