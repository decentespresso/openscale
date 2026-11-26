const elements = {
  connection: document.getElementById('connection'),
  weight: document.getElementById('weight'),
  timer: document.getElementById('timer'),
  flow: document.getElementById('flow'),
  ratio: document.getElementById('ratio'),
  dose: document.getElementById('dose'),
  extraction: document.getElementById('extraction'),
  modePour: document.getElementById('mode-pour'),
  modeEspresso: document.getElementById('mode-espresso'),
  timerStart: document.getElementById('timer-start'),
  timerStop: document.getElementById('timer-stop'),
};

const state = {
  weight: 0,
  timer: 0,
  timerRunning: false,
  extraction: false,
  mode: 0,
  flow: 0,
  ratio: 0,
  dose: 0,
};

let socket = null;
let retryTimer = null;

function formatTimer(seconds) {
  if (state.mode === 1) return `${seconds}s`;
  const minutes = Math.floor(seconds / 60);
  const rest = seconds % 60;
  return `${String(minutes).padStart(2, '0')}:${String(rest).padStart(2, '0')}`;
}

function render() {
  elements.weight.innerHTML = `${state.weight.toFixed(1)}<span class="unit">g</span>`;
  elements.timer.textContent = formatTimer(state.timer);
  elements.flow.textContent = `${state.flow.toFixed(1)} g/s`;
  elements.ratio.textContent = `1:${state.ratio.toFixed(1)}`;
  elements.dose.textContent = `${state.dose.toFixed(1)} g`;
  elements.extraction.textContent = state.extraction
    ? 'Extraction on'
    : 'Extraction off';
  elements.modePour.classList.toggle('active', state.mode === 0);
  elements.modeEspresso.classList.toggle('active', state.mode === 1);
  elements.timerStart.classList.toggle('active', state.timerRunning);
}

function setConnection(connected) {
  elements.connection.textContent = connected ? 'online' : 'offline';
  elements.connection.classList.toggle('online', connected);
  elements.connection.classList.toggle('offline', !connected);
}

function applyStatus(message) {
  if (typeof message.grams === 'number') state.weight = message.grams;
  if (typeof message.timer_seconds === 'number') state.timer = message.timer_seconds;
  if (typeof message.timer_running === 'boolean') state.timerRunning = message.timer_running;
  if (typeof message.drs_extraction === 'boolean') state.extraction = message.drs_extraction;
  if (typeof message.drs_mode === 'number') state.mode = message.drs_mode;
  if (typeof message.drs_flow === 'number') state.flow = message.drs_flow;
  if (typeof message.drs_ratio === 'number') state.ratio = message.drs_ratio;
  if (typeof message.drs_dose === 'number') state.dose = message.drs_dose;
}

function applyWeight(message) {
  if (typeof message.grams !== 'number') return;
  state.weight = message.grams;
  const drs = message.drs;
  if (!drs) return;
  state.extraction = drs.extract === 1;
  state.mode = drs.mode;
  state.flow = drs.flow;
  state.ratio = drs.ratio;
  state.dose = drs.dose;
  state.timer = drs.timer;
  state.timerRunning = drs.trun === 1;
}

function handleMessage(event) {
  let message;
  try {
    message = JSON.parse(event.data);
  } catch (error) {
    return;
  }
  if (message.type === 'status') {
    applyStatus(message);
  } else {
    applyWeight(message);
  }
  render();
}

function connect() {
  socket = new WebSocket(`${location.protocol === 'https:' ? 'wss://' : 'ws://'}${location.host}/snapshot`);
  socket.addEventListener('open', () => {
    setConnection(true);
    socket.send('status');
  });
  socket.addEventListener('message', handleMessage);
  socket.addEventListener('close', () => {
    setConnection(false);
    scheduleReconnect();
  });
  socket.addEventListener('error', () => {
    socket.close();
  });
}

function scheduleReconnect() {
  if (retryTimer) return;
  retryTimer = setTimeout(() => {
    retryTimer = null;
    connect();
  }, 2000);
}

function send(command) {
  if (!socket || socket.readyState !== WebSocket.OPEN) return;
  socket.send(command);
}

document.querySelectorAll('[data-command]').forEach(button => {
  button.addEventListener('click', () => send(button.dataset.command));
});

render();
connect();
