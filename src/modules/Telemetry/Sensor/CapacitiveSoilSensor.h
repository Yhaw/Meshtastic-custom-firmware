#ifndef CAPACITIVE_SOIL_SENSOR_H
#define CAPACITIVE_SOIL_SENSOR_H

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(ENABLE_SOIL_MOISTURE_CAPACITIVE)

#include "TelemetrySensor.h"

/**
 * Driver for Capacitive Soil Moisture Sensor v1.2
 * Connected to Analog Pin 14.
 * 
 * Note: These sensors are analog. Higher voltage = drier soil.
 */
class CapacitiveSoilSensor : public TelemetrySensor {
public:
    CapacitiveSoilSensor();
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual int32_t runOnce() override;

private:
    uint16_t readAnalog();
    
    const uint8_t SOIL_PIN = 34; // Using Pin 34 (ADC1) to avoid LoRa conflicts
    const uint8_t SAMPLES = 10;
};

#endif
#endif
