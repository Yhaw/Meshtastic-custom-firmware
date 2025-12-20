#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<DallasTemperature.h>)

#pragma once

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <DallasTemperature.h>
#include <OneWire.h>

class DS18B20Sensor : public TelemetrySensor
{
  private:
    OneWire *oneWire = nullptr;
    DallasTemperature *sensors = nullptr;
    uint8_t deviceCount = 0;
    bool conversionRequested = false;
    uint32_t lastConversionRequest = 0;

  protected:
    virtual void setup() override;

  public:
    DS18B20Sensor();
    ~DS18B20Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;

    // DS18B20 specific methods
    bool initOneWire();
    uint8_t getDeviceCount();
};

#endif
