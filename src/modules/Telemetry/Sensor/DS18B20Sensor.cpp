#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<DallasTemperature.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "DS18B20Sensor.h"
#include "TelemetrySensor.h"
#include <DallasTemperature.h>
#include <OneWire.h>
#include "mesh/Throttle.h"

DS18B20Sensor::DS18B20Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "DS18B20")
{
    // Constructor - initialization happens in initOneWire()
}

DS18B20Sensor::~DS18B20Sensor()
{
    // Clean up allocated resources
    if (sensors) {
        delete sensors;
        sensors = nullptr;
    }
    if (oneWire) {
        delete oneWire;
        oneWire = nullptr;
    }
}

bool DS18B20Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    // DS18B20 uses OneWire, not I2C, so we ignore the bus parameter
    // This will be called from EnvironmentTelemetry with NONE device type
    return initOneWire();
}

bool DS18B20Sensor::initOneWire()
{
#ifdef ONEWIRE_PIN
    LOG_INFO("Initializing DS18B20 on GPIO %d", ONEWIRE_PIN);

    // Create OneWire instance
    oneWire = new OneWire(ONEWIRE_PIN);
    if (!oneWire) {
        LOG_ERROR("Failed to create OneWire instance");
        return false;
    }

    // Create DallasTemperature instance
    sensors = new DallasTemperature(oneWire);
    if (!sensors) {
        LOG_ERROR("Failed to create DallasTemperature instance");
        delete oneWire;
        oneWire = nullptr;
        return false;
    }

    // Initialize sensors
    sensors->begin();
    deviceCount = sensors->getDeviceCount();
    LOG_INFO("DS18B20: Found %d sensors on GPIO %d", deviceCount, ONEWIRE_PIN);

    if (deviceCount == 0) {
        LOG_WARN("No DS18B20 sensors found on GPIO %d", ONEWIRE_PIN);
        LOG_WARN("Check wiring: DQ->GPIO%d, VDD->3.3V, GND->GND, 4.7k pullup DQ->VDD", ONEWIRE_PIN);
        delete sensors;
        delete oneWire;
        sensors = nullptr;
        oneWire = nullptr;
        return false;
    }

    // Silenced individual log to favor unified broadcast

    LOG_INFO("Found %d DS18B20 sensor(s)", deviceCount);

    // Set resolution to 12-bit (0.0625°C precision)
    // This takes ~750ms per conversion
    sensors->setResolution(12);

    // Set to async mode for non-blocking reads
    sensors->setWaitForConversion(false);

    // Log device addresses for debugging
    for (uint8_t i = 0; i < deviceCount; i++) {
        DeviceAddress addr;
        if (sensors->getAddress(addr, i)) {
            LOG_DEBUG("DS18B20 #%d address: %02X%02X%02X%02X%02X%02X%02X%02X", i, addr[0], addr[1], addr[2], addr[3],
                      addr[4], addr[5], addr[6], addr[7]);
        }
    }

    status = true;
    initialized = true;

    return initI2CSensor();
#else
    LOG_ERROR("ONEWIRE_PIN not defined in variant.h");
    LOG_ERROR("Add '#define ONEWIRE_PIN <gpio>' to your variant file");
    return false;
#endif
}

void DS18B20Sensor::setup()
{
    // Additional setup if needed
    // Called after initDevice succeeds
}

int32_t DS18B20Sensor::runOnce()
{
    if (!initialized || !sensors) {
        return INT32_MAX;
    }

    // Only request every 60 seconds to match the global telemetry interval
    // and avoid spamming the log/bus.
    if (Throttle::isWithinTimespanMs(lastConversionRequest, 60000)) {
        return 1000; // Check again in 1s
    }

    // Request temperature reading (non-blocking)
    sensors->requestTemperatures();
    conversionRequested = true;
    lastConversionRequest = millis();

    // Redundant log removed to stop spam

    return 60000; // Wait 60s
}

bool DS18B20Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!sensors || deviceCount == 0 || !initialized) {
        LOG_WARN("DS18B20 not initialized");
        return false;
    }

    // If we just requested conversion, wait for it to complete
    // 12-bit conversion takes ~750ms
    if (conversionRequested) {
        uint32_t elapsed = millis() - lastConversionRequest;
        if (elapsed < 800) {
            // Conversion not complete yet, wait a bit more
            LOG_DEBUG("Waiting for DS18B20 conversion (elapsed: %dms)", elapsed);
            delay(800 - elapsed);
        }
        conversionRequested = false;
    }

    // Read temperature from first sensor (index 0)
    // For multiple sensors, we read the first one
    // Future enhancement: average all sensors or expose all readings
    float tempC = sensors->getTempCByIndex(0);

    // Check for valid reading
    // DEVICE_DISCONNECTED_C is -127.0
    // 85.0 is the power-on reset value, indicates sensor not ready
    if (tempC == DEVICE_DISCONNECTED_C) {
        LOG_ERROR("DS18B20 disconnected or not responding");
        return false;
    }

    if (tempC == 85.0) {
        LOG_WARN("DS18B20 returned power-on value (85.0°C), sensor may not be ready");
        // Try one more read
        delay(100);
        tempC = sensors->getTempCByIndex(0);
        if (tempC == 85.0 || tempC == DEVICE_DISCONNECTED_C) {
            return false;
        }
    }

    // Sanity check - temperature should be within reasonable range
    if (tempC < -55.0 || tempC > 125.0) {
        // Silenced individual log to favor unified BROADCAST format
        return false;
    }

    // Silenced individual log to favor unified broadcast

    // If multiple sensors, log all readings
    if (deviceCount > 1) {
        LOG_DEBUG("Additional DS18B20 readings:");
        for (uint8_t i = 1; i < deviceCount; i++) {
            float temp = sensors->getTempCByIndex(i);
            if (temp != DEVICE_DISCONNECTED_C && temp != 85.0) {
                LOG_DEBUG("  Sensor #%d: %.2f°C", i, temp);
            }
        }
    }

    // CROWDSENSE: Map DS18B20 to soil_temperature to separate from BME280 ambient temp
    measurement->variant.environment_metrics.soil_temperature = tempC;
    measurement->variant.environment_metrics.has_soil_temperature = true;

    return true;
}

uint8_t DS18B20Sensor::getDeviceCount()
{
    return deviceCount;
}

#endif
