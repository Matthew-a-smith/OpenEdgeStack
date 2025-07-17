# OpenEdgeStack

OpenEdgeStack is an open-source encryption library for Arduino that enables secure communication over LoRa radios without the need for LoRaWAN gateways or backend infrastructure.

---

## Compatible Hardware

This project builds on [RadioLib](https://github.com/jgromes/RadioLib) and provides an encryption layer for embedded edge devices.

Supported LoRa boards:
- SX126x series (SX1261, SX1262, SX1268)

---
## Usage

This library enables LoRa-style communication **without** requiring a LoRaWAN gateway or centralized network. Any LoRa-compatible device can act as a receiver.

Key Features:
- Devices join via **OTAA** (Over-the-Air Activation)
- All communication is **AES-128 encrypted**
- Supports 8-byte **Device EUI** and 8-byte **HMAC** for message integrity
- Sessions are **stored in RAM** and persist across reboots
- Designed for lightweight operation between multiple edge devices and gateways

Note:
Currently, this library provides the encrypted LoRa communication layer.
A future extension is planned to add an MQTT-based console for viewing data when a network connection is available.

---

### Transmission Flow

```
End Device             →          Receiver (Gateway/Server)
───────────────                    ─────────────────────────────
Send JoinReq          ───────────▶  Parse & verify
Receive JoinAccept    ◀───────────  Send encrypted JoinAccept
Derive session keys
```
### Session Handling for End Devices
To enable secure communication, each end device must first call sendJoinRequest() during setup. This step allows the device to receive a join response and derive its encryption session keys.

If sendJoinRequest() is called too early—before the radio enters receive mode—the join response may be missed, resulting in failed session negotiation.

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
  int maxRetries = 3 //Number of retries
  int retryDelay = 3000 //Timeout per attempt in milliseconds
   sendJoinRequest(maxRetries, retryDelay);  // Wait for session handshake
}

```
### Radio Module Integration
The radio module (e.g., SX1262) is accessed using a PhysicalLayer* pointer.
This allows for generic access to shared radio methods such as:

  - readData()

  - startReceive() 

### Session Management on the Gateway

Multiple end devices (sensors, nodes, etc.) can connect to a single gateway—typically up to 8 or more depending on available memory.

Each device must send a valid JoinRequest() using its unique 8-byte DevEUI.

Upon acceptance, the gateway derives and stores session keys (AppSKey, NwkSKey) associated with that DevEUI.

  There's no hardcoded limit to how many devices can join The real constraints are:

  - RAM: For storing active sessions at runtime.

  - Flash: If you choose to persist sessions across reboots.

### Session Flushing & Recovery

If a device loses its session (e.g., due to memory wipe), it can simply re-join.

The gateway will flush any stale session tied to the same DevEUI upon recognizing a new valid JoinRequest().

You can configure retry attempts and timeouts to allow a device to automatically reattempt a join if its session becomes invalid or lost.

---

### Packet Format

```
[SenderID (8 bytes)] + [Encrypted Payload] + [HMAC (8 bytes)]
```

### Grouped Packet Storage

Packets can be grouped into bins and stored in memory or on disk, then transmitted later as a single encrypted blob.
This reduces transmission frequency and improves energy efficiency.

* Groups can be up to **255 bytes**
* Overflow is handled by creating new group files with prefixes

---


### Group Configuration Example

```cpp
/*
    Group1 
    Stores 57 raw bytes to /Grp1_0.bin as the raw payload.
    1 byte type + 57 raw bytes = 58 byte payload size
    58 bytes + 2 byte length = 60 byte file size
     60 bytes → padded to 64 bytes
     
      - 8 bytes sender ID
      - 64 bytes encrypted payload
      - 8 bytes HMAC

    8 + 64 + 8 = 80 bytes total    
*/

GroupConfig groupConfig = {
    .maxFileSize = 80,
    .groupLimit = 4,
    .groupPrefixLimit = 2
};

const char* Group1 = "Grp1";

String groupOne = "this is a test sentence up to and over 16 bytes in length";
storePacket((const uint8_t*)groupOne.c_str(), groupOne.length(), TYPE_TEXT, Group1);

// When ready
sendStoredGroupFile(Group1);
```
## Normal messages
You can also send messages immediately (without grouping), and they’ll still be encrypted.

```cpp
  String payload = "this is a test sentence up to and over 16 bytes in length";
  sendLora((const uint8_t*)groupOne.c_str(), groupOne.length(), TYPE_TEXT);

  // Delays 5 seconds before sending
  pollLora((const uint8_t*)groupOne.c_str(), groupOne.length(), TYPE_TEXT, 5000);

```

---
## Wiki

See [https://github.com/Matthew-a-smith/OpenEdgeStack.wiki.git](https://github.com/Matthew-a-smith/OpenEdgeStack.wiki.git)

---
## API

See [API.md](API.md)

---
## Examples

See the [examples](examples) folder.

---

## FAQ

**1) What libraries are required?**
This library builds on top of [RadioLib](https://github.com/jgromes/RadioLib). It handles encryption and addressing only — radio handling is left to the underlying library.


```cpp
#include <RadioLib.h>

//Optional used for perstiance and storage
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>
#include <map>
```

## Quick Links to radio Lib

- 📖 [**Wiki**](https://github.com/jgromes/RadioLib/wiki) – usage documentation  
- ❓ [**FAQ**](https://github.com/jgromes/RadioLib/wiki/Frequently-Asked-Questions)  
- 📘 [**API Reference**](https://jgromes.github.io/RadioLib) – auto-generated docs  

**2) Can other radios see the packets I’m sending?**
Yes, if another LoRa radio is configured identically and in range, it can receive packets — but all data is encrypted and HMAC-verified.

**3) Is the data encrypted?**
Absolutely. Every packet is AES-128 encrypted, padded to 16-byte blocks, and verified with an 8-byte HMAC.

**4) How is this different from LoRaWAN?**
This is a lightweight “LoRaWAN Lite” model. You don’t need to rely on an official gateway or backend server. Instead, any LoRa device can act as your gateway. Ideal for rural, offline, or minimal setups.

**6) Which frequencies can I use?**
Refer to [this LoRaWAN frequency table](https://www.thethingsnetwork.org/wiki/LoRaWAN/Frequencies/By-Country) and your hardware datasheet. Always comply with your country’s legal duty cycle limits.

---

## License

This library is licensed under the [MIT License](LICENSE).

---
