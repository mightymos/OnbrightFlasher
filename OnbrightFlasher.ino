// Jonathan Armstrong
// 8/25/2023
// attempt to behave like the MSM9066 flasher on inexpensive arduino platform
// Supports ESP8265, ESP8266, and ESP32 so far
// traces of the official programmer are found at: https://github.com/mightymos/msm9066_capture

// written for onbright 8051 microcontroller
#include "onbrightFlasher.h"

// example: https://github.com/WestfW/Duino-hacks/blob/master/hvTiny28prog/hvTiny28prog.ino
#include "simpleParser.h"

// For unknown reasons SoftWire seems to work perfectly on ESP8285/ESP8286.
// However, my ESP32 board sometimes has errors.
// ESP32 has hardware I2C which seems to work perfectly with Wire.
// NOTE ***************************************************
// SEE onbrightFlasher.h to uncomment/comment defines
// NOTE ***************************************************
#if defined(USE_SOFTWIRE_LIBRARY) && defined(USE_WIRE_LIBRARY)
  #error Please uncomment either USE_SOFTWIRE_LIBRARY or USE_WIRE_LIBRARY but not both.
#elif defined(USE_SOFTWIRE_LIBRARY)
  // needed for softwire timeouts
  #include <AsyncDelay.h>
  #include <SoftWire.h>
#elif defined(USE_WIRE_LIBRARY)
  #include <Wire.h>
#endif


// microcontroller flash size
#define TARGET_FLASH_SIZE 8192

// uncomment for more print() statements
// however, some extra information is not very helpful to users
//#define VERBOSE_DEBUG

// not every board has an LED attached
// and want to avoid toggling a pin that might interfere with some other function
#if defined(LED_BUILTIN)
  #define LED_AVAILABLE
  int ledPin = LED_BUILTIN;
#endif

// not used currently
//#define PUSH_BUTTON_AVAILABLE
#if defined(PUSH_BUTTON_AVAILABLE)
  // Sonoff bridge (gpio0)
  int pushButton = 0;
#endif

// choose pin definintions based on various chips/boards
#if defined(ESP8266)
  // Sonoff ESP8285 pin 16 and pin 24 (gpio4 and gpio5) (or USBRXD and UXBTXD on J3 connector)
  // Wemos D1 mini pin D2 and pin D1  (gpio4 and gpio5)
  int sdaPin = 4;
  int sclPin = 5;
#elif defined (CONFIG_IDF_TARGET_ESP32S3)
  // from [https://esp32.com/viewtopic.php?t=26127]
  // pinout: [https://mischianti.org/vcc-gnd-studio-yd-esp32-s3-devkitc-1-clone-high-resolution-pinout-and-specs/]
  int sdaPin = 8;
  int sclPin = 9;
#elif defined(CONFIG_IDF_TARGET_ESP32)
  // ESP32-WROOM-32 38 pins
  int sdaPin = 32;
  int sclPin = 33;
#endif

#if defined(USE_SOFTWIRE_LIBRARY)
  // use the same name "Wire" so that calls in onbrightFlasher.cpp remain the same
  SoftWire Wire(sdaPin, sclPin);
#endif

// parses received serial strings looking for hex lines or commands
simpleParser<100> ttycli(Serial);
byte debug = 0;

// valid commands
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


// 8051 microcontroller flashing protocol
OnbrightFlasher flasher;

// applies if we use beginTransmission()/endTransmission()
// which we do anyway now in order to be compatible with Wire library
char swTxBuffer[32];
char swRxBuffer[32];


// storage for reading or writing to microcontroller flash memory
uint8_t flashMemory[TARGET_FLASH_SIZE];

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
  connected
};

// state machine for handshake
unsigned char state = idle;

// count and display an index to user just so they know program is still running
int heartbeatCount = 0;

// there are various posts that show this switch()
// but have not confirmed it is correct
void checkError(const byte error)
{
  // helps making parsing on PC side easier
  Serial.print("Status: ");
  Serial.println(error);

  // specific error reason
  switch (error) {
    case 0:
      Serial.println("Success");
      break;
    case 1:
      Serial.println("Data too long to fit in transmit buffer");
      break;
    case 2:
      Serial.println("NACK on transmit of address");
      break;
    case 3:
      Serial.println("NACK on transmit of data");
      break;
    case 4:
      Serial.println("Other error");
      break;
    case 5:
      Serial.println("Timeout");
      break;
    default:
      Serial.print("Unknown error");
  }
}

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

#if defined(LED_AVAILABLE)
        // toggle
        digitalWrite(ledPin, !digitalRead(ledPin));
#endif

        lastToggle = now;
    }
}

void setup()
{

#if defined(LED_AVAILABLE)
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);
#endif

#if defined(USE_SOFTWIRE_LIBRARY)
  // often esp gpio pins have internal pullups
  // so use them instead of having to add external resistors
  Wire.enablePullups(true);

  // shorten from 100ms default so watchdog does not reset processor
  Wire.setTimeout_ms(20);

  // not sure if buffers are required or not but no harm?
  Wire.setTxBuffer(swTxBuffer, sizeof(swTxBuffer));
  Wire.setRxBuffer(swRxBuffer, sizeof(swRxBuffer));

  // 10 kHz (default is 100 khz per source code)
  //Wire.setClock(10000);

  Wire.begin();
#elif defined(USE_WIRE_LIBRARY)
  // milliseconds
  Wire.setTimeout(20);
  Wire.begin(sdaPin, sclPin);
#endif

  // esp32 uses different functions to initialize and control watchdog
#if defined(ESP8266)
  // esp8265/66 hardware watchdog cannot be disabled
  // however, software watchdog can be disabled
  ESP.wdtDisable();
#endif

  Serial.begin(115200);

  // delay so serial monitor in the Arduino IDE has time to connect
  delay(5000);

  while (!Serial)
  {
      // wait for serial port to connect. Needed for native USB port only
      // AND you want to block until there's a connection
      // otherwise the shell can quietly drop output.
  }

  Serial.println(F("Ready."));
  Serial.println(F("Entering [idle] state."));
  Serial.println(F("Type [handshake] to attempt connection to target."));
  Serial.println(F("Type [idle] and then [handshake] to start from the beginning"));
}


void loop()
{
  // track state for handshake
  static uint8_t state = idle;
  static uint8_t status;

  // should be 0xA for Onbright
  uint8_t chipType;

  // for ack, nack, etc. results
  byte result;

  // used for handshake only
  bool gotAck;

  // for parsing of serial
  int clicmd;
  int16_t addr;
  uint8_t results[64];

  unsigned int writeCount = 0;
  unsigned int errorCount = 0;

#if defined(ESP8266)
  // clear watchdog to avoid reset if using an ESP8265/8266
  ESP.wdtFeed();
#endif

  // want similar to what getLineWait does but not blocking
  if (status != 0)
  {
    ttycli.reset();
  }

  // blocking version will trigger watchdog, so avoid that
  // returns 0 until end-of-line seen.
  status = ttycli.getLine();

  if (status != 0)
  {
    clicmd = ttycli.tryihex(&addr, results);

    // we have an intel hex file line?
    if (clicmd > 0)
    { 
      writeCount = 0;
      errorCount = 0;

      // try to flash hex line
      for (int i = 0; i < clicmd; i++)
      {
        // try to actually flash target
        result = flasher.writeFlashByte(addr, results[i]);
        if (result > 0)
        {
          checkError(result);

#ifdef VERBOSE_DEBUG
          Serial.print("Write failed at addr 0x");
          Serial.print(addr, HEX);
          Serial.print(" for 0x");
          Serial.println(results[i], HEX);
#endif
          errorCount += 1;
        } else {
          writeCount += 1;
        }

        addr++;
      }

      if (errorCount > 0)
      {
        Serial.println("Write FAILED");
        Serial.print("Errors: ");
        Serial.println(errorCount);
        Serial.println("[can try sending hex line again]");
      } else {
        Serial.println("Write successful");
        Serial.print("Wrote ");
        Serial.print(writeCount);
        Serial.println(" bytes");
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
        case CMD_IDLE:
          // FIXME: a messy code organization here
          // impacts state machine below
          Serial.println("State changing to idle");
          state = idle;
          break;
        case CMD_HANDSHAKE:
          Serial.println("State changing to handshake");
          Serial.println("cycle power to target (start with power off and then turn on)");
          state = handshake;
          break;
        case CMD_ERASE:
          Serial.println("Erasing chip...");
          result = flasher.eraseChip();
          checkError(result);

          if (result > 0)
          {
            Serial.println("Chip erase FAILED");
          } else {
            Serial.println("Chip erase successful");
          }
          break;
        case CMD_SIGNATURE:
          Serial.println("Read chip type...");
          result = flasher.readChipType(chipType);
          checkError(result);

          if (result > 0)
          {
            Serial.println("Chip read type FAILED");
            Serial.print("Chip type reported was: 0x");
            Serial.println(chipType, HEX);
          } else {
            Serial.print("Chip read: 0x");
            Serial.println(chipType, HEX);
          }
          break;
        case CMD_GET_FUSE:
          Serial.println("Get configuration byte...");
          addr = ttycli.number();
          result = flasher.readConfigByte(addr, results[0]);
          checkError(result);

          if (result > 0)
          {
            Serial.println("Get configuration byte FAILED");
          } else {
            Serial.print("Configuration byte at (");
            Serial.print(addr);
            Serial.print(") is: ");
            Serial.println(results[0]);
          }
          break;
        case CMD_SET_FUSE:
          Serial.println("Set configuration byte...");
          addr = ttycli.number();
          results[0] = ttycli.number();
          result = flasher.writeConfigByte(addr, results[0]);
          checkError(result);

          if (result > 0)
          {
            Serial.println("Write configuration byte FAILED");
          } else {
            // FIXME: it would be a good idea to have a read, write, verify option however
            // or tell user to read back to verify
            Serial.println("Wrote configuration byte");
          }
          break;
        case CMD_MCU_RESET:
          Serial.println("MCU reset...");
          flasher.resetMCU();
          break;
        default:
          break;
      }
    }
  }

  // put your main code here, to run repeatedly:

  // state machine for handshake
  // FIXME: this should probably be implemented properly, eventually
  // [https://www.aleksandrhovhannisyan.com/blog/implementing-a-finite-state-machine-in-cpp/]
  switch(state)
  {
    case idle:
      break;
    case handshake:
      gotAck = flasher.onbrightHandshake();

      // cannot really depend on nack/ack to indicate success in this instance
      // because there is a mix of expected nacks or expected acks
      // but handshake will return true if first expected ack is received
      if (!gotAck)
      {

#ifdef VERBOSE_DEBUG
        Serial.print("Handshake FAILED (");
        Serial.print(heartbeatCount);
        Serial.println(")");
        Serial.println("cycle power to target (start with power off and then turn on)");
#endif

        heartbeatCount += 1;
      } else {
        Serial.println("Handshake succeeded");

        // there seems to be about a 120ms delay in official programmer traces
        delay(120);

        // we apparently read chip type after handshake
        result = flasher.readChipType(chipType);
        checkError(result);

        if (result > 0)
        {
          Serial.println("Chip read type FAILED");
          Serial.print("Chip type reported was: 0x");
          Serial.println(chipType, HEX);
          Serial.println("Can try command [signature] or [idle] then [handshake] to retry");

          state = idle;
        } else {
          Serial.print("Chip read: 0x");
          Serial.println(chipType, HEX);

          state = connected;
        }
      }
      break;
    case connected:
      Serial.println("Connected...");
      Serial.println("Returning to idle state...");
      state = idle;
      break;
  }

  // periodic led blink to show board is alive
  // this will only actually toggle pin if LED_AVAILABLE is defined
  toggleLED_nb();
}