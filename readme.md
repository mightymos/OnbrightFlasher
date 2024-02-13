## Background
This is intended to allow flashing 8051 based OBS38S003 microcontrollers as a target.  

For example, the ESP8285 present in the Sonoff RF Bridge R2 v2.2 serves as the source flasher.  
The USBRXD pin is bridged to SCL, while the USBTXD pin is bridged to SDA.  

The sketch has successfully flashed a simple blink program.  

## Downsides
First, the reset function on the reset pin of the Sonoff RF Bridge is probably disabled.  
Additionally, handshaking is apparently performed at microcontroller power up or restart.  
Therefore, for the first use of this sketch an independently powered Arduino chip would need to be used as source flasher.  

Second, each hex file line must be copy and pasted into the serial monitor individually.  
A script is being contributed that would make file upload automatic.  

## Flashers
| Board | Status | Note | 
| ------------- | ------------- | ------------- | 
|  ESP8265 (in Sonoff) | WORKING  | none | 
|  ESP8266 (Wemos D1 Mini) | WORKING  | none | 
|  ESP-WROOM-32 | WORKING  | Wire works better than SoftWire, and erase works but times out... | 
|  ESP32S3 | WORKING  | none | 

## Status
| Item | Status | Note | 
| ------------- | ------------- | ------------- | 
|  Write flash memory | DONE  | Manually one byte or one hex line at a time | 
|  Read flash memory | DONE  | Manually one byte at a time | 
|  Reading/writing configuration bits | PARTIAL  | Need read-modify-write scheme | 
|  Verify flash memory | TODO  | none | 
## Usage

### Script Mode:

1. Connect Arduino I2C/GPIO pins to the microcontroller while unpowered (it's okay for the ground to be connected on the target; software or hardware I2C can be used - see projectDefs.h).
2. Upload the sketch to ESP8265/ESP8266/ESP32.
3. Copy the desired hex file from [GitHub Releases](https://github.com/mightymos/RF-Bridge-OB38S003/releases) to the script directory.
4. Run `flashScript.py` and follow the instructions in the console.
5. For `blink.ihx`, the red LED on the Sonoff target should begin blinking with a one-second period.
6. For `RF-Bridge-OB38S003_PassthroughMode.hex`, the red LED on Sonoff should light up once at startup.

### Manual Mode:

1. Connect Arduino I2C/GPIO pins to the microcontroller while unpowered (it's okay for the ground to be connected on the target; software or hardware I2C can be used - see projectDefs.h).
2. Upload the sketch to ESP8265/ESP8266/ESP32.
3. Set serial monitor to "Both NL & CR" at 115200 baud.
4. Type "handshake" into the serial monitor.
5. Power on the target microcontroller with 3.3V.
6. The serial monitor should display 'Handshake succeeded' along with chip type as (0xA). If not, follow instructions to retry.
7. Type 'erase' command since the microcontroller is likely protected (this erases flash, cannot be recovered!).
8. Type "setfuse 18 249" (sets reset pin as reset functionality rather than GPIO).
9. Copy-paste hex lines starting with ':' into the serial monitor and hit the enter key.
10. Successful or failed writes should be displayed in the serial monitor.
11. If all successful, type "mcureset" to reset the microcontroller.
12. For `blink.ihx`, the red LED on the Sonoff target should begin blinking with a one-second period.
13. For `RF-Bridge-OB38S003_PassthroughMode.hex`, the red LED on Sonoff should light up once at startup.

