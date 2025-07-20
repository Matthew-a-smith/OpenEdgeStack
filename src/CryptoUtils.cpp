#include <CryptoUtils.h>
#include "Gateway.h"
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

void aes128_encrypt_ctr(const uint8_t* key, const uint8_t* nonce, const uint8_t* input, size_t length, uint8_t* output) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, 128);

  uint8_t stream_block[16];
  size_t nc_off = 0;
  uint8_t nonce_counter[16];
  memcpy(nonce_counter, nonce, 16); // nonce should be unique per packet

  mbedtls_aes_crypt_ctr(&ctx, length, &nc_off, nonce_counter, stream_block, input, output);
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

// ────── Encrypted Payload Packet Layout ───────────────
// Offset | Size         | Field           | Description
// -------|--------------|------------------|------------------------------
// 0      | 8            | Sender ID        | Sender devEUI (used for session lookup)
// 8      | 16           | Nonce            | 8B Sender ID + 8B random counter
// 24     | payloadLen   | Encrypted Payload| AES-128-CTR encrypted data (no padding)
// 24+N   | 8            | HMAC             | First 8 bytes of HMAC-SHA256
//
// Notes:
// - AES-128-CTR mode used (no padding, stream cipher)
// - Nonce format: [Sender ID (8B) | Random CTR (8B)]
// - HMAC is computed over: [Sender ID + Nonce + Encrypted Payload]
// - Final packet length = 8 (Sender) + 16 (Nonce) + payloadLen + 8 (HMAC)
// - Caller must free returned buffer
//
// Inputs:
//   - payloadData: Raw data to encrypt
//   - payloadLen: Length of payloadData
//   - session: Contains appSKey
//   - finalLen: Reference set to total packet length
//   - Sender: 8-byte devEUI
//
// Output:
//   - Returns full packet: [Sender ID][Nonce][Encrypted Payload][HMAC]
//   - Ensures confidentiality (AES) and integrity/authenticity (HMAC)



uint8_t* encryptAndPackage(
  const uint8_t* payloadData, size_t payloadLen,
  const SessionInfo& session,
  size_t& finalLen,
  const uint8_t* Sender
) {
  uint8_t appSKey[16];
  memcpy(appSKey, session.appSKey, 16);

  // 1. Prepare nonce (CTR IV): use sender ID + a counter or random value
  uint8_t nonce[16] = {0};
  memcpy(nonce, Sender, 8);
  uint64_t ctr = esp_random(); // ensure different for each packet
  memcpy(nonce + 8, &ctr, 8);

  // 2. Encrypt with CTR
  uint8_t* encryptedPayload = new uint8_t[payloadLen];
  aes128_encrypt_ctr(appSKey, nonce, payloadData, payloadLen, encryptedPayload);

  // 3. Build [Sender ID][Nonce][Encrypted Payload]
  size_t baseLen = 8 + 16 + payloadLen;
  uint8_t* fullPayload = new uint8_t[baseLen];
  memcpy(fullPayload, Sender, 8);
  memcpy(fullPayload + 8, nonce, 16);
  memcpy(fullPayload + 24, encryptedPayload, payloadLen);

  // 4. Compute HMAC over [Sender ID + Nonce + EncryptedPayload]
  uint8_t hmacResult[32];
  computeHMAC_SHA256(hmacKey, sizeof(hmacKey), fullPayload, baseLen, hmacResult);

  // 5. Final packet = fullPayload + HMAC (truncated 8B)
  finalLen = baseLen + 8;
  uint8_t* finalPacket = new uint8_t[finalLen];
  memcpy(finalPacket, fullPayload, baseLen);
  memcpy(finalPacket + baseLen, hmacResult, 8);

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


void decryptPayload(uint8_t* appSKey, uint8_t* nonce, uint8_t* encryptedPayload, size_t length, uint8_t* output) {
  aes128_encrypt_ctr(appSKey, nonce, encryptedPayload, length, output);
}


