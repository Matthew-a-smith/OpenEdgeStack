# OpenEdgeStack [![arduino-library-badge](https://www.ardu-badge.com/badge/OpenEdgeStack.svg?)](https://www.ardu-badge.com/OpenEdgeStack)

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
- All communication is **AES-128/CTR encrypted**
- Supports 8-byte **Device EUI** and 8-byte **HMAC** for message integrity
- Sessions are **stored in RAM** and persist across reboots
- Designed for lightweight operation between multiple edge devices and gateways

Note:
Currently, this library provides the encrypted LoRa communication layer.
A future extension is planned to add an MQTT-based console for viewing data when a network connection is available.

---
## Transmission Flow
```
End Device             →          Receiver (Gateway/Server)
───────────────                    ─────────────────────────────
Send JoinReq          ───────────▶  Parse & verify
Receive JoinAccept    ◀───────────  Send encrypted JoinAccept
Derive session keys
```

### Radio Module Integration
The radio module (e.g., SX1262) is accessed using a PhysicalLayer* pointer.
This allows for generic access to shared radio methods such as:

  - readData()

  - startReceive() 

## Key Management

🔐 **Important** The Examples does **not** include default keys. Each device must be provisioned with unique cryptographic keys. 🔐

**You have two options for securely generating keys:**

  ### Option 1: Use the Provided Python Script
  - Run the provided `generate_keys.py` inside of the extra folder to create unique and secure device credentials:
  - This will output keys in C-style arrays ready to paste into your Arduino sketch.
  - Just change the GatewayEui into the devEUi for the selected Gateway.
    
    ```bash
    python generate_keys.py
    ```

  ### Option 2: Use The Things Network (TTN)
  - You can also use The Things Network to generate LoRaWAN-compatible credentials and manually copy them into your device configuration.

---
### Session Management on the Gateway
Multiple end devices (sensors, nodes, etc.) can connect to a single gateway typically up to 8 or more depending on available memory.

Each device must send a valid JoinRequest() using its unique 8-byte DevEUI.

Upon acceptance, the gateway derives and stores session keys (AppSKey, NwkSKey) associated with that DevEUI.

  There's no hardcoded limit to how many devices can join The real constraints are:

  - RAM: For storing active sessions at runtime.

  - Flash: If you choose to persist sessions across reboots.

---
### Packet Format
```
[SenderID (8 bytes)] + [Nonce (16 bytes)] + [Encrypted Payload] + [HMAC (8 bytes)]
```
Key Notes:
  -  No padding is applied.
  -  AES-CTR encryption allows variable-length payloads.
  -  The nonce is 16 bytes: first 8 bytes are the sender's DevEUI, next 8 are random (per packet).
  -  HMAC-SHA256 is computed over [SenderID + Nonce + Encrypted Payload], then truncated to 8 bytes.

### Grouped Packet Storage
Packets can be grouped into bins and stored in memory or on disk, then transmitted later as a single encrypted blob.
This reduces transmission frequency and improves energy efficiency.

* Groups can be up to **255 bytes**
* Overflow is handled by creating new group files with prefixes

check out [**transmitterGroup**](examples/transmitterGroup/transmitterGroup.ino) from examples for more info on it.
 
---
### Normal messages
You can also send messages immediately (without grouping), and they’ll still be encrypted.

Their is also a poll send as well that acts a delay for sending.

---
### Quick Links 

- 📖 [**Wiki**](https://github.com/Matthew-a-smith/OpenEdgeStack/wiki) – usage documentation  
- ❓ [**Examples**](examples)  
- 📘 [**API Reference**](API.md) 

### FAQ

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

### Quick Links to radio Lib
- 📖 [**Wiki**](https://github.com/jgromes/RadioLib/wiki) – usage documentation  
- ❓ [**FAQ**](https://github.com/jgromes/RadioLib/wiki/Frequently-Asked-Questions)  
- 📘 [**API Reference**](https://jgromes.github.io/RadioLib) – auto-generated docs  

**2) Can other radios see the packets I’m sending?**
Yes, if another LoRa radio is configured identically and in range, it can receive packets — but all data is encrypted and HMAC-verified. Check out the wiki for a full breakdown of how the encrypttion and the hmac keys help protect data in transit.

**3) How is this different from LoRaWAN?**
This is a lightweight “LoRaWAN Lite” model. You don’t need to rely on an official gateway or backend server. Instead, any LoRa device can act as your gateway. Ideal for rural, offline, or minimal setups.

**4) Which frequencies can I use?**
Refer to [this LoRaWAN frequency table](https://www.thethingsnetwork.org/wiki/LoRaWAN/Frequencies/By-Country) and your hardware datasheet. Always comply with your country’s legal duty cycle limits.

### What about LoRaWAN?
This isn't LoRaWAN, but it mirrors the same goals
 - Secure sessions
 - Authentication
 - MIC protection
 - AES key derivation
   
### Why Not Just Use LoRaWAN?
 - I don’t want gateways
 - I don’t want to rely on TTN or any backend
 - I want full control, custom join, and encryption
 - I also want offline operation, even peer-to-peer
   
### So Why Does It Look Similar?
Because those patterns exist for a reason. LoRaWAN’s join procedure, MIC format, AES usage those aren’t random. They’re good practices.
recreating many core ideas, but in my own way, with
- Lighter code
- Fully offline capability
- Flexibility for embedded and constrained devices

---
## License
This library is licensed under the [MIT License](LICENSE).

---
