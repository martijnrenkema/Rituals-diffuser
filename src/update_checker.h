#ifndef UPDATE_CHECKER_H
#define UPDATE_CHECKER_H

#include <Arduino.h>
#include "config.h"

// Update check states
enum class UpdateCheckState {
    IDLE,
    CHECKING,
    DOWNLOADING,    // ESP32 only
    ERROR
};

// Update result info
struct UpdateInfo {
    bool available;
    char latestVersion[16];     // e.g., "1.6.0"
    char currentVersion[16];    // e.g., "1.5.4"
#ifdef PLATFORM_ESP8266
    // ESP8266 uses web OTA, not direct download - smaller buffers
    char downloadUrl[1];        // Placeholder, not used on ESP8266
    char spiffsUrl[1];          // Placeholder, not used on ESP8266
    char releaseUrl[100];       // GitHub releases page URL (shorter)
    char errorMessage[48];      // Shorter error messages
#else
    char downloadUrl[196];      // GitHub release asset URL (firmware)
    char spiffsUrl[196];        // GitHub release asset URL (SPIFFS)
    char releaseUrl[128];       // GitHub releases page URL
    char errorMessage[64];
#endif
    unsigned long lastCheckTime;// millis() of last successful check
    uint8_t downloadProgress;   // 0-100 for ESP32 download
};

class UpdateChecker {
public:
    void begin();
    void loop();

    // Check for updates (non-blocking trigger, actual check happens in loop)
    void checkForUpdates();

    // Get current state
    UpdateCheckState getState() const { return _state; }
    const UpdateInfo& getInfo() const { return _info; }
    bool isUpdateAvailable() const { return _info.available; }
    const char* getLatestVersion() const { return _info.latestVersion; }
    const char* getCurrentVersion() const { return _info.currentVersion; }
    const char* getReleaseUrl() const { return _info.releaseUrl; }
    const char* getErrorMessage() const { return _info.errorMessage; }
    uint8_t getDownloadProgress() const { return _info.downloadProgress; }
    unsigned long getLastCheckTime() const { return _info.lastCheckTime; }

    // ESP32 only: Start OTA download from GitHub
    #ifndef PLATFORM_ESP8266
    void startOTAUpdate();
    bool isDownloading() const { return _state == UpdateCheckState::DOWNLOADING; }
    #endif

    // Callback for state changes (for MQTT publish trigger)
    typedef void (*UpdateStateCallback)();
    void onStateChange(UpdateStateCallback callback) { _stateCallback = callback; }

private:
    UpdateCheckState _state = UpdateCheckState::IDLE;
    UpdateInfo _info;
    UpdateStateCallback _stateCallback = nullptr;

    unsigned long _lastAutoCheck = 0;
    unsigned long _bootTime = 0;  // Stored at begin() for overflow-safe timing
    bool _checkRequested = false;

    // Actual HTTP check (blocking, called from loop)
    void performCheck();
    bool fetchGitHubRelease();
    bool parseReleaseJson(const char* json, size_t length);
    int compareVersions(const char* v1, const char* v2);

    #ifndef PLATFORM_ESP8266
    bool _otaRequested = false;
    void performOTAUpdate();
    bool downloadAndInstall(const char* url, int updateType, const char* label);
    #endif
};

extern UpdateChecker updateChecker;

#endif // UPDATE_CHECKER_H
