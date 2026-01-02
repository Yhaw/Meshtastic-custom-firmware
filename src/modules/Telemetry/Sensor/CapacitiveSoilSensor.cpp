#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(ENABLE_SOIL_MOISTURE_CAPACITIVE)

#include "CapacitiveSoilSensor.h"
#include <Arduino.h>

CapacitiveSoilSensor::CapacitiveSoilSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "Capacitive Soil")
{
}

bool CapacitiveSoilSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s on Pin %d", sensorName, SOIL_PIN);
    pinMode(SOIL_PIN, INPUT);
    status = true;
    return true;
}

uint16_t CapacitiveSoilSensor::readAnalog()
{
    uint32_t total = 0;
    for (int i = 0; i < SAMPLES; i++) {
        total += analogRead(SOIL_PIN);
        delay(5);
    }
    return total / SAMPLES;
}

// Calibration values for Capacitive Soil Moisture Sensor v1.2 (3.3V)
// Air ~ 3300-3500+ (User reports ~15-19% with full range 4095)
// Water ~ 1200-1500 typically
#define SOIL_DRY_ADC 3000 // Threshold: Readings above this (drier) will be 0%
#define SOIL_WET_ADC 1200 // Reading at 100% moisture

bool CapacitiveSoilSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    uint16_t raw = readAnalog();
    
    // Convert using calibrated range
    // map(value, fromLow, fromHigh, toLow, toHigh)
    // Note: Capacitive sensors read LOW when WET, HIGH when DRY.
    long pct = map(raw, SOIL_DRY_ADC, SOIL_WET_ADC, 0, 100);

    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    LOG_INFO("Soil Sensor: Raw=%d, Pct=%ld%%", raw, pct);

    measurement->variant.environment_metrics.soil_moisture = (uint32_t)pct;
    measurement->variant.environment_metrics.has_soil_moisture = true;
    
    return true;
}

int32_t CapacitiveSoilSensor::runOnce()
{
    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

#endif
