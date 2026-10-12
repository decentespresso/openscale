void connectHandoffStation() {
  WiFi.stationStatus = WL_CONNECTED;
  WiFi.ip.value = 1;
  wifiGotIpGeneration = wifiGotIpGeneration + 1;
  wifiProcessSetup();
}

uint32_t prepareHandoffTest(bool provisioning = false, bool savedRecovery = false) {
  seedCredentials(provisioning && !savedRecovery, provisioning && !savedRecovery);
  wifiSetupRuntime = {};
  wifiCancelSetup();
  WiFi.currentMode = provisioning ? WIFI_AP : WIFI_STA;
  const int previousApStops = WiFi.apStops;
  const int previousApStarts = apStarts;
  WifiCredentials candidate;
  strcpy(candidate.ssid, "New network");
  strcpy(candidate.pass, "new pass");
  uint32_t id;
  assert(wifiQueueSetup(WifiSetupCommand::Test, candidate, id));
  assert(!wifiQueueActivation(id));
  wifiProcessSetup();
  clockMs += 500;
  wifiProcessSetup();
  assert(WiFi.currentMode == (provisioning ? WIFI_AP_STA : WIFI_STA));
  if (provisioning) assert(WiFi.apStops == previousApStops && apStarts == previousApStarts);
  connectHandoffStation();
  clockMs += 3000;
  wifiProcessSetup();
  assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Returning);
  assert(storageWrites == 0);
  assert(!wifiQueueActivation(id));
  if (!provisioning) connectHandoffStation();
  else wifiProcessSetup();
  const WifiSetupStatus status = wifiReadSetupStatus();
  assert(status.phase == WifiSwitchPhase::Ready && !wifiSetupBusy());
  assert(strcmp(status.testedIp, "192.168.50.30") == 0);
  assert(strcmp(status.requestedSsid, "New network") == 0);
  assert(status.operationId == id && storageWrites == 0);
  if (provisioning) {
    assert(status.accessPoint && params.hasCredentials() == savedRecovery);
    assert(WiFi.apStops == previousApStops && apStarts == previousApStarts);
  }
  if (!provisioning || savedRecovery) assertPreviousCredentials();
  assert(!wifiQueueActivation(0) && !wifiQueueActivation(id + 1));
  assert(wifiReserveExternalOperation());
  assert(!wifiQueueActivation(id));
  wifiReleaseExternalOperation();
  b_ota = true;
  assert(!wifiQueueActivation(id));
  b_ota = false;
  return id;
}

void activateHandoff(uint32_t id) {
  assert(wifiQueueActivation(id));
  assert(!wifiQueueActivation(id));
  wifiProcessSetup();
  assert(wifiSetupRuntime.operationId == id && storageWrites == 0);
  assert(strcmp(wifiSetupRuntime.candidate.ssid, "New network") == 0);
  clockMs += 500;
  wifiProcessSetup();
  assert(WiFi.stationSsid == "New network");
}

void testWifiHandoffRuntime() {
  for (const int source : {0, 1, 2}) {
    const uint32_t id = prepareHandoffTest(source != 0, source == 2);
    activateHandoff(id);
    connectHandoffStation();
    clockMs += 3000;
    wifiProcessSetup();
    assert(wifiReadSetupStatus().phase == WifiSwitchPhase::Succeeded);
    assert(!wifiSetupBusy() && params.getSSID() == "New network");
    assert(wifiSetupRuntime.candidate.ssid[0] == 0);
    WiFiParams reloaded;
    reloaded.init();
    assert(reloaded.getSSID() == "New network" && reloaded.getPass() == "new pass");
    assert(!wifiQueueActivation(id));
    const int completedStationStarts = stationStarts;
    clockMs += 600000;
    wifiProcessSetup();
    assert(stationStarts == completedStationStarts);
    assert(wifiReadSetupStatus().phase == WifiSwitchPhase::Succeeded);
  }
  for (const bool storageFailure : {false, true}) {
    activateHandoff(prepareHandoffTest());
    if (storageFailure) {
      connectHandoffStation();
      failedWrites = 1;
      clockMs += 3000;
    } else clockMs += 20000;
    wifiProcessSetup();
    assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Restoring);
    connectHandoffStation();
    assert(wifiReadSetupStatus().phase == WifiSwitchPhase::Failed);
    assert(WiFi.stationSsid == "Old network" && !wifiSetupBusy());
    assertPreviousCredentials();
  }
  for (const unsigned long elapsed : {119999ul, 120000ul}) {
    const uint32_t id = prepareHandoffTest();
    clockMs += elapsed;
    wifiProcessSetup();
    assertPreviousCredentials();
    assert(wifiQueueActivation(id) == (elapsed < 120000));
    if (elapsed < 120000) {
      wifiProcessSetup();
      wifiCancelSetup();
    } else assert(wifiSetupRuntime.candidate.ssid[0] == 0);
  }
  prepareHandoffTest();
  assert(wifiReserveExternalOperation());
  clockMs += 120000;
  wifiProcessSetup();
  assert(wifiReadSetupStatus().phase == WifiSwitchPhase::Failed);
  assert(wifiSetupRuntime.candidate.ssid[0] == 0);
  assertPreviousCredentials();
  assert(wifiSetupBusy());
  wifiReleaseExternalOperation();
  const uint32_t id = prepareHandoffTest();
  assert(wifiQueueActivation(id));
  clockMs += 120000;
  wifiProcessSetup();
  assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Failed);
  assertPreviousCredentials();
  for (const bool candidateWorks : {false, true}) {
    seedCredentials(false);
    wifiSetupRuntime = {};
    WifiCredentials candidate;
    strcpy(candidate.ssid, "New network");
    strcpy(candidate.pass, "new pass");
    uint32_t operationId;
    assert(wifiQueueSetup(WifiSetupCommand::Test, candidate, operationId));
    wifiProcessSetup();
    clockMs += 500;
    wifiProcessSetup();
    if (candidateWorks) {
      connectHandoffStation();
      clockMs += 3000;
    } else clockMs += 20000;
    wifiProcessSetup();
    clockMs += 20000;
    wifiProcessSetup();
    assert(wifiReadSetupStatus().phase == WifiSwitchPhase::Failed);
    assert(WiFi.currentMode == WIFI_AP && !wifiSetupBusy());
    assert(!wifiQueueActivation(operationId) && storageWrites == 0);
    assertPreviousCredentials();
  }
  prepareHandoffTest();
  wifiCancelSetup();
  wifiPublishSetupStatus();
  assert(!wifiQueueActivation(id));
  assert(wifiSetupRuntime.candidate.ssid[0] == 0);
  stationStarts = 0;
  apStarts = 0;
}

void testSetupApFailureKeepsBrowserNetwork() {
  for (const bool savedRecovery : {false, true}) {
    seedCredentials(!savedRecovery, !savedRecovery);
    wifiCancelSetup();
    WiFi.currentMode = WIFI_AP;
    const int previousApStops = WiFi.apStops;
    const int previousApStarts = apStarts;
    WifiCredentials candidate;
    strcpy(candidate.ssid, "New network");
    strcpy(candidate.pass, "wrong password");
    uint32_t id;
    assert(wifiQueueSetup(WifiSetupCommand::Test, candidate, id));
    wifiProcessSetup();
    clockMs += 500;
    wifiProcessSetup();
    assert(WiFi.currentMode == WIFI_AP_STA);
    wifiDisconnectReason = WIFI_REASON_AUTH_FAIL;
    clockMs += 20000;
    wifiProcessSetup();
    const WifiSetupStatus status = wifiReadSetupStatus();
    assert(status.phase == WifiSwitchPhase::Failed && status.error == WifiSetupError::Authentication);
    assert(status.accessPoint && !status.connected && !wifiSetupBusy());
    assert(WiFi.currentMode == WIFI_AP && storageWrites == 0);
    assert(WiFi.apStops == previousApStops && apStarts == previousApStarts);
    assert(params.hasCredentials() == savedRecovery);
    if (savedRecovery) assertPreviousCredentials();
  }
  const int previousRadioStops = WiFi.radioStops;
  WiFi.disconnectOk = false;
  prepareHandoffTest(true);
  assert(WiFi.radioStops == previousRadioStops);
  WiFi.disconnectOk = true;
  wifiCancelSetup();
  stationStarts = 0;
  apStarts = 0;
}

void testSetupApRecoveryRetryReleasesReservation() {
  for (const bool candidateWorks : {false, true}) {
    seedCredentials(false);
    wifiSetupRuntime = {};
    wifiCancelSetup();
    WiFi.currentMode = WIFI_AP;
    WifiCredentials candidate;
    strcpy(candidate.ssid, "New network");
    strcpy(candidate.pass, "new pass");
    uint32_t id;
    assert(wifiQueueSetup(WifiSetupCommand::Test, candidate, id));
    wifiProcessSetup();
    clockMs += 500;
    wifiProcessSetup();
    if (candidateWorks) {
      connectHandoffStation();
      clockMs += 3000;
      wifiProcessSetup();
      wifiProcessSetup();
      assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Ready);
      clockMs += 120000;
    } else {
      wifiDisconnectReason = WIFI_REASON_AUTH_FAIL;
      clockMs += 20000;
    }
    wifiProcessSetup();
    assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Failed);
    assert(WiFi.currentMode == WIFI_AP && !wifiSetupBusy());
    assertPreviousCredentials();
    const unsigned long openedAt = wifiSetupRuntime.recoveryApAt;
    const int previousStationStarts = stationStarts;
    clockMs = openedAt + 599999;
    wifiProcessSetup();
    assert(stationStarts == previousStationStarts);
    assert(wifiReserveExternalOperation());
    clockMs = openedAt + 600000;
    wifiProcessSetup();
    assert(stationStarts == previousStationStarts);
    wifiReleaseExternalOperation();
    wifiProcessSetup();
    assert(stationStarts == previousStationStarts + 1 && WiFi.stationSsid == "Old network");
    assert(WiFi.currentMode == WIFI_STA && wifiSetupBusy());
    assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Restoring);
    assert(!wifiSetupRuntime.change.testOnly);
    clockMs += 20000;
    wifiProcessSetup();
    assert(WiFi.currentMode == WIFI_AP && !wifiSetupBusy());
    assert(wifiReserveExternalOperation());
    wifiReleaseExternalOperation();
    clockMs += 600000;
    wifiProcessSetup();
    assert(stationStarts == previousStationStarts + 2);
    assert(WiFi.currentMode == WIFI_STA && wifiSetupBusy());
    connectHandoffStation();
    assert(!wifiSetupBusy() && wifiSetupRuntime.recoveryApAt == 0);
    assert(storageWrites == 0);
    assertPreviousCredentials();
  }
  wifiCancelSetup();
  stationStarts = 0;
  apStarts = 0;
}
