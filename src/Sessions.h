// Sessions.h
#ifndef SESSIONS_H
#define SESSIONS_H
#include <Arduino.h>
#include <LoraWANLite.h>
#include <Preferences.h>
#include <map>

// ─────────────────────────────────────────────
// SessionInfo Structure
// Holds cryptographic and identification session data per device
// ─────────────────────────────────────────────
struct SessionInfo {
    uint32_t devAddr;           // Unique device address (assigned by server)
    uint8_t devEUI[8];          // Device EUI (unique 64-bit identifier)
    uint8_t appSKey[16];        // Application session key
    uint8_t nwkSKey[16];        // Network session key
    uint8_t joinNonce[3];       // Server-generated JoinNonce
    uint8_t netID[3];           // Network ID
    uint8_t devNonce[2];        // Device-sent nonce for join
};

// Declare map only AFTER SessionInfo is defined
extern std::map<String, SessionInfo> sessionMap;
extern Preferences preferences;
enum SessionStatus {
  SESSION_OK,
  SESSION_NOT_FOUND,
  SESSION_INVALID_HMAC,
  SESSION_EXPIRED,
  SESSION_CORRUPTED
};


void deriveSessionKey(uint8_t* outKey, uint8_t keyType, const uint8_t* appKey,
                      const uint8_t* joinNonce, const uint8_t* netID, const uint8_t* devNonce);
                      
void saveSessionToNVS(const String& devEUI, SessionInfo session);

bool loadSessionFromNVS(const String& devEUI, SessionInfo& session);

void storeSessionFor(String devEUI, const SessionInfo& session);

bool getSessionFor(String devEUI, SessionInfo& session);

bool sessionExists(const String& devEUI);

SessionStatus verifySession(const String& srcID, SessionInfo& session);
SessionStatus verifyHmac(uint8_t* buffer, size_t length, uint8_t* receivedHMAC);

void flushAllSessions();

#endif // SESSIONS_H
