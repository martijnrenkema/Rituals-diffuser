#include "update_checker.h"
#include "config.h"
#include "logger.h"
#include <ArduinoJson.h>

#ifdef PLATFORM_ESP8266
    #include <ESP8266WiFi.h>
    #include <ESP8266HTTPClient.h>
    #include <WiFiClientSecure.h>
#else
    #include <WiFi.h>
    #include <HTTPClient.h>
    #include <WiFiClientSecure.h>
    #include <Update.h>
#endif

UpdateChecker updateChecker;

// GitHub API endpoint
static const char* GITHUB_API_URL = "https://api.github.com/repos/" UPDATE_GITHUB_REPO "/releases/latest";
static const char* GITHUB_RELEASES_URL = "https://github.com/" UPDATE_GITHUB_REPO "/releases";

void UpdateChecker::begin() {
    // Store boot time for overflow-safe first check timing
    _bootTime = millis();

    // Initialize current version
    strlcpy(_info.currentVersion, FIRMWARE_VERSION, sizeof(_info.currentVersion));
    memset(_info.latestVersion, 0, sizeof(_info.latestVersion));
    memset(_info.downloadUrl, 0, sizeof(_info.downloadUrl));
    memset(_info.releaseUrl, 0, sizeof(_info.releaseUrl));
    memset(_info.errorMessage, 0, sizeof(_info.errorMessage));
    _info.available = false;
    _info.lastCheckTime = 0;
    _info.downloadProgress = 0;

    // Set release URL to default
    strlcpy(_info.releaseUrl, GITHUB_RELEASES_URL, sizeof(_info.releaseUrl));

    logger.info("Update checker initialized");
}

void UpdateChecker::loop() {
    // Handle requested check
    if (_checkRequested && _state == UpdateCheckState::IDLE) {
        _checkRequested = false;
        performCheck();
    }

    // Auto-check every 24 hours (only when WiFi connected and idle)
    if (_state == UpdateCheckState::IDLE && WiFi.status() == WL_CONNECTED) {
        unsigned long now = millis();
        // Check if 24 hours have passed, or if we never checked and 2 minutes after boot
        bool shouldAutoCheck = false;

        if (_lastAutoCheck == 0) {
            // First check: wait 2 minutes after boot to let everything stabilize
            // Use subtraction to handle millis() overflow correctly
            if (now - _bootTime >= 120000) {
                shouldAutoCheck = true;
            }
        } else if (now - _lastAutoCheck >= UPDATE_CHECK_INTERVAL) {
            shouldAutoCheck = true;
        }

        if (shouldAutoCheck) {
            _lastAutoCheck = now;
            performCheck();
        }
    }

    #ifndef PLATFORM_ESP8266
    // Handle OTA update request
    if (_otaRequested && _state == UpdateCheckState::IDLE) {
        _otaRequested = false;
        performOTAUpdate();
    }
    #endif
}

void UpdateChecker::checkForUpdates() {
    if (_state != UpdateCheckState::IDLE) {
        logger.warn("Update check already in progress");
        return;
    }
    _checkRequested = true;
}

void UpdateChecker::performCheck() {
    if (WiFi.status() != WL_CONNECTED) {
        strlcpy(_info.errorMessage, "WiFi not connected", sizeof(_info.errorMessage));
        _state = UpdateCheckState::ERROR;
        if (_stateCallback) _stateCallback();
        _state = UpdateCheckState::IDLE;
        return;
    }

    _state = UpdateCheckState::CHECKING;
    memset(_info.errorMessage, 0, sizeof(_info.errorMessage));
    if (_stateCallback) _stateCallback();

    logger.info("Checking for updates...");

    if (fetchGitHubRelease()) {
        _info.lastCheckTime = millis();
        if (_info.available) {
            logger.infof("Update available: v%s", _info.latestVersion);
        } else {
            logger.info("Firmware is up to date");
        }
    } else {
        logger.warnf("Update check failed: %s", _info.errorMessage);
        _state = UpdateCheckState::ERROR;
        if (_stateCallback) _stateCallback();
    }

    _state = UpdateCheckState::IDLE;
    if (_stateCallback) _stateCallback();
}

bool UpdateChecker::fetchGitHubRelease() {
    WiFiClientSecure client;

    #ifdef PLATFORM_ESP8266
    client.setInsecure();  // Skip certificate verification
    client.setTimeout(UPDATE_CHECK_TIMEOUT);
    #else
    client.setInsecure();  // Skip certificate verification
    client.setTimeout(UPDATE_CHECK_TIMEOUT / 1000);  // ESP32 uses seconds
    #endif

    HTTPClient http;
    http.setTimeout(UPDATE_CHECK_TIMEOUT);
    http.setUserAgent("ESP-Rituals-Diffuser/" FIRMWARE_VERSION);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, GITHUB_API_URL)) {
        strlcpy(_info.errorMessage, "HTTP begin failed", sizeof(_info.errorMessage));
        return false;
    }

    // GitHub API requires Accept header
    http.addHeader("Accept", "application/vnd.github.v3+json");

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        if (httpCode == 403) {
            strlcpy(_info.errorMessage, "Rate limited", sizeof(_info.errorMessage));
        } else if (httpCode == 404) {
            strlcpy(_info.errorMessage, "No releases found", sizeof(_info.errorMessage));
        } else if (httpCode < 0) {
            snprintf(_info.errorMessage, sizeof(_info.errorMessage), "Connection failed: %d", httpCode);
        } else {
            snprintf(_info.errorMessage, sizeof(_info.errorMessage), "HTTP error: %d", httpCode);
        }
        http.end();
        return false;
    }

    // Get response
    String payload = http.getString();
    http.end();

    if (payload.length() == 0) {
        strlcpy(_info.errorMessage, "Empty response", sizeof(_info.errorMessage));
        return false;
    }

    return parseReleaseJson(payload.c_str(), payload.length());
}

bool UpdateChecker::parseReleaseJson(const char* json, size_t length) {
    // Use a filter to only parse the fields we need (reduces memory usage significantly)
    // GitHub API response is ~10KB but we only need a few fields
    StaticJsonDocument<200> filter;
    filter["tag_name"] = true;
    filter["html_url"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;

    // Parse JSON response with filter - this drastically reduces memory needed
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, json, length, DeserializationOption::Filter(filter));

    if (err) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "JSON error: %s", err.c_str());
        return false;
    }

    // Get tag_name (e.g., "v1.6.0")
    const char* tagName = doc["tag_name"];
    if (!tagName || strlen(tagName) == 0) {
        strlcpy(_info.errorMessage, "No tag_name in response", sizeof(_info.errorMessage));
        return false;
    }

    // Strip 'v' prefix if present
    const char* versionStr = tagName;
    if (versionStr[0] == 'v' || versionStr[0] == 'V') {
        versionStr++;
    }
    strlcpy(_info.latestVersion, versionStr, sizeof(_info.latestVersion));

    // Store release URL
    const char* htmlUrl = doc["html_url"];
    if (htmlUrl) {
        strlcpy(_info.releaseUrl, htmlUrl, sizeof(_info.releaseUrl));
    }

    // Compare versions
    _info.available = compareVersions(_info.latestVersion, _info.currentVersion) > 0;

    // Find firmware download URL (for ESP32 auto-update)
    #ifndef PLATFORM_ESP8266
    JsonArray assets = doc["assets"];
    for (JsonObject asset : assets) {
        const char* name = asset["name"];
        if (name) {
            // Look for esp32 firmware binary
            if (strstr(name, "esp32") != nullptr && strstr(name, ".bin") != nullptr) {
                const char* downloadUrl = asset["browser_download_url"];
                if (downloadUrl) {
                    strlcpy(_info.downloadUrl, downloadUrl, sizeof(_info.downloadUrl));
                    break;
                }
            }
        }
    }
    #endif

    return true;
}

int UpdateChecker::compareVersions(const char* v1, const char* v2) {
    // Parse semantic version: MAJOR.MINOR.PATCH
    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;

    sscanf(v1, "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(v2, "%d.%d.%d", &major2, &minor2, &patch2);

    if (major1 != major2) return major1 - major2;
    if (minor1 != minor2) return minor1 - minor2;
    return patch1 - patch2;
}

// ESP32-specific OTA update from GitHub
#ifndef PLATFORM_ESP8266

void UpdateChecker::startOTAUpdate() {
    if (_state != UpdateCheckState::IDLE) {
        logger.warn("Cannot start OTA: busy");
        return;
    }
    if (!_info.available) {
        strlcpy(_info.errorMessage, "No update available", sizeof(_info.errorMessage));
        return;
    }
    if (strlen(_info.downloadUrl) == 0) {
        strlcpy(_info.errorMessage, "No download URL", sizeof(_info.errorMessage));
        return;
    }

    _otaRequested = true;
}

void UpdateChecker::performOTAUpdate() {
    logger.infof("Starting OTA update from: %s", _info.downloadUrl);

    _state = UpdateCheckState::DOWNLOADING;
    _info.downloadProgress = 0;
    memset(_info.errorMessage, 0, sizeof(_info.errorMessage));
    if (_stateCallback) _stateCallback();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(60);  // 60 second timeout for download

    HTTPClient http;
    http.setTimeout(60000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, _info.downloadUrl)) {
        strlcpy(_info.errorMessage, "HTTP begin failed", sizeof(_info.errorMessage));
        _state = UpdateCheckState::ERROR;
        if (_stateCallback) _stateCallback();
        _state = UpdateCheckState::IDLE;
        return;
    }

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "Download failed: %d", httpCode);
        http.end();
        _state = UpdateCheckState::ERROR;
        if (_stateCallback) _stateCallback();
        _state = UpdateCheckState::IDLE;
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        strlcpy(_info.errorMessage, "Invalid content length", sizeof(_info.errorMessage));
        http.end();
        _state = UpdateCheckState::ERROR;
        if (_stateCallback) _stateCallback();
        _state = UpdateCheckState::IDLE;
        return;
    }

    logger.infof("Firmware size: %d bytes", contentLength);

    // Start update
    if (!Update.begin(contentLength)) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "Update begin failed: %s", Update.errorString());
        http.end();
        _state = UpdateCheckState::ERROR;
        if (_stateCallback) _stateCallback();
        _state = UpdateCheckState::IDLE;
        return;
    }

    // Stream firmware
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    size_t written = 0;
    size_t lastProgress = 0;
    unsigned long lastDataTime = millis();
    const unsigned long OTA_STREAM_TIMEOUT = 30000;  // 30 second timeout for stalled downloads

    while (http.connected() && written < (size_t)contentLength) {
        size_t available = stream->available();
        if (available > 0) {
            size_t toRead = min(available, sizeof(buffer));
            size_t bytesRead = stream->readBytes(buffer, toRead);

            if (Update.write(buffer, bytesRead) != bytesRead) {
                snprintf(_info.errorMessage, sizeof(_info.errorMessage), "Write failed: %s", Update.errorString());
                Update.abort();
                http.end();
                _state = UpdateCheckState::ERROR;
                if (_stateCallback) _stateCallback();
                _state = UpdateCheckState::IDLE;
                return;
            }

            written += bytesRead;
            lastDataTime = millis();  // Reset timeout on successful read

            // Update progress
            _info.downloadProgress = (written * 100) / contentLength;

            // Log progress every 10%
            if (_info.downloadProgress >= lastProgress + 10) {
                lastProgress = _info.downloadProgress;
                logger.infof("Download progress: %d%%", _info.downloadProgress);
                if (_stateCallback) _stateCallback();
            }
        } else {
            // No data available - check for timeout
            if (millis() - lastDataTime > OTA_STREAM_TIMEOUT) {
                strlcpy(_info.errorMessage, "Download timeout: no data", sizeof(_info.errorMessage));
                Update.abort();
                http.end();
                _state = UpdateCheckState::ERROR;
                if (_stateCallback) _stateCallback();
                _state = UpdateCheckState::IDLE;
                return;
            }
            delay(10);  // Small delay when waiting for data
        }
        yield();  // Feed watchdog
    }

    http.end();

    if (written != (size_t)contentLength) {
        strlcpy(_info.errorMessage, "Incomplete download", sizeof(_info.errorMessage));
        Update.abort();
        _state = UpdateCheckState::ERROR;
        if (_stateCallback) _stateCallback();
        _state = UpdateCheckState::IDLE;
        return;
    }

    // Finish update
    if (!Update.end(true)) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "Update end failed: %s", Update.errorString());
        _state = UpdateCheckState::ERROR;
        if (_stateCallback) _stateCallback();
        _state = UpdateCheckState::IDLE;
        return;
    }

    logger.info("OTA update successful! Restarting...");
    _info.downloadProgress = 100;
    if (_stateCallback) _stateCallback();

    delay(1000);
    ESP.restart();
}

#endif // !PLATFORM_ESP8266
