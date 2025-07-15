#ifndef END_DEVICE_H
#define END_DEVICE_H

#include <Arduino.h>
#include <LoraWANLite.h>

extern String globalReply;

struct GroupConfig {
    size_t maxFileSize;
    int groupLimit;
    int groupPrefixLimit;
};

// Must be defined by user sketch
extern GroupConfig groupConfig;

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
 * @param timeout Timeout per attempt in milliseconds
 * @param attempt Current join attempt count
 */
void sendJoinRequest(int maxRetries, int timeout, int attempt);

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

#endif // END_DEVICE_H