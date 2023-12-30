// Jonathan Armstrong
// 8/25/2023
// attempt to behave like the MSM9066 flasher on inexpensive arduino platform (using ESP32 here)
// traces of the flasher are found at: https://github.com/mightymos/msm9066_capture

#include "onbrightFlasher.h"

//#include <Wire.h>
#include <SoftWire.h>
#include <AsyncDelay.h>

#include <user_interface.h>

//#include <ESP.h>


// example
// https://github.com/WestfW/Duino-hacks/blob/master/hvTiny28prog/hvTiny28prog.ino
#include "simpleParser.h"

simpleParser<100> ttycli(Serial);
byte debug = 0;

static const char PROGMEM cmds[] = 
#define CMD_HANDSHAKE 0
  "AYT "
#define CMD_VERSION 1
  "version "
#define CMD_SIGNATURE 2
  "signature "
#define CMD_GET_FUSE 3
  "fuse "
#define CMD_GET_CALIBRATE 4
  "calibrate "
#define CMD_READ_FLASH 5
  "read "
#define CMD_WRITE_FLASH 6
  "write "
#define CMD_SET_FUSE 7
  "setfuse "
#define CMD_ERASE 8
  "erase "
#define CMD_HELP 9
  "? "
  ;

// FIXME: comment
// pin 16 and pin 24
int sclPin = 4;
int sdaPin = 5;

SoftWire sw(sdaPin, sclPin);
OnbrightFlasher flasher;

//char swTxBuffer[1024];
//char swRxBuffer[1024];


// the official msm9066 programmer appears to only be capable of issuing commands
// if the processor has just been power cycled (or reset?)
// (but assigned reset pin function is unknown currently)
// (the official programmer, if not supplying power itself, must consequently be attached to monitor 3.3V)
//int vccMonitorPin = 19;

int pushButton = 0;

// on sonoff r2.2 with esp8265 led is GPIO13 (pin 12)
int ledPin = 13;

// FIXME: no magic numbers
// storage for reading or writing to microcontroller flash memory
uint8_t flashMemory[8192];


int togglePeriod = 1000;

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
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        lastToggle = now;
    }
}


void setup()
{
  pinMode(ledPin, OUTPUT);

  //rst_info* rinfo = ESP.getResetInfoPtr();
  //Serial.println(rinfo->reason);  

  //
  sw.enablePullups(true);
  sw.begin();

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


  //example
  //shell.attach(Serial);
  //shell.addCommand(F("id?"), showID);

  //basic command
  //shell.addCommand(F("setTogglePeriod"), setTogglePeriod);
  // command with hint for help
  //shell.addCommand(F("setTogglePeriod <milliseconds>"), setTogglePeriod);
  //shell.addCommand(F("getTogglePeriod"), getTogglePeriod);

  //onbright commands
  //shell.addCommand(F("setHandshake <bool>"), setHandshake);
  //shell.addCommand(F("readType"), readType);
  //shell.addCommand(F("eraseChip"), eraseChip);
  //shell.addCommand(F("resetChip"), resetChip);
  //shell.addCommand(F("readConfig <address>"), readConfig);
  //shell.addCommand(F("writeConfig <byte> <address>"), writeConfig);

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
    if (clicmd > 0)
    {  // We have an intel hex file line?
      for (int i=0; i < clicmd; i++)
      {
        if (flasher.writeFlashByte(sw, addr, results[i]))
        {
          Serial.println("Write");
        } else {
          Serial.print("Write failed at addr 0x");
          Serial.print(addr, HEX);
          Serial.print(" for 0x");
          Serial.println(results[i], HEX);
        }

        addr++;
      }
    } else {
      // else try an "interactive" command.

      // look for a command.
      clicmd = ttycli.keyword(cmds);
      if (debug)
      {
        printf("Have command %d\n", clicmd);
      }

      switch (clicmd) {
        case CMD_HANDSHAKE:
          Serial.println("Reset handshake");
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
      state = handshake;
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
      if (resetToHandshake)
      {
        resetToHandshake = false;
        state = idle;
      }
      break;
    case finished:
      break;
  }

  // show loop() is still running -- not waiting
  toggleLED_nb();
}