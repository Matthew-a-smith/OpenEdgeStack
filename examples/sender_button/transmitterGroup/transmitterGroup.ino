/*
  OpenEdgeStack SX126x Transmit with Groups Example

  This example demonstrates how to transmit structured groups of data 
  over raw LoRa using the SX126x module family.

  Each group is stored on the device as an encrypted binary blob, referred to as a "group".
  The example sets up group configuration parameters, generates sample data, 
  and transmits it over LoRa using a secure and lightweight custom format.

  Notes:
  - Data is encrypted using appSKey before transmission.
  - The transmitted packet format is:
      [SenderID (8 bytes)] + [Encrypted Group Data] + [HMAC (8 bytes)]

  - Other SX126x family modules are supported.

  Ideal for demonstrating secure, chunked transmissions with minimal LoRaWAN overhead.
*/

#include <EndDevice.h>
#include <LoraWANLite.h>
#include <Sessions.h>

// SENDER
#include <RadioLib.h>
#include <Wire.h>

#include <FS.h>
#include <SPIFFS.h>

// LoRa SX1262 pins for Heltec V3
#define LORA_CS     8
#define LORA_RST    12
#define LORA_BUSY   13
#define LORA_DIO1   14

#define BUTTON_PIN 7  // Change as needed
#define LED_PIN    6 // Change as needed



float frequency_plan = 915.0;
SX1262 lora = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

uint8_t devEUI[8] = {
  0x4F, 0x65, 0x75, 0xC5, 0xF0, 0x31, 0x00, 0x00
};  // Device EUI (64-bit)

uint8_t appEUI[8] = {
  0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80
}; // Application EUI (64-bit)

uint8_t appKey[16] = {
  0x2A, 0xC3, 0x76, 0x13, 0xE4, 0x44, 0x26, 0x50,
  0x2B, 0x8D, 0x7E, 0xEE, 0xAB, 0xA9, 0x57, 0xCD
}; // App root key (AES-128)

const uint8_t hmacKey[16] = {
  0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44,
  0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xDE, 0xAD
}; // Shared 16-byte static HMAC key

/*
group settings  can be change depending on size requirements
maxFileSize applies only to the raw stored group files on the sender  
not to the final encrypted and transmitted LoRa packets, which will grow due to 
padding, metadata, and HMAC.

 PacketGroup:
   - maxFileSize       → Max bytes allowed per group file
   - groupLimit           → Max total group files per prefix
   - groupPrefixLimit    → Max unique group name prefixes allowed (Grp1, Grp1.1...)
*/
GroupConfig groupConfig = {
    .maxFileSize = 80,
    .groupLimit = 4,
    .groupPrefixLimit = 2
};

// -------------------- State Flags -----------------------

// Tracks acknowledgment (ACK) responses between the end device and gateway.
// Declared globally to be accessible across all functions.
String globalReply = "";

// Flags used by interrupt handlers and other logic to track received messages
// and outgoing transmissions. Required when using the receiver with an end device.
volatile bool receivedFlag = false;
volatile bool transmissonFlag = false;

void setFlags() {
  if (!transmissonFlag) {
    receivedFlag = true;
  }
}
// ------------------- Application State ------------------
// Tracks the current and previous button states for edge detection.
int buttonState = 0;
int lastButtonState;


const char* Group1 = "Grp1";
const char* Group2 = "Grp2";
const char* Group3 = "Grp3";
const char* Group4 = "Grp4";

// Counter to prevent repeated data collection inside of loop.
int collectedCount = 0;

// Flags to track whether each group has already been transmitted.
bool firstPressDone = false;
bool secondPressDone = false;
bool thirdPressDone = false;
bool fourthPressDone = false;

bool awaitingAckForGroup1 = false;
bool awaitingAckForGroup2 = false;
bool awaitingAckForGroup3 = false;
bool awaitingAckForGroup4 = false;



//void flushSession() {
//  // Manually brute-force delete /GrpX_Y.bin files
//  const char* groupPrefixes[] = { "Grp1", "Grp2", "Grp3", "Grp4" };
//  const int maxSuffix = 500;  // Arbitrary upper limit
//
//  for (int i = 0; i < 4; i++) {
//    const char* prefix = groupPrefixes[i];
//    for (int suffix = 0; suffix < maxSuffix; suffix++) {
//      char filename[32];
//      snprintf(filename, sizeof(filename), "/%s_%d.bin", prefix, suffix);
//
//      if (SPIFFS.exists(filename)) {
//        if (SPIFFS.remove(filename)) {
//          Serial.printf("[CLEANUP] Removed %s\n", filename);
//        } else {
//          Serial.printf("[ERROR] Failed to remove %s\n", filename);
//        }
//      } else {
//        // Once we miss one in a group, assume the rest are gone too
//        break;
//      }
//    }
//  }
//}


void setup() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS Mount Failed");
    while (true);
  }

  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  lastButtonState = digitalRead(BUTTON_PIN);

  Serial.println("[INFO] LoRa Init...");
//  flushSession();
  sessionMap.clear();
  Serial.println("[DEBUG] Cleared RAM session map.");
  preferences.begin("lora", false);  // open for lifetime
  preferences.clear();
   preferences.end();
   Serial.println("[NVS] All sessions cleared from NVS.");

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


// Deletes stored binary files associated with a specific group after
// successful transmission and acknowledgment.

void handleFile(const char* pathBase) {
  for (int suffix = 0; suffix < 9; suffix++) {
    char path1[32];
    snprintf(path1, sizeof(path1), "/%s_%d.bin", pathBase, suffix);

    if (!SPIFFS.exists(path1)) continue;

    SPIFFS.remove(path1);
    Serial.printf("[OK] Deleted: %s\n", path1);

    char path2[32];
    snprintf(path2, sizeof(path2), "/%s_%d.bin", pathBase, suffix + 1);
    if (SPIFFS.exists(path2)) {
      SPIFFS.remove(path2);
      Serial.printf("[OK] Deleted: %s\n", path2);
    }

    break;  // Only clean up first pair
  }
}

void loop() {

  listenForIncoming();  // Process incoming packets (non-blocking)

 // ---------- Data Collection (runs only once) ----------
/*
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
*/


  if (collectedCount < 1) {
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
    String groupOne = "this is a test sentence up to and over 16 bytes in length";
    storePacket((const uint8_t*)groupOne.c_str(), groupOne.length(), TYPE_TEXT, Group1);

     /*
     Group 2
     Stores 5 single bytes to /Grp2_0.bin
     1 byte type + 5 raw bytes = 6 bytes payload size
     6 bytes + 2 byte length = 8 byte file size
     8 bytes → padded to 16 bytes 

       - 8 bytes sender ID
       - 16 bytes encrypted payload
       - 8 bytes HMAC
     8 + 16 + * = 32 bytes total
    */
    uint8_t groupTwo[] = { 0x10, 0xF0, 0xAF, 0x08, 0xAE };
    storePacket(groupTwo, sizeof(groupTwo), TYPE_BYTES, Group2);

        /*
     Group 3
     Stores 23 individual words into /Grp3_X.bin files.
     Each word is stored as a separate entry, where each entry adds:
       - 2 bytes for the length header (uint16_t)
       - 1 byte for the type
       - N bytes for the word

     So, each word takes up (2 + 1 + N) bytes in the raw file.

     Group file size limit is 80 bytes, so once the limit is exceeded,
     storage rolls over to a second file.

     ▸ Breakdown:
     Grp3_0.bin
     - Total file size before encryption: 80 bytes
     - After extracting only the useful parts (type + data), size becomes 56 bytes
     - AES padding rounds this up to 64 bytes
     - Final encrypted packet:
         - 8 bytes Sender ID
         - 64 bytes Encrypted Payload
         - 8 bytes HMAC
         = 80 bytes total

     Grp3_1.bin
     - File size before encryption: 75 bytes
     - Useful extracted payload: 55 bytes
     - After AES padding: 64 bytes
     - Final encrypted packet:
         - 8 bytes Sender ID
         - 64 bytes Encrypted Payload
         - 8 bytes HMAC
         = 80 bytes total

     Note:
     - The last word "together" is not stored, as it would overflow the 80-byte group file limit.
     - Parsed payload includes only:
         [1 byte type][data bytes] per entry
         → So actual payload sent is smaller than file size.
    */

    const char* groupThreeWords[] = {
      "this","test","sentence","is","up","to","and","over","80","bytes",
      "in","length","it","should","be","broken","into","two","different","groups","and",
      "sent", "togehter"
    };
    for (int i = 0; i < 23; i++) {
      storePacket((const uint8_t*)groupThreeWords[i], strlen(groupThreeWords[i]), TYPE_TEXT, Group3);
    }

     /*
     Group 4
     Stores 9 individual 4 byte floats into /Grp4_X.bin files.
     
     1 byte type + 4 byte float = 5 bytes payload size
     5 byte length + 2 byte length = 7 byte entry
     7 bytes * 9 bytes entrys = 63 byte file size

     5 payload bytes * 9 entries = 45 bytes raw payload size before encyption

     45 bytes → padded to 48 bytes 

       - 8 bytes sender ID
       - 48 bytes encrypted payload
       - 8 bytes HMAC
     8 + 48 + 8 = 64 bytes total

     */
    float groupFour[] = {
  33.4f, 123334.5454f, 434.4343f, 23.2f, 17.5f,
  13.2f, 23.2f, 909.8f, 12.2f
};

uint8_t floatBytes[sizeof(groupFour)];
memcpy(floatBytes, groupFour, sizeof(groupFour));
storePacket(floatBytes, sizeof(groupFour), TYPE_FLOATS, Group4);

    collectedCount++;
    delay(500);
  }

  // ---------- Button Press Handling ----------
  /*
 Sends each group one at a time each time button is pressed. 
  */
  int currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    Serial.println("[BUTTON] Press detected. Sending...");

    if (!firstPressDone) {
      sendStoredGroupFile(Group1);
      awaitingAckForGroup1 = true;
      firstPressDone = true;
    } else if (!secondPressDone) {
      sendStoredGroupFile(Group2);
      awaitingAckForGroup2 = true;
      secondPressDone = true;
    } else if (!thirdPressDone) {
      sendStoredGroupFile(Group3);
      awaitingAckForGroup3 = true;
      thirdPressDone = true;
    } else if (!fourthPressDone) {
      sendStoredGroupFile(Group4);
      awaitingAckForGroup4 = true;
      fourthPressDone = true;
    } else {
      Serial.println("[INFO] All groups have already been sent.");
    }

    Serial.println("[TX] Data sent!");
  }

  // ---------- Handle ACK ----------

  /*
  Check for ack back from the reciver when recvied indicates a succesful session ack checks are also 
  encpyted this is not neccary just a nice way to check and clean up unused file space.
  */
  if (globalReply == "ACK:") {
    Serial.print("[REPLY] ");
    Serial.println(globalReply);

    if (awaitingAckForGroup1) {
      handleFile(Group1);
      awaitingAckForGroup1 = false;
    } else if (awaitingAckForGroup2) {
      handleFile(Group2);
      awaitingAckForGroup2 = false;
    } else if (awaitingAckForGroup3) {
      handleFile(Group3);
      awaitingAckForGroup3 = false;
    } else if (awaitingAckForGroup4) {
      handleFile(Group4);
      awaitingAckForGroup4 = false;
    }

    globalReply = "";  // Clear reply after handling
  }

  lastButtonState = currentButtonState;
  delay(50);  // Debounce
}