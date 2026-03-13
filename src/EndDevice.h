#ifndef END_DEVICE_H
#define END_DEVICE_H

#include <Arduino.h>
#include "Gateway.h"
#include "Gateway.h"
#include "EndDevice.h"
#include "CryptoUtils.h"
#include "Sessions.h"
extern String globalReply;

struct GroupConfig {
    size_t maxFileSize;
    int groupLimit;
    int groupPrefixLimit;
};

// Must be defined by user sketch
extern GroupConfig groupConfig;
extern String devEUIHex;
// ─────────────────────────────────────────────
// Function Declarations
// ─────────────────────────────────────────────


/**
 * @brief Stores a packet to a group file in SPIFFS.
 *
 * @param data Pointer to the payload data
 * @param length Length of the payload
 * @param dataType Type of data (used as 1-byte header)
 * @param pathBase Base name for the group file (e.g., "Grp1")
 */
void storePacket(const uint8_t* data, size_t length, DataType dataType, const char* pathBase);

/**
 * @brief Listens for incoming LoRa packets and processes them.
 */
void listenForIncoming();

/**
 * @brief Sends an encrypted LoRa packet.
 *
 * @param finalPacket Pointer to encrypted packet
 * @param finalLen Length of packet
 */
void sender(const uint8_t* finalPacket, size_t finalLen);

/**
 * @brief Sends a LoRaWAN JoinRequest and waits for JoinAccept.
 *
 * @param maxRetries Number of retries
 * @param retryDelay Timeout per attempt in milliseconds
 * 
 */
void sendJoinRequest(int maxRetries, unsigned long retryDelay);

/**
 * @brief Polls a target device for a response using encrypted packets.
 *
 * @param payloadData Pointer to payload data
 * @param payloadLen Length of payload
 * @param preDelayMillis Delay to send packet
 * @param dataType Type of payload
 */
void pollLora(
    const uint8_t* payloadData, 
    size_t payloadLen, 
    DataType dataType,
    unsigned long preDelayMillis = 0
);


/**
 * @brief Sends one or two stored group files using LoRa.
 *
 * @param pathBase Prefix of stored group file (e.g., "Grp1")
 */
void sendStoredGroupFile(const char* pathBase);

/**
 * @brief Sends an encrypted payload with a type tag and receives ACK.
 *
 * @param payloadData Pointer to payload data
 * @param payloadLen Length of payload
 * @param dataType Type of payload
 */
void sendLora(
    const uint8_t* payloadData, 
    size_t payloadLen, 
    DataType dataType
);


/**
 * @brief Processes a received LoRa packet (session validation, HMAC, decryption).
 *
 * @param buffer Pointer to raw packet data
 * @param length Length of received packet
 */
void handlePacket(uint8_t* buffer, size_t length);

#define STREAM_END 0xFF   // EOT

class PolymorphicLoraSender {
public:
    PolymorphicLoraSender() = default;
    virtual ~PolymorphicLoraSender() = default;

    // Virtual function for sending a single chunk (max 255 bytes)
    // Session info is now passed in so we don't verify each chunk
    virtual void sendChunk(const uint8_t* chunk, size_t len, DataType type, const SessionInfo& session) {

        // Build packet: 1 byte type + payload
        size_t totalLen = len + 1;
        uint8_t* packetData = new uint8_t[totalLen];
        packetData[0] = (uint8_t)type;
        memcpy(packetData + 1, chunk, len);

        // Encrypt + package
        size_t finalLen = 0;
        uint8_t* finalPacket = encryptAndPackage(packetData, totalLen, session, finalLen, devEUI);

        // Send over LoRa
        transmissonFlag = true;
        lora->standby();
        delay(5);
        int result = lora->transmit(finalPacket, finalLen);
        delay(10);
        lora->startReceive();
        transmissonFlag = false;

        if (result == RADIOLIB_ERR_NONE) {
            Serial.printf("[PolymorphicLoraSender] Sent chunk of %zu bytes successfully.\n", len);
        } else {
            Serial.printf("[PolymorphicLoraSender] Failed to send chunk of %zu bytes.\n", len);
        }

        delete[] finalPacket;
        delete[] packetData;
    }

    // Send an arbitrary-length stream in 255-byte chunks
    void sendStream(const uint8_t* data, size_t totalLen, DataType type = TYPE_STREAM) {
        const size_t MAX_CHUNK = 200;
        size_t offset = 0;

        // --- Verify session ONCE ---
        SessionInfo session;
        SessionStatus status = verifySession(devEUIHex, session);
        if (status != SESSION_OK) {
            Serial.println("[ERROR] Session not found, cannot send stream.");
            return;
        }

        while (offset < totalLen) {
            size_t remaining = totalLen - offset;
            size_t chunkLen = (remaining > MAX_CHUNK) ? MAX_CHUNK : remaining;

            // If this is the LAST chunk, append the end marker
            if (offset + chunkLen >= totalLen) {
                uint8_t* finalChunk = new uint8_t[chunkLen + 1];
                memcpy(finalChunk, data + offset, chunkLen);
                finalChunk[chunkLen] = STREAM_END;   // <-- identifier byte at end

                sendChunk(finalChunk, chunkLen + 1, type, session);
                delete[] finalChunk;

            } else {
                sendChunk(data + offset, chunkLen, type, session);
            }

            offset += chunkLen;
            delay(5);
        }

        Serial.printf("[PolymorphicLoraSender] Stream sent (%zu bytes + end marker)\n", totalLen);
    }
};

#endif // END_DEVICE_H