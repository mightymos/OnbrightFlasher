## Background
This is intended to allow flashing 8051 based OBS38S003 microcontrollers as a target.  

For example, the ESP8285 present in the Sonoff RF Bridge R2 v2.2 serves as the source flasher.  
The USBRXD pin is bridged to SCL, while the USBTXD pin is bridged to SDA.  

The sketch has successfully flashed a simple blink program.  

## Downsides
First, the reset function on the reset pin of the Sonoff RF Bridge is probably disabled.  
Additionally, handshaking is apparently performed at microcontroller power up or restart.  
Therefore, for the first use of this sketch an independently powered Arduino chip would need to be used as source flasher.  
Once setfuse is changed (see below) the ESP8285 on the Sonoff itself could be used later to reflash the microcontroller.  

Second, each hex file line must be copy and pasted into the serial monitor individually.  
A script is being contributed that would make file upload automatic.  

## Flashers
| Board | Status | Note | 
| ------------- | ------------- | ------------- | 
|  ESP8265 (e.g. in Sonoff) | WORKING  | none | 
|  ESP8266 (e.g. Wemos D1 Mini) | WORKING  | none | 
|  ESP-WROOM-32 | WORKING  | none | 
|  ESP32S3 | WORKING  | none | 
|  Arduino Mega 2560 board | WORKING | 5V to 3.3V level translation recommended |

## Status
| Item | Status | Note | 
| ------------- | ------------- | ------------- | 
|  Write flash memory | DONE  | Manually one byte or one hex line at a time | 
|  Read flash memory | DONE  | Manually one byte at a time | 
|  Reading/writing configuration bits | PARTIAL  | Need read-modify-write scheme | 
|  Verify flash memory | TODO  | none | 
## Usage

### Hardware setup
1. Connect Arduino I2C/GPIO pins to the microcontroller while unpowered.  
2. Target microcontroller can be powered by an independent supply, or from 3.3V output regulator on Arduino board.  
3. Using output regulators on Arduino boards seems to lead to power up glitches (e.g., serial monitor disconnects, handshake fails).  
4. Target should remain unpowered until handshake is started.  
5. Specify software or hardware I2C in project - see projectDefs.h.  
6. Upload the sketch to ESP8265/ESP8266/ESP32.  

### Script Mode:
1. Copy the desired hex file from [GitHub Releases](https://github.com/mightymos/RF-Bridge-OB38S003/releases) to the script directory.
2. Run `flashScript.py` and follow the instructions in the console.
3. For `blink.ihx`, the red LED on the Sonoff target should begin blinking with a one-second period.
4. For `RF-Bridge-OB38S003_PassthroughMode.hex`, the red LED on Sonoff should light up once at startup.

### Manual Mode:

1. Set serial monitor to "Both NL & CR" at 115200 baud.
2. Type "handshake" into the serial monitor.
3. Power on the target microcontroller with 3.3V.
4. If chip is protected, chip read will appear to fail due to NACK but chip type reported should be (0xA). Proceed to 'erase' step to unprotect chip.
5. If chip is unprotected, serial monitor should display 'Handshake succeeded' along with chip type as (0xA). If not, follow instructions to retry.
6. Type 'erase' command since the microcontroller is likely protected (this erases flash, cannot be recovered!).
7. Type "setfuse 18 249" (sets reset pin as reset functionality rather than GPIO).
8. Copy-paste hex lines starting with ':' into the serial monitor and hit the enter key.
9. Successful or failed writes should be displayed in the serial monitor.
10. If all successful, type "mcureset" to reset the microcontroller.
11. For `blink.ihx`, the red LED on the Sonoff target should begin blinking with a one-second period.
12. For `RF-Bridge-OB38S003_PassthroughMode.hex`, the red LED on Sonoff should light up once at startup.

### Web Upload Mode (WARNING: NEEDS TESTING!):
1. Set gateway, dns, local IP, SSID and password in Network.h
2. Upload sketch
3. Follow the Manual Mode handshake instructions to handshake and erase chip
4. Navigate to 192.168.10.150 in browser
5. Upload "firmware.hex" (exact file name and in hex format - i.e., ihx format will not work (use packihx if needed); for now must use Unix line endings LF (use dos2unix if needed))
6. Type "flashhex" in serial monitor

## More in depth [flashing guide by example](flashing-guide-by-example.md). ##