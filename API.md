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

## Join Request not working
Send a join request **before** starting the receiver. This is required to successfully derive encryption keys.

## Sessions are not saveing 
SPIFFS is required during setup before sending the join to save all of the session infomation.

```cpp
void setup() {
    if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS Mount Failed");
    while (true);  // prevent further operation
  }
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  lastButtonState = digitalRead(BUTTON_PIN);

  Serial.println("[INFO] LoRa Init...");
  int state = lora.begin(frequency_plan);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println("[ERROR] LoRa Init FAIL");
    while (true);
  }

  int maxRetries = 3;
  int attempts = 2;
  int timeout = 3000;
  sendJoinRequest(maxRetries, timeout, attempts);  

  lora.setDio1Action(setFlags);
  lora.setPacketReceivedAction(setFlags);  
  state = lora.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa] startReceive failed: %d\n", state);
    while (true);
  }
}
```

---

## Start Receiving on gateway

```cpp
void setup() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS Mount Failed");
    while (true);  // prevent further operation
  }
  Serial.begin(115200);


  delay(1000);
  // LoRa
  int state = lora.begin(frequency_plan);
  if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LoRa] LoRa Init FAIL: %d\n", state);
    while (true);
  }
  
  lora.setDio1Action(setFlags);
  lora.setPacketReceivedAction(setFlags);  
  state = lora.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa] startReceive failed: %d\n", state);
    while (true);
  }

  Serial.println("[Setup] Setup complete.");
}
```

---

## Sending Packets

Each packet contains up to 256 bytes of data, in the form of:
  - Arduino String
  - null-terminated char array (C-string)
  - arbitrary binary data (byte array)
  - floats (4 bytes)
  
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