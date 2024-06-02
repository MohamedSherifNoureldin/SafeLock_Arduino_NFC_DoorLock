#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include "Countimer.h"
#include <SoftwareSerial.h>

#define RST_PIN 49           
#define SS_PIN 53          
#define SERVO_PIN 2

#define IRQ_PIN 3

#define KEY_LEN 4
#define BUZZER_PIN 7

#define MAX_NUM_KEYS 50
#define RED_LED 12
#define GREEN_LED 10


MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.
// SoftwareSerial HM10(19, 18); // RX = 19, TX = 18
HardwareSerial& blehm10(Serial1);

byte card_uid[MAX_NUM_KEYS][KEY_LEN] = {{0x96, 0xC3, 0x1C, 0x82}, {144, 136, 172, 41}}; //Change to match NFC card

MFRC522::MIFARE_Key key;

bool bNewInt = false;
byte regVal = 0x7F;
void activateRec();
void clearInt();

Servo lock;
int numAttempts = 0;
bool nfcPresent = false;

Countimer timer;
Countimer redTimer;
Countimer greenTimer;
Countimer unlockTimer;

byte prevCard[KEY_LEN] = {0x00, 0x00, 0x00, 0x00};
byte nullBuffer[KEY_LEN] = {0x00, 0x00, 0x00, 0x00};

String correctPassword = "medomedo";

// debouncing
bool rfid_tag_present_prev = false;
bool rfid_tag_present = false;
bool tag_found = false;
bool sameCard = false;
int rfid_error_counter = 0;


bool sentMenu = false;
bool loggedIn = false;
bool sentLoginMenu = false;

bool redHigh = false;
bool greenHigh = false;

int NUM_KEYS = 2;

void setup(){
  // set LED_BUILT_IN
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  digitalWrite(GREEN_LED, LOW); // Turn on the LED
  digitalWrite(RED_LED, LOW); // Turn on the LED


  //set alarm and alarm timer
  pinMode(BUZZER_PIN, OUTPUT);
	timer.setCounter(0, 0, 5, timer.COUNT_DOWN, resetAttempts);  // Attach the callback function
  timer.setInterval(print_time, 1000);
  greenTimer.setCounter(0, 0, 3, timer.COUNT_DOWN, doNothing);  // Attach the callback function
  greenTimer.setInterval(alternateGreen, 500);
  redTimer.setCounter(0, 0, 3, timer.COUNT_DOWN, doNothing);  // Attach the callback function
  redTimer.setInterval(alternateRed, 500);
  unlockTimer.setCounter(0, 0, 3, timer.COUNT_DOWN, lockDoor);  // Attach the callback function
  unlockTimer.setInterval(doNothing, 1000);
  //set baud rate
  // HM10.begin(9600);
  blehm10.begin(9600);
  blehm10.write("BLE On....");
  Serial.begin(9600);
  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(IRQ_PIN, INPUT_PULLUP);

  /*
   * Allow the ... irq to be propagated to the IRQ pin
   * For test purposes propagate the IdleIrq and loAlert
   */
  regVal = 0xA0; //rx irq
  mfrc522.PCD_WriteRegister(mfrc522.ComIEnReg, regVal);

  bNewInt = false; //interrupt flag

  /*Activate the interrupt*/
  attachInterrupt(digitalPinToInterrupt(IRQ_PIN), handleInt, LOW);

  // do { //clear a spourious interrupt at start
  //   ;
  // } while (!bNewInt);
  bNewInt = false;

  for (byte i = 0; i < 6; i++) {
        key.keyByte[i] = 0xFF;
  }
  Serial.print(F("Using key (for A and B):"));
  // dump_byte_array(key.keyByte, MFRC522::MF_KEY_SIZE);
  lock.attach(SERVO_PIN);
  lock.write(135);
  delay(400);
  // lock.detach();
}

void loop(){
  timer.run();
  redTimer.run();
  greenTimer.run();
  unlockTimer.run();

  // readCard();
  readBluetooth();
  activateRec();
  delay(200);
}


/*
 * The function sending to the MFRC522 the needed commands to activate the reception
 */
void activateRec() {
  mfrc522.PCD_WriteRegister(mfrc522.FIFODataReg, mfrc522.PICC_CMD_REQA);
  mfrc522.PCD_WriteRegister(mfrc522.CommandReg, mfrc522.PCD_Transceive);
  mfrc522.PCD_WriteRegister(mfrc522.BitFramingReg, 0x87);
}

/*
 * The function to clear the pending interrupt bits after interrupt serving routine
 */
void clearInt() {
  mfrc522.PCD_WriteRegister(mfrc522.ComIrqReg, 0x7F);
}

void handleInt() {
  bNewInt = true;
  readCard();
  Serial.println("WE are in HANDLERRR");
}


void alternateRed() {
  if(redHigh) {
    digitalWrite(RED_LED, LOW); // Turn on the LED
    redHigh = false;
  } else {
    digitalWrite(RED_LED, HIGH); // Turn on the LED
    redHigh = true;
  }
}

void alternateGreen() {
  if(greenHigh) {
    digitalWrite(GREEN_LED, LOW); // Turn on the LED
    greenHigh = false;
  } else {
    digitalWrite(GREEN_LED, HIGH); // Turn on the LED
    greenHigh = true;
  }
}

void doNothing() {
  return;
}


void dump_byte_array(byte *buffer, byte bufferSize) {
    for (byte i = 0; i < bufferSize; i++) {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
    }
}



void dump_byte_array_to_bluetooth(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
      blehm10.print(buffer[i] < 0x10 ? " 0" : "");
      blehm10.print(buffer[i], HEX);
  }
}

bool check_uid(byte *buffer, byte bufferSize){

  int correct = 0;
  for(byte i=0;i<NUM_KEYS;i++){
    Serial.print("Checking against ID ");
    Serial.println(i+1);
    if(equalCard(card_uid[i], buffer, bufferSize)){
      unlockDoor();
      return true;
    }
  }
  incorrectAttempt(false);
  return false;
}

void resetAttempts() {
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RED_LED, LOW); // Turn on the LED

  numAttempts = 0;
}

void print_time()
{
	Serial.print("time: ");
	Serial.println(timer.getCurrentTime());
}

bool equalCard(byte *buffer1, byte *buffer2, byte bufferSize) 
{
  for(int i=0; i<bufferSize; i++) {
    if(buffer1[i] != buffer2[i]) 
      return false;
  }
  return true;
}


void updatePreviousCard(byte *buffer1, byte bufferSize) 
{
  for(int i=0; i<KEY_LEN; i++) {
    prevCard[i] = buffer1[i];
  }
}

void readCard()
{
  if(!bNewInt) {
    return;
  }

  mfrc522.PICC_ReadCardSerial();

  Serial.print(F("Card UID:"));
  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  if(loggedIn && blehm10.availableForWrite())
  {
    char buffer[100] = {0};
    blehm10.write("CARD DETECTED: ");
    dump_byte_array_to_bluetooth(mfrc522.uid.uidByte, mfrc522.uid.size);
    blehm10.write("\r\n");
  }
  Serial.println();
  Serial.print(F("PICC type: "));
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  Serial.println(mfrc522.PICC_GetTypeName(piccType));
  check_uid(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.println("\n");

  clearInt();
  mfrc522.PICC_HaltA();
  bNewInt = false;
}


// void readCard()
// {
//   if(!bNewInt) {
//     return;
//   }
//   rfid_error_counter += 1;
//   if(rfid_error_counter > 2){
//     updatePreviousCard(nullBuffer, 4);
//   }
//   // if ( ! mfrc522.PICC_IsNewCardPresent() || ! mfrc522.PICC_ReadCardSerial()) {
//   //   return;
//   // }
//   mfrc522.PICC_ReadCardSerial();

//   rfid_error_counter = 0;

//   sameCard = equalCard(prevCard, mfrc522.uid.uidByte, mfrc522.uid.size);
//   updatePreviousCard(mfrc522.uid.uidByte, 4);
//   if (!sameCard) {
//     Serial.print(F("Card UID:"));
//     dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
//     if(loggedIn && blehm10.availableForWrite())
//     {
//       char buffer[100] = {0};
//       blehm10.write("CARD DETECTED: ");
//       dump_byte_array_to_bluetooth(mfrc522.uid.uidByte, mfrc522.uid.size);
//       blehm10.write("\r\n");
//     }
//     Serial.println();
//     Serial.print(F("PICC type: "));
//     MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
//     Serial.println(mfrc522.PICC_GetTypeName(piccType));
//     check_uid(mfrc522.uid.uidByte, mfrc522.uid.size);
//     Serial.println("\n");
//   }

//   clearInt();
//   mfrc522.PICC_HaltA();
//   bNewInt = false;
// }

void lockDoor() 
{
  Serial.println("Locked Door!");
  lock.write(135);
}

void unlockDoor() 
{
  Serial.println("ACCESS GRANTED");
  lock.attach(SERVO_PIN);
  greenTimer.start();
  unlockTimer.start();
  lock.write(180);
  // for (int i = 0; i < 3; i++) {
  //   digitalWrite(GREEN_LED, HIGH); // Turn on the LED
  //   delay(500); // Wait for 500 milliseconds (half a second)
  //   digitalWrite(GREEN_LED, LOW); // Turn off the LED
  //   delay(500); // Wait for another 500 milliseconds
  // }
  numAttempts = 0;
}

void incorrectAttempt(bool bluetooth)
{
  if(numAttempts >= 3) {
    timer.start();
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("Number of attempts exceeds limits");
    if(blehm10.availableForWrite())
    {
      blehm10.write("NUMBER OF ATTEMPTS EXCEEDS LIMITS\r\n");
    }

    digitalWrite(RED_LED, HIGH); // Turn on the LED

    // numAttempts = 0;
    return false;
  }

  numAttempts++;
  if(bluetooth) {
    // while(!blehm10.availableForWrite())
    // {}
    if(blehm10.availableForWrite()) {
      blehm10.write("INCORRECT PASSWORD PROVIDED OVER BLUETOOTH");
    }
    Serial.println("Incorrect password provided over bluetooth");
  } else {
    Serial.println("Incorrect card provided over NFC");
  }
  Serial.print("Number of incorrect attempts: ");
  Serial.println(numAttempts);
  redTimer.start();
  // for (int i = 0; i < 3; i++) {
  //   digitalWrite(RED_LED, HIGH); // Turn on the LED
  //   delay(500); // Wait for 500 milliseconds (half a second)
  //   digitalWrite(RED_LED, LOW); // Turn off the LED
  //   delay(500); // Wait for another 500 milliseconds
  // }

}

void validatePassword(String password)
{
  if(correctPassword == password) 
  {
    loggedIn = true;
  } else {
    incorrectAttempt(true);
  }
}

void parseKey(String key, byte (&byteArray)[KEY_LEN])
{
  int smallIndex = 0;
  for(int i=0; i<key.length(); i += 2) 
  {
    byteArray[smallIndex++] = (byte)strtol(key.substring(i, i + 2).c_str(), nullptr, 16);
  }
}

void addCard(String key)
{
  if(key.length() != KEY_LEN*2)
  {
    blehm10.write("INVALID KEY SIZE\r\n");
  } else {
    parseKey(key, card_uid[NUM_KEYS]);
    // card_uid[NUM_KEYS] = parseKey(key);
    // card_uid[NUM_KEYS++] = newKey;
    dump_byte_array(card_uid[NUM_KEYS], KEY_LEN);
    NUM_KEYS++;
    blehm10.write("KEY ADDED SUCCESSFULLY\r\n");
  }
}

void removeCard(String key){
  if(key.length() != KEY_LEN*2)
  {
    blehm10.write("INVALID KEY SIZE\r\n");
  }
  else 
  {
    bool foundKey = false;
    int foundIndex = 0;
    byte parsedKey[KEY_LEN];
    parseKey(key, parsedKey);
    // loop over array of keys to search for key
    for(int i=0; i<NUM_KEYS; i++) {
      if(equalCard(card_uid[i], parsedKey, KEY_LEN))
      {
        foundKey = true;
        foundIndex = i;
        break;
      }
    }
    // check if card was found
    if(foundKey) {
      for(int i=foundIndex; i<NUM_KEYS-1; i++) {
        memmove(&card_uid[i], &card_uid[i+1], KEY_LEN); 
        // card_uid[i] = card_uid[i+1];
      }
      NUM_KEYS--;
      blehm10.write("KEY REMOVED SUCCESSFULLY\r\n");
    } else {
      blehm10.write("KEY PROVIDED DOESN'T EXIST IN LIST OF VERIFIED KEYS\r\n");
    }
  }
}

void parseAdminCommand(String command)
{
  String addPrefix = "ADD";
  String remPrefix = "REM";
  String unlockPrefix = "UNLOCK";
  String key = "";
  if (command.substring(0, addPrefix.length()) == addPrefix) {
    // Find the position of the first space after "PASS"
    size_t spacePos = command.indexOf(' ', addPrefix.length());
    if (spacePos != -1) {
        // If space found, extract password excluding space
        key = command.substring(addPrefix.length() + 1, spacePos - addPrefix.length() - 1);
    } else {
        // If no space found, extract password till end of string
        key = command.substring(addPrefix.length() + 1);
    }
    addCard(key);
  } else if (command.substring(0, remPrefix.length()) == remPrefix) {
    // Find the position of the first space after "PASS"
    size_t spacePos = command.indexOf(' ', remPrefix.length());
    if (spacePos != -1) {
        // If space found, extract password excluding space
        key = command.substring(remPrefix.length() + 1, spacePos - remPrefix.length() - 1);
    } else {
        // If no space found, extract password till end of string
        key = command.substring(remPrefix.length() + 1);
    }
    Serial.print("KEY ");
    Serial.println(key);
    // remove card
    removeCard(key);
  } else if (command.substring(0, unlockPrefix.length()) == unlockPrefix) {
    unlockDoor();
  } else {
    blehm10.write("INVALID COMMAND PROVIDED\r\n");
  }
}


void readBluetooth() 
{
  if(!loggedIn && !sentLoginMenu) 
  {
    if(blehm10.availableForWrite())
    {
      blehm10.write("Welcome to Smart Door Lock\r\n");
      blehm10.write("To begin please authenticate first through sending 'PASS <youpassword>'\r\n");
      sentLoginMenu = true;
    }
  } else if(loggedIn && !sentMenu) {
    if(blehm10.availableForWrite())
    {
      blehm10.write("Welcome Admin\r\n");
      blehm10.write("You will get logs for all login attempts here\r\n");
      blehm10.write("Additionally, You have the following commands:\r\n");
      blehm10.write("- UNLOCK -> to unlock door\r\n");
      blehm10.write("- ADD <cardUID without any spaces> -> to add a new card\r\n");
      blehm10.write("- REM <cardUID without any spaces> -> to revoke access of one of the added cards\r\n");
      sentMenu = true;
    }
  }
  String blehm10Str = "";
  // check if new connection or losing connection
  if (blehm10.available())
  {
    //Read a complete line from bluetooth terminal
    blehm10Str = blehm10.readStringUntil('\n');
    Serial.print("BT Raw Data: ");
    Serial.println(blehm10Str);
  } else {
    return;
  }
  // check if new connection or losing connection
  if(blehm10Str.indexOf("OK+CONN") != -1 || blehm10Str.indexOf("OK+LOST") != -1)
  {
    loggedIn = false;
    sentLoginMenu = false;
    sentMenu = false;
  }

  if(!loggedIn)
  {
    String prefix = "PASS";
    String password = "";
    if (blehm10Str.substring(0, prefix.length()) == prefix) {
        // Find the position of the first space after "PASS"
        size_t spacePos = blehm10Str.indexOf(' ', prefix.length());
        if (spacePos != -1) {
            // If space found, extract password excluding space
            password = blehm10Str.substring(prefix.length() + 1, spacePos - prefix.length() - 1);
        } else {
            // If no space found, extract password till end of string
            password = blehm10Str.substring(prefix.length() + 1);
        }
        Serial.print("Password ");
        Serial.println(password);
        validatePassword(password);
    } else {
      if(sentLoginMenu) {
        blehm10.write("INVALID COMMAND PROVIDED\r\n");
      }
    }
  } else {
    parseAdminCommand(blehm10Str);
  }

}

