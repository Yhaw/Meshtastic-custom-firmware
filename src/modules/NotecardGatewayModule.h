#pragma once

#include "mesh/MeshModule.h"
#include "concurrency/OSThread.h"
#include <Notecard.h>
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include <map>
#include "concurrency/Lock.h"

/**
 * NotecardGatewayModule forwards Meshtastic mesh data to the cloud via a Blues Notecard.
 * It handles Telemetry (Environment, Device, Air Quality, Power) from both local and remote nodes.
 * 
 * It uses the Notecard's internal Outbound Queue (.qo) for automatic persistence and sync.
 */
class NotecardGatewayModule : public MeshModule, private concurrency::OSThread {
public:
    NotecardGatewayModule();
    virtual ~NotecardGatewayModule() {}

    /**
     * Public static hook for local modules to queue their telemetry data.
     */
    static void queueTelemetry(NodeNum from, const meshtastic_Telemetry& telemetry);

protected:
    virtual void setup() override;

    /**
     * Handle received mesh packets on the Telemetry port.
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    /**
     * @return true if you want to receive the specified portnum
     */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;

private:
    Notecard notecard;
    bool isNotecardPresent = false;

    static NotecardGatewayModule* instance;
    
    concurrency::Lock lock;

    // Deduplication cache: nodeNum -> last telemetry timestamp
    std::map<NodeNum, uint32_t> lastSeenTelemetry;

    /**
     * Internal implementation of telemetry queuing.
     */
    void doQueueTelemetry(NodeNum from, const meshtastic_Telemetry& telemetry, int8_t rssi = 0, float snr = 0);

    /**
     * Field mappings for different telemetry types.
     */
    void addEnvironmentMetrics(struct J* body, const meshtastic_EnvironmentMetrics& m);

    /**
     * Periodic GPS update logic.
     */
    virtual int32_t runOnce() override;
    void updateGpsLocation();
    
    void logJ(const char *prefix, struct J *json);
    
    uint32_t lastGpsUpdate = 0;
    bool hasHadFirstFix = false;
    static const uint32_t GPS_BOOT_POLL_INTERVAL_MS = 2 * 60 * 1000;   // 2 minutes during initial fix
    static const uint32_t GPS_NORMAL_POLL_INTERVAL_MS = 15 * 60 * 1000; // 15 minutes normally
};
