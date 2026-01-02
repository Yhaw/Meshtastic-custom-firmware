#pragma once

#include "detect/ScanI2C.h"
#include <LiquidCrystal_I2C.h>
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "MeshTypes.h"

namespace graphics {

class LCDDisplay {
public:
    explicit LCDDisplay(ScanI2C::DeviceAddress address);
    void setup();
    void clear();
    void displayTelemetry(NodeNum from, const meshtastic_EnvironmentMetrics& metrics);
    void notifySensorFound(const char* sensorName);
    void manageBacklight();

private:
    LiquidCrystal_I2C lcd;
    ScanI2C::DeviceAddress addr;
    bool initialized = false;
    uint32_t telemetryCount = 0;
    uint32_t lastBacklightTime = 0;
    bool backlightOn = true;
};

extern LCDDisplay* lcdDisplay;

} // namespace graphics
