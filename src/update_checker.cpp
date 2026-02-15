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
    memset(_info.spiffsUrl, 0, sizeof(_info.spiffsUrl));
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

    // Auto-check logic (only when WiFi connected and idle)
    if (_state == UpdateCheckState::IDLE && WiFi.status() == WL_CONNECTED) {
        unsigned long now = millis();
        bool shouldAutoCheck = false;

#ifdef PLATFORM_ESP8266
        // ESP8266: Only check ONCE, 15 seconds after boot
        // This runs before MQTT is fully connected, when more heap is available
        // Skip periodic checks - not enough RAM for BearSSL TLS after everything starts
        if (_lastAutoCheck == 0 && (now - _bootTime >= 15000)) {
            shouldAutoCheck = true;
        }
#else
        // ESP32: Check 2 minutes after boot, then every 24 hours
        if (_lastAutoCheck == 0) {
            if (now - _bootTime >= 120000) {
                shouldAutoCheck = true;
            }
        } else if (now - _lastAutoCheck >= UPDATE_CHECK_INTERVAL) {
            shouldAutoCheck = true;
        }
#endif

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

#ifdef PLATFORM_ESP8266
    // ESP8266: Check if we have enough memory for BearSSL TLS handshake
    uint32_t freeHeap = ESP.getFreeHeap();
    logger.infof("Free heap for update check: %lu bytes", freeHeap);

    // Need ~15KB minimum for BearSSL
    if (freeHeap < 15000) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "Low memory (%lu bytes)", freeHeap);
        logger.warnf("Update check skipped: only %lu bytes free", freeHeap);
        _state = UpdateCheckState::ERROR;
        if (_stateCallback) _stateCallback();
        _state = UpdateCheckState::IDLE;
        return;
    }
#endif

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
    // ESP8266 has limited RAM (~80KB) - reduce BearSSL buffers from default 16KB
    // This prevents OOM when making HTTPS requests
    client.setBufferSizes(512, 512);
    client.setInsecure();  // Skip certificate verification
    client.setTimeout(UPDATE_CHECK_TIMEOUT);
    logger.infof("Free heap: %d bytes", ESP.getFreeHeap());
    #else
    // ESP32 - older framework versions may not have setInsecure()
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 2
    client.setInsecure();  // Skip certificate verification (ESP32 v2.x+)
    #endif
    client.setTimeout(UPDATE_CHECK_TIMEOUT / 1000);  // ESP32 uses seconds
    #endif

    HTTPClient http;
    http.setTimeout(UPDATE_CHECK_TIMEOUT);
    http.setUserAgent("ESP-Rituals-Diffuser/" FIRMWARE_VERSION);
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 2
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // ESP32 v2.x+ only
    #endif

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
    // Reduced from 2048 to 1536 bytes for better ESP8266 memory usage
    DynamicJsonDocument doc(1536);
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

    // Find firmware and SPIFFS download URLs (for ESP32 auto-update)
    #ifndef PLATFORM_ESP8266
    JsonArray assets = doc["assets"];
    for (JsonObject asset : assets) {
        const char* name = asset["name"];
        if (name) {
            const char* downloadUrl = asset["browser_download_url"];
            if (downloadUrl) {
                #ifdef ESP32C3_SUPERMINI
                // ESP32-C3: Look for esp32c3 binaries
                if (strstr(name, "firmware") != nullptr && strstr(name, "esp32c3") != nullptr) {
                    strlcpy(_info.downloadUrl, downloadUrl, sizeof(_info.downloadUrl));
                }
                else if (strstr(name, "spiffs") != nullptr && strstr(name, "esp32c3") != nullptr) {
                    strlcpy(_info.spiffsUrl, downloadUrl, sizeof(_info.spiffsUrl));
                }
                #else
                // Standard ESP32: Look for esp32 binaries but NOT esp32c3
                if (strstr(name, "firmware") != nullptr && strstr(name, "esp32") != nullptr && strstr(name, "esp32c3") == nullptr) {
                    strlcpy(_info.downloadUrl, downloadUrl, sizeof(_info.downloadUrl));
                }
                else if (strstr(name, "spiffs") != nullptr && strstr(name, "esp32") != nullptr && strstr(name, "esp32c3") == nullptr) {
                    strlcpy(_info.spiffsUrl, downloadUrl, sizeof(_info.spiffsUrl));
                }
                #endif
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

bool UpdateChecker::downloadAndInstall(const char* url, int updateType, const char* label) {
    // ESP8266: Check heap before starting OTA to prevent crashes
    #ifdef PLATFORM_ESP8266
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 25000) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "Low memory: %lu bytes", freeHeap);
        logger.errorf("OTA aborted: only %lu bytes free", freeHeap);
        return false;
    }
    #endif

    logger.infof("Downloading %s from: %s", label, url);

    WiFiClientSecure client;
    #if defined(PLATFORM_ESP8266) || (defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 2)
    client.setInsecure();  // Skip certificate verification
    #endif
    client.setTimeout(60);

    // Reduce BearSSL buffers on ESP8266 to prevent OOM during OTA
    #ifdef PLATFORM_ESP8266
    client.setBufferSizes(512, 512);
    #endif

    HTTPClient http;
    http.setTimeout(60000);
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 2
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // ESP32 v2.x+ only
    #endif

    if (!http.begin(client, url)) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "%s: HTTP begin failed", label);
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "%s failed: %d", label, httpCode);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "%s: invalid size", label);
        http.end();
        return false;
    }

    logger.infof("%s size: %d bytes", label, contentLength);

    if (!Update.begin(contentLength, updateType)) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "%s begin failed: %s", label, Update.errorString());
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    #ifdef PLATFORM_ESP8266
    uint8_t buffer[512];  // Smaller buffer for ESP8266's limited stack
    #else
    uint8_t buffer[1024];
    #endif
    size_t written = 0;
    size_t lastProgress = 0;
    unsigned long lastDataTime = millis();
    const unsigned long OTA_STREAM_TIMEOUT = 30000;

    while (http.connected() && written < (size_t)contentLength) {
        size_t available = stream->available();
        if (available > 0) {
            size_t toRead = min(available, sizeof(buffer));
            size_t bytesRead = stream->readBytes(buffer, toRead);

            // Feed watchdog before flash write (can be slow)
            #ifdef PLATFORM_ESP8266
            ESP.wdtFeed();
            #endif
            yield();

            if (Update.write(buffer, bytesRead) != bytesRead) {
                snprintf(_info.errorMessage, sizeof(_info.errorMessage), "%s write failed", label);
                Update.abort();
                http.end();
                return false;
            }

            written += bytesRead;
            lastDataTime = millis();
            _info.downloadProgress = (written * 100) / contentLength;

            if (_info.downloadProgress >= lastProgress + 10) {
                lastProgress = _info.downloadProgress;
                logger.infof("%s progress: %d%%", label, _info.downloadProgress);
                if (_stateCallback) _stateCallback();
            }

            // Feed watchdog after flash write
            #ifdef PLATFORM_ESP8266
            ESP.wdtFeed();
            #endif
            yield();
        } else {
            if (millis() - lastDataTime > OTA_STREAM_TIMEOUT) {
                snprintf(_info.errorMessage, sizeof(_info.errorMessage), "%s timeout", label);
                Update.abort();
                http.end();
                return false;
            }
            delay(10);
            #ifdef PLATFORM_ESP8266
            ESP.wdtFeed();
            #endif
        }
        yield();
    }

    http.end();

    if (written != (size_t)contentLength) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "%s incomplete", label);
        Update.abort();
        return false;
    }

    if (!Update.end(true)) {
        snprintf(_info.errorMessage, sizeof(_info.errorMessage), "%s end failed: %s", label, Update.errorString());
        return false;
    }

    logger.infof("%s complete!", label);
    return true;
}

void UpdateChecker::performOTAUpdate() {
    _state = UpdateCheckState::DOWNLOADING;
    _info.downloadProgress = 0;
    memset(_info.errorMessage, 0, sizeof(_info.errorMessage));
    if (_stateCallback) _stateCallback();

    // Step 1: Download and install firmware
    if (!downloadAndInstall(_info.downloadUrl, U_FLASH, "Firmware")) {
        _state = UpdateCheckState::ERROR;
        if (_stateCallback) _stateCallback();
        _state = UpdateCheckState::IDLE;
        return;
    }

    // Step 2: Download and install SPIFFS (if URL available)
    if (strlen(_info.spiffsUrl) > 0) {
        _info.downloadProgress = 0;
        if (_stateCallback) _stateCallback();

        if (!downloadAndInstall(_info.spiffsUrl, U_SPIFFS, "SPIFFS")) {
            // SPIFFS failed, but firmware was already installed
            // Log warning but still restart to apply firmware
            logger.warn("SPIFFS update failed, but firmware was installed");
        }
    }

    logger.info("OTA update complete! Restarting...");
    _info.downloadProgress = 100;
    if (_stateCallback) _stateCallback();

    delay(1000);
    ESP.restart();
}

#endif // !PLATFORM_ESP8266
