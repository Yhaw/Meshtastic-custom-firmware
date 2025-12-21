#include <stddef.h>
#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "Default.h"
#include "EnvironmentTelemetry.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "UnitConversions.h"
#include "buzz.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "main.h"
#include "modules/ExternalNotificationModule.h"
#include "power.h"
#include "sleep.h"
#include "target_specific.h"
#include <OLEDDisplay.h>

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR_EXTERNAL

// Sensors
#include "Sensor/CGRadSensSensor.h"
#include "Sensor/RCWL9620Sensor.h"
#include "Sensor/nullSensor.h"

namespace graphics
{
extern void drawCommonHeader(OLEDDisplay *display, int16_t x, int16_t y, const char *titleStr, bool force_no_invert,
                             bool show_date);
}
#if defined(ENABLE_DS18B20) && __has_include(<DallasTemperature.h>)
#include "Sensor/DS18B20Sensor.h"
#endif

#if defined(ENABLE_SOIL_MOISTURE_CAPACITIVE)
#include "Sensor/CapacitiveSoilSensor.h"
#endif

#if defined(ENABLE_BME280) && __has_include(<Adafruit_BME280.h>)
#include "Sensor/BME280Sensor.h"
#endif

#if defined(ENABLE_BME680) && __has_include(<bsec2.h>)
#include "Sensor/BME680Sensor.h"
#endif

#if defined(ENABLE_LTR390) && __has_include(<Adafruit_LTR390.h>)
#include "Sensor/LTR390UVSensor.h"
#endif

#if defined(ENABLE_VEML7700) && __has_include(<Adafruit_VEML7700.h>)
#include "Sensor/VEML7700Sensor.h"
#endif

#if defined(ENABLE_INA219) && __has_include(<Adafruit_INA219.h>)
#include "Sensor/INA219Sensor.h"
#endif

#define FAILED_STATE_SENSOR_READ_MULTIPLIER 10
#define DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN true

#include "graphics/ScreenFonts.h"
#include <Throttle.h>

#include <forward_list>

static std::forward_list<TelemetrySensor *> sensors;

template <typename T> void addSensor(ScanI2C *i2cScanner, ScanI2C::DeviceType type)
{
    ScanI2C::FoundDevice dev = i2cScanner->find(type);
    if (dev.type != ScanI2C::DeviceType::NONE || type == ScanI2C::DeviceType::NONE) {
        TelemetrySensor *sensor = new T();
#if WIRE_INTERFACES_COUNT > 1
        TwoWire *bus = ScanI2CTwoWire::fetchI2CBus(dev.address);
        if (dev.address.port != ScanI2C::I2CPort::WIRE1 && sensor->onlyWire1()) {
            // This sensor only works on Wire (Wire1 is not supported)
            delete sensor;
            return;
        }
#else
        TwoWire *bus = &Wire;
#endif
        if (sensor->initDevice(bus, &dev)) {
            sensors.push_front(sensor);
            return;
        }
        // destroy sensor
        delete sensor;
    }
}

void EnvironmentTelemetryModule::i2cScanFinished(ScanI2C *i2cScanner)
{
    if (!moduleConfig.telemetry.environment_measurement_enabled && !ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE) {
        return;
    }
    LOG_INFO("Environment Telemetry adding I2C devices...");

    // order by priority of metrics/values (low top, high bottom)

#if defined(ENABLE_BME680) && __has_include(<bsec2.h>)
    if (i2cScanner->exists(ScanI2C::DeviceType::BME_680)) {
        addSensor<BME680Sensor>(i2cScanner, ScanI2C::DeviceType::BME_680);
    } else
#endif
#if defined(ENABLE_BME280) && __has_include(<Adafruit_BME280.h>)
    if (i2cScanner->exists(ScanI2C::DeviceType::BME_280)) {
        addSensor<BME280Sensor>(i2cScanner, ScanI2C::DeviceType::BME_280);
    }
#endif

#if defined(ENABLE_LTR390) && __has_include(<Adafruit_LTR390.h>)
    addSensor<LTR390UVSensor>(i2cScanner, ScanI2C::DeviceType::LTR390UV);
#endif

#if defined(ENABLE_VEML7700) && __has_include(<Adafruit_VEML7700.h>)
    addSensor<VEML7700Sensor>(i2cScanner, ScanI2C::DeviceType::VEML7700);
#endif

#if defined(ENABLE_INA219) && __has_include(<Adafruit_INA219.h>)
    addSensor<INA219Sensor>(i2cScanner, ScanI2C::DeviceType::INA219);
#endif

#if defined(ENABLE_DS18B20) && __has_include(<DallasTemperature.h>)
    // DS18B20 uses OneWire, not I2C, so we pass NONE as device type
    addSensor<DS18B20Sensor>(i2cScanner, ScanI2C::DeviceType::NONE);
#endif

#if defined(ENABLE_SOIL_MOISTURE_CAPACITIVE)
    // Analog sensor
    addSensor<CapacitiveSoilSensor>(i2cScanner, ScanI2C::DeviceType::NONE);
#endif

#endif
}

int32_t EnvironmentTelemetryModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        sleepOnNextExecution = false;
        uint32_t nightyNightMs = Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.environment_update_interval,
                                                                   default_telemetry_broadcast_interval_secs);
        LOG_DEBUG("Sleep for %ims, then awake to send metrics again", nightyNightMs);
        doDeepSleep(nightyNightMs, true, false);
    }

    uint32_t result = UINT32_MAX;
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.telemetry.environment_measurement_enabled = 1;
    // moduleConfig.telemetry.environment_screen_enabled = 1;
    // moduleConfig.telemetry.environment_update_interval = 15;

    if (!(moduleConfig.telemetry.environment_measurement_enabled || moduleConfig.telemetry.environment_screen_enabled ||
          ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE)) {
        // If this module is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return disable();
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = 0;

        if (moduleConfig.telemetry.environment_measurement_enabled || ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE) {
            LOG_INFO("Environment Telemetry: init");

            // check if we have at least one sensor
            if (!sensors.empty()) {
                result = DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
            }

#ifdef T1000X_SENSOR_EN
#elif !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR_EXTERNAL
#if !MESHTASTIC_EXCLUDE_INA219
            if (ina219Sensor.hasSensor())
                result = ina219Sensor.runOnce();
#endif
#if !MESHTASTIC_EXCLUDE_INA260
            if (ina260Sensor.hasSensor())
                result = ina260Sensor.runOnce();
#endif
#if !MESHTASTIC_EXCLUDE_INA3221
            if (ina3221Sensor.hasSensor())
                result = ina3221Sensor.runOnce();
#endif
#if !MESHTASTIC_EXCLUDE_MAX17048
            if (max17048Sensor.hasSensor())
                result = max17048Sensor.runOnce();
#endif
                // this only works on the wismesh hub with the solar option. This is not an I2C sensor, so we don't need the
                // sensormap here.
#ifdef HAS_RAKPROT
            result = rak9154Sensor.runOnce();
#endif
#endif
        }
        // it's possible to have this module enabled, only for displaying values on the screen.
        // therefore, we should only enable the sensor loop if measurement is also enabled
        return result == UINT32_MAX ? disable() : setStartDelay();
    } else {
        // if we somehow got to a second run of this module with measurement disabled, then just wait forever
        if (!moduleConfig.telemetry.environment_measurement_enabled && !ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE) {
            return disable();
        }

        for (TelemetrySensor *sensor : sensors) {
            uint32_t delay = sensor->runOnce();
            if (delay < result) {
                result = delay;
            }
        }

        if (((lastSentToMesh == 0) ||
             !Throttle::isWithinTimespanMs(lastSentToMesh, Default::getConfiguredOrDefaultMsScaled(
                                                               moduleConfig.telemetry.environment_update_interval,
                                                               default_telemetry_broadcast_interval_secs, numOnlineNodes))) &&
            airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
            airTime->isTxAllowedAirUtil()) {
            sendTelemetry();
            lastSentToMesh = millis();
        } else if (((lastSentToPhone == 0) || !Throttle::isWithinTimespanMs(lastSentToPhone, sendToPhoneIntervalMs)) &&
                   (service->isToPhoneQueueEmpty())) {
            // Just send to phone when it's not our time to send to mesh yet
            // Only send while queue is empty (phone assumed connected)
            sendTelemetry(NODENUM_BROADCAST, true);
            lastSentToPhone = millis();
        }
    }
    return min(sendToPhoneIntervalMs, result);
}

bool EnvironmentTelemetryModule::wantUIFrame()
{
    return moduleConfig.telemetry.environment_screen_enabled;
}

#if HAS_SCREEN
void EnvironmentTelemetryModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // === Setup display ===
    display->clear();
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    int line = 1;

    // === Set Title
    const char *titleStr = (graphics::isHighResolution) ? "Environment" : "Env.";

    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr);

    // === Row spacing setup ===
    const int rowHeight = FONT_HEIGHT_SMALL - 4;
    int currentY = graphics::getTextPositions(display)[line++];

    // === Show "No Telemetry" if no data available ===
    if (!lastMeasurementPacket) {
        display->drawString(x, currentY, "No Telemetry");
        return;
    }

    // Decode the telemetry message from the latest received packet
    const meshtastic_Data &p = lastMeasurementPacket->decoded;
    meshtastic_Telemetry telemetry;
    if (!pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &telemetry)) {
        display->drawString(x, currentY, "No Telemetry");
        return;
    }

    const auto &m = telemetry.variant.environment_metrics;

    // Check if any telemetry field has valid data
    bool hasAny = m.has_temperature || m.has_relative_humidity || m.barometric_pressure != 0 || m.iaq != 0 || m.voltage != 0 ||
                  m.current != 0 || m.lux != 0 || m.white_lux != 0 || m.weight != 0 || m.distance != 0 || m.radiation != 0;

    if (!hasAny) {
        display->drawString(x, currentY, "No Telemetry");
        return;
    }

    // === First line: Show sender name + time since received (left), and first metric (right) ===
    const char *sender = getSenderShortName(*lastMeasurementPacket);
    uint32_t agoSecs = service->GetTimeSinceMeshPacket(lastMeasurementPacket);
    String agoStr = (agoSecs > 864000) ? "?"
                    : (agoSecs > 3600) ? String(agoSecs / 3600) + "h"
                    : (agoSecs > 60)   ? String(agoSecs / 60) + "m"
                                       : String(agoSecs) + "s";

    String leftStr = String(sender) + " (" + agoStr + ")";
    display->drawString(x, currentY, leftStr); // Left side: who and when

    // === Collect sensor readings as label strings (no icons) ===
    std::vector<String> entries;

    if (m.has_temperature) {
        String tempStr = moduleConfig.telemetry.environment_display_fahrenheit
                             ? "Tmp: " + String(UnitConversions::CelsiusToFahrenheit(m.temperature), 1) + "°F"
                             : "Tmp: " + String(m.temperature, 1) + "°C";
        entries.push_back(tempStr);
    }
    if (m.has_relative_humidity)
        entries.push_back("Hum: " + String(m.relative_humidity, 0) + "%");
    if (m.barometric_pressure != 0)
        entries.push_back("Prss: " + String(m.barometric_pressure, 0) + " hPa");
    if (m.iaq != 0) {
        String aqi = "IAQ: " + String(m.iaq);
        const char *bannerMsg = nullptr; // Default: no banner

        if (m.iaq <= 25)
            aqi += " (Excellent)";
        else if (m.iaq <= 50)
            aqi += " (Good)";
        else if (m.iaq <= 100)
            aqi += " (Moderate)";
        else if (m.iaq <= 150)
            aqi += " (Poor)";
        else if (m.iaq <= 200) {
            aqi += " (Unhealthy)";
            bannerMsg = "Unhealthy IAQ";
        } else if (m.iaq <= 300) {
            aqi += " (Very Unhealthy)";
            bannerMsg = "Very Unhealthy IAQ";
        } else {
            aqi += " (Hazardous)";
            bannerMsg = "Hazardous IAQ";
        }

        entries.push_back(aqi);

        // === IAQ alert logic ===
        static uint32_t lastAlertTime = 0;
        uint32_t now = millis();

        bool isOwnTelemetry = lastMeasurementPacket->from == nodeDB->getNodeNum();
        bool isCooldownOver = (now - lastAlertTime > 60000);

        if (isOwnTelemetry && bannerMsg && isCooldownOver) {
            LOG_INFO("drawFrame: IAQ %d (own) — showing banner: %s", m.iaq, bannerMsg);
            screen->showSimpleBanner(bannerMsg, 3000);

            // Only buzz if IAQ is over 200
            if (m.iaq > 200 && moduleConfig.external_notification.enabled && !externalNotificationModule->getMute()) {
                playLongBeep();
            }

            lastAlertTime = now;
        }
    }
    if (m.voltage != 0 || m.current != 0)
        entries.push_back(String(m.voltage, 1) + "V / " + String(m.current, 0) + "mA");
    if (m.lux != 0)
        entries.push_back("Light: " + String(m.lux, 0) + "lx");
    if (m.white_lux != 0)
        entries.push_back("White: " + String(m.white_lux, 0) + "lx");
    if (m.weight != 0)
        entries.push_back("Weight: " + String(m.weight, 0) + "kg");
    if (m.distance != 0)
        entries.push_back("Level: " + String(m.distance, 0) + "mm");
    if (m.radiation != 0)
        entries.push_back("Rad: " + String(m.radiation, 2) + " µR/h");

    // === Show first available metric on top-right of first line ===
    if (!entries.empty()) {
        String valueStr = entries.front();
        int rightX = SCREEN_WIDTH - display->getStringWidth(valueStr);
        display->drawString(rightX, currentY, valueStr);
        entries.erase(entries.begin()); // Remove from queue
    }

    // === Advance to next line for remaining telemetry entries ===
    currentY += rowHeight;

    // === Draw remaining entries in 2-column format (left and right) ===
    for (size_t i = 0; i < entries.size(); i += 2) {
        // Left column
        display->drawString(x, currentY, entries[i]);

        // Right column if it exists
        if (i + 1 < entries.size()) {
            int rightX = SCREEN_WIDTH / 2;
            display->drawString(rightX, currentY, entries[i + 1]);
        }

        currentY += rowHeight;
    }
    graphics::drawCommonFooter(display, x, y);
}
#endif

bool EnvironmentTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    if (t->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
        const char *sender = getSenderShortName(mp);

        LOG_INFO("CROWDSENSE REMOTE from %s: [Temp:%.2fC Hum:%.1f%% Press:%.1fhPa Gas:%.1fk IAQ:%u Lux:%.1flx UV:%.2f INA:%.2fV/%.1fmA SoilT:%.2fC Soil:%u%%]",
                 sender,
                 t->variant.environment_metrics.temperature,
                 t->variant.environment_metrics.relative_humidity,
                 t->variant.environment_metrics.barometric_pressure,
                 t->variant.environment_metrics.gas_resistance,
                 (uint32_t)t->variant.environment_metrics.iaq,
                 t->variant.environment_metrics.lux,
                 t->variant.environment_metrics.uv_lux,
                 t->variant.environment_metrics.voltage,
                 t->variant.environment_metrics.current,
                 t->variant.environment_metrics.soil_temperature,
                 (uint32_t)t->variant.environment_metrics.soil_moisture);
#endif
        // release previous packet before occupying a new spot
        if (lastMeasurementPacket != nullptr)
            packetPool.release(lastMeasurementPacket);

        lastMeasurementPacket = packetPool.allocCopy(mp);
    }

    return false; // Let others look at this message also if they want
}
bool EnvironmentTelemetryModule::getEnvironmentTelemetry(meshtastic_Telemetry *m)
{
    bool valid = true;
    bool hasSensor = false;
    m->time = getTime();
    m->which_variant = meshtastic_Telemetry_environment_metrics_tag;
    m->variant.environment_metrics = meshtastic_EnvironmentMetrics_init_zero;

    for (TelemetrySensor *sensor : sensors) {
        valid = valid && sensor->getMetrics(m);
        hasSensor = true;
    }

#ifndef T1000X_SENSOR_EN
#if !MESHTASTIC_EXCLUDE_INA219
    if (ina219Sensor.hasSensor()) {
        valid = valid && ina219Sensor.getMetrics(m);
        hasSensor = true;
    }
#endif
#if !MESHTASTIC_EXCLUDE_INA260
    if (ina260Sensor.hasSensor()) {
        valid = valid && ina260Sensor.getMetrics(m);
        hasSensor = true;
    }
#endif
#if !MESHTASTIC_EXCLUDE_INA3221
    if (ina3221Sensor.hasSensor()) {
        valid = valid && ina3221Sensor.getMetrics(m);
        hasSensor = true;
    }
#endif
#if !MESHTASTIC_EXCLUDE_MAX17048
    if (max17048Sensor.hasSensor()) {
        valid = valid && max17048Sensor.getMetrics(m);
        hasSensor = true;
    }
#endif
#endif
#ifdef HAS_RAKPROT
    valid = valid && rak9154Sensor.getMetrics(m);
    hasSensor = true;
#endif
    return valid && hasSensor;
}

meshtastic_MeshPacket *EnvironmentTelemetryModule::allocReply()
{
    if (currentRequest) {
        auto req = *currentRequest;
        const auto &p = req.decoded;
        meshtastic_Telemetry scratch;
        meshtastic_Telemetry *decoded = NULL;
        memset(&scratch, 0, sizeof(scratch));
        if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &scratch)) {
            decoded = &scratch;
        } else {
            LOG_ERROR("Error decoding EnvironmentTelemetry module!");
            return NULL;
        }
        // Check for a request for environment metrics
        if (decoded->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
            meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
            if (getEnvironmentTelemetry(&m)) {
                LOG_INFO("Environment telemetry reply to request");
                return allocDataProtobuf(m);
            } else {
                return NULL;
            }
        }
    }
    return NULL;
}

bool EnvironmentTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
{
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    m.which_variant = meshtastic_Telemetry_environment_metrics_tag;
    m.time = getTime();

    if (getEnvironmentTelemetry(&m)) {
        // Calculate raw soil ADC back from percentage for the log (approximate but useful)
        uint32_t soilRaw = 4095 - (m.variant.environment_metrics.soil_moisture * 4095 / 100);

        LOG_INFO("CROWDSENSE BROADCAST: [Temp:%.2fC Hum:%.1f%% Press:%.1fhPa Gas:%.1fk IAQ:%u Lux:%.1flx UV:%.2f INA:%.2fV/%.1fmA SoilT:%.2fC Soil:%u%% (ADC:%u)]",
                 m.variant.environment_metrics.temperature,
                 m.variant.environment_metrics.relative_humidity,
                 m.variant.environment_metrics.barometric_pressure,
                 m.variant.environment_metrics.gas_resistance,
                 (uint32_t)m.variant.environment_metrics.iaq,
                 m.variant.environment_metrics.lux,
                 m.variant.environment_metrics.uv_lux,
                 m.variant.environment_metrics.voltage,
                 m.variant.environment_metrics.current,
                 m.variant.environment_metrics.soil_temperature,
                 (uint32_t)m.variant.environment_metrics.soil_moisture,
                 soilRaw);

        sensor_read_error_count = 0;

        meshtastic_MeshPacket *p = allocDataProtobuf(m);
        p->to = dest;
        p->decoded.want_response = false;
        if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR)
            p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
        else
            p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        // release previous packet before occupying a new spot
        if (lastMeasurementPacket != nullptr)
            packetPool.release(lastMeasurementPacket);

        lastMeasurementPacket = packetPool.allocCopy(*p);
        if (phoneOnly) {
            LOG_INFO("Send packet to phone");
            service->sendToPhone(p);
        } else {
            LOG_INFO("Send packet to mesh");
            service->sendToMesh(p, RX_SRC_LOCAL, true);

            if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && config.power.is_power_saving) {
                meshtastic_ClientNotification *notification = clientNotificationPool.allocZeroed();
                notification->level = meshtastic_LogRecord_Level_INFO;
                notification->time = getValidTime(RTCQualityFromNet);
                sprintf(notification->message, "Sending telemetry and sleeping for %us interval in a moment",
                        Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.environment_update_interval,
                                                          default_telemetry_broadcast_interval_secs) /
                            1000U);
                service->sendClientNotification(notification);
                sleepOnNextExecution = true;
                LOG_DEBUG("Start next execution in 5s, then sleep");
                setIntervalFromNow(FIVE_SECONDS_MS);
            }
        }
        return true;
    }
    return false;
}

AdminMessageHandleResult EnvironmentTelemetryModule::handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                                 meshtastic_AdminMessage *request,
                                                                                 meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result = AdminMessageHandleResult::NOT_HANDLED;
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR_EXTERNAL

    for (TelemetrySensor *sensor : sensors) {
        result = sensor->handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }

#if !MESHTASTIC_EXCLUDE_INA219
    if (ina219Sensor.hasSensor()) {
        result = ina219Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
#endif
#if !MESHTASTIC_EXCLUDE_INA260
    if (ina260Sensor.hasSensor()) {
        result = ina260Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
#endif
#if !MESHTASTIC_EXCLUDE_INA3221
    if (ina3221Sensor.hasSensor()) {
        result = ina3221Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
#endif
#if !MESHTASTIC_EXCLUDE_MAX17048
    if (max17048Sensor.hasSensor()) {
        result = max17048Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
#endif
#endif
    return result;
}

#endif
