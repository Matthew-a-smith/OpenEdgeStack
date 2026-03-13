/*
  OpenEdgeStack SX126x – Polymorphic Stream Transmit Example

  This example demonstrates how to send **large encrypted LoRa messages**
  using the SX126x module family when a button is pressed.

  Using the new **PolymorphicLoraSender** class, messages exceeding the
  typical LoRa payload limit (>200 bytes) are automatically split into
  chunks and sent sequentially as a stream. The receiver can reassemble
  the full message using the STREAM_END identifier.

  Features:
  - Data is encrypted using the AppSKey before transmission.
  - Messages larger than 200 bytes are split into 200-byte chunks.
  - Each chunk is encrypted and sent using the existing session system.
  - A final chunk includes a STREAM_END marker to indicate end-of-stream.
  - Polymorphism ensures a single runtime instance can send the entire stream.
  - Designed for continuous transmission without requiring ACK handling.

  Packet Format:
    [SenderID (8 bytes)] + [Nonce (16 bytes)] + [Encrypted Payload] + [HMAC (8 bytes)]

  Requirements:
  - RadioLib library.
  - LoRa module: SX126x (tested with Heltec V3).

  Optional:
  - SPIFFS mounted for session persistence.

  Notes:
  - Uploading without valid keys will result in a compile error.
  - Each device must be provisioned with unique cryptographic keys.
  - Keys can be generated using:
      1) Provided Python script `generate_keys.py` in 'extras' folder.
         Produces C-style arrays ready to copy into this sketch.
      2) The Things Network (TTN) – generate device credentials manually.

  Usage:
  - Press the configured button to send the large payload.
  - The entire payload is transmitted as a continuous stream to the receiver.
  - No ACK handling; ensure the receiver can process full streams.
*/

#include <OpenEdgeStack.h>

// SENDER
#include <RadioLib.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>
#include <map>

// LoRa SX1262 pins for Heltec V3
#define LORA_CS     8
#define LORA_RST    12
#define LORA_BUSY   13
#define LORA_DIO1   14

#define BUTTON_PIN 0  // Change as needed
#define LED_PIN    6 // Change as needed


Module module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY); // Pin configuration
SX1262 radioModule(&module); // Create SX1262 instance

PhysicalLayer* lora = &radioModule; // Set global radio pointer

float frequency_plan = 915.0; // Frequency (in MHz)

/*
  -------------------------------------------------------------------
  IMPORTANT: Uploading a sketch without valid keys will result in a compile error.
  
  The key arrays below have been intentionally commented out to prevent the
  use of default or weak keys. This measure ensures that users must provide
  unique and secure keys before compiling.

  Each device must be provisioned with its own cryptographic keys to
  securely communicate over LoRa.

  You have two options for generating these keys:

  1) Use the provided Python script `generate_keys.py` located in the 'extras' folder.
     This script outputs keys as C-style arrays ready to be copied here.
     Rember the app and hmacKey get shared between devices.
     Use gatewayEUI in the python script as the secnd devEUI or vice versa.

  2) Use The Things Network (TTN) to generate compatible device credentials,
     then manually paste those values into the arrays below.
  -------------------------------------------------------------------
*/

// ───── Runtime Globals ────────────────────────────────

uint8_t devEUI[8] = {
  0xE5, 0x27, 0x4D, 0x58, 0x8F, 0x23, 0xD5, 0x52
}; // devEUI (64-bit)

uint8_t appEUI[8] = {
  0x3A, 0x4B, 0x97, 0xFC, 0xFF, 0x19, 0x80, 0xD0
}; // appEUI (64-bit)

uint8_t appKey[16] = {
  0xB6, 0x89, 0x9F, 0xB5, 0x4F, 0x99, 0x8F, 0x5E,
  0x8B, 0xF5, 0x3B, 0xB2, 0xDC, 0x48, 0x2E, 0xB8
}; // appKey (128-bit)

const uint8_t hmacKey[16] = {
  0x06, 0x83, 0x24, 0x3D, 0xD8, 0xEB, 0x4E, 0x5C,
  0x4B, 0xB7, 0x8A, 0x03, 0xE2, 0xD0, 0x7E, 0x86
}; // hmacKey (128-bit)



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

// Flags to track whether each group has already been transmitted.
bool firstPressDone = false;

bool awaitingAckForGroup1 = false;

void setup() {
 // Mount SPIFFS to persist sessions across reboots
 if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS Mount Failed");
    while (true);  // prevent further operation
  }

  // Start preferences for sessions  
  preferences.begin("lora", false);
  flushAllSessions();
  // set pins for buttons to prevent sending at start
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  lastButtonState = digitalRead(BUTTON_PIN);

  Serial.begin(115200);
  delay(100);

  // Register the chosen radio module globally
  setRadioModule(&radioModule);  
  delay(1000);

  // Initialize the radio module
  Serial.println("[INFO] LoRa Init...");
  int state = radioModule.begin(frequency_plan);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("Radio init failed: %d\n", state);
    while (true);
  }
  // Set flags  
  radioModule.setDio1Action(setFlags);
  radioModule.setPacketReceivedAction(setFlags);
  
  // begin listening
  state = radioModule.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa] startReceive failed: %d\n", state);
    while (true);
  }
  Serial.println("[Setup] Setup complete.");

  // IMPORTANT: Send join request AfTER enabling receive mode
  int maxRetries = 3; //Number of retries
  int retryDelay = 3000; //Timeout per attempt in milliseconds
  sendJoinRequest(maxRetries, retryDelay);  // Wait for session handshake
}

void loop() {
    // You can still call this if you want to process non-blocking events,
    // otherwise you can remove it entirely since sending is the main goal.
    listenForIncoming();  

    int currentButtonState = digitalRead(BUTTON_PIN);
    if (currentButtonState == HIGH && lastButtonState == LOW) {
        Serial.println("<--------------------------------->");
        Serial.println("[BUTTON] Press detected. Sending...");

        // Large test string > 255 bytes
        String groupOne = "";
        
            groupOne += 
            "There was nothing so VERY remarkable in that; nor did Alice "
            "think it so VERY much out of the way to hear the Rabbit say to "
            "itself, `Oh dear!  Oh dear!  I shall be late!'  (when she thought "
            "it over afterwards, it occurred to her that she ought to have "
            "wondered at this, but at the time it all seemed quite natural); "
            "but when the Rabbit actually TOOK A WATCH OUT OF ITS WAISTCOAT- "
            "POCKET, and looked at it, and then hurried on, Alice started to "
            "her feet, for it flashed across her mind that she had never "
            "before seen a rabbit with either a waistcoat-pocket, or a watch to "
            "take out of it, and burning with curiosity, she ran across the "
            "field after it, and fortunately was just in time to see it pop "
            "down a large rabbit-hole under the hedge. "
            "In another moment down went Alice after it, never once "
            "considering how in the world she was to get out again. "
            "The rabbit-hole went straight on like a tunnel for some way, "
            "and then dipped suddenly down, so suddenly that Alice had not a "
            "moment to think about stopping herself before she found herself "
            "falling down a very deep well. "
            "Either the well was very deep, or she fell very slowly, for she "
            "had plenty of time as she went down to look about her and to "
            "wonder what was going to happen next. First, she tried to look "
            "down and make out what she was coming to, but it was too dark "
            "to see anything; then she looked at the sides of the well, and "
            "noticed that they were filled with cupboards and bookshelves; "
            "here and there she saw maps and pictures hung upon pegs. She "
            "took down a jar from one of the shelves as she passed; it was "
            "labelled `ORANGE MARMALADE', but to her great disappointment it "
            "was empty: she did not like to drop the jar for fear of killing "
            "somebody, so managed to put it into one of the cupboards as she "
            "fell past it ";
        
        size_t groupOneLen = groupOne.length();
        Serial.printf("[INFO] groupOne length: %zu bytes\n", groupOneLen);

        // Convert to bytes
        const uint8_t* groupOneData = (const uint8_t*)groupOne.c_str();

        // --- Use the polymorphic sender ---
        PolymorphicLoraSender sender;
        sender.sendStream(groupOneData, groupOneLen);  // splits automatically into 255-byte chunks

        Serial.println("[TX] groupOne stream sent!");
    }

    globalReply = "";  // Clear reply after handling
    lastButtonState = currentButtonState;
    delay(50);  // Debounce
}