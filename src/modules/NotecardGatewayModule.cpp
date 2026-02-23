#include "NotecardGatewayModule.h"
#include "configuration.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "main.h"
#include "RTC.h"
#include "RedirectablePrint.h"
#include "SerialConsole.h"
#include "PositionModule.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <Notecard.h>
#include <note-c/n_cjson.h>
#include "gps/GPS.h"
#include <Arduino.h>
#include "GPSStatus.h"

#define PRODUCT_UID "com.afrilogicsolutions.arnold.kimkpe:crowdsense_terra"
#include "concurrency/LockGuard.h"

NotecardGatewayModule* NotecardGatewayModule::instance = nullptr;

NotecardGatewayModule::NotecardGatewayModule() : MeshModule("NotecardGateway"), concurrency::OSThread("NotecardGateway") {
    LOG_INFO("🔷 NotecardGatewayModule: Constructor called");
    instance = this;
}

void NotecardGatewayModule::logJ(const char *prefix, J *json) {
    if (json) {
        char *str = JPrintUnformatted(json);
        if (str) {
            LOG_INFO("Notecard %s %s", prefix, str);
            JFree(str);
        }
    }
}

void NotecardGatewayModule::setup() {
    LOG_INFO("🔷 NotecardGatewayModule: setup() called");
    concurrency::LockGuard guard(&lock);
    
    notecard.setDebugOutputStream(Serial);
    notecard.begin();
    
    J *req = notecard.newRequest("card.version");
    logJ("REQ:", req);
    J *rsp = notecard.requestAndResponse(req);
    logJ("RSP:", rsp);
    
    if (rsp != NULL) {
        LOG_INFO("Notecard Gateway: Detected Notecard");
        isNotecardPresent = true;
        notecard.deleteResponse(rsp);

        // Configure Notecard Hub connection
        J *hubReq = notecard.newRequest("hub.set");
        JAddStringToObject(hubReq, "product", PRODUCT_UID);
        JAddStringToObject(hubReq, "mode", "periodic");
        JAddNumberToObject(hubReq, "outbound", 60); // 1 hour
        JAddNumberToObject(hubReq, "inbound", 30);  // 30 min check
        logJ("REQ:", hubReq);
        notecard.sendRequest(hubReq);

        // Configure Notecard Location mode
        J *locReq = notecard.newRequest("card.location.mode");
        JAddStringToObject(locReq, "mode", "periodic");
        logJ("REQ:", locReq);
        notecard.sendRequest(locReq);

        // Start the periodic update thread after 3 minutes to give Notecard time to sync
        setIntervalFromNow(3 * 60 * 1000); 
    } else {
        LOG_WARN("Notecard Gateway: Notecard not found or unresponsive");
    }
}

void NotecardGatewayModule::queueTelemetry(NodeNum from, const meshtastic_Telemetry& telemetry) {
    if (instance && instance->isNotecardPresent) {
        instance->doQueueTelemetry(from, telemetry);
    }
}

bool NotecardGatewayModule::wantPacket(const meshtastic_MeshPacket *p) {
    bool wants = p->which_payload_variant == meshtastic_MeshPacket_decoded_tag && 
                 p->decoded.portnum == meshtastic_PortNum_TELEMETRY_APP;
    if (wants) {
        LOG_DEBUG("🔷 NotecardGatewayModule: wantPacket=true for portnum=%d", p->decoded.portnum);
    }
    return wants;
}

ProcessMessage NotecardGatewayModule::handleReceived(const meshtastic_MeshPacket &mp) {
    LOG_INFO("🔷 NotecardGatewayModule: handleReceived called, isNotecardPresent=%d", isNotecardPresent);
    if (!isNotecardPresent) {
        LOG_WARN("🔷 NotecardGatewayModule: Notecard not present, skipping");
        return ProcessMessage::CONTINUE;
    }

    meshtastic_Telemetry decoded = meshtastic_Telemetry_init_zero;
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Telemetry_msg, &decoded)) {
        doQueueTelemetry(getFrom(&mp), decoded, mp.rx_rssi, mp.rx_snr);
    }

    return ProcessMessage::CONTINUE;
}

void NotecardGatewayModule::doQueueTelemetry(NodeNum from, const meshtastic_Telemetry& telemetry, int8_t rssi, float snr) {
    if (!isNotecardPresent) return;

    // Deduplication check
    if (lastSeenTelemetry.count(from) && lastSeenTelemetry[from] == telemetry.time && telemetry.time != 0) {
        // Skip repeated data from same node at same time
        return;
    }
    
    // Bounded cache: evict oldest entry if cache is full (prevents memory leak)
    if (lastSeenTelemetry.size() >= MAX_DEDUP_CACHE_SIZE && !lastSeenTelemetry.count(from)) {
        // Find and erase the oldest entry (smallest timestamp)
        auto oldest = lastSeenTelemetry.begin();
        for (auto it = lastSeenTelemetry.begin(); it != lastSeenTelemetry.end(); ++it) {
            if (it->second < oldest->second) {
                oldest = it;
            }
        }
        LOG_DEBUG("NotecardGateway: Cache full, evicting node 0x%x", oldest->first);
        lastSeenTelemetry.erase(oldest);
    }
    
    lastSeenTelemetry[from] = telemetry.time;

    const char *senderName = "Unknown";
    auto node = nodeDB->getMeshNode(from);
    if (node) {
        senderName = node->user.long_name;
    }

    LOG_INFO("Gateway: Queuing telemetry from 0x%x (%s)", from, senderName);

    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "telemetry.qo");
    JAddBoolToObject(req, "sync", false);

    J *body = JAddObjectToObject(req, "body");
    char hexStr[12];
    snprintf(hexStr, sizeof(hexStr), "%x", from);
    JAddStringToObject(body, "node", hexStr);
    JAddStringToObject(body, "name", senderName);
    
    // User Request: Explicitly fetch time from Notecard for this field
    // instead of relying on local time or omitting it.
    J *timeReq = notecard.newRequest("card.time");
    J *timeRsp = notecard.requestAndResponse(timeReq);
    uint32_t notecardTime = 0;
    if (timeRsp != NULL) {
        notecardTime = (uint32_t)JGetNumber(timeRsp, "time");
        notecard.deleteResponse(timeRsp);
    }

    if (notecardTime > 0) {
        JAddNumberToObject(body, "time", notecardTime);
    } else if (telemetry.time != 0) {
        // Fallback to packet time if Notecard time fails
        JAddNumberToObject(body, "time", telemetry.time);
    }

    if (rssi != 0) JAddNumberToObject(body, "rssi", (double)rssi);
    if (snr != 0) JAddNumberToObject(body, "snr", (double)snr);

    if (telemetry.which_variant == meshtastic_Telemetry_environment_metrics_tag) {
        addEnvironmentMetrics(body, telemetry.variant.environment_metrics);
    } else {
        // Only sending environment metrics for this custom setup
        notecard.deleteResponse(req);
        return;
    }

    LOG_INFO("📥 Adding telemetry to Notecard queue from node 0x%x (%s)", from, senderName);
    // logJ("REQ note.add:", req); // disabling verbose json log to reduce noise
    
    // Ensure I2C clock is correct and slow for stability
    Wire.setClock(50000);
    // Give Notecard breathing room
    delay(250);
    
    uint32_t startTime = millis();
    J *rsp = nullptr;
    for (int attempt = 1; attempt <= 3; attempt++) {
         if (attempt > 1) {
             LOG_WARN("⚠️ Retry attempt %d for Notecard I2C - Resetting Bus...", attempt);
             Wire.end();
             delay(50);
             Wire.begin();
             delay(250);
             notecard.begin();
         }
         
         rsp = notecard.requestAndResponse(req);
         if (rsp != nullptr) break;
    }
    uint32_t elapsed = millis() - startTime;
    
    LOG_DEBUG("⏱️ requestAndResponse() returned after %u ms", elapsed);
    
    if (rsp) {
        logJ("RSP note.add:", rsp);
        if (notecard.responseError(rsp)) {
            LOG_WARN("❌ Failed to queue telemetry: %s", JGetString(rsp, "err"));
        } else {
            LOG_INFO("✅ Successfully queued telemetry from 0x%x to Notecard", from);
        }
        notecard.deleteResponse(rsp);
    } else {
        LOG_ERROR("❌ No response from Notecard for note.add (timeout after %u ms)", elapsed);
    }
}

void NotecardGatewayModule::addEnvironmentMetrics(J* body, const meshtastic_EnvironmentMetrics& m) {
    JAddStringToObject(body, "type", "env");
    
    // BME680 metrics (Basic Environmental)
    if (m.has_temperature) JAddNumberToObject(body, "temp", m.temperature);
    if (m.has_relative_humidity) JAddNumberToObject(body, "hum", m.relative_humidity);
    if (m.has_barometric_pressure) JAddNumberToObject(body, "press", m.barometric_pressure);
    
    // Capacitive Moisture
    if (m.has_soil_moisture) JAddNumberToObject(body, "soil_h", (double)m.soil_moisture);
    
    // DS18B20 Temperature
    if (m.has_soil_temperature) JAddNumberToObject(body, "soil_t", m.soil_temperature);
    
    // INA219 Power metrics
    if (m.has_voltage) JAddNumberToObject(body, "volt", m.voltage);
    if (m.has_current) JAddNumberToObject(body, "curr", m.current);
}

int32_t NotecardGatewayModule::runOnce() {
    // Force initialization if setup() wasn't called
    static bool initialized = false;
    if (!initialized) {
        LOG_INFO("🔷 NotecardGatewayModule: runOnce() - forcing initialization");
        setup();
        initialized = true;
    }

    if (!isNotecardPresent) {
        return INT32_MAX; // Don't run again if no Notecard
    }

    updateGpsLocation();
    
    // Return interval based on GPS fix status
    if (!hasHadFirstFix) {
        return GPS_BOOT_POLL_INTERVAL_MS;
    }
    return GPS_NORMAL_POLL_INTERVAL_MS;
}

void NotecardGatewayModule::updateGpsLocation() {
    LOG_INFO("Notecard Gateway: Polling GPS location...");
    
    J *req = notecard.newRequest("card.location");
    logJ("REQ:", req);
    J *rsp = notecard.requestAndResponse(req);
    logJ("RSP:", rsp);
    
    if (rsp != NULL) {
        // card.location returns lat, lon as doubles and time as a uint32
        double lat = JGetNumber(rsp, "lat");
        double lon = JGetNumber(rsp, "lon");
        uint32_t time = (uint32_t)JGetNumber(rsp, "time");
        
        if (lat != 0 && lon != 0) {
            if (!hasHadFirstFix) {
                LOG_INFO("Notecard Gateway: First GPS fix acquired!");
                hasHadFirstFix = true;
            }
            LOG_INFO("Notecard Gateway: Got Location Lat=%f Lon=%f", lat, lon);

            // IMPORTANT: Inject into NodeDB
            // This is the correct way to update the node's authoritative position
            // so it can be sent to the mesh and displayed.
            meshtastic_Position pos = meshtastic_Position_init_default;
            pos.latitude_i = (int32_t)(lat * 1e7);
            pos.longitude_i = (int32_t)(lon * 1e7);
            pos.time = time;
            pos.timestamp = time;
            pos.location_source = meshtastic_Position_LocSource_LOC_EXTERNAL;
            pos.has_latitude_i = true;
            pos.has_longitude_i = true;
            
            // Set it as the current local position
            if (nodeDB) {
                nodeDB->setLocalPosition(pos);
                LOG_INFO("Notecard Gateway: Injected location into NodeDB");
            }

            // IMPORTANT: Update the global gpsStatus which notifies the app and mesh
            if (gpsStatus) {
                meshtastic::GPSStatus newGpsStatus(true, true, false, pos);
                gpsStatus->updateStatus(&newGpsStatus);
                LOG_INFO("📍 Notecard Gateway: SENT TO APP/MESH - Lat=%f Lon=%f", lat, lon);
            }
        } else {
            LOG_DEBUG("Notecard Gateway: No GPS fix yet");
        }
        notecard.deleteResponse(rsp);
    }
}
