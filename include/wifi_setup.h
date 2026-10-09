#ifndef WIFI_SETUP
#define WIFI_SETUP

#include <stddef.h>
#include <Arduino.h>
#include "wifi_credentials.h"
#include "wifi_switch.h"
#include "mdns_name.h"

void setupWifi();
void stopWifi();
bool saveCredentials(const String &ssid, const String &pass);
bool wifiCredentialsSaved();
#if HDS_FEATURE_MDNS
bool wifiEnsureMdnsReadyForSta();
#endif

const char *wifiDeviceName();

bool saveDeviceNameForRestart(const char *name, char *stored, size_t storedSize);

void wifiSupervise();

constexpr unsigned long WIFI_SUPERVISE_INTERVAL_MS = 250;

enum class WifiSetupCommand : uint8_t { None, Switch, Scan, Reset };
enum class WifiScanState : uint8_t { Idle, Running, Complete, Failed };

struct WifiSetupRequest {
  WifiSetupCommand command = WifiSetupCommand::None;
  WifiCredentials credentials;
  uint32_t operationId = 0;
  unsigned long queuedAt = 0;
};

struct WifiSetupStatus {
  WifiSwitchPhase phase = WifiSwitchPhase::Idle;
  WifiSetupError error = WifiSetupError::None;
  uint32_t operationId = 0;
  char requestedSsid[33] = {};
  char ssid[33] = {};
  char ip[16] = {};
  bool connected = false;
  bool credentialsSaved = false;
  bool accessPoint = false;
};

struct WifiScanNetwork {
  char ssid[33] = {};
  int16_t rssi = 0;
  bool secure = false;
};

struct WifiScanResult {
  WifiScanState state = WifiScanState::Idle;
  uint8_t count = 0;
  WifiScanNetwork networks[20] = {};
};

struct WifiSetupRuntime {
  WifiSwitch change;
  WifiCredentials candidate;
  uint32_t operationId = 0;
  char requestedSsid[33] = {};
  uint32_t ipBaseline = 0;
  unsigned long stationStartedAt = 0;
  unsigned long recoveryApAt = 0;
  unsigned long scanStartedAt = 0;
  uint8_t scanPreviousMode = 0;
};

void wifiInitLocks();
bool wifiRequestWorkerStart();
void wifiWorkerStarted();
void wifiWorkerStartFailed();
bool wifiWorkerNeedsStart();
bool wifiQueueSetup(WifiSetupCommand command, const WifiCredentials &credentials,
                   uint32_t &operationId);
bool wifiSetupBusy();
bool wifiReserveExternalOperation();
void wifiReleaseExternalOperation();
WifiSetupStatus wifiReadSetupStatus();
WifiScanResult wifiReadScanResult();
void wifiProcessSetup();
void wifiCancelSetup();
void wifiPublishSetupStatus();
void wifiStartStation(const char *ssid, const char *pass);
void setupAP();
const char *wifiSetupPhaseName(WifiSwitchPhase phase);
const char *wifiSetupErrorName(WifiSetupError error);

extern portMUX_TYPE wifiSetupMux;
extern SemaphoreHandle_t wifiRadioMutex;
extern SemaphoreHandle_t wifiSettingsMutex;
extern StaticSemaphore_t wifiRadioMutexStorage;
extern StaticSemaphore_t wifiSettingsMutexStorage;
extern volatile TaskHandle_t wifiWorkerTask;
extern volatile bool wifiWorkerStarting;
extern volatile bool wifiWorkerStartRequested;
extern volatile bool wifiWorkerStopRequested;
extern volatile bool wifiOperationReserved;
extern volatile WifiSetupRequest wifiPendingRequest;
extern volatile WifiSetupStatus wifiSetupStatus;
extern volatile WifiScanResult wifiScanResult;
extern volatile uint32_t wifiGotIpGeneration;
extern volatile uint32_t wifiDisconnectGeneration;
extern volatile uint16_t wifiDisconnectReason;
extern volatile bool wifiStaAssociated;
extern WifiSetupRuntime wifiSetupRuntime;

extern volatile bool b_wifiEnabled;

extern const char *wifiPrefsKey;
extern const char *wifiSSIDKey;
extern const char *wifiPassKey;
extern const char *wifiMdnsNameKey;

#endif
