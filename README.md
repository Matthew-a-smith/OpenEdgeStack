[![Build Status](https://github.com/Matthew-a-smith/Agent/blob/main/proxy.c)](https://github.com/Matthew-a-smith/Agent/blob/main/proxy.c)

An open-source encryption library for [Arduino](https://arduino.cc/) to send and receive data using [LoRa](https://www.lora-alliance.org/) radios.

---

## Compatible Hardware

This project builds on [RadioLib](https://github.com/jgromes/RadioLib) and provides an encryption layer for embedded edge devices.

Supported LoRa boards:
- SX126x series (SX1261, SX1262, SX1268)

---


---

## Usage

This library enables LoRa-style communication **without** requiring a LoRaWAN gateway or centralized network. Any LoRa-compatible device can act as a receiver.

Key Features:
- Devices join via **OTAA** (Over-the-Air Activation)
- All communication is **AES-128 encrypted**
- Supports 8-byte **Device EUI** and 8-byte **HMAC** for message integrity
- Sessions are **stored in RAM** and persist across reboots
- Designed for lightweight operation between multiple edge devices and gateways

---

### Transmission Flow

```
End Device             →          Receiver (Gateway/Server)
───────────────                    ─────────────────────────────
Send JoinReq          ───────────▶  Parse & verify
Receive JoinAccept    ◀───────────  Send encrypted JoinAccept
Derive session keys
```

On end devices, make sure to call sendJoinRequest() before starting lora.startReceive().
This is essential because the join request logic only listens for a response during the timeout window right after sending. If startReceive() is called too early, the join response may not be received correctly, and the device will fail to derive encryption keys.

Do not add the join to the device being used as a gateway and make sure that the gateway is always listening so it can handle join requests and add other devices.

```cpp
//For end devices / transmitters
void setup() {
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

//For gateway / recivers
void setup() {
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

### Packet Format

```
[SenderID (8 bytes)] + [Encrypted Payload] + [HMAC (8 bytes)]
```

### Grouped Packet Storage

Packets can be grouped into bins in memory and sent later as a bulk encrypted blob. This reduces transmission frequency and increases efficiency.

* Groups can be up to **255 bytes**
* Overflow is handled by creating new group files with prefixes

---

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
## Normral messages
you can also send data normally encrypted as well with the following.
and their also a pollsend that delays the packet before sending it.
```cpp
  String payload = "this is a test sentence up to and over 16 bytes in length";
  sendLora((const uint8_t*)groupOne.c_str(), groupOne.length(), TYPE_TEXT);

  // Delays 5 seconds before sending
  pollLora((const uint8_t*)groupOne.c_str(), groupOne.length(), TYPE_TEXT, 5000);

```
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

## Quick Links

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