// CryptoUtils.h
#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <Sessions.h>
#include <Arduino.h>
#include "Gateway.h"

uint8_t* encryptAndPackage(
  const uint8_t* payloadData, size_t payloadLen,
  const SessionInfo& session,
  size_t& finalLen,
  const uint8_t* Sender
);

/**
 * @brief Decrypts a full encrypted payload using AES-128 in ECB mode. 
 *
 * @param appSKey 16-byte session encryption key
 * @param payload encrypted payload data
 * @param length total length (must be multiple of 16)
 * @param output buffer to hold decrypted data
 */
void decryptPayload(uint8_t* appSKey, uint8_t* nonce, uint8_t* encryptedPayload, size_t length, uint8_t* output);

/**
 * @brief Prints hex Characters. 
 *
 * @param data  payload data
 * @param len total length 
 * @param label Label to add for data type (eg.. printHex(data, len, "[JOIN] label: ");)
 */
void printHex(const uint8_t* data, size_t len, const char* label);

/**
 * @brief Computes an HMAC-SHA256 digest for the given message using the provided key. 
 *
 * @param key  HMAC secret key
 * @param keyLen length of the key (typically 16 bytes) 
 * @param msg pointer to the message data
 * @param msgLen length of the message
 * @param out output buffer (32 bytes) where full HMAC digest will be stored
 *            Used to ensure message authenticity and integrity before decryption.
 */
void computeHMAC_SHA256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t* out);

/**
 * @brief Verifies an incoming HMAC by comparing the first 8 bytes of the computed HMAC
 *        against the received one. 
 *
 * @param buffer original message data (Sender ID + Encrypted Payload)
 * @param length total length of message (including 8-byte HMAC at end) 
 * @param receivedHMAC pointer to the last 8 bytes of the packet (received HMAC)
 */
bool verifyHMAC(uint8_t* buffer, size_t length, uint8_t* receivedHMAC);

bool verifyMIC(uint8_t* buffer, size_t length, uint8_t* receivedHMAC);

/**
 * @brief Encrypts/Decrypts a single 16-byte block using AES-128 in ECB mode. 
 *
 * @param key 16-byte AES key (e.g. appSKey)
 * @param input 16-byte block to encrypt/decrypt
 * @param output 16-byte destination buffer
 */
void aes128_encrypt_block(const uint8_t* key, const uint8_t* input, uint8_t* output);

void aes128_decrypt_block(const uint8_t* key, const uint8_t* input, uint8_t* output);

void encryptSession(const SessionInfo& session, uint8_t* out);

void decryptSession(const uint8_t* in, SessionInfo& session);

void aes128_encrypt_ctr(const uint8_t* key, const uint8_t* nonce, const uint8_t* input, size_t length, uint8_t* output);

#endif // CRYPTO_UTILS_H

