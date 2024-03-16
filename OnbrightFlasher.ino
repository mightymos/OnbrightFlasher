// Jonathan Armstrong
// 8/25/2023
// attempt to behave like the MSM9066 flasher on inexpensive arduino platform
// Supports ESP8265, ESP8266, and ESP32 so far
// traces of the official programmer are found at: https://github.com/mightymos/msm9066_capture

// written for onbright 8051 microcontroller
#include "onbrightFlasher.h"

// example: https://github.com/WestfW/Duino-hacks/blob/master/hvTiny28prog/hvTiny28prog.ino
#include "simpleParser.h"

// chooses which i2c wire compatible library to use (e.g., software based, hardware based, etc.)
#include "projectDefs.h"

// same intel hex parser used by Tasmota (originally from c2_prog_wifi project)
#include "ihx.h"

// https://github.com/G6EJD/ESP32-8266-File-Upload
// https://tttapa.github.io/ESP8266/Chap12%20-%20Uploading%20to%20Server.html
// https://randomnerdtutorials.com/install-esp8266-nodemcu-littlefs-arduino/
#ifdef ESP8266
  #include <ESP8266WiFi.h>       // Built-in
  #include <ESP8266WiFiMulti.h>  // Built-in
  #include <ESP8266WebServer.h>  // Built-in
  #include <ESP8266mDNS.h>
#else
  #include <WiFi.h>              // Built-in
  #include <WiFiMulti.h>         // Built-in
  #include <ESP32WebServer.h>    // https://github.com/Pedroalbuquerque/ESP32WebServer download and place in your Libraries folder
  #include <ESPmDNS.h>
  #include "FS.h"
#endif

// html and wifi credentials
#include "secrets.h"
#include "Network.h"
#include "Sys_Variables.h"
#include "CSS.h"

// we do not have nor need an external sd card
//#include <SD.h> 
//#include <SPI.h>

// LittleFS
#include <LittleFS.h>

#ifdef ESP8266
  ESP8266WiFiMulti wifiMulti; 
  ESP8266WebServer server(80);
#else
  WiFiMulti wifiMulti;
  ESP32WebServer server(80);
#endif

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


// microcontroller flash size
//#define TARGET_FLASH_SIZE 8192

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
//#define PUSH_BUTTON_AVAILABLE
#if defined(PUSH_BUTTON_AVAILABLE)
  // Sonoff bridge (gpio0)
  int pushButton = 0;
#endif

// 
#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  // example: Sonoff ESP8285 pin 16 and pin 24 (gpio4 and gpio5) (or USBRXD and UXBTXD on J3 connector)
  int sdaPin = PIN_WIRE_SDA;
  int sclPin = PIN_WIRE_SCL;
#else
  #error Please specify SDA pin and SCL pin for your board
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
#define FLASH_HEX 12
 "flashhex "
  ;


// 8051 microcontroller flashing protocol
OnbrightFlasher flasher;

// applies if we use beginTransmission()/endTransmission() style
// which we do anyway now in order to be compatible with Wire library
char swTxBuffer[64];
char swRxBuffer[64];

// littlefs
File hexFile;
size_t filesize;

//uint8_t data[8192];
uint32_t size;

// led blink task
int togglePeriod = 1000;

uint8_t filearray[32767];

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

// https://github.com/arendst/Tasmota/blob/master/tasmota/tasmota_xdrv_driver/xdrv_06_snfbridge.ino
// requires a hex file so on PC side can do: packihx foo.ihx > foo.hex
uint32_t rf_decode_and_write(uint8_t *record, size_t size) {
  uint8_t err = ihx_decode(record, size);
  if (err != IHX_SUCCESS)
  {
    // Failed to decode RF firmware
    return 13;
  }

  ihx_t *h = (ihx_t *) record;
  if (h->record_type == IHX_RT_DATA)
  {
    int retries = 5;
    uint16_t address = h->address_high * 0x100 + h->address_low;

    // DEBUG:
    //Serial.print("Addr: ");
    //Serial.println(address, HEX);

    do {
    //  err = c2_programming_init(C2_DEVID_EFM8BB1);
      err = flasher.writeFlashBlock(address, h->data, h->len);
    } while (err > 0 && retries--);
  } else if (h->record_type == IHX_RT_END_OF_FILE) {
    // RF firmware upgrade done, restarting RF chip
    flasher.resetMCU();
  }

  if (err > 0) {
    return 12;
  }  // Failed to write to RF chip

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

  while (addr < size) {
    memcpy(buf, p_data, sizeof(buf));  // Must load flash using memcpy on 4-byte boundary

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

#if defined(ESP8266)
    // FIXME: no idea what happens to wifi while we are busy here
    // this seems like a nonideal hack
    // clear watchdog to avoid reset if using an ESP8265/8266
    ESP.wdtFeed();
#endif

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

  if (!WiFi.config(local_IP, gateway, subnet, dns))
  {
    Serial.println("WiFi STATION Failed to configure Correctly"); 
  } 

  // add Wi-Fi networks you want to connect to, it connects strongest to weakest
  wifiMulti.addAP(ssid_1, password_1);

  
  Serial.println("Connecting ...");

  // Wait for the Wi-Fi to connect: scan for Wi-Fi networks, and connect to the strongest of the networks above
  while (wifiMulti.run() != WL_CONNECTED)
  {
    delay(50);
    Serial.print('.');
  }

  // Report which SSID and IP is in use
  Serial.println("\nConnected to "+WiFi.SSID()+" Use IP address: "+WiFi.localIP().toString());

  // The logical name http://fileserver.local will also access the device if you have 'Bonjour' running or your system supports multicast dns
  // Set your preferred server name, if you use "myserver" the address would be http://myserver.local/
  if (!MDNS.begin(servername))
  {
    Serial.println(F("Error setting up MDNS responder!")); 
    ESP.restart(); 
  } 


  // filesystem is used to store upload firmware files to flash
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  // 
  //----------------------------------------------------------------------   
  ///////////////////////////// Server Commands 
  server.on("/",         HomePage);
  server.on("/download", File_Download);
  server.on("/upload",   File_Upload);
  server.on("/fupload",  HTTP_POST,[](){ server.send(200);}, handleFileUpload);
  ///////////////////////////// End of Request commands

  server.begin();
  Serial.println("HTTP server started");
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

  // Listen for client connections
  server.handleClient();

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
        case FLASH_HEX:
        {
          Serial.println("Opening hex file");
          hexFile = LittleFS.open("firmware.hex", "r");

          if (!hexFile) {
              Serial.println("file open failed");
          } else {

            //the size of the file in bytes 
            filesize = hexFile.size();

            Serial.print("File size: ");
            Serial.println(filesize);

            Serial.print("Buffer size: ");
            Serial.println(sizeof(filearray));

            Serial.println("Reading file...");
            hexFile.read(filearray, filesize);

            Serial.println("Closing file...");
            hexFile.close();

            Serial.println("Starting hex parsing...");
            uint32_t result = rf_search_and_write(filearray, filesize);

            Serial.print("rf_search_and_write() returned: ");
            Serial.println(result);
          }
          break;
        }
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
  // this will only actually toggle pin if LED_BUILTIN is defined
  toggleLED_nb();
}

// All supporting functions from here...
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void HomePage(){
  SendHTML_Header();
  webpage += F("<a href='/download'><button>Download</button></a>");
  webpage += F("<a href='/upload'><button>Upload</button></a>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop(); // Stop is needed because no content length was sent
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void File_Download(){ // This gets called twice, the first pass selects the input, the second pass then processes the command line arguments
  if (server.args() > 0 ) { // Arguments were received
    if (server.hasArg("download"))
      FS_file_download(server.arg(0));
  }
  else SelectInput("Enter filename to download","download","download");
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void FS_file_download(String filename){

    File download = LittleFS.open("/"+filename, "r");

    if (download) {
      server.sendHeader("Content-Type", "text/text");
      server.sendHeader("Content-Disposition", "attachment; filename="+filename);
      server.sendHeader("Connection", "close");
      server.streamFile(download, "application/octet-stream");
      download.close();
    } else ReportFileNotPresent("download"); 
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void File_Upload(){
  append_page_header();
  webpage += F("<h3>Select File to Upload</h3>"); 
  webpage += F("<FORM action='/fupload' method='post' enctype='multipart/form-data'>");
  webpage += F("<input class='buttons' style='width:40%' type='file' name='fupload' id = 'fupload' value=''><br>");
  webpage += F("<br><button class='buttons' style='width:10%' type='submit'>Upload File</button><br>");
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  server.send(200, "text/html",webpage);
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
File UploadFile; 

// upload a new file to the Filing system
void handleFileUpload()
{
  // See https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer/srcv
  // For further information on 'status' structure, there are other reasons such as a failed transfer that could be used
  HTTPUpload& uploadfile = server.upload();

  if(uploadfile.status == UPLOAD_FILE_START)
  {
    String filename = uploadfile.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("Upload File Name: "); Serial.println(filename);
    // Remove a previous version, otherwise data is appended the file again
    LittleFS.remove(filename);
    // Open the file for writing in SPIFFS (create it, if doesn't exist)
    UploadFile = LittleFS.open(filename, "w");
    filename = String();
  }
  else if (uploadfile.status == UPLOAD_FILE_WRITE)
  {
    // Write the received bytes to the file
    if(UploadFile)
      UploadFile.write(uploadfile.buf, uploadfile.currentSize);
  } 
  else if (uploadfile.status == UPLOAD_FILE_END)
  {
    // If the file was successfully created
    if(UploadFile)
    {                                    
      UploadFile.close();   // Close the file again
      Serial.print("Upload Size: ");
      Serial.println(uploadfile.totalSize);
      webpage = "";
      append_page_header();
      webpage += F("<h3>File was successfully uploaded</h3>"); 
      webpage += F("<h2>Uploaded File Name: "); webpage += uploadfile.filename+"</h2>";
      webpage += F("<h2>File Size: "); webpage += file_size(uploadfile.totalSize) + "</h2><br>"; 
      append_page_footer();
      server.send(200,"text/html",webpage);
    } 
    else
    {
      ReportCouldNotCreateFile("upload");
    }
  }
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SendHTML_Header(){
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate"); 
  server.sendHeader("Pragma", "no-cache"); 
  server.sendHeader("Expires", "-1"); 
  server.setContentLength(CONTENT_LENGTH_UNKNOWN); 
  server.send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves. 
  append_page_header();
  server.sendContent(webpage);
  webpage = "";
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SendHTML_Content(){
  server.sendContent(webpage);
  webpage = "";
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SendHTML_Stop(){
  server.sendContent("");
  server.client().stop(); // Stop is needed because no content length was sent
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SelectInput(String heading1, String command, String arg_calling_name){
  SendHTML_Header();
  webpage += F("<h3>"); webpage += heading1 + "</h3>"; 
  webpage += F("<FORM action='/"); webpage += command + "' method='post'>"; // Must match the calling argument e.g. '/chart' calls '/chart' after selection but with arguments!
  webpage += F("<input type='text' name='"); webpage += arg_calling_name; webpage += F("' value=''><br>");
  webpage += F("<type='submit' name='"); webpage += arg_calling_name; webpage += F("' value=''><br>");
  webpage += F("<a href='/'>[Back]</a>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void ReportSDNotPresent(){
  SendHTML_Header();
  webpage += F("<h3>No SD Card present</h3>"); 
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void ReportFileNotPresent(String target){
  SendHTML_Header();
  webpage += F("<h3>File does not exist</h3>"); 
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void ReportCouldNotCreateFile(String target){
  SendHTML_Header();
  webpage += F("<h3>Could Not Create Uploaded File (write-protected?)</h3>"); 
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
String file_size(int bytes){
  String fsize = "";
  if (bytes < 1024)                 fsize = String(bytes)+" B";
  else if(bytes < (1024*1024))      fsize = String(bytes/1024.0,3)+" KB";
  else if(bytes < (1024*1024*1024)) fsize = String(bytes/1024.0/1024.0,3)+" MB";
  else                              fsize = String(bytes/1024.0/1024.0/1024.0,3)+" GB";
  return fsize;
}