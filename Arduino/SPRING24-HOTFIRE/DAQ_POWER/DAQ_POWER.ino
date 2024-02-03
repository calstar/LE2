/*
11-7-2023


This code runs on the DAQ ESP32 and has a couple of main tasks.
1. Read sensor data
2. Send sensor data to COM ESP32
3. Actuate hotfire sequence
*/

//::::::Libraries::::::://
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include "HX711.h"
#include "Adafruit_MAX31855.h"
#include <EasyPCF8575.h>
#include "RunningMedian.h"
#include "PCF8575.h"  // https://github.com/xreef/PCF8575_library
// Set i2c address
PCF8575 pcf8575(0x20);

//::::::Global Variables::::::://

// DEBUG TRIGGER: SET TO 1 FOR DEBUG MODE.
// MOSFET must not trigger while in debug.
int DEBUG = 1;      // Simulate LOX and Eth fill.
int WIFIDEBUG = 1;  // Don't send/receive data.

// MODEL DEFINED PARAMETERS FOR TEST/HOTFIRE. Pressures in psi //
float pressureFuel = 390;   //405;  // Set pressure for fuel: 412
float pressureOx = 450;     //460;  // Set pressure for lox: 445
float threshold = 0.995;   // re-psressurrization threshold (/1x)
float ventTo = 5;          // c2se solenoids at this pressure to preserve lifetime.
#define abortPressure 525  // Cutoff pressure to automatically trigger abort
#define period 0.5         // Sets period for bang-bang control
float sendDelay = 250;     // Sets frequency of data collection. 1/(sendDelay*10^-3) is frequency in Hz
// END OF USER DEFINED PARAMETERS //
// refer to https://docs.google.com/spreadsheets/d/17NrJWC0AR4Gjejme-EYuIJ5uvEJ98FuyQfYVWI3Qlio/edit#gid=1185803967 for all pinouts


//::::::DEFINE INSTRUMENT PINOUTS::::::://
#define TimeOut 100

// GPIO expander
#define I2C_SDA 21

#define I2C_SCL 22

////////////////////////////// MOSFETS ///////////////////////////////////////////////////////////////////
#define MOSFET_ETH_MAIN 7    //P07
#define MOSFET_ETH_PRESS 6   //P06
#define MOSFET_VENT_ETH 5    //P05
#define MOSFET_EXTRA 4       //CAN USE THIS PIN FOR ANYTHING JUST CHANGE ASSIGNMENT AND HARNESS
#define MOSFET_QD_LOX 3      //P03
#define MOSFET_IGNITER 8     //P10
#define MOSFET_LOX_MAIN 9    //P11
#define MOSFET_LOX_PRESS 10  //P12
#define MOSFET_VENT_LOX 11   //P13
#define MOSFET_QD_ETH 12     //P14


// Initialize mosfets' io expander.
//#define MOSFET_PCF_ADDR 0x20
//EasyPCF8575 mosfet_pcf;
bool mosfet_pcf_found;

//::::::STATE VARIABLES::::::://
enum STATES { IDLE,
              ARMED,
              PRESS,
              QD,
              IGNITION,
              HOTFIRE,
              ABORT };
String state_names[] = { "Idle", "Armed", "Press", "QD", "Ignition", "HOTFIRE", "Abort" };
int COMState;
int DAQState;
bool ethComplete = false;
bool oxComplete = false;
bool oxVentComplete = false;
bool ethVentComplete = false;
int hotfireStart;

// Delay between loops.
#define IDLE_DELAY 250
#define GEN_DELAY 20


//::::DEFINE READOUT VARIABLES:::://
String serialMessage;
float sendTime;
short int queueLength = 0;

// Define variables to store readings to be sent
struct struct_data_board {
public:
float filteredReading = 4;
float rawReading = 4;
}


struct_data_board PT_O1{{}};
struct_data_board PT_O2{{}};
struct_data_board_E1{{}};
struct_data_board PT_E2{{}};
struct_data_board PT_C1{{}};
struct_data_board LC_1{{}};
struct_data_board LC_2{{}};
struct_data_board LC_3{{}};
struct_data_board TC_1{{}};
struct_data_board TC_2{{}};
struct_data_board TC_3{{}};
struct_data_board TC_4{{}};


// Structure example to send data.
// Must match the receiver structure.
typedef struct struct_message {
  int id;
  int messageTime;
  float PT_O1;
  float PT_O2;
  float PT_E1;
  float PT_E2;
  float PT_C1;
  float LC_1;
  float LC_2;
  float LC_3;
  float TC_1;
  float TC_2;
  float TC_3;
  float TC_4;
  int COMState;
  int DAQState; 
  short int queueLength;
  bool ethComplete;
  bool oxComplete;
  // bool oxvent;
  // bool ethVent;
  // bool VentComplete;
} struct_message;

struct_message myData;
//struct_message for incoming SENSE Board Readings
struct_message SENSE;
// Create a struct_message to hold incoming commands
struct_message Commands;
// Create a struct_message called Packet to be sent.
struct_message Packet;
// Create a queue for Packet in case Packets are dropped.
struct_message PacketQueue[120];

struct_message boardsStruct[2] = {SENSE, Commands};
float Board_ID = 2; //POWER DAQ Board ID

//::::::Broadcast Variables::::::://
esp_now_peer_info_t peerInfo;
// REPLACE WITH THE MAC Address of your receiver

// OLD COM BOARD {0xC4, 0xDD, 0x57, 0x9E, 0x91, 0x6C}
// COM BOARD {0x7C, 0x9E, 0xBD, 0xD7, 0x2B, 0xE8}
// HEADERLESS BOARD {0x7C, 0x87, 0xCE, 0xF0 0x69, 0xAC}
// NEWEST COM BOARD IN EVA {0x24, 0x62, 0xAB, 0xD2, 0x85, 0xDC}
// uint8_t broadcastAddress[] = {0x24, 0x62, 0xAB, 0xD2, 0x85, 0xDC};
//uint8_t broadcastAddress[] = {0xC8, 0xF0, 0x9E, 0x4F, 0xAF, 0x40};
uint8_t broadcastAddress[] = {0xB0, 0xA7, 0x32, 0xDE, 0xC1, 0xFC};
// uint8_t broadcastAddress[] = {0x48, 0xE7, 0x29, 0xA3, 0x0D, 0xA8}; // TEST
// uint8_t broadcastAddress[] = { 0x48, 0xE7, 0x29, 0xA3, 0x0D, 0xA8 }; // TEST COM
// {0x7C, 0x87, 0xCE, 0xF0, 0x69, 0xAC};
// {0x3C, 0x61, 0x05, 0x4A, 0xD5, 0xE0};
// {0xC4, 0xDD, 0x57, 0x9E, 0x96, 0x34};
// Callback when data is sent
// void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
//  sendTime = millis();
// }

// Callback when data is received, should we add this to the daq_sense board?
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.printf("Board ID %u: %u bytes\n", myData.id, len);
  boardsStruct[myData.id-1].messageTime = myData.messageTime;
  boardsStruct[myData.id-1].PT_O1 = myData.PT_O1;
  boardsStruct[myData.id-1].PT_O2 = myData.PT_O2;
  boardsStruct[myData.id-1].PT_E1 = myData.PT_E1;
  boardsStruct[myData.id-1].PT_E2 = myData.PT_E2;
  boardsStruct[myData.id-1].PT_C1 = myData.PT_C1;
  boardsStruct[myData.id-1].LC_1 = myData.LC_1;
  boardsStruct[myData.id-1].LC_2 = myData.LC_2;
  boardsStruct[myData.id-1].LC_3 = myData.LC_3;
  boardsStruct[myData.id-1].TC_1 = myData.TC_1;
  boardsStruct[myData.id-1].TC_2 = myData.TC_2;
  boardsStruct[myData.id-1].TC_3 = myData.TC_3;
  boardsStruct[myData.id-1].TC_4 = myData.TC_4;
  boardsStruct[myData.id-1].COMState = myData.COMState;
  boardsStruct[myData.id-1].DAQState = myData.DAQState;
  boardsStruct[myData.id-1].queueLength = myData.queueLength;
  boardsStruct[myData.id-1].ethComplete = myData.ethComplete;
  boardsStruct[myData.id-1].oxComplete = myData.oxComplete;
}


// Initialize all sensors and parameters.
/*
void setup() {
  // pinMode(ONBOARD_LED,OUTPUT);

  Serial.begin(115200);

  while (!Serial) delay(1);  // wait for Serial on Leonardo/Zero, etc.

  // HX711.
  PT_O1.scale.begin(PT_O1.gpio, PT_O1.clk);
  PT_O1.scale.set_gain(64);
  PT_O2.scale.begin(PT_O2.gpio, PT_O2.clk);
  PT_O2.scale.set_gain(64);
  PT_E1.scale.begin(PT_E1.gpio, PT_E1.clk);
  PT_E1.scale.set_gain(64);
  PT_E2.scale.begin(PT_E2.gpio, PT_E2.clk);
  PT_E2.scale.set_gain(64);
  PT_C1.scale.begin(PT_C1.gpio, PT_C1.clk);
  PT_C1.scale.set_gain(64);
  LC_1.scale.begin(LC_1.gpio, LC_1.clk);
  LC_1.scale.set_gain(64);
  LC_2.scale.begin(LC_2.gpio, LC_2.clk);
  LC_2.scale.set_gain(64);
  LC_3.scale.begin(LC_3.gpio, LC_3.clk);
  LC_3.scale.set_gain(64);

  // Thermocouple.
  pinMode(TC_1.cs, OUTPUT);
  pinMode(TC_2.cs, OUTPUT);
  pinMode(TC_3.cs, OUTPUT);
  pinMode(TC_4.cs, OUTPUT);
*/
  // MOSFET.
  //  mosfet_pcf.startI2C(I2C_SDA, I2C_SCL, MOSFET_PCF_ADDR); // Only SEARCH, if using normal pins in Arduino
  mosfet_pcf_found = true;

  // Set pinMode to OUTPUT
  for (int i = 0; i < 16; i++) {
    pcf8575.pinMode(i, OUTPUT);
  }
  pcf8575.begin();
  mosfet_pcf_found = true;
  mosfetCloseAllValves();  // make sure everything is off by default (NMOS: Down = Off, Up = On)
  delay(500);              // startup time to make sure its good for personal testing

  // Broadcast setup.
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  // Print MAC Accress on startup for easier connections
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for Send CB to
  // get the status of Trasnmitted packet
  // esp_now_register_send_cb(OnDataSent);

  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
  // Register for a callback function that will be called when data is received
  if (!WIFIDEBUG) {
    esp_now_register_recv_cb(OnDataRecv);
  }

  sendTime = millis();
  DAQState = IDLE;
}


//::::::STATE MACHINE::::::://

// Main Structure of State Machine.
void loop() {
  fetchCOMState();
  if (DEBUG || COMState == ABORT) {
    syncDAQState();
  }
  //  Serial.print("testing");
  logData();
  //  Serial.print("made it");
  sendDelay = GEN_DELAY;
  switch (DAQState) {
    case (IDLE):
      sendDelay = IDLE_DELAY;
      if (COMState == ARMED) { syncDAQState(); }
      idle();
      break;

    case (ARMED):  // NEED TO ADD TO CASE OPTIONS //ALLOWS OTHER CASES TO TRIGGER //INITIATE TANK PRESS LIVE READINGS
      if (COMState == IDLE || COMState == PRESS) { syncDAQState(); }
      armed();
      break;

    case (PRESS):
      if (COMState == IDLE || (COMState == QD)) {
        syncDAQState();
        int QDStart = millis();
        mosfetCloseAllValves();
      }
      press();
      break;

    case (QD):
      if (COMState == IDLE || COMState == IGNITION) {
        syncDAQState();
        mosfetCloseAllValves();
      }
      quick_disconnect();
      break;

    case (IGNITION):
      if (COMState == IDLE || COMState == HOTFIRE) {
        syncDAQState();
        hotfireStart = millis();
      }
      ignition();
      break;

    case (HOTFIRE):
      hotfire();
      break;

    case (ABORT):
      abort_sequence();
      if (COMState == IDLE && oxVentComplete && ethVentComplete) { syncDAQState(); }
      break;
  }
}

// State Functions.

// Everything should be off.
void reset() {
  oxComplete = false;
  ethComplete = false;
  oxVentComplete = false;
  ethVentComplete = false;
  PT_O1.resetReading();
  PT_O2.resetReading();
  PT_E1.resetReading();
  PT_E2.resetReading();
  PT_C1.resetReading();
  LC_1.resetReading();
  LC_2.resetReading();
  LC_3.resetReading();
  TC_1.resetReading();
  TC_2.resetReading();
  TC_3.resetReading();
  TC_4.resetReading();
}

void idle() {

  mosfetCloseAllValves();
  reset();  // must set oxComplete and ethComplete to false!
}

// Oxygen and fuel should not flow yet.
// This function is the same as idle?
void armed() {
  // mosfetCloseValve(MOSFET_LOX_PRESS);
  // mosfetCloseValve(MOSFET_ETH_PRESS);
  mosfetCloseAllValves();
}

void press() {
  if (!(oxComplete && ethComplete)) {
    if (SENSE.PT_O1 < pressureOx * threshold) {
      mosfetOpenValve(MOSFET_LOX_PRESS);
      if (DEBUG) {
        SENSE.PT_O1 += (0.00075 * GEN_DELAY);
      }
    } else {
      mosfetCloseValve(MOSFET_LOX_PRESS);
      oxComplete = true;
    }
    if (SENSE.PT_E1 < pressureFuel * threshold) {
      mosfetOpenValve(MOSFET_ETH_PRESS);
      if (DEBUG) {
        SENSE.PT_E1 += (0.001 * GEN_DELAY);
      }
    } else {
      mosfetCloseValve(MOSFET_ETH_PRESS);
      ethComplete = true;
    }
  }
  CheckAbort();
}

// Disconnect harnessings and check state of rocket.
void quick_disconnect() {
  mosfetCloseValve(MOSFET_ETH_PRESS); //close press valves
  mosfetCloseValve(MOSFET_LOX_PRESS);

  // vent valves/vent the lines themselves
  // vent the pressure solenoid for 1 full second
  //if millis() >= (QDStart+1000){
  // then, disconnect the lines from the rocket itself
  // }
  CheckAbort();
}

void ignition() {
  mosfetOpenValve(MOSFET_IGNITER);
}

void hotfire() {
  mosfetCloseValve(MOSFET_IGNITER);
  mosfetOpenValve(MOSFET_ETH_MAIN);
  //
  //  if (millis() >= hotfireStart+3000) {
  //    mosfetCloseValve(MOSFET_LOX_MAIN);
  //  } else {

  if (millis() >= hotfireStart + 5) {
    mosfetOpenValve(MOSFET_LOX_MAIN);
    // Serial.print(hotfireStart);
  }
  //  }
}

void abort_sequence() {
  if (DEBUG) {
    mosfetOpenValve(MOSFET_VENT_LOX);
    mosfetOpenValve(MOSFET_VENT_ETH);
//    delay(50);
  }
  // Waits for LOX pressure to decrease before venting Eth through pyro
  mosfetCloseValve(MOSFET_LOX_PRESS);
  mosfetCloseValve(MOSFET_ETH_PRESS);
  mosfetCloseValve(MOSFET_LOX_MAIN);
  mosfetCloseValve(MOSFET_ETH_MAIN);
  mosfetCloseValve(MOSFET_IGNITER);
  //
  int currtime = millis();
  if (SENSE.PT_O1 > 1.3 * ventTo) {  // 1.3 is magic number.
    oxVentComplete = false;
  }
  if (SENSE.PT_E1 > 1.3 * ventTo) {  // 1.3 is magic number.
    ethVentComplete = false;
  }

  if (!(oxVentComplete && ethVentComplete)) {
    if (SENSE.PT_O1 > ventTo) {  // vent only lox down to vent to pressure
      mosfetOpenValve(MOSFET_VENT_LOX);
      if (DEBUG) {
        SENSE.PT_O1 = SENSE.PT_O1 - (0.0005 * GEN_DELAY);
      }
    } else {                              // lox vented to acceptable hold pressure
      mosfetCloseValve(MOSFET_VENT_LOX);  // close lox
      oxVentComplete = true;
    }
    if (SENSE.PT_E1 > ventTo) {
      mosfetOpenValve(MOSFET_VENT_ETH);  // vent ethanol
      if (DEBUG) {
        SENSE.PT_E1 = SENSE.PT_E1 - (0.0005 * GEN_DELAY);
      }
    } else {
      mosfetCloseValve(MOSFET_VENT_ETH);
      ethVentComplete = true;
    }
  }
  if (DEBUG) {
    SENSE.PT_E1 = SENSE.PT_O1 + (0.00005 * GEN_DELAY);
    SENSE.PT_E1 = SENSE.PT_E1 + (0.00005 * GEN_DELAY);
  }
}

//// Helper Functions
//
//// Get commanded state from COM board.
void fetchCOMState() {
  COMState = Commands.COMState;
  if (Serial.available() > 0) {
    // Serial.read reads a single character as ASCII. Number 1 is 49 in ASCII.
    // Serial sends character and new line character "\n", which is 10 in ASCII.
    int SERIALState = Serial.read() - 48;
    if (SERIALState >= 0 && SERIALState <= 9) {
      COMState = SERIALState;
      DAQSenseState = SERIALState;
    }
  }
}

// Sync state of DAQ board with DAQ board.
void syncDAQState() {
  //DAQState = COMState;
  DAQState = DAQSenseState;
}

void CheckAbort() {
  if (COMState == ABORT || SENSE.PT_O1 >= abortPressure || SENSE.PT_E1 >= abortPressure) {
    mosfetCloseValve(MOSFET_ETH_PRESS);
    mosfetCloseValve(MOSFET_LOX_PRESS);
    DAQState = ABORT;
  }
}

void mosfetCloseAllValves() {
  if (mosfet_pcf_found /*&& !DEBUG*/) {
    for (int i = 0; i < 16; i++) {
      pcf8575.digitalWrite(i, LOW);
    }
  }
}

void mosfetCloseValve(int num) {
  if (mosfet_pcf_found /* && !DEBUG*/) {
    pcf8575.digitalWrite(num, LOW);
  }
}

void mosfetOpenValve(int num) {
  if (mosfet_pcf_found) {
    pcf8575.digitalWrite(num, HIGH);
  }
}

//::::::DATA LOGGING AND COMMUNICATION::::::://
void logData() {
  printSensorReadings();
  if (millis() - sendTime > sendDelay) {
    sendTime = millis();
    sendData();
    // saveData();
  }
}

void printSensorReadings() {
  serialMessage = " ";
  serialMessage.concat(millis());
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.PT_O1);
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.PT_O2);
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.PT_E1);
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.PT_E2);
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.PT_C1);
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.LC_1);
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.LC_2);
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.LC_3);
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.TC_1);
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.TC_2);
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.TC_3);
  serialMessage.concat(" ");
  serialMessage.concat(SENSE.TC_4);
  serialMessage.concat(" ");
  serialMessage.concat(ethComplete);
  serialMessage.concat(" ");
  serialMessage.concat(oxComplete);
  serialMessage.concat(" ");
  serialMessage.concat(state_names[DAQState]);
  //  serialMessage.concat(readingCap1);
  //  serialMessage.concat(" ");
  //  serialMessage.concat(readingCap2);
  serialMessage.concat(" Queue Length: ");
  serialMessage.concat(queueLength);
  Serial.println(serialMessage);
}

// Send data to COM board.
void sendData() {
  addPacketToQueue();
  sendQueue();
}

void addPacketToQueue() {
  if (queueLength < 40) {
    queueLength += 1;
    PacketQueue[queueLength].id = Board_ID; //POWER ID = 1
    PacketQueue[queueLength].messageTime = millis();
    PacketQueue[queueLength].PT_O1 = SENSE.PT_O1;
    PacketQueue[queueLength].PT_O2 = SENSE.PT_O2;
    PacketQueue[queueLength].PT_E1 = SENSE.PT_E1;
    PacketQueue[queueLength].PT_E2 = SENSE.PT_E2;
    PacketQueue[queueLength].PT_C1 = SENSE.PT_C1;
    PacketQueue[queueLength].LC_1 = SENSE.LC_1;
    PacketQueue[queueLength].LC_2 = SENSE.LC_2;
    PacketQueue[queueLength].LC_3 = SENSE.LC_3;
    PacketQueue[queueLength].TC_1 = SENSE.TC_1;
    PacketQueue[queueLength].TC_2 = SENSE.TC_2;
    PacketQueue[queueLength].TC_3 = SENSE.TC_3;
    PacketQueue[queueLength].TC_4 = SENSE.TC_4;

    // PacketQueue[queueLength].TC_3 = TC_3.rawReading; // sinc daq and com when adding tcs
    PacketQueue[queueLength].queueLength = queueLength;
    PacketQueue[queueLength].DAQState = DAQState;
    PacketQueue[queueLength].oxComplete = oxComplete;
    PacketQueue[queueLength].ethComplete = ethComplete;
  }
}

void sendQueue() {
  if (queueLength < 0) {
    return;
  }
  // Set values to send
  Packet = PacketQueue[queueLength];

  if (!WIFIDEBUG) {
    // Send message via ESP-NOW
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&Packet, sizeof(Packet));

    if (result == ESP_OK) {
      // Serial.println("Sent with success Data Send");
      queueLength -= 1;
    } else {
      Serial.println("Error sending the data");
    }
  }
}
