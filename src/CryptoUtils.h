// CryptoUtils.h
#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <Sessions.h>
#include <Arduino.h>
#include <LoraWANLite.h>



String bytesToHex(const uint8_t* data, size_t len);
String encodeDevEUI();
String devEUIToString(const uint8_t* id, size_t len = 8);
String idToHexString(uint8_t* id, size_t len = 8);
size_t trimTrailingZeros(const uint8_t* data, size_t len);

uint8_t* encryptAndPackage(
  const uint8_t* payloadData, size_t payloadLen,
  const SessionInfo& session,
  size_t& finalLen,
  const uint8_t* Sender
);

void decryptPayload(uint8_t* appSKey, uint8_t* payload, size_t length, uint8_t* output);

void printHex(const uint8_t* data, size_t len, const char* label);

void computeHMAC_SHA256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t* out);

bool verifyHMAC(uint8_t* buffer, size_t length, uint8_t* receivedHMAC);

bool verifyMIC(uint8_t* buffer, size_t length, uint8_t* receivedHMAC);

void aes128_encrypt_block(const uint8_t* key, const uint8_t* input, uint8_t* output);

void aes128_decrypt_block(const uint8_t* key, const uint8_t* input, uint8_t* output);

void encryptSession(const SessionInfo& session, uint8_t* out);

void decryptSession(const uint8_t* in, SessionInfo& session);

#endif // CRYPTO_UTILS_H

