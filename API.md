```markdown
# EdgeStack API

## Include Library

```arduino
#include <LoraWANLite.h>
#include <EndDevice.h>

#include <RadioLib.h>
#include <Wire.h>

#include <FS.h>
#include <SPIFFS.h>
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
Module module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY); // Pin configuration
SX1262 radioModule(&module);                            // Create SX1262 instance
PhysicalLayer* lora = &radioModule;                     // Set global radio pointer
float frequency_plan = 915.0;                           // Frequency (in MHz)

// Frequency in MHz
float frequency_plan = 915.0;

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
On end devices, it's essential to call sendJoinRequest() during setup.
This ensures the device can properly receive the join response and derive session keys for encryption.

If sendJoinRequest() is called too early, the join response might be missed due to the radio not being in receive mode. This can result in failed session negotiation and no encryption keys.


## Sessions are not saveing 
SPIFFS is required during setup before sending the join to save all of the session infomation.
You can connect multiple end devices (e.g., sensors, nodes, etc.) to a single gateway — usually up to 8 or more depending on memory.

Each device must send a valid JoinRequest() first. Once accepted, the session keys are derived and stored separately using the unique 8-byte DevEUI of that device.

There’s no hardcoded limit to how many devices can join. The only constraints are the available RAM (for runtime session storage) and flash space (if persisting sessions across reboots).

```cpp

```cpp
// For end devices (transmitters)
Module module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY); // Pin configuration
SX1262 radioModule(&module);                            // Create SX1262 instance
PhysicalLayer* lora = &radioModule;                     // Set global radio pointer
float frequency_plan = 915.0;                           // Frequency (in MHz)

void setup() {
  // Mount SPIFFS to persist sessions across reboots
  if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS Mount Failed");
    while (true);
  }

  // Register the chosen radio module globally
  setRadioModule(&radioModule);  
  delay(1000);

  // Initialize the radio module
  int state = radioModule.begin(frequency_plan);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa] Init FAILED: %d\n", state);
    while (true);
  }

  // Set flags and begin listening
  radioModule.setDio1Action(setFlags);
  radioModule.setPacketReceivedAction(setFlags);
  
  state = radioModule.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa] startReceive FAILED: %d\n", state);
    while (true);
  }
  // IMPORTANT: Send join request AfTER enabling receive mode
  int maxRetries = 3; //Number of retries
  int retryDelay = 3000; //Timeout per attempt in milliseconds
   sendJoinRequest(maxRetries, retryDelay);  // Wait for session handshake
}

```

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
  delay(100);
  sessionMap.clear();
  Serial.println("[DEBUG] Cleared RAM session map.");
  preferences.begin("lora", false);  // open for lifetime
  preferences.clear();
   preferences.end();
   Serial.println("[NVS] All sessions cleared from NVS.");

  setRadioModule(&radioModule);  // ✅ this is valid no
  delay(1000);

  // LoRa
  int state = radioModule.begin(frequency_plan);
  if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LoRa] LoRa Init FAIL: %d\n", state);
    while (true);
  }
  
  radioModule.setDio1Action(setFlags);
  radioModule.setPacketReceivedAction(setFlags);  
  state = radioModule.startReceive();
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
everything includeing groups gets paddded to the nearest 16 for AES standards.

group settings  can be change depending on size requirements
maxFileSize applies only to the raw stored group files on the sender  
not to the final encrypted and transmitted LoRa packets, which will grow due to 
padding, metadata, and HMAC.

  Storage logic overview:

  Each entry stored to a group file follows this format:
    [2-byte length][1-byte type][N-byte raw payload]

  So, total entry size = 2 + 1 + N bytes

  Important considerations:
  - AES encryption works in 16-byte blocks.
  - Therefore, the raw payload is padded to the next multiple of 16 before encryption.
  - Final encrypted packet structure:
      - 8 bytes  : Sender ID
      - N bytes  : Encrypted payload (AES padded)
      - 8 bytes  : HMAC
    → Total size = 8 + padded(N) + 8 bytes

  Keep this in mind when designing how much data you store per group file,
  especially when you want to avoid splitting into multiple files due to size limits.


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