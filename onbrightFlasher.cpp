/*
  OnbrightFlasher.cpp -  implementation
  Copyright (c) 2023 Jonathan Armstrong.  All right reserved.
*/

// include this library's description file
#include "onbrightFlasher.h"

#if defined(USE_SOFTWIRE_LIBRARY)
  // softwire I2C
  // uncomment either softwire or wire
  #include <SoftWire.h>
  extern SoftWire Wire;
#elif defined (USE_WIRE_LIBRARY)
  #include <Wire.h>
#endif

// Constructor /////////////////////////////////////////////////////////////////
// Function that handles the creation and setup of instances

//OnbrightFlasher::OnbrightFlasher(void)
//{
//  //FIXME: need to do anything here?
//}

// Public Methods //////////////////////////////////////////////////////////////
// Functions available in Wiring sketches, this library, and other libraries


// Private Methods /////////////////////////////////////////////////////////////
// Functions only available to other functions in this library

bool OnbrightFlasher::onbrightHandshake(void)
{
  byte result;
  unsigned int index;

  bool gotFirstAck = false;

  // indicate a write
  Wire.beginTransmission(DEVICE_ADDRESS);
  result = Wire.endTransmission();

  for (index = 0; index < MAX_HANDSHAKE_RETRIES; index++)
  {
    Wire.beginTransmission(RESET_CHIP);
    result = Wire.endTransmission();

    // if we received ack, proceed with the rest of the handshake
    if (result == 0)
    {
      Wire.beginTransmission(HANDSHAKE01);
      result = Wire.endTransmission();
      Wire.beginTransmission(HANDSHAKE02);
      result = Wire.endTransmission();

      // no actual read seems to be performed
      Wire.requestFrom(DEVICE_ADDRESS, 1);
      result = Wire.endTransmission();

      //Serial.print("Retried times: ");
      //Serial.println(index);
      //Serial.println("Tried handshake 01+02+Read");
      //Serial.print("Result: ");
      //Serial.println(result);

      gotFirstAck = true;

      index = MAX_HANDSHAKE_RETRIES;
    }
  }

  return gotFirstAck;
}

// should be 0x0A for OnBright OBS38S003 8051 based microcontroller
byte OnbrightFlasher::readChipType(unsigned char& chipType)
{
  // used to check for nack/ack, success, etc.
  byte result;
  //byte numBytes;

  // check chip type
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(READ_CONFIG_BYTE);
  Wire.write(CHIP_TYPE_BYTE);

  result = Wire.endTransmission();

  // this returns number of bytes so could use that
  Wire.requestFrom(DATA_ADDRESS, 1);
  while(Wire.available() > 0)
  {
    chipType = Wire.read();
  }

  return result;
}

void OnbrightFlasher::resetMCU(void)
{
  // FIXME: reset command not able to return any indication of success/failure?
  // for example, no ack or nack
  //byte result = 0;

  // we do not actually read anything
  Wire.requestFrom(RESET_CHIP, 1);

  //return result;
}

byte OnbrightFlasher::eraseChip(void)
{
  byte result;

  // erase chip
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(ERASE_CHIP);
  result = Wire.endTransmission();

  return result;
}

// common fuses as a check
// address zero should read 10 (0xA) which is the chip type
byte OnbrightFlasher::readConfigByte(const unsigned char address, unsigned char &configByte)
{
  byte result;

  //
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(READ_CONFIG_BYTE);
  Wire.write(address);
  result = Wire.endTransmission();

  Wire.requestFrom(DATA_ADDRESS, 1);

  while(Wire.available() > 0)
  {
    configByte = Wire.read();
  }

  return result;
}

byte OnbrightFlasher::writeConfigByte(const unsigned char address, const unsigned char configByte)
{
  byte result;
  unsigned int index;

  // we save to write twice according to traces from official programmer
  for (index = 0; index < 2; index++)
  {
    Wire.beginTransmission(DEVICE_ADDRESS);
    Wire.write(WRITE_CONFIG_BYTE);
    Wire.write(address);
    Wire.endTransmission();

    // FIXME: we send READ even though writing, need to consider what is actually happening
    // see include file too for notes
    Wire.beginTransmission(DATA_ADDRESS);
    Wire.write(configByte);
    result = Wire.endTransmission();
  }

  return result;
}



// TODO: might want something that works across entire block size in the future
// read single byte from flash
// there are 16 blocks * 512 bytes = 8192 bytes total flash memory
byte OnbrightFlasher::readFlashByte(const unsigned int flashAddress, unsigned char &flashByte)
{
  const uint8_t highByte = (flashAddress >> 8) & 0xff;
  const uint8_t lowByte  = flashAddress & 0xff;

  byte result;

  //
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(READ_FLASH);
  Wire.write(highByte);
  Wire.write(lowByte);
  result = Wire.endTransmission();

  Wire.requestFrom(DATA_ADDRESS, 1);

  while(Wire.available() > 0)
  {
    flashByte = Wire.read();
  }

  return result;
}

// TODO:
byte OnbrightFlasher::writeFlashByte(const unsigned int flashAddress, const unsigned char flashByte)
{
  const uint8_t highByte = (flashAddress >> 8) & 0xff;
  const uint8_t lowByte  = flashAddress & 0xff;

  byte result;

  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(WRITE_FLASH);
  Wire.write(highByte);
  Wire.write(lowByte);
  Wire.endTransmission();

  Wire.beginTransmission(DATA_ADDRESS);
  Wire.write(flashByte);
  result = Wire.endTransmission();

  return result;
}