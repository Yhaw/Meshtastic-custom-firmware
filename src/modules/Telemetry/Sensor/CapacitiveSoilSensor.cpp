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

bool CapacitiveSoilSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    uint16_t raw = readAnalog();
    
    // Convert 0-4095 to 0-100% (Inverting: 0 is wettest/100%, 4095 is driest/0%)
    // formula: % = (4095 - raw) * 100 / 4095
    float pct = ((4095.0f - (float)raw) * 100.0f) / 4095.0f;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    measurement->variant.environment_metrics.soil_moisture = (uint32_t)pct;
    measurement->variant.environment_metrics.has_soil_moisture = true;
    
    return true;
}

int32_t CapacitiveSoilSensor::runOnce()
{
    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

#endif
