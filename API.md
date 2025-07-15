```markdown
# EdgeStack API

## Include Library

```arduino
#include <LoraWANLite.h>
#include <EndDevice.h>
```

---

## Setup End Device

### Initialization (`begin`)

Initialize the library just like The Things Network.  
The following parameters are required:

- Frequency (e.g., 915.0 MHz)
- Device EUI (8 bytes)
- Application EUI (8 bytes)
- App Key (16 bytes)
- HMAC Key (16 bytes)

```cpp
// Frequency in MHz
float frequency_plan = 915.0;

// Create LoRa module instance (example: SX1262)
SX1262 lora = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

// Device EUI (64-bit unique device ID)
uint8_t devEUI[8] = {
  0x4F, 0x65, 0x75, 0xC5, 0xF0, 0x31, 0x00, 0x00
};

// Application EUI (64-bit application ID)
uint8_t appEUI[8] = {
  0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80
};

// Application root key (AES-128 encryption key)
uint8_t appKey[16] = {
  0x2A, 0xC3, 0x76, 0x13, 0xE4, 0x44, 0x26, 0x50,
  0x2B, 0x8D, 0x7E, 0xEE, 0xAB, 0xA9, 0x57, 0xCD
};

// Shared 16-byte static HMAC key for message integrity
const uint8_t hmacKey[16] = {
  0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44,
  0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xDE, 0xAD
};
```

---

## Join Request

Send a join request **before** starting the receiver. This is required to successfully derive encryption keys.

```cpp
int maxRetries = 3;
int attempts = 2;
int timeout = 3000;

sendJoinRequest(maxRetries, timeout, attempts);
```

---

## Start Receiving

```cpp
lora.setDio1Action(setFlags);
lora.setPacketReceivedAction(setFlags);

int state = lora.startReceive();
if (state != RADIOLIB_ERR_NONE) {
  Serial.printf("[LoRa] startReceive failed: %d\n", state);
  while (true);
}
```

---

## Sending Packets

### Normal Encrypted Send

Send a single encrypted packet.

```cpp
String payload = "this is a test sentence up to and over 16 bytes in length";
sendLora((const uint8_t*)payload.c_str(), payload.length(), TYPE_TEXT);
```

### Delayed Send (Poll Send)

Send an encrypted packet after a delay.

```cpp
pollLora((const uint8_t*)payload.c_str(), payload.length(), TYPE_TEXT, 5000); // Delay 5000 ms
```

---

## Grouped Packet Storage

Packets can be grouped and sent later as bulk encrypted files to reduce transmission frequency.

### Group Settings

| Setting            | Description                                         |
| ------------------ | --------------------------------------------------- |
| `maxFileSize`      | Max bytes allowed per group file                    |
| `groupLimit`       | Max total group files per prefix                    |
| `groupPrefixLimit` | Max unique group name prefixes (e.g., Grp1, Grp1.1) |

---

### Function Parameters

| Parameter Type    | Name     |
| ----------------- | -------- |
| `const uint8_t *` | data     |
| `size_t`          | length   |
| `enum DataType`   | dataType |
| `const char *`    | pathBase |

---

### Supported Data Types

| Data Type     |
| ------------- |
| `TYPE_TEXT`   |
| `TYPE_BYTES`  |
| `TYPE_FLOATS` |

---

### Example Group Configuration and Use

```cpp
GroupConfig groupConfig = {
    .maxFileSize = 80,
    .groupLimit = 4,
    .groupPrefixLimit = 2
};

const char* Group1 = "Grp1";

String groupOne = "this is a test sentence up to and over 16 bytes in length";
storePacket((const uint8_t*)groupOne.c_str(), groupOne.length(), TYPE_TEXT, Group1);

// When ready, send the stored group file
sendStoredGroupFile(Group1);
```

---

## Packet Format

```
[SenderID (8 bytes)] + [Encrypted Payload] + [HMAC (8 bytes)]
```

---

## Supported Data Types

| Data Type   |
|-------------|
| `TYPE_TEXT` |
| `TYPE_BYTES`|
| `TYPE_FLOATS`|
```