#include "config.h"  // Must be first for PLATFORM_ESP8266 detection

#ifdef PLATFORM_ESP8266

#include "sync_ota.h"
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <Updater.h>
#include <LittleFS.h>
#include "wifi_manager.h"
#include "mqtt_handler.h"
#include "led_controller.h"
// Note: Don't include logger.h - we avoid flash writes during OTA

// External variables from main.cpp
extern bool otaInProgress;
extern void updateLedStatus();

// Flag to signal main loop to switch to sync OTA mode
volatile bool requestSyncOTAMode = false;

// Linker symbols for filesystem size
extern "C" uint32_t _FS_start;
extern "C" uint32_t _FS_end;

// Forward declaration of webServer stop function
class WebServer;
extern WebServer webServer;

// =====================================================
// Synchronous OTA Server for ESP8266
// Used because AsyncWebServer + Update causes __yield panic
// =====================================================

// GitHub releases URL for manual checking
#define GITHUB_RELEASES_URL "https://github.com/" UPDATE_GITHUB_REPO "/releases"

// Generate OTA page with styling matching main update page
String generateOTAPage() {
    String p = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP8266 Firmware Update</title><style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
        "background:linear-gradient(135deg,#1a1a2e 0%,#16213e 100%);min-height:100vh;color:#fff;padding:20px}"
        ".container{max-width:500px;margin:0 auto}"
        "h1{text-align:center;margin-bottom:8px;font-size:1.5em}"
        ".subtitle{text-align:center;color:#888;margin-bottom:24px;font-size:.9em}"
        ".card{background:rgba(255,255,255,.05);border-radius:16px;padding:20px;margin-bottom:16px;"
        "border:1px solid rgba(255,255,255,.1)}"
        ".card h2{font-size:1em;margin-bottom:12px}"
        ".version{color:#888;font-size:.85em;margin-bottom:12px}"
        ".ok{color:#22c55e}.err{color:#ef4444}"
        "input[type=file]{width:100%;padding:12px;margin:8px 0;background:rgba(255,255,255,.1);"
        "border:1px solid rgba(255,255,255,.2);border-radius:8px;color:#fff}"
        "button{width:100%;padding:14px;border:none;border-radius:10px;font-size:1em;font-weight:600;"
        "cursor:pointer;background:linear-gradient(135deg,#6366f1,#8b5cf6);color:#fff;margin-top:8px}"
        "button:hover{opacity:.9}"
        ".link{display:inline-block;padding:14px 20px;background:linear-gradient(135deg,#6366f1,#8b5cf6);"
        "color:#fff;text-decoration:none;border-radius:10px;font-weight:600;text-align:center;width:100%;margin-top:8px}"
        ".link:hover{opacity:.9}"
        "</style></head><body><div class='container'>"
        "<h1>Firmware Update</h1>"
        "<p class='subtitle'>ESP8266 Safe Update Mode</p>"
        "<div class='card'><h2>Version Info</h2>"
        "<p class='version'>Current version: <b>");
    p += FIRMWARE_VERSION;
    p += F("</b></p>"
        "<a class='link' href='" GITHUB_RELEASES_URL "' target='_blank'>View Releases on GitHub</a></div>"
        "<div class='card'><h2>Upload Firmware</h2>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='firmware' accept='.bin' required>"
        "<button type='submit'>Upload Firmware</button></form></div>"
        "<div class='card'><h2>Upload Web Interface</h2>"
        "<form method='POST' action='/update-fs' enctype='multipart/form-data'>"
        "<input type='file' name='filesystem' accept='.bin' required>"
        "<button type='submit'>Upload Filesystem</button></form></div>"
        "<a class='link' href='/restart' style='background:#374151;margin-top:8px'>Exit Safe Mode & Restart</a>"
        "</div></body></html>");
    return p;
}

const char SYNC_OTA_SUCCESS[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Update Success</title>
    <style>
        body{font-family:-apple-system,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);min-height:100vh;color:#fff;display:flex;align-items:center;justify-content:center}
        .msg{text-align:center;padding:40px}
        h1{color:#22c55e;margin-bottom:20px}
    </style>
</head>
<body>
    <div class="msg">
        <h1>Update Successful!</h1>
        <p>Device is restarting...</p>
        <p style="color:#888;margin-top:20px">You will be redirected in 10 seconds.</p>
    </div>
    <script>setTimeout(()=>location.href='/',10000);</script>
</body>
</html>
)rawliteral";

// Minimal error page - no template replacement needed to save RAM
const char SYNC_OTA_FAIL[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Update Failed</title>
    <style>
        body{font-family:-apple-system,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);min-height:100vh;color:#fff;display:flex;align-items:center;justify-content:center}
        .msg{text-align:center;padding:40px}
        h1{color:#ef4444;margin-bottom:20px}
        a{color:#6366f1}
    </style>
</head>
<body>
    <div class="msg">
        <h1>Update Failed</h1>
        <p>Check serial monitor for details</p>
        <p style="margin-top:20px"><a href="/">Try again</a></p>
    </div>
</body>
</html>
)rawliteral";

// Run the synchronous OTA server (blocking - takes over from main loop)
void runSyncOTAServer() {
    Serial.println("[OTA-SYNC] Starting synchronous OTA server...");
    // Note: Don't use logger during OTA - it writes to flash which can conflict

    // Stop MQTT to free memory and prevent interference
    mqttHandler.disconnect();
    Serial.println("[OTA-SYNC] MQTT disconnected");

    // Stop the async web server by calling its stop method
    // We use extern to access it without including the header
    extern void stopAsyncWebServer();
    stopAsyncWebServer();
    Serial.println("[OTA-SYNC] Async web server stopped");

    // Show OTA LED status
    otaInProgress = true;
    updateLedStatus();

    // Give some time for connections to close and memory to be freed
    delay(500);

    // Log free heap after cleanup
    Serial.printf("[OTA-SYNC] Free heap after cleanup: %u bytes\n", ESP.getFreeHeap());

    // Create synchronous web server
    ESP8266WebServer syncServer(80);

    // Serve the OTA page with dynamic version info
    syncServer.on("/", HTTP_GET, [&syncServer]() {
        String page = generateOTAPage();
        syncServer.send(200, "text/html", page);
    });

    // Exit safe mode and restart
    syncServer.on("/restart", HTTP_GET, [&syncServer]() {
        syncServer.send(200, "text/html",
            "<html><body style='background:#1a1a2e;color:#fff;font-family:sans-serif;text-align:center;padding:50px'>"
            "<h2>Restarting...</h2><p>Returning to normal mode.</p>"
            "<script>setTimeout(()=>location.href='/',5000)</script></body></html>");
        delay(500);
        ESP.restart();
    });

    // Handle firmware upload
    syncServer.on("/update", HTTP_POST, [&syncServer]() {
        if (Update.hasError()) {
            Serial.printf("[OTA-SYNC] Firmware update error: %s\n", Update.getErrorString().c_str());
            syncServer.send_P(200, "text/html", SYNC_OTA_FAIL);  // Send from PROGMEM, no RAM copy
        } else {
            syncServer.send_P(200, "text/html", SYNC_OTA_SUCCESS);
            delay(1000);
            ESP.restart();
        }
    }, [&syncServer]() {
        HTTPUpload& upload = syncServer.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("[OTA-SYNC] Firmware upload start: %s\n", upload.filename.c_str());
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (!Update.begin(maxSketchSpace, U_FLASH)) {
                Serial.printf("[OTA-SYNC] Update.begin failed: %s\n", Update.getErrorString().c_str());
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Serial.printf("[OTA-SYNC] Update.write failed: %s\n", Update.getErrorString().c_str());
            }
            // Feed watchdog
            ESP.wdtFeed();
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("[OTA-SYNC] Firmware update success: %u bytes\n", upload.totalSize);
            } else {
                Serial.printf("[OTA-SYNC] Update.end failed: %s\n", Update.getErrorString().c_str());
            }
        }
    });

    // Handle filesystem upload
    syncServer.on("/update-fs", HTTP_POST, [&syncServer]() {
        if (Update.hasError()) {
            Serial.printf("[OTA-SYNC] Filesystem update error: %s\n", Update.getErrorString().c_str());
            syncServer.send_P(200, "text/html", SYNC_OTA_FAIL);  // Send from PROGMEM, no RAM copy
        } else {
            syncServer.send_P(200, "text/html", SYNC_OTA_SUCCESS);
            delay(1000);
            ESP.restart();
        }
    }, [&syncServer]() {
        HTTPUpload& upload = syncServer.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("[OTA-SYNC] Filesystem upload start: %s\n", upload.filename.c_str());
            size_t fsSize = ((size_t)&_FS_end - (size_t)&_FS_start);
            LittleFS.end();  // Unmount filesystem before update
            if (!Update.begin(fsSize, U_FS)) {
                Serial.printf("[OTA-SYNC] Update.begin failed: %s\n", Update.getErrorString().c_str());
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Serial.printf("[OTA-SYNC] Update.write failed: %s\n", Update.getErrorString().c_str());
            }
            ESP.wdtFeed();
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("[OTA-SYNC] Filesystem update success: %u bytes\n", upload.totalSize);
            } else {
                Serial.printf("[OTA-SYNC] Update.end failed: %s\n", Update.getErrorString().c_str());
            }
        }
    });

    syncServer.begin();
    Serial.println("[OTA-SYNC] Server started on port 80");
    Serial.println("[OTA-SYNC] Navigate to http://" + wifiManager.getIP() + "/ to upload firmware");

    // Run the server indefinitely (until reboot after update)
    // This is blocking - takes over from main loop
    while (true) {
        syncServer.handleClient();
        ESP.wdtFeed();
        delay(10);
    }
}

#endif // PLATFORM_ESP8266
