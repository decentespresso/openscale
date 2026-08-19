import { DecentScale } from './modules/scale.js';
import { UIController } from './modules/ui-controller.js';
import { TimerManager } from './modules/timer.js';

document.addEventListener('DOMContentLoaded', () => {
    const timerManager = new TimerManager();
    const ui = new UIController(timerManager);
    const scale = new DecentScale(ui, timerManager);

    timerManager.setUIController(ui);

    let currentWeight = 0;
    let currentTimestamp = 0
    const ws = new ReconnectingWebSocket(`ws://${window.location.host}/snapshot`);
    ws.debug = true;

    ws.addEventListener('message', (event) => {
  try {
    const jsondata = JSON.parse(event.data);
    if (jsondata.grams !== undefined) {
      const weight = Number(jsondata.grams);
      if (!Number.isFinite(weight)) {
        return;
      }
      currentWeight = weight;
      ui.updateWeightDisplay(currentWeight);
            scale.processWeight(currentWeight);
    }
    if (jsondata.ms !== undefined) {
      currentTimestamp = jsondata.ms;
    }
  } catch (e) {
    console.error('Failed to parse WebSocket message as JSON:', event.data, e);
  }

    });
    ws.addEventListener('open', () => {
    console.log('WebSocket connection established.');
    ui.updateStatus('Connected');
});
    ws.addEventListener('error', () => {
        ui.updateStatus('Connection error');
    });

    ws.addEventListener('close', () => {
        ui.updateStatus('Disconnected');
    });

    document.getElementById('tareButton')?.addEventListener('click', () => {
        if (ws.readyState === WebSocket.OPEN) {
            ws.send('tare');
        } else {
            alert('WebSocket not connected.');
        }
    });

    document.getElementById('toggleTimer')?.addEventListener('click', () => {
        ui.toggleTimer();
    });




    document.getElementById('exportCSV')?.addEventListener('click', () => scale.exportToCSV());
    document.getElementById('exportJSON')?.addEventListener('click', () => scale.exportToJSON());
});
