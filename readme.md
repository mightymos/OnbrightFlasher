## Background
This is intended to allow flashing 8051 based OBS38S003 microcontrollers as a target.  

For example, the ESP8285 present in the Sonoff RF Bridge R2 v2.2 serves as the source flasher.  
The USBRXD pin is bridged to SCL, while the USBTXD pin is bridged to SDA.  

The sketch has successfully flashed a simple blink program.  

## Downsides
The downside is that currently each hex file line must be copy and pasted into the serial monitor individually.  
Additionally, handshaking is apparently performed at microcontroller power up or restart.  
Unfortunately, the reset function on the reset pin of the Sonoff RF Bridge is probably disabled.  
However, this sketch could be used on an independently powered Arduino chip for the first flash.  

## Todo
[1] Reading/writing configuration bytes
[2] Read flash memory
[3] Verify flash memory

## Usage
[1] Connect Arduino I2C pins to microcontroller  
[2] Power up or reset microcontroller  
[3] Serial monitor should display 'connected' along with chip type as (0xA), if not retry  
[4] Issue 'erase' command since microcontroller is likely protected (this erases flash, cannot be recovered!)  
[5] Copy-paste hex lines into serial monitor set to send NL and CR and send (e.g., with enter key)  
[6] Successful or failed writes should be displayed in serial monitor  

## Help
If anyone really wants to use this, I can try to help make it better.