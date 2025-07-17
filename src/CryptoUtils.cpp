#include <CryptoUtils.h>
#include <LoraWANLite.h>
#include <Sessions.h>

#include <Arduino.h>
#include "mbedtls/md.h"
#include "mbedtls/aes.h"



// ────── HMAC-SHA256 Computation ──────

// Computes an HMAC-SHA256 digest for the given message using the provided key.
// Inputs:
//   - key: HMAC secret key
//   - keyLen: length of the key (typically 16 bytes)
//   - msg: pointer to the message data
//   - msgLen: length of the message
//   - out: output buffer (32 bytes) where full HMAC digest will be stored
// Used to ensure message authenticity and integrity before decryption.

void computeHMAC_SHA256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t* out) {
  const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(mdInfo, key, keyLen, msg, msgLen, out);
}

// Verifies an incoming HMAC by comparing the first 8 bytes of the computed HMAC
// against the received one.
// Inputs:
//   - buffer: original message data (Sender ID + Encrypted Payload)
//   - length: total length of message (including 8-byte HMAC at end)
//   - receivedHMAC: pointer to the last 8 bytes of the packet (received HMAC)
// Returns:
//   - true if HMAC is valid; false otherwise
// Purpose:
//   - Prevents tampered or spoofed packets from being processed

bool verifyHMAC(uint8_t* buffer, size_t length, uint8_t* receivedHMAC) {
  uint8_t computedHMAC[32];
  computeHMAC_SHA256(hmacKey, sizeof(hmacKey), buffer, length - 8, computedHMAC);
  printHex(computedHMAC, 8, "[INFO] Truncated for compare: ");

  for (int i = 0; i < 8; i++) {
    if (computedHMAC[i] != receivedHMAC[i]) {
      return false;
    }
  }
  return true;
}

bool verifyMIC(uint8_t* buffer, size_t length, uint8_t* receivedHMAC) {
  uint8_t computedHMAC[32];
  computeHMAC_SHA256(hmacKey, sizeof(hmacKey), buffer, length - 4, computedHMAC);
  printHex(computedHMAC, 4, "[INFO] Truncated for compare: ");

  for (int i = 0; i < 4; i++) {
    if (computedHMAC[i] != receivedHMAC[i]) {
      return false;
    }
  }
  return true;
}


// ────── AES-128 Block Functions ───

// Encrypts/Decrypts a single 16-byte block using AES-128 in ECB mode.
// Inputs:
//   - key: 16-byte AES key (e.g. appSKey)
//   - input: 16-byte block to encrypt/decrypt
//   - output: 16-byte destination buffer
// Used for:
//   - Encrypting join accept payloads
//   - Encrypting session structs
//   - Blockwise payload encryption

void aes128_encrypt_block(const uint8_t* key, const uint8_t* input, uint8_t* output) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, 128);
  mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, input, output);
  mbedtls_aes_free(&ctx);
}

void aes128_decrypt_block(const uint8_t* key, const uint8_t* input, uint8_t* output) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_dec(&ctx, key, 128);
  mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, input, output);
  mbedtls_aes_free(&ctx);
}

// ────── Session Encryption ──────

// Encrypts or decrypts an entire SessionInfo struct (32 bytes total) using two AES blocks.
// Purpose:
//   - Used when securely storing or transmitting session data (to persistent storage or peers)
// Inputs:
//   - For encryption: session struct + appKey → encrypted 32 bytes
//   - For decryption: 32-byte input + appKey → reconstructed session struct

void encryptSession(const SessionInfo& session, uint8_t* out) {
  aes128_encrypt_block(appKey, (uint8_t*)&session, out);  // 32 bytes → 2 AES blocks
  aes128_encrypt_block(appKey, ((uint8_t*)&session) + 16, out + 16);
}

void decryptSession(const uint8_t* in, SessionInfo& session) {
  aes128_decrypt_block(appKey, in, (uint8_t*)&session);
  aes128_decrypt_block(appKey, in + 16, ((uint8_t*)&session) + 16);
}

// ────── Encrypted Payload Packet Layout ──────

// ────── Encrypted Payload Packet Layout ─────────────────
// Offset | Size          | Field           | Description
// -------|---------------|------------------|------------------------------
// 0      | 8             | Sender ID        | Sender devEUI in byte format
// 8      | N (padded)    | AES Encrypted    | Payload encrypted with appSKey
// 8+N    | 8             | HMAC             | First 8 bytes of HMAC-SHA256
//
// Notes:
// - AES encryption uses 128-bit blocks; payload padded to nearest multiple of 16
// - HMAC computed on: [Sender ID + Encrypted Payload]
// - Final length returned via `finalLen`
// - Function allocates memory for packet and returns pointer to final buffer

// Inputs:
//   - payloadData: raw message content to send (e.g. text or file chunk)
//   - payloadLen: length of raw payload
//   - session: SessionInfo struct holding appSKey
//   - finalLen: reference that will be set to final packet length
//   - Sender: 8-byte sender devEUI
//
// Outputs:
//   - Returns full encrypted packet: [Sender ID][Encrypted Payload][HMAC (8B)]
//   - HMAC is computed over [Sender ID + Encrypted Payload]
//   - Encrypted payload is padded to nearest 16-byte block for AES
//   - Caller must free the returned buffer manually
//
// Final layout:
//   ┌────────────┬──────────────────────┬─────────────┐
//   │ Sender ID  │ AES-Encrypted Payload│ Trunc. HMAC │
//   │ (8 bytes)  │ 16*N bytes           │ (8 bytes)   │
//   └────────────┴──────────────────────┴─────────────┘
//
// Used for:
//   - Sending encrypted, signed data packets over LoRa
//   - Ensures confidentiality + integrity


uint8_t* encryptAndPackage(
  const uint8_t* payloadData, size_t payloadLen,
  const SessionInfo& session,
  size_t& finalLen,
  const uint8_t* Sender
) {

  uint8_t appSKey[16], localNwkSKey[16];
  memcpy(appSKey, session.appSKey, 16);

  size_t encryptedLen = (payloadLen % 16 == 0) ? payloadLen : ((payloadLen / 16) + 1) * 16; 

  uint8_t* paddedPayload = new uint8_t[encryptedLen]();
  memcpy(paddedPayload, payloadData, payloadLen);

  uint8_t* encryptedPayload = new uint8_t[encryptedLen];
  for (size_t i = 0; i < encryptedLen; i += 16) {
    aes128_encrypt_block(appSKey, paddedPayload + i, encryptedPayload + i);
  }

  size_t baseLen = 8 + encryptedLen; 
  uint8_t* fullPayload = new uint8_t[baseLen];
  memcpy(fullPayload, Sender, 8);
  memcpy(fullPayload + 8, encryptedPayload, encryptedLen);

  uint8_t hmacResult[32];
  computeHMAC_SHA256(hmacKey, sizeof(hmacKey), fullPayload, baseLen, hmacResult);

  finalLen = baseLen + 8;
  uint8_t* finalPacket = new uint8_t[finalLen];
  memcpy(finalPacket, fullPayload, baseLen);
  memcpy(finalPacket + baseLen, hmacResult, 8);

  delete[] paddedPayload;
  delete[] encryptedPayload;
  delete[] fullPayload;
  return finalPacket;
}

// Decrypts a full encrypted payload using AES-128 in ECB mode.
// Inputs:
//   - appSKey: 16-byte session encryption key
//   - payload: encrypted payload data
//   - length: total length (must be multiple of 16)
//   - output: buffer to hold decrypted data
//
// Used after verifying HMAC and before interpreting the decrypted content.
// Caller must ensure `output` is allocated with at least `length` bytes.


void decryptPayload(uint8_t* appSKey, uint8_t* payload, size_t length, uint8_t* output) {
  for (size_t i = 0; i < length; i += 16) {
    aes128_decrypt_block(appSKey, payload + i, output + i);
  }
}

