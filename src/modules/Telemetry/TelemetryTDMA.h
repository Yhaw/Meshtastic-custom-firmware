#pragma once

/**
 * @file TelemetryTDMA.h
 * @brief MAC-based wTDMA (Wireless Time Division Multiple Access) for CrowdSense Terra
 * 
 * Implements application-layer transmit scheduling to reduce packet collisions
 * in dense agricultural deployments. Each node gets a deterministic 10-second
 * slot based on its MAC address within a 10-minute cycle.
 * 
 * CRITICAL: This ONLY gates originating telemetry. It does NOT affect:
 * - Receiving packets (always active)
 * - Routing/rebroadcasting (always active)
 * - Acknowledgements (always active)
 */

#include <Arduino.h>

// TDMA Configuration
#define TDMA_SLOT_DURATION_SEC 10      // Each slot is 10 seconds
#define TDMA_CYCLE_DURATION_SEC 600    // Full cycle is 10 minutes
#define TDMA_TOTAL_SLOTS 60            // 600 / 10 = 60 slots

/**
 * @brief Get this node's assigned TDMA slot (0-59)
 * 
 * Uses ESP32 MAC address to deterministically assign a slot.
 * The slot remains constant across reboots for the same device.
 * 
 * @return uint8_t Slot number (0-59)
 */
inline uint8_t getTDMASlot() {
    static uint8_t cachedSlot = 255; // Cache to avoid recalculation
    
    if (cachedSlot == 255) {
        // Get ESP32 MAC address (6 bytes)
        uint64_t mac = ESP.getEfuseMac();
        
        // Simple hash: XOR all bytes together for distribution
        uint8_t hash = 0;
        for (int i = 0; i < 6; i++) {
            hash ^= (mac >> (i * 8)) & 0xFF;
        }
        
        // Map to slot range [0, 59]
        cachedSlot = hash % TDMA_TOTAL_SLOTS;
    }
    
    return cachedSlot;
}

/**
 * @brief Get current TDMA slot based on system uptime (0-59)
 * 
 * Uses millis() to determine which slot the system is currently in.
 * No RTC dependency - works from boot.
 * 
 * @return uint8_t Current slot number (0-59)
 */
inline uint8_t getCurrentTDMASlot() {
    uint32_t uptimeSec = millis() / 1000;
    uint32_t cyclePosition = uptimeSec % TDMA_CYCLE_DURATION_SEC;
    return (cyclePosition / TDMA_SLOT_DURATION_SEC) % TDMA_TOTAL_SLOTS;
}

/**
 * @brief Check if this node can transmit telemetry now
 * 
 * Returns true only when current slot matches this node's assigned slot.
 * 
 * @return bool True if transmission is allowed, false otherwise
 */
inline bool canTransmitTelemetry() {
    return getCurrentTDMASlot() == getTDMASlot();
}

/**
 * @brief Get milliseconds until next transmit window
 * 
 * Useful for scheduling the next check.
 * 
 * @return uint32_t Milliseconds until this node's slot begins
 */
inline uint32_t getMillisUntilNextSlot() {
    uint8_t currentSlot = getCurrentTDMASlot();
    uint8_t nodeSlot = getTDMASlot();
    
    uint32_t uptimeSec = millis() / 1000;
    uint32_t cyclePosition = uptimeSec % TDMA_CYCLE_DURATION_SEC;
    uint32_t currentSlotStart = (cyclePosition / TDMA_SLOT_DURATION_SEC) * TDMA_SLOT_DURATION_SEC;
    
    uint32_t slotsUntilNext;
    if (currentSlot == nodeSlot) {
        // We're in our slot now, next is one full cycle away
        slotsUntilNext = TDMA_TOTAL_SLOTS;
    } else if (nodeSlot > currentSlot) {
        // Our slot is later in this cycle
        slotsUntilNext = nodeSlot - currentSlot;
    } else {
        // Our slot is in the next cycle
        slotsUntilNext = (TDMA_TOTAL_SLOTS - currentSlot) + nodeSlot;
    }
    
    uint32_t secondsUntilNext = (slotsUntilNext * TDMA_SLOT_DURATION_SEC) - (cyclePosition - currentSlotStart);
    return secondsUntilNext * 1000;
}
