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
#include "fan_controller.h"
// Note: Don't include logger.h - we avoid flash writes during OTA

// External variables from main.cpp
extern volatile bool otaInProgress;
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

// Generate OTA page with XHR-based uploads and progress bars
String generateOTAPage() {
    String p = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP8266 Firmware Update</title><style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        ":root{--bg:#FAFAFA;--c:#fff;--i:#09090B;--m:#71717A;--l:#E4E4E7;--s:#F4F4F5;--p:#2563EB}"
        "@media(prefers-color-scheme:dark){:root{--bg:#09090B;--c:#18181B;--i:#FAFAFA;--m:#A1A1AA;--l:#27272A;--s:#202023}}"
        "body{font:15px/1.45 Inter,system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--i);padding:20px 16px}"
        ".ct{max-width:480px;margin:0 auto}"
        "h1{font-size:26px;font-weight:600;letter-spacing:-.025em}"
        ".sub{color:var(--m);margin:2px 0 20px;font-size:14px}"
        ".cd{background:var(--c);border:1px solid var(--l);border-radius:12px;padding:16px;margin-bottom:12px}"
        ".cd h2{font-size:16px;font-weight:600;margin-bottom:10px}"
        ".ver{color:var(--m);font-size:14px;margin-bottom:12px}"
        ".ok{color:#12B76A}.err{color:#D92D20}"
        "input[type=file]{width:100%;padding:10px;margin-bottom:8px;background:var(--s);border:1px solid var(--l);border-radius:8px;color:var(--i);font:inherit;font-size:14px}"
        "button{width:100%;height:44px;border:none;border-radius:8px;font:inherit;font-weight:600;cursor:pointer;background:var(--p);color:#fff}"
        "button:hover{background:#1D4ED8}button:disabled{opacity:.45;cursor:not-allowed}"
        ".pb{height:8px;background:var(--s);border-radius:999px;margin-top:12px;overflow:hidden;display:none}"
        ".pf{height:100%;background:var(--p);border-radius:999px;width:0%;transition:width .3s}"
        ".st{margin-top:8px;font-size:14px;min-height:1.2em}"
        ".warn{color:#B54708;font-weight:600;font-size:14px;margin-top:8px;display:none}"
        ".lk{display:flex;align-items:center;justify-content:center;height:44px;border:1px solid var(--l);background:var(--c);color:var(--i);text-decoration:none;border-radius:8px;font-weight:600}"
        ".lk:hover{background:var(--s)}"
        "</style></head><body><div class='ct'>"
        "<h1>Firmware update</h1>"
        "<p class='sub'>ESP8266 Safe Update mode</p>"
        "<div class='cd'><h2>Version</h2>"
        "<p class='ver'>Installed: <b>");
    p += FIRMWARE_VERSION;
    p += F("</b></p>"
        "<a class='lk' href='" GITHUB_RELEASES_URL "' target='_blank'>Release notes on GitHub</a></div>"
        "<div class='cd'><h2>Firmware</h2>"
        "<input type='file' id='fw-file' accept='.bin'>"
        "<button id='fw-btn' onclick='upload(\"fw\")'>Upload firmware</button>"
        "<div class='pb' id='fw-pb'><div class='pf' id='fw-pf'></div></div>"
        "<div class='st' id='fw-st'></div>"
        "<div class='warn' id='fw-warn'>Do not interrupt!</div></div>"
        "<div class='cd'><h2>Web interface</h2>"
        "<input type='file' id='fs-file' accept='.bin'>"
        "<button id='fs-btn' onclick='upload(\"fs\")'>Upload web interface</button>"
        "<div class='pb' id='fs-pb'><div class='pf' id='fs-pf'></div></div>"
        "<div class='st' id='fs-st'></div>"
        "<div class='warn' id='fs-warn'>Do not interrupt!</div></div>"
        "<a class='lk' href='/restart' style='margin-top:8px'>Exit Safe Update mode</a>"
        "</div><script>"
        "function upload(t){"
        "var f=document.getElementById(t+'-file').files[0];"
        "if(!f){alert('Select a .bin file first');return;}"
        "var url=t==='fw'?'/update':'/update-fs';"
        "var btn=document.getElementById(t+'-btn');"
        "var pb=document.getElementById(t+'-pb');"
        "var pf=document.getElementById(t+'-pf');"
        "var st=document.getElementById(t+'-st');"
        "var wn=document.getElementById(t+'-warn');"
        "btn.disabled=true;pb.style.display='block';wn.style.display='block';"
        "st.textContent='Uploading...';st.className='st';"
        "var fd=new FormData();fd.append('file',f,f.name);"
        "var xhr=new XMLHttpRequest();"
        "xhr.upload.onprogress=function(e){"
        "if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);"
        "pf.style.width=p+'%';st.textContent='Uploading: '+p+'%';}};"
        "xhr.onload=function(){"
        "wn.style.display='none';"
        "if(xhr.status===200){st.textContent='Success! Restarting...';st.className='st ok';"
        "pf.style.width='100%';setTimeout(function(){location.href='/';},10000);}"
        "else{st.textContent='Failed: '+xhr.responseText;st.className='st err';btn.disabled=false;}};"
        "xhr.onerror=function(){wn.style.display='none';"
        "st.textContent='Connection error';st.className='st err';btn.disabled=false;};"
        "xhr.open('POST',url);xhr.send(fd);}"
        "</script></body></html>");
    return p;
}

// Run the synchronous OTA server (blocking - takes over from main loop)
void runSyncOTAServer() {
    Serial.println("[OTA-SYNC] Starting synchronous OTA server...");
    // Note: Don't use logger during OTA - it writes to flash which can conflict

    // The main loop (fan timer, interval mode) stops running from here on,
    // so don't leave the fan spinning unattended
    fanController.turnOff();

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

    // CSRF protection: a browser-sent Origin/Referer must match our Host
    // (same rule as isSameOrigin() in web_server.cpp)
    syncServer.collectHeaders("Origin", "Referer");
    auto sameOrigin = [&syncServer]() -> bool {
        String origin = syncServer.hasHeader("Origin") ? syncServer.header("Origin")
                      : syncServer.hasHeader("Referer") ? syncServer.header("Referer") : String();
        if (origin.length() == 0) return true;   // Not browser-driven
        int schemeEnd = origin.indexOf("://");
        if (schemeEnd < 0) return false;          // Includes Origin: null
        origin = origin.substring(schemeEnd + 3);
        int pathStart = origin.indexOf('/');
        if (pathStart >= 0) origin = origin.substring(0, pathStart);
        return origin.equalsIgnoreCase(syncServer.hostHeader());
    };

    // Per-upload state. Update.hasError() alone is not enough: a rejected or
    // never-started upload has no error but must not trigger a restart.
    static bool uploadOk = false;

    // Restart automatically when nobody uses safe mode (e.g. page closed)
    static unsigned long lastActivity = 0;
    lastActivity = millis();

    // Serve the OTA page with dynamic version info
    syncServer.on("/", HTTP_GET, [&syncServer]() {
        lastActivity = millis();
        String page = generateOTAPage();
        syncServer.send(200, "text/html", page);
    });

    // Exit safe mode and restart
    syncServer.on("/restart", HTTP_GET, [&syncServer]() {
        syncServer.send_P(200, "text/html", PSTR(
            "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'></head>"
            "<body style='font:15px system-ui,sans-serif;text-align:center;padding:60px 20px'>"
            "<h2>Restarting...</h2><p>Returning to normal mode.</p>"
            "<script>setTimeout(()=>location.href='/',8000)</script></body></html>"));
        delay(500);
        ESP.restart();
    });

    // Handle firmware upload
    syncServer.on("/update", HTTP_POST, [&syncServer]() {
        lastActivity = millis();
        if (!uploadOk || Update.hasError()) {
            Serial.printf("[OTA-SYNC] Firmware update error: %s\n", Update.getErrorString().c_str());
            syncServer.send(500, "text/plain", uploadOk ? Update.getErrorString() : String(F("Upload rejected or failed")));
        } else {
            syncServer.send(200, "text/plain", F("OK"));
            delay(1000);
            ESP.restart();
        }
    }, [&syncServer, sameOrigin]() {
        HTTPUpload& upload = syncServer.upload();
        lastActivity = millis();
        if (upload.status == UPLOAD_FILE_START) {
            uploadOk = false;
            if (!sameOrigin()) {
                Serial.println("[OTA-SYNC] Upload rejected: cross-site request");
                return;
            }
            if (Update.isRunning()) {
                Update.end();  // Reset leftovers from an earlier failed upload
            }
            Serial.printf("[OTA-SYNC] Firmware upload start: %s\n", upload.filename.c_str());
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (!Update.begin(maxSketchSpace, U_FLASH)) {
                Serial.printf("[OTA-SYNC] Update.begin failed: %s\n", Update.getErrorString().c_str());
                return;
            }
            uploadOk = true;
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
            // Browser closed / connection lost: reset the updater so a retry works
            Serial.println("[OTA-SYNC] Upload aborted");
            Update.end();
            uploadOk = false;
        } else if (!uploadOk) {
            return;
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Serial.printf("[OTA-SYNC] Update.write failed: %s\n", Update.getErrorString().c_str());
                uploadOk = false;
            }
            // Feed watchdog
            ESP.wdtFeed();
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("[OTA-SYNC] Firmware update success: %u bytes\n", upload.totalSize);
            } else {
                Serial.printf("[OTA-SYNC] Update.end failed: %s\n", Update.getErrorString().c_str());
                uploadOk = false;
            }
        }
    });

    // Handle filesystem upload
    syncServer.on("/update-fs", HTTP_POST, [&syncServer]() {
        lastActivity = millis();
        if (!uploadOk || Update.hasError()) {
            Serial.printf("[OTA-SYNC] Filesystem update error: %s\n", Update.getErrorString().c_str());
            syncServer.send(500, "text/plain", uploadOk ? Update.getErrorString() : String(F("Upload rejected or failed")));
        } else {
            syncServer.send(200, "text/plain", F("OK"));
            delay(1000);
            ESP.restart();
        }
    }, [&syncServer, sameOrigin]() {
        HTTPUpload& upload = syncServer.upload();
        lastActivity = millis();
        if (upload.status == UPLOAD_FILE_START) {
            uploadOk = false;
            if (!sameOrigin()) {
                Serial.println("[OTA-SYNC] Upload rejected: cross-site request");
                return;
            }
            if (Update.isRunning()) {
                Update.end();  // Reset leftovers from an earlier failed upload
            }
            Serial.printf("[OTA-SYNC] Filesystem upload start: %s\n", upload.filename.c_str());
            size_t fsSize = ((size_t)&_FS_end - (size_t)&_FS_start);
            LittleFS.end();  // Unmount filesystem before update
            if (!Update.begin(fsSize, U_FS)) {
                Serial.printf("[OTA-SYNC] Update.begin failed: %s\n", Update.getErrorString().c_str());
                return;
            }
            uploadOk = true;
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
            // Browser closed / connection lost: reset the updater so a retry works
            Serial.println("[OTA-SYNC] Upload aborted");
            Update.end();
            uploadOk = false;
        } else if (!uploadOk) {
            return;
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Serial.printf("[OTA-SYNC] Update.write failed: %s\n", Update.getErrorString().c_str());
                uploadOk = false;
            }
            ESP.wdtFeed();
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("[OTA-SYNC] Filesystem update success: %u bytes\n", upload.totalSize);
            } else {
                Serial.printf("[OTA-SYNC] Update.end failed: %s\n", Update.getErrorString().c_str());
                uploadOk = false;
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

        if (millis() - lastActivity > SYNC_OTA_TIMEOUT_MS) {
            Serial.println("[OTA-SYNC] No activity for 10 minutes - restarting to normal mode");
            delay(100);
            ESP.restart();
        }
    }
}

#endif // PLATFORM_ESP8266
