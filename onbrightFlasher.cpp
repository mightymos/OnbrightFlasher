/*
  OnbrightFlasher.cpp -  implementation
  Copyright (c) 2023 Jonathan Armstrong.  All right reserved.
*/

// include core Wiring API
//#include "WProgram.h"

// include this library's description file
#include "onbrightFlasher.h"

// include description files for other libraries used (if any)
//#include "HardwareSerial.h"

#include <SoftWire.h>


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
// should be 0x0A for OnBright OBS38S003 8051 based microcontroller

bool OnbrightFlasher::onbrightHandshake(SoftWire sw)
{
  SoftWire::result_t result;
  SoftWire::mode_t   mode;
  uint8_t addr;

  uint8_t index;

  bool gotAck = false;

  // indicate a write
  addr = WRITE_ADDRESS;
  mode = SoftWire::writeMode;
  result = sw.llStart((addr << 1) + mode);
  sw.stop();

  // the official programmer has to retry until ack is received
  for (index = 0; index < MAX_HANDSHAKE_RETRIES; index++)
  {
    addr = RESET_CHIP;
    mode = SoftWire::writeMode;
    result = sw.llStart((addr << 1) + mode);
    sw.stop();

    if (result == SoftWire::ack) {
      addr = HANDSHAKE01;
      mode = SoftWire::writeMode;
      result = sw.llStart((addr << 1) + mode);
      sw.stop();

      addr = HANDSHAKE02;
      mode = SoftWire::writeMode;
      result = sw.llStart((addr << 1) + mode);
      sw.stop();

      // send read address command, however no actual read seems to be performed
      addr = WRITE_ADDRESS;
      mode = SoftWire::readMode;
      result = sw.llStart((addr << 1) + mode);

      // FIXME: do we read or generate ack after sending read command?
      sw.stop();

      // ack indicates that onbright microcontroller is responding to handshake
      gotAck = true;

      // force exit from loop
      index = MAX_HANDSHAKE_RETRIES;
    }
  }

  return gotAck;
}

bool OnbrightFlasher::readChipType(SoftWire sw, unsigned char& chipType)
{
  SoftWire::mode_t mode;
  uint8_t addr;
  uint8_t result;

  bool flag = true;

  // check chip type
  addr = WRITE_ADDRESS;
  mode = SoftWire::writeMode;
  result = sw.llStart((addr << 1) + mode);
  if (result == SoftWire::nack) {
    // we need to still fall through to send stop condition
    // or send stop condition every time nack is found
    flag = false;
  }

  result = sw.llWrite(READ_CONFIG_BYTE);
  if (result == SoftWire::nack) {
    flag = false;
  }

  result = sw.llWrite(CHIP_TYPE_BYTE);
  if (result == SoftWire::nack) {
    flag = false;
  }

  sw.stop();

  addr = READ_ADDRESS;
  mode = SoftWire::readMode;
  result = sw.llStart((addr << 1) + mode);
  if (result == SoftWire::nack) {
    flag = false;
  }

  // send nack since there's only one byte to read
  result = sw.llRead(chipType, false);
  if (result == SoftWire::nack) {
    flag = false;
  }
  
  sw.stop();

  return flag;
}

bool OnbrightFlasher::resetMCU(SoftWire sw)
{
  SoftWire::mode_t mode;
  uint8_t addr;
  uint8_t result;

  uint8_t unknownData;

  bool flag = true;

  //
  addr = RESET_CHIP;
  mode = SoftWire::readMode;
  result = sw.llStart((addr << 1) + mode);
  if (result == SoftWire::nack) {
    flag = false;
  }

  sw.stop();

  return flag;
}

bool OnbrightFlasher::eraseChip(SoftWire sw)
{
  SoftWire::mode_t mode;
  uint8_t addr;
  uint8_t result;

  bool flag = true;

  // erase chip
  addr = WRITE_ADDRESS;
  mode = SoftWire::writeMode;
  result = sw.llStart((addr << 1) + mode);
  if (result == SoftWire::nack) {
    flag = false;
  }

  result = sw.llWrite(ERASE_CHIP);
  if (result == SoftWire::nack) {
    flag = false;
  }

  sw.stop();

  return flag;
}


bool OnbrightFlasher::readConfigByte(SoftWire sw, const unsigned char address, unsigned char &configByte)
{
  SoftWire::mode_t mode;
  uint8_t addr;
  uint8_t result;

  bool flag = true;

  // check chip type
  addr = WRITE_ADDRESS;
  mode = SoftWire::writeMode;
  result = sw.llStart((addr << 1) + mode);
  if (result == SoftWire::nack) {
    flag = false;
  }

  result = sw.llWrite(READ_CONFIG_BYTE);
  if (result == SoftWire::nack) {
    flag = false;
  }

  result = sw.llWrite(address);
  if (result == SoftWire::nack) {
    flag = false;
  }

  sw.stop();

  addr = READ_ADDRESS;
  mode = SoftWire::readMode;
  result = sw.llStart((addr << 1) + mode);
  if (result == SoftWire::nack) {
    flag = false;
  }

  result = sw.llRead(configByte, false);

  if (result == SoftWire::nack) {
    flag = false;
  }
  
  sw.stop();

  return flag;
}

bool OnbrightFlasher::writeConfigByte(SoftWire sw, unsigned char address, unsigned char configByte)
{
  SoftWire::mode_t mode;
  uint8_t addr;
  uint8_t result;

  uint8_t index;

  bool flag = true;

  // we save to write twice according to traces from official programmer
  for (index = 0; index < 2; index++)
  {
    // check chip type
    addr = WRITE_ADDRESS;
    mode = SoftWire::writeMode;
    result = sw.llStart((addr << 1) + mode);
    if (result == SoftWire::nack) {
      Serial.println("write address - nack");
      flag = false;
    }

    result = sw.llWrite(WRITE_CONFIG_BYTE);
    if (result == SoftWire::nack) {
      Serial.println("write config byte - nack");
      flag = false;
    }

    result = sw.llWrite(address);
    if (result == SoftWire::nack) {
      Serial.println("config byte addr - nack");
      flag = false;
    }

    sw.stop();

    // FIXME: we send READ even though writing, need to consider what is actually happening
    // see include file too for notes
    addr = READ_ADDRESS;
    mode = SoftWire::writeMode;
    result = sw.llStart((addr << 1) + mode);
    if (result == SoftWire::nack) {
      Serial.println("write address - nack");
      flag = false;
    }

    result = sw.llWrite(configByte);

    if (result == SoftWire::nack) {
      Serial.println("config byte - nack");
      flag = false;
    }
    
    sw.stop();
  }

  return flag;
}



// TODO: might want something that works across entire block size in the future
// read single byte from flash
// there are 16 blocks * 512 bytes = 8192 bytes total flash memory
bool OnbrightFlasher::readFlashByte(SoftWire sw, const unsigned int flashAddress, unsigned char &flashByte)
{
  SoftWire::mode_t mode;

  uint8_t addr;
  uint8_t data;
  uint8_t result;

  unsigned int index;

  uint8_t highByte = (flashAddress >> 8) & 0xff;
  uint8_t lowByte  = flashAddress & 0xff;

  bool flag = true;

  addr = WRITE_ADDRESS;
  mode = SoftWire::writeMode;
  result = sw.llStart((addr << 1) + mode);
  if (result == SoftWire::nack) {
    flag = false;
  }

  result = sw.llWrite(READ_FLASH);
  if (result == SoftWire::nack) {
    flag = false;
  }

  // send address of flash to read
  sw.llWrite(highByte);
  if (result == SoftWire::nack) {
    flag = false;
  }

  sw.llWrite(lowByte);
  if (result == SoftWire::nack) {
    flag = false;
  }

  sw.stop();

  // actually read
  addr = READ_ADDRESS;
  mode = SoftWire::readMode;
  result = sw.llStart((addr << 1) + mode);
  if (result == SoftWire::nack) {
    flag = false;
  }

  // indicate read is done after a single byte read
  result = sw.llRead(flashByte, true);


  // FIXME: check if correct
  sw.stop();

  return flag;
}

// TODO:
bool OnbrightFlasher::writeFlashByte(SoftWire sw, const unsigned int flashAddress, const unsigned char flashByte)
{
  SoftWire::mode_t mode;

  uint8_t addr;
  uint8_t data;
  uint8_t result;

  unsigned int index;

  uint8_t highByte = (flashAddress >> 8) & 0xff;
  uint8_t lowByte  = flashAddress & 0xff;

  bool flag = true;

  addr = WRITE_ADDRESS;
  mode = SoftWire::writeMode;
  result = sw.llStart((addr << 1) + mode);
  if (result == SoftWire::nack) {
    Serial.println("write address failed");
    flag = false;
  }

  // send address of flash to write
  sw.llWrite(WRITE_FLASH);
  if (result == SoftWire::nack) {
    Serial.println("write flash failed");
    flag = false;
  }

  sw.llWrite(highByte);
  if (result == SoftWire::nack) {
    Serial.println("high byte failed");
    flag = false;
  }

  sw.llWrite(lowByte);
  if (result == SoftWire::nack) {
    Serial.println("low byte failed");
    flag = false;
  }

  sw.stop();

  // actually write
  addr = READ_ADDRESS;
  mode = SoftWire::writeMode;
  result = sw.llStart((addr << 1) + mode);
  if (result == SoftWire::nack) {
    Serial.println("read address failed");
    flag = false;
  }

  // data to write to flash address
  result = sw.llWrite(flashByte);
  if (result == SoftWire::nack) {
    Serial.println("write byte failed");
    flag = false;
  }

  // FIXME: check if correct
  sw.stop();

  return flag;
}