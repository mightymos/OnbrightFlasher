// Jonathan Armstrong
// 8/25/2023
// attempt to behave like the MSM9066 flasher on inexpensive arduino platform
// Supports ESP8265, ESP8266, and ESP32 so far
// traces of the official programmer are found at: https://github.com/mightymos/msm9066_capture

// written for onbright 8051 microcontroller
#include "onbrightFlasher.h"

// example: https://github.com/WestfW/Duino-hacks/blob/master/hvTiny28prog/hvTiny28prog.ino
#include "simpleParser.h"

// chooses which i2c wire compatible library to use (e.g., software based Softwire, hardware based Wire, or SoftwareWire)
#include "projectDefs.h"

// same intel hex parser used by Tasmota (originally from c2_prog_wifi project)
#include "ihx.h"

// SoftWire seems to work perfectly on ESP8285/ESP8286.
// However, my ESP32 board sometimes has errors for unknown reasons.
// ESP32 has hardware I2C which seems to work better with Wire
// (no write errors, though erase times out but still works...).
#if defined(USE_SOFTWIRE_LIBRARY) && defined(USE_WIRE_LIBRARY)
  // FIXME: account for SoftwareWire library also
  #error Please uncomment either USE_SOFTWIRE_LIBRARY or USE_WIRE_LIBRARY but not both.
#elif defined(USE_SOFTWIRE_LIBRARY)
  // needed for softwire timeouts
  #include <AsyncDelay.h>
  #include <SoftWire.h>
#elif defined (USE_SOFTWAREWIRE_LIBRARY)
  #include "SoftwareWire.h"
#elif defined(USE_WIRE_LIBRARY)
  #include <Wire.h>
#endif

// these need to be uncommented and defined only if your board definitions do not specify the i2c pins
// or you want to use alternative pins for software i2c for example
//
// e.g., SDA (32) and SCL (33) are for my ESP32-WROOM with 38 pins
//#define PIN_WIRE_SDA 32
//#define PIN_WIRE_SCL 33

// microcontroller flash size
#define TARGET_FLASH_SIZE 8192
#define CONFIG_BYTE_SIZE    64

// NOTE USED CURRENTLY
//#define OUTPUT_TO_CONTROL_RESET_AVAILABLE
//#define PUSH_BUTTON_AVAILABLE

// uncomment for more print() statements
// however, some extra information is not very helpful to users
//#define VERBOSE_DEBUG

// not every board has an LED attached
// and want to avoid toggling a pin on one board that might interfere with some other function on another board
#if defined(LED_BUILTIN)
  int ledPin = LED_BUILTIN;
#else
  #warning LED_BUILTIN not defined so no LED will blink to show board is alive
#endif

// not used currently
#if defined(PUSH_BUTTON_AVAILABLE)
  // Sonoff bridge (gpio0)
  int pushButton = 0;
#endif

// not used currently
#if defined(OUTPUT_TO_CONTROL_RESET_AVAILABLE)
 // Sonoff bridge (gpio2)
 int outputToControlReset = 2;
#endif

// 
#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  // example: Sonoff ESP8285 pin 16 and pin 24 (gpio4 and gpio5) (or USBRXD and UXBTXD on J3 connector)
  int sdaPin = PIN_WIRE_SDA;
  int sclPin = PIN_WIRE_SCL;
#else
  #error Please specify PIN_WIRE_SDA pin and PIN_WIRE_SCL pin for your board in the #define(s) present in OnbrightFlasher.ino
#endif

#if defined(USE_SOFTWIRE_LIBRARY)
  // use the same name "Wire" so that calls in onbrightFlasher.cpp remain the same
  SoftWire Wire(sdaPin, sclPin);
#elif defined(USE_SOFTWAREWIRE_LIBRARY)
  // FIXME: enable pull ups, but not sure if we should detect clock stretching or not (i.e. last parameter)
  SoftwareWire Wire(sdaPin, sclPin, true, false);
#endif

// parses received serial strings looking for hex lines or commands
simpleParser<100> ttycli(Serial);

// set to 1 to allow some debugging messages
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
#define CMD_FLASH_HEX 12
 "flashhex "
#define CMD_READ_HEX 13
  "readhex "
#define CMD_READ_CONFIGS 14
  "readconfigs "
  ;


// 8051 microcontroller flashing protocol
OnbrightFlasher flasher;

// applies if we use beginTransmission()/endTransmission() style
// which we do anyway now in order to be compatible with Wire library
char swTxBuffer[64];
char swRxBuffer[64];


// led blink task
int togglePeriod = 1000;

// FIXME: no magic numbers
// stores content to write or content read from flash
uint8_t fileArray[32767];

//uint32_t size;
uint8_t configBytes[255];

// the stock programmer allowed choosing initial value of either 0x00 or 0xFF
// so this needs to be supported and accounted for as well - i.e., checksum will be different in either case
// the checksum can be compared to checksum calculated by loading hex file into SMAP AC MSM 9066 PC software
// FIXME: how do they erase to either 0x00 or 0xFF instead of just NOP?
uint32_t writeChecksum;
uint32_t readChecksum;

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
//unsigned char state = idle;

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

// https://github.com/arendst/Tasmota/blob/master/tasmota/tasmota_xdrv_driver/xdrv_06_snfbridge.ino
// requires a hex file so on PC side can do: packihx foo.ihx > foo.hex
uint32_t rf_decode_and_write(uint8_t *record, size_t size)
{
  uint8_t err = ihx_decode(record, size);
  uint8_t index;

  if (err != IHX_SUCCESS)
  {
    // Failed to decode mcu firmware
    return 13;
  }

  ihx_t *h = (ihx_t *) record;
  if (h->record_type == IHX_RT_DATA)
  {
    int retries = 5;
    uint16_t address = h->address_high * 0x100 + h->address_low;

    // keep running sum of bytes written
    while (index < h->len)
    {
      // we subtract from erased value
      writeChecksum -= 0xFF - h->data[index];
      index++;
    }

    // try actual flash
    do {
      // err = c2_programming_init(C2_DEVID_EFM8BB1);
      // handshake needs to have happened prior to write attempts because it requires power cycle
      // in contrast, EFM8BB1 was able to reset by holding a clock(?) line for a long period of time
      err = flasher.writeFlashBlock(address, h->data, h->len);
    } while (err > 0 && retries--);
  } else if (h->record_type == IHX_RT_END_OF_FILE) {
    // mcu firmware upgrade done, restarting RF chip
    flasher.resetMCU();
  }

  // Failed to write to mcu chip
  if (err > 0) {
    return 12;
  }

  return 0;
}

uint32_t rf_search_and_write(uint8_t *data, size_t size) {
  // Binary contains a set of commands, decode and program each one
  uint8_t buf[64];
  uint8_t* p_data = data;
  uint32_t addr = 0;
  uint32_t rec_end;
  uint32_t rec_start;
  uint32_t rec_size;
  uint32_t err;
  uint8_t index;

  // 8192 * 0xFF in other words checksum of an erased chip (0x1FE000)
  writeChecksum = 2088960;

  while (addr < size) {
    // Must load flash using memcpy on 4-byte boundary
    memcpy(buf, p_data, sizeof(buf));

    // Find starts and ends of commands
    for (rec_start = 0; rec_start < 8; rec_start++) {
      if (':' == buf[rec_start]) { break; }
    }

    // Record invalid - RF Remnant data did not start with a start token
    if (rec_start > 7) {
      return 8;
    }

    for (rec_end = rec_start; rec_end < sizeof(buf); rec_end++)
    {
      // FIXME: should be made to support Windows style line endings (i.e., \r\n)
      // otherwise record will be deemed too large
      // or use dos2unix on PC side
      if ('\n' == buf[rec_end]) {
        break;
      }
    }

    // Record too large - Failed to decode RF firmware
    if (rec_end == sizeof(buf)) {
      return 9;
    }

    rec_size = rec_end - rec_start;

//    AddLog(LOG_LEVEL_DEBUG, PSTR("DBG: %*_H"), rec_size, (uint8_t*)&buf + rec_start);
//    Serial.print("Parsing record with address: ");
//    Serial.println(addr);

//#if defined(ESP8266)
    // FIXME: no idea what happens to wifi while we are busy here
    // this seems like a nonideal hack
    // clear watchdog to avoid reset if using an ESP8265/8266
    //ESP.wdtFeed();
    yield();
//#endif

    err = rf_decode_and_write(buf + rec_start, rec_size);
    if (err != 0) {
      return err;
    }

    addr += rec_size +1;
    p_data += (rec_end & 0xFFFC);  // Stay on 4-byte boundary
    delay(0);
  }

  // Buffer was perfectly aligned, start and end found without any remaining trailing characters
  return 0;
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

#if defined(LED_BUILTIN)
        // toggle
        digitalWrite(ledPin, !digitalRead(ledPin));
#endif

        lastToggle = now;
    }
}

uint8_t state_machine_command(int clicmd, uint8_t state)
{
  // for ack, nack, etc. results
  byte result;

  // FIXME: add comment
  uint8_t results[64];

  int16_t addr;
  // should be 0xA for Onbright
  uint8_t chipType;

  switch (clicmd)
  {
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
    case CMD_VERSION:
      Serial.print("Date: ");
      Serial.print(__DATE__);
      Serial.print(" Time: ");
      Serial.println(__TIME__);
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
    case CMD_ERASE:
      Serial.println("Erasing chip...");
      result = flasher.eraseChip();
      checkError(result);

      // FIXME: this is a hack for now, because sometimes Wire timeouts
      // even though erase worked (confirmed by reading flash byte at 0 as 255 (i.e., 0xFF))
      if ((result != 0) && (result != 5))
      {
        Serial.println("Chip erase FAILED");
      } else {
        Serial.println("Chip erase successful");
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
    case CMD_READ_FLASH:
      Serial.println("Reading flash...");
      addr = ttycli.number();
      result = flasher.readFlashByte(addr, results[0]);
      checkError(result);

      if (result > 0)
      {
        Serial.println("Read flash FAILED");
      } else {
        Serial.print("Flash at (");
        Serial.print(addr);
        Serial.print(") is: ");
        Serial.println(results[0]);
      }
      break;
    case CMD_WRITE_FLASH:
      Serial.println("Writing flash...");
      addr = ttycli.number();
      results[0] = ttycli.number();
      result = flasher.writeFlashByte(addr, results[0]);
      checkError(result);

      if (result > 0)
      {
        Serial.println("Write flash FAILED");
      } else {
        Serial.println("Wrote flash byte");
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
    case CMD_FLASH_HEX:
      Serial.println("Flash hex file - unused");
      break;
    case CMD_READ_HEX:
    {
      flasher.readFlashBlock(0, fileArray, TARGET_FLASH_SIZE);

      uint32_t checksum = 0;
      for (uint16_t index = 0; index < TARGET_FLASH_SIZE; index++)
      {
        checksum += fileArray[index];
      }

      Serial.print("Checksum: 0x");
      Serial.println(checksum, HEX);
    }
      break;
    case CMD_READ_CONFIGS:
    {
      // beyond 64 bytes wraps around to zero address as best I can tell
      flasher.readConfigBlock(0, configBytes, CONFIG_BYTE_SIZE);

      uint16_t checksum = 0;

      for (uint8_t index = 0; index < CONFIG_BYTE_SIZE; index++)
      {
        checksum += configBytes[index];

        Serial.print("config[0x");
        Serial.print(index, HEX);
        Serial.print("]: ");
        Serial.println(configBytes[index]);
      }

      Serial.print("Checksum: 0x");
      Serial.println(checksum, HEX);
    }
      break;
  }

  return state;
}

uint8_t state_machine_flasher(uint8_t state)
{
  // used for handshake only
  bool gotAck;

  // for ack, nack, etc. results
  byte result;

  // should be 0xA for Onbright
  uint8_t chipType;

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

  return state;
}

void setup()
{

#if defined(LED_BUILTIN)
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

  // slowing down bus speed to hopefully avoid errors
  // 10 kHz (default is 100 khz per source code)
  //Wire.setClock(10000);

  Wire.begin();
#elif defined(USE_SOFTWAREWIRE_LIBRARY)
  Wire.begin();
#elif defined(USE_WIRE_LIBRARY)  
  // milliseconds
  Wire.setTimeout(20);

  // FIXME: AVR core does not seem to support specifying pins
  // https://github.com/arduino/ArduinoCore-avr/blob/master/libraries/Wire/src/utility/twi.c
  Wire.begin(sdaPin, sclPin);
#endif

  // esp32 uses different functions to initialize and control watchdog
#if defined(ESP8266)
  // esp8265/66 hardware watchdog cannot be disabled
  // however, software watchdog can be disabled
  ESP.wdtDisable();
#endif

  // the boot text on some esp might be garbled due to other baud rates, but 115200 should be easily achievable afterward
  Serial.begin(115200);

  // delay so serial monitor in the Arduino IDE has time to connect
  delay(5000);

  while (!Serial)
  {
      // wait for serial port to connect. Needed for native USB port only
      // AND you want to block until there's a connection
      // otherwise the shell can quietly drop output.
  }

  Serial.println(" ");
  Serial.println(F("Ready."));
  Serial.print("Date: ");
  Serial.print(__DATE__);
  Serial.print(" Time: ");
  Serial.println(__TIME__);
  Serial.println(F("Entering [idle] state."));
  Serial.println(F("Type [handshake] to attempt connection to target."));
  Serial.println(F("Type [idle] and then [handshake] to retry from the beginning"));
}


void loop()
{
  // track state for handshake
  static uint8_t state = idle;
  static uint8_t status;

  // for parsing of serial
  int clicmd;
  int16_t addr;
  // for ack, nack, etc. results
  byte result;
  uint8_t results[64];

  unsigned int writeCount = 0;
  unsigned int errorCount = 0;

#if defined(ESP8266)
  // clear watchdog to avoid reset if using an ESP8265/8266
  ESP.wdtFeed();
  //yield();
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

      state = state_machine_command(clicmd, state);
    }
  }


  // put your main code here, to run repeatedly:

  state = state_machine_flasher(state);

  // periodic led blink to show board is alive
  // this will only actually toggle pin if LED_BUILTIN is defined
  toggleLED_nb();
}