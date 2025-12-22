#include "LCDDisplay.h"
#include "NodeDB.h"

namespace graphics {

LCDDisplay* lcdDisplay = nullptr;

LCDDisplay::LCDDisplay(ScanI2C::DeviceAddress address) 
    : lcd(address.address, 20, 4), addr(address) {
}

void LCDDisplay::setup() {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Terra Fallback UI");
    lcd.setCursor(0, 1);
    lcd.print("Scanning for data...");
    // [x] Enforce and verify telemetry-driven behavior
    //     [x] Audit `LCDDisplay` class for background tasks/state
    //     [x] Update `LCDDisplay::displayTelemetry` for "Updated: now" requirement
    //     [x] Verify `EnvironmentTelemetryModule` hook placement
    //     [x] Verify zero idle CPU usage (no background loops)
    //     [x] Confirm minimal memory footprint (statelessness)
    // [x] Final Verification & Report
    //     [x] Build for `meshtastic-diy-v1`
    //     [x] Generate final PASS/FIXED report
    initialized = true;
}

void LCDDisplay::clear() {
    if (initialized) {
        lcd.clear();
    }
}

void LCDDisplay::displayTelemetry(NodeNum from, const meshtastic_EnvironmentMetrics& metrics) {
    if (!initialized) return;

    lcd.clear();
    
    telemetryCount++;

    // Get node name
    const char* name = "Unknown";
    auto node = nodeDB->getMeshNode(from);
    if (node) {
        name = node->user.long_name;
    }

    String logMsg = "LCD Update from " + String(name) + " (#" + String(telemetryCount) + "): ";
    if (metrics.has_temperature) logMsg += "Amb:" + String(metrics.temperature, 1) + "C ";
    if (metrics.has_relative_humidity) logMsg += "Hum:" + String(metrics.relative_humidity, 0) + "% ";
    if (metrics.has_soil_temperature) logMsg += "SoilT:" + String(metrics.soil_temperature, 1) + "C ";
    if (metrics.has_soil_moisture) logMsg += "Mst:" + String(metrics.soil_moisture) + "% ";
    if (metrics.has_barometric_pressure) logMsg += "Pres:" + String(metrics.barometric_pressure, 1) + "hPa";
    
    LOG_INFO("%s", logMsg.c_str());

    // Line 0: [Name] #[Count]
    char line0[21];
    snprintf(line0, sizeof(line0), "%.14s #%u", name, telemetryCount);
    lcd.setCursor(0, 0);
    lcd.print(line0);

    // Line 1: Amb: 26.4C  Hum:61%
    char line1[21];
    float temp = metrics.has_temperature ? metrics.temperature : 0;
    int hum = metrics.has_relative_humidity ? (int)metrics.relative_humidity : 0;
    snprintf(line1, sizeof(line1), "Amb:  %.1fC  Hum:%d%%", temp, hum);
    lcd.setCursor(0, 1);
    lcd.print(line1);

    // Line 2: Soil: 24.1C  Mst:45%
    char line2[21];
    float soilTemp = metrics.has_soil_temperature ? metrics.soil_temperature : 0;
    int soilMoist = metrics.has_soil_moisture ? (int)metrics.soil_moisture : 0;
    snprintf(line2, sizeof(line2), "Soil: %.1fC  Mst:%d%%", soilTemp, soilMoist);
    lcd.setCursor(0, 2);
    lcd.print(line2);

    // Line 3: 1008.3hPa | now
    char line3[21];
    float pres = metrics.has_barometric_pressure ? metrics.barometric_pressure : 0;
    snprintf(line3, sizeof(line3), "%.1fhPa | now", pres);
    lcd.setCursor(0, 3);
    lcd.print(line3);
}

} // namespace graphics
