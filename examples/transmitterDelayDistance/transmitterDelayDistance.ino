/*
  OpenEdgeStack SX126x - Transmit with Groups Example

  This example demonstrates how to transmit encrypted packets using the SX126x
  LoRa module family and OpenEdgeStack.

  Functionality:
  - Uses an ultrasonic distance sensor to monitor distance in cm.
  - When the measured distance exceeds 20 cm, the value is read, encrypted,
    and transmitted over LoRa.
  - Utilizes `pollLora()` with a 3-second timeout for sending.
  
  Notes:
  - Data is encrypted using AppSKey before transmission.
  - Best used with reciverSimple or remove the sendACK call from reciverFull for the best results.
  - Compatible with all SX126x family LoRa modules.
*/

#include <OpenEdgeStack.h>

#include <RadioLib.h>
#include <FS.h>
#include <SPIFFS.h>

// Lora SX1262 pins for Heltec V3
#define LORA_CS     8
#define LORA_RST    12
#define LORA_BUSY   13
#define LORA_DIO1   14

// Ultrasonic sensor pins
#define TRIG_PIN 7
#define ECHO_PIN 6

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

// uint8_t devEUI[8] = {
//   /* your devEUI */
// };  // Device EUI (64-bit)

// uint8_t appEUI[8] = {
//   /* your AppEUI */
// }; // Application EUI (64-bit)

// uint8_t appKey[16] = {
//   /* your appKEY */  
// }; // AppKey (AES-128)

// const uint8_t hmacKey[16] = {
//    /* yourHMAC key */
// }; // Shared HMAC key (16 bytes)


// ───── Interrupt ──────────────────────────────────────
// Flags used by interrupt handlers and other logic to track received messages
volatile bool receivedFlag = false;
volatile bool transmissonFlag = false;


// Tracks acknowledgment (ACK) responses between the end device and gateway.
// Declared globally to be accessible across all functions.
String globalReply = "";

void setFlags() {
  if (!transmissonFlag) {
    receivedFlag = true;
  }
}

void setup() {
 
  // Initialize entropy for nonces, random devNonce etc.
  randomSeed(analogRead(0));

  // Power external devices (e.g. sensors, radio modules)
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);  // Enable Vext
  delay(100);

  // Setup ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Mount SPIFFS to persist sessions across reboots
 if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS Mount Failed");
    while (true);  // prevent further operation
  }

  // Start preferences for sessions  
  preferences.begin("lora", false);

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

// measure distance
float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Wait for echo response
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  
  // Slow down distance checks directly here
  delay(1000);  // <-- 1 second delay between each measurement

  return duration * 0.034 / 2.0;
}

void loop() {
  // Listen for incoming LoRa messages (flags will be set on RX)
  listenForIncoming();

  // Read ultrasonic distance
  float distance = measureDistance();

  // Trigger if object is farther than 20cm
  if (distance > 20.0) {
    Serial.println("\n--- Distance Triggered Send ---");
    Serial.println("[INFO] Distance: " + String(distance, 1) + " cm");

    // Pack float into byte array (for encrypted sending)
    uint8_t distanceBytes[4];
    memcpy(distanceBytes, &distance, sizeof(float));

    // Send via LoRa using type `TYPE_FLOATS` and a 3-second timeout
    pollLora(distanceBytes, sizeof(distanceBytes), TYPE_FLOATS, 3000);
  } else {
    // Too close — no transmission
    Serial.println("Dist: " + String(distance, 1) + " cm");
    Serial.println("Too close (<20cm)");
  }

  // Wait before next loop (reduce spam)
  delay(100);
}




