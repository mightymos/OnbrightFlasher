## Background
This is intended to allow flashing 8051 based OBS38S003 microcontrollers as a target.  

An external ESP8266/ESP32 module or similar serves as the flasher.

Note that the reset function on the reset pin of the stock Sonoff RF Bridge is probably disabled.  
In other words, the pin is configured as GPIO by fuse and thus the target cannot be held in reset.  
Handshaking is apparently performed at microcontroller power up or restart.  
Therefore, the flasher is independently powered while a second power source is used to the target.  


Once setfuse is changed (see below) the ESP8285 on the Sonoff itself could be used to reflash the microcontroller.  
However, this would require soldering a wire to the reset pad.  
Additionally, the serial pins communicating with the Arduino serial monitor mighty be interferred with by microcontroller activity.  
The original Sonoff black case with EFM8BB1 allowed microcontroller reset by a long pulse on an C2D pin but that is not available here.  
It is probably easiest to just use an external flasher.  

For manual programming each HEX line must be pasted into the serial monitor individually.  
A script has been contributed that would make file upload automatic.  

The sketch has successfully flashed a simple blink program.  


## Flashing using the Sonoff's internal ESP8285
For example, the ESP8285 present in the Sonoff RF Bridge R2 v2.2 serves as the source flasher.  
The USBRXD pin is bridged to SCL, while the USBTXD pin is bridged to SDA.  
The reset pad would need to be soldered with a wire, though the datasheet is unclear if reset is active low or active high.  
It is probably easiest to just use an external programmer unfortunately.  

## External Flashers
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
|  Verify flash memory | TODO  | checksums displayed as a basic check | 
## Usage

### Preparing the external flasher
1. Grab an external ESP8266 (NodeMCU/D1 mini), ESP32, or Arduino Mega 2560 board
2. Download the current source code, open "OnbrightFlasher.ino" into arduino IDE, and then compile and upload it to the external flasher
3. Done

### Flashing the radio chip
1. Connect ESP or Arduino I2C/GPIO pins to the microcontroller while unpowered.  
2. Target microcontroller can be powered by an independent supply,  from 3.3V output regulator on Arduino board, or by powering the RFbridge via its USB port.  
3. Using output regulators on Arduino boards seems to lead to power up glitches (e.g., serial monitor disconnects, handshake fails).  
4. Target should remain unpowered until handshake is started.
5. Excute "FalshScript.py" and choose the desired [firmware](https://github.com/mightymos/RF-Bridge-OB38S003/releases/) to upload. When prompted, supply power to the radio chip.
6. Wait for flashing to finish.
7. Done


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
Not included at this time.


## More in depth [flashing guide by example](flashing-guide-by-example.md). ##
