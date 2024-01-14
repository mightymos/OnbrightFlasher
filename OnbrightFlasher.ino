// Jonathan Armstrong
// 8/25/2023
// attempt to behave like the MSM9066 flasher on inexpensive arduino platform (using ESP8265 here)
// traces of the flasher are found at: https://github.com/mightymos/msm9066_capture

#include "onbrightFlasher.h"

//#include <Wire.h>
#include <SoftWire.h>
#include <AsyncDelay.h>

// FIXME: what was this for again?
//#include <user_interface.h>
//#include <ESP.h>

// example
// https://github.com/WestfW/Duino-hacks/blob/master/hvTiny28prog/hvTiny28prog.ino
#include "simpleParser.h"

// ESP32
//int ledPin = 2;
// ESP8265 in Sonoff (pin 12 or GPIO13)
int ledPin = 13;

// Sonoff
int pushButton = 0;

// FIXME: comment
// Sonoff ESP8285 pin 16 and pin 24 (gpio4 and gpio5) (or USBRXD and UXBTXD on J3 connector)
// Wemos D1 mini D2 and D1 (gpio4 and gpio5)
int sdaPin = 4;
int sclPin = 5;

// the official msm9066 programmer appears to only be capable of issuing commands
// if the processor has just been power cycled (or reset?)
// (but assigned reset pin function is unknown currently)
// (the official programmer, if not supplying power itself, must consequently be attached to monitor 3.3V)
//int vccMonitorPin = 19;

simpleParser<100> ttycli(Serial);
byte debug = 0;

static const char PROGMEM cmds[] = 
#define CMD_IDLE 0
  "idle "
#define CMD_HANDSHAKE 1
  "handshake "
#define CMD_VERSION 2
  "version "
#define CMD_SIGNATURE 3
  "signature "
#define CMD_GET_FUSE 4
  "fuse "
#define CMD_GET_CALIBRATE 5
  "calibrate "
#define CMD_READ_FLASH 6
  "read "
#define CMD_WRITE_FLASH 7
  "write "
#define CMD_SET_FUSE 8
  "setfuse "
#define CMD_ERASE 9
  "erase "
#define CMD_HELP 10
  "? "
#define CMD_MCU_RESET 11
 "mcureset "
  ;

SoftWire sw(sdaPin, sclPin);
OnbrightFlasher flasher;

//char swTxBuffer[1024];
//char swRxBuffer[1024];


// FIXME: no magic numbers
// storage for reading or writing to microcontroller flash memory
uint8_t flashMemory[8192];

// led blink task
int togglePeriod = 1000;

// global to reset handshake mode
bool resetToIdle      = false;
bool resetToHandshake = false;

// track handshake status
enum
{
  idle,
  handshake,
  connected,
  prompt,
  finished
};

// state machine for handshake
unsigned char state = idle;


////////////////////////////////////////////////////////////////////////////////
// non blocking LED toggle
//
void toggleLED_nb(void)
{
    // saved between calls
    static auto lastToggle = millis();
    auto now = millis();

    if (now - lastToggle > (unsigned int) (togglePeriod / 2) )
    {
        // toggle
        digitalWrite(ledPin, !digitalRead(ledPin));
        lastToggle = now;
    }
}


void setup()
{
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);

  //rst_info* rinfo = ESP.getResetInfoPtr();
  //Serial.println(rinfo->reason);  

  //
  sw.enablePullups(true);
  sw.begin();

  // FIXME: necessary on ESP8265 but not on ESP32?
  // disable software watchdog
  // (what about hardware watchdog?)
  ESP.wdtDisable();

  //Serial.begin(9600); older default bit rate
  Serial.begin(115200);
  while (!Serial)
  {
      // wait for serial port to connect. Needed for native USB port only
      // AND you want to block until there's a connection
      // otherwise the shell can quietly drop output.
  }

  Serial.println(F("Ready."));
}


void loop()
{
  static uint8_t state = idle;

  uint8_t configByte;
  uint8_t chipType;

  unsigned int index;

  bool gotAck;
  static uint8_t status;

  int clicmd;
  int16_t addr, val;
  uint8_t results[64];

  unsigned int writeCount = 0;

  // clear watchdog to avoid reset
  ESP.wdtFeed();

  //Serial.print("Cmd: ");

  // FIXME: no idea if this is correct or not
  if (status != 0)
  {
    ttycli.reset();
  }

  // blocking version will trigger watchdog, so avoid that
  //ttycli.getLineWait();
  
  // similar to what getLineWait does anyway
  status = ttycli.getLine();

  if (status != 0)
  {
    clicmd = ttycli.tryihex(&addr, results);

    // We have an intel hex file line?
    if (clicmd > 0)
    { 
      writeCount = 0;
      for (int i=0; i < clicmd; i++)
      {
        if (flasher.writeFlashByte(sw, addr, results[i]))
        {
          writeCount += 1;
        } else {
          Serial.print("Write failed at addr 0x");
          Serial.print(addr, HEX);
          Serial.print(" for 0x");
          Serial.println(results[i], HEX);
        }

        addr++;
      }

      Serial.print("Wrote ");
      Serial.print(writeCount);
      Serial.println(" bytes");
    } else {
      // else try an "interactive" command.

      // look for a command.
      clicmd = ttycli.keyword(cmds);
      if (debug)
      {
        printf("Have command %d\n", clicmd);
      }

      switch (clicmd) {
        case CMD_IDLE:
          Serial.println("Reset to idle");
          resetToIdle = true;
          break;
        case CMD_HANDSHAKE:
          Serial.println("Reset to handshake");
          resetToHandshake = true;
          break;
        case CMD_ERASE:
          if (flasher.eraseChip(sw))
          {
            Serial.println("Chip erase successful");
          } else {
            Serial.println("Chip erase FAILed");
          }
          break;
        case CMD_SIGNATURE:
          Serial.print("Read chip type...");
          if (flasher.readChipType(sw, chipType))
          {
            Serial.print("Chip read: ");
            Serial.println(chipType, HEX);
          } else {
            Serial.println("Chip failed to read");
          }
          break;
        case CMD_GET_FUSE:
          Serial.println("Get configuration byte...");
          addr = ttycli.number();
          if (flasher.readConfigByte(sw, addr, results[0]))
          {
            Serial.print("Read configuration byte ");
            Serial.print(addr);
            Serial.print(" to be ");
            Serial.println(results[0]);
          } else {
            Serial.println("Getting configuration byte FAILED");
          }
          break;
        case CMD_SET_FUSE:
          Serial.println("Set configuration byte...");
          addr = ttycli.number();
          results[0] = ttycli.number();
          if (flasher.writeConfigByte(sw, addr, results[0]))
          {
            Serial.println("Wrote configuration byte");
          } else {
            Serial.println("Writing configuration byte FAILED");
          }
          break;
        case CMD_MCU_RESET:
          Serial.println("MCU reset...");
          flasher.resetMCU(sw);
          break;
        default:
          break;
      }
    }
  }

  // put your main code here, to run repeatedly:

  // state machine for handshake
  switch(state)
  {
    case idle:
      if(resetToHandshake)
      {
        resetToHandshake = false;
        state = handshake;
      }
      break;
    case handshake:
      gotAck = flasher.onbrightHandshake(sw);
      if (gotAck)
      {
        // we must read chip type to complete handshake
        if (flasher.readChipType(sw, chipType))
        {
          Serial.print("Chip read: ");
          Serial.println(chipType, HEX);

          state = connected;
        } else {
          Serial.println("Chip failed to read");

          state = idle;
        }
      }
      break;
    case connected:
      Serial.println("Connected...");
      state = prompt;
      break;
    case prompt:
      if (resetToIdle)
      {
        resetToIdle = false;
        state = idle;
      }
      break;
    case finished:
      break;
  }

  // show loop() is still running -- not waiting
  toggleLED_nb();
}