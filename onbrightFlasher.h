/*
  OnbrightFlasher.h  - description
  Copyright (c) 2023 Jonathan Armstrong.  All right reserved.
*/

// ensure this library description is only included once
#ifndef Onbright_flasher_h
#define Onbright_flasher_h

// for byte type
#include <Arduino.h>

// array sizes
#define CONFIG_BYTES_MAX   255
#define FILE_ARRAY_MAX   32767

// advice on switching between SoftWire and Wire libraries
// [https://arduino-craft-corner.de/index.php/2023/11/29/replacing-the-wire-library-sometimes/]

// notes
// setfuse 18 249
// reset function, input only, no deglitch

// chip ID for OBS38S03
#define RESET_CHIP    0x7c
#define HANDSHAKE01   0x7d
#define HANDSHAKE02   0x2d

// FIXME: need to rename because sometimes we send READ even when writing as per official programmer
#define DEVICE_ADDRESS 0x7e
#define DATA_ADDRESS  0x7f

// chip commands
#define ERASE_CHIP  0x03
#define WRITE_FLASH 0x05
#define READ_FLASH  0x06
#define WRITE_CONFIG_BYTE 0x08
#define READ_CONFIG_BYTE  0x09
//

// config bytes
#define CHIP_TYPE_BYTE 0x00

#define CONFIG_BYTE01 0x11
#define CONFIG_BYTE02 0x12
#define CONFIG_BYTE03 0x13
#define CONFIG_BYTE04 0x14
#define CONFIG_BYTE05 0x15
#define CONFIG_BYTE06 0x16
#define CONFIG_BYTE07 0x17

// Checksum is calculated as a SUM of all FLASH bytes? Same is for EEPROM?
#define FLASH_CHECKSUM01 0x2c
#define FLASH_CHECKSUM02 0x2d
#define FLASH_CHECKSUM03 0x2e
#define FLASH_CHECKSUM04 0x2f

// seems to be enough to achieve handshake
#define MAX_HANDSHAKE_RETRIES 10

// target flash memory addresses
#define BLOCK_SIZE 512

enum { block00 = 0x0000,
       block01 = 0x0200,
       block02 = 0x0400,
       block03 = 0x0600,
       block04 = 0x0800,
       block05 = 0x0A00,
       block06 = 0x0C00,
       block07 = 0x0E00,
       block08 = 0x1000,
       block09 = 0x1200,
       block10 = 0x1400,
       block11 = 0x1600,
       block12 = 0x1800,
       block13 = 0x1A00,
       block14 = 0x1C00,
       block15 = 0x1E00
};

// library interface description
class OnbrightFlasher
{
  // user-accessible "public" interface
  public:

    byte eraseChip(void);
    bool onbrightHandshake(void);

    byte readConfigByte(const unsigned char address, unsigned char &configByte);
    byte writeConfigByte(const unsigned char address, const unsigned char configByte);

    byte readConfigBlock(const unsigned char address, unsigned char (&configByte)[CONFIG_BYTES_MAX], const unsigned char length);

    byte readFlashByte(const unsigned int address, unsigned char &flashByte);
    byte writeFlashByte(const unsigned int address, const unsigned char flashByte);

    byte readFlashBlock(const unsigned int flashAddress, unsigned char (&flashbyte)[FILE_ARRAY_MAX], const unsigned int length);
    byte writeFlashBlock(const unsigned int flashAddress, unsigned char* flashbyte, const unsigned int length);

    byte readChipType(unsigned char& chipType);
    void resetMCU(void);

  // library-accessible "private" interface
  private:
    int dummyVariable;
};

#endif

