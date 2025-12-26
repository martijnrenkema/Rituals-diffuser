#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <Arduino.h>

class OTAHandler {
public:
    void begin();
    void loop();

    // Callback for OTA events
    typedef void (*OTACallback)(int progress);
    void onProgress(OTACallback callback);
    void onStart(void (*callback)());
    void onEnd(void (*callback)());

private:
    OTACallback _progressCallback = nullptr;
    void (*_startCallback)() = nullptr;
    void (*_endCallback)() = nullptr;
};

extern OTAHandler otaHandler;

#endif // OTA_HANDLER_H
