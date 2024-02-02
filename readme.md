## Background
This is intended to allow flashing 8051 based OBS38S003 microcontrollers as a target.  

For example, the ESP8285 present in the Sonoff RF Bridge R2 v2.2 serves as the source flasher.  
The USBRXD pin is bridged to SCL, while the USBTXD pin is bridged to SDA.  

The sketch has successfully flashed with blink.ihx program.  

## Downsides
One downside is that currently each hex file line must be copy and pasted into the serial monitor individually.  
Additionally, handshaking is apparently performed at microcontroller power up or restart.  
Unfortunately, the reset function on the reset pin of the Sonoff RF Bridge is probably disabled.  
Therefore, for the first use of this sketch an independently powered Arduino chip would need to be used as source flasher.  

## Status
| Item | Status | Note | 
| ------------- | ------------- | ------------- | 
|  Reading/writing configuration bits | PARTIAL  | Could be easier | 
|  Read flash memory | TODO  | none | 
|  Verify flash memory | TODO  | none | 

## Usage
[1]  Connect Arduino I2C pins to microcontroller while unpowered (ok for ground to be connected on target; software I2C is used so GPIO can be used)  
[2]  Upload sketch to ESP8265/ESP8266/ESP32.  
[3]  Set serial monitor to "Both NL & CR" at 115200 baud  
[4]  Type "handshake" into serial monitor.  
[5]  Power on target microcontroller with ground and 3.3V.  
[6]  Serial monitor should display 'Handshake complete' along with chip type as (0xA), if not follow instructions to retry  
[7]  Type 'erase' command since microcontroller is likely protected (this erases flash, cannot be recovered!)  
[8]  Type "setfuse 18 249"  (sets reset pin as reset functionality rather than gpio)  
[9]  Copy-paste hex lines starting with ':' into serial monitor and hit enter key  
[10]  Successful or failed writes should be displayed in serial monitor  
[11] If all succcessful, type "mcureset" to reset microcontroller  
[12] For blink.ihx red LED on Sonoff target should begin blinking with one second period  
