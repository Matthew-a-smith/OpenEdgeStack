#ifndef SESSIONS_H
#define SESSIONS_H

#include <Arduino.h>
#include "Gateway.h"
#include <Preferences.h>
#include <map>

// ─────────────────────────────────────────────
// Utilities
// ─────────────────────────────────────────────

/**
 * @brief Converts a byte array to a hexadecimal string.
 *
 * @param data Pointer to the byte array
 * @param len Number of bytes
 * @return Hex string
 */
String bytesToHex(const uint8_t* data, size_t len);

/**
 * @brief Encodes the device EUI into a string.
 *
 * @return Base64 or hex-encoded DevEUI string
 */
String encodeDevEUI();

/**
 * @brief Converts a DevEUI byte array to string.
 *
 * @param id Pointer to DevEUI bytes
 * @param len Length of DevEUI (default 8)
 * @return Hex string representation
 */
String devEUIToString(const uint8_t* id, size_t len = 8);

/**
 * @brief Converts any byte array to a hex string.
 *
 * @param id Pointer to byte array
 * @param len Length of data
 * @return Hex string
 */
String idToHexString(uint8_t* id, size_t len = 8);

/**
 * @brief Trims trailing zero bytes from a data buffer.
 *
 * @param data Pointer to buffer
 * @param len Original length
 * @return New length without trailing zeros
 */
size_t trimTrailingZeros(const uint8_t* data, size_t len);

// ─────────────────────────────────────────────
// SessionInfo Structure
// ─────────────────────────────────────────────

/**
 * @brief Holds cryptographic and identity info for a LoRa session.
 */
struct SessionInfo {
    uint32_t devAddr;           ///< Unique device address
    uint8_t devEUI[8];          ///< Device's EUI (64-bit)
    uint8_t appSKey[16];        ///< Application session key
    uint8_t nwkSKey[16];        ///< Network session key
    uint8_t joinNonce[3];       ///< Join nonce from server
    uint8_t netID[3];           ///< Network ID
    uint8_t devNonce[2];        ///< Device join nonce
};

// ─────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────

extern std::map<String, SessionInfo> sessionMap;
extern Preferences preferences;

enum SessionStatus {
    SESSION_OK,
    SESSION_NOT_FOUND,
    SESSION_INVALID_HMAC,
    SESSION_EXPIRED,
    SESSION_CORRUPTED
};

// ─────────────────────────────────────────────
// Session Management Functions
// ─────────────────────────────────────────────

/**
 * @brief Derives a session key using LoRaWAN spec.
 *
 * @param outKey Output buffer (16 bytes)
 * @param keyType Key type: 0x01 for NwkSKey, 0x02 for AppSKey
 * @param appKey AppKey (16 bytes)
 * @param joinNonce Join nonce (3 bytes)
 * @param netID Network ID (3 bytes)
 * @param devNonce Dev nonce (2 bytes)
 */
void deriveSessionKey(uint8_t* outKey, uint8_t keyType, const uint8_t* appKey,
                      const uint8_t* joinNonce, const uint8_t* netID, const uint8_t* devNonce);

/**
 * @brief Saves a session to NVS (non-volatile storage).
 *
 * @param devEUI String version of device EUI
 * @param session SessionInfo struct to save
 */
void saveSessionToNVS(const String& devEUI, SessionInfo session);

/**
 * @brief Loads a session from NVS into memory.
 *
 * @param devEUI String version of device EUI
 * @param session Reference to destination struct
 * @return true if session was found and loaded; false otherwise
 */
bool loadSessionFromNVS(const String& devEUI, SessionInfo& session);

/**
 * @brief Stores a session in the in-memory session map.
 *
 * @param devEUI String version of device EUI
 * @param session SessionInfo struct
 */
void storeSessionFor(String devEUI, const SessionInfo& session);

/**
 * @brief Attempts to get a session from memory.
 *
 * @param devEUI String version of device EUI
 * @param session Reference to fill with session data
 * @return true if session exists; false otherwise
 */
bool getSessionFor(String devEUI, SessionInfo& session);

/**
 * @brief Checks if a session exists in memory.
 *
 * @param devEUI String version of device EUI
 * @return true if session exists
 */
bool sessionExists(const String& devEUI);

/**
 * @brief Verifies if a session is valid and not expired.
 *
 * @param srcID Device EUI or address
 * @param session Reference to session
 * @return SessionStatus enum
 */
SessionStatus verifySession(const String& srcID, SessionInfo& session);

/**
 * @brief Verifies an HMAC against session keys.
 *
 * @param buffer Packet data
 * @param length Packet length
 * @param receivedHMAC Pointer to received HMAC (16 bytes)
 * @return SessionStatus
 */
SessionStatus verifyHmac(uint8_t* buffer, size_t length, uint8_t* receivedHMAC);

/**
 * @brief Removes a session from memory and storage.
 *
 * @param devEUI String version of device EUI
 */
void flushSessionFor(const String& devEUI);

/**
 * @brief Flushes all in-memory and stored sessions.
 */
void flushAllSessions();

void printBinaryBits(uint8_t* payload, size_t length);

#endif // SESSIONS_H
