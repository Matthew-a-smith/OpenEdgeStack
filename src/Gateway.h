#ifndef LORA_SESSION_H
#define LORA_SESSION_H

#include <Arduino.h>
#include <RadioLib.h>

/*
 * ───────────────────────────────────────────────────────────────
 * LoRaWAN Session Definitions & Packet Handlers
 * 
 * Handles JoinRequests, session key derivation, LoRa packet processing,
 * and HMAC verification for a custom LoRaWAN-like stack.
 * ───────────────────────────────────────────────────────────────
 */


enum DataType : uint8_t {
  TYPE_TEXT   = 0x01,
  TYPE_BYTES  = 0x02,
  TYPE_FLOATS = 0x03
};

// ─────────────────────────────────────────────
// Global Flags (set from IRQs or main loop)
// ─────────────────────────────────────────────
extern volatile bool transmissonFlag;
extern volatile bool receivedFlag;

// ─────────────────────────────────────────────
// LoRa Module Configuration
// ─────────────────────────────────────────────

extern PhysicalLayer* lora;   // pointer to any RadioLib-compatible module
// ─────────────────────────────────────────────
// Device & Application IDs (must be 8 or 16 bytes)
// ─────────────────────────────────────────────
extern uint8_t devEUI[8];              // Device EUI (64-bit)
extern uint8_t appEUI[8];              // Application EUI (64-bit)
extern uint8_t appKey[16];             // App root key (AES-128)

extern const uint8_t hmacKey[16];      // HMAC key (shared)
// ─────────────────────────────────────────────
// Function Declarations
// ─────────────────────────────────────────────
/**
 * @brief Sets the radio module globally
 * 
 * @param module // value for RadioLib-compatible module
 */
void setRadioModule(PhysicalLayer* module);
/**
 * @brief Handle JoinRequest only if device has not joined yet.
 * 
 * @param buffer Raw packet buffer (must start with DevEUI)
 * @param len    Length of buffer
 */
void handleJoinIfNeeded(uint8_t* buffer, size_t len);

/**
 * @brief Handles JoinRequest .
 * 
 * @param buffer Raw packet buffer (must start with DevEUI)
 * @param len    Length of buffer
 */
void handleJoinRequest(uint8_t* buffer, size_t len);
/**
 * @brief Process a received uplink LoRa packet (non-join).
 * 
 * @param buffer Raw received bytes
 * @param length Packet length
 */
void handleLoRaPacket(uint8_t* buffer, size_t length);

/**
 * @brief Send data acknowledgment back to device.
 * 
 * @param srcID     Device ID string (hex)
 * @param SenderID  Raw sender DevEUI (8 bytes)
 */
void sendDataAck(const String& srcID, uint8_t* SenderID);

/**
 * @brief Main packet receiver function (poll or ISR-driven).
 *        Handles both JoinRequest and normal packets.
 */
void Recive();


void printBinaryBits(uint8_t* payload, size_t length);

void decryptPayloadWithKey(uint8_t* appSKey, uint8_t* nonce, uint8_t* payload, size_t payloadLength, uint8_t* out);



#endif // LORA_SESSION_H
