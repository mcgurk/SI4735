
/*
   This sketch tries to apply patches on Si4735 devices.
   In this context, a patch is a piece of software used to change the behavior of the SI4735 device.

   There is little information available about patching the SI4735. The following information is the understanding
   of the author of this sketch and is not necessarily correct.

   A patch is executed internally (run by internal MCU) of the device. Usually, patches are  used to fixes bugs
   or add improvements and  new features of the firmware installed in the internal ROM of the device.
   Patches to the SI4735 are distributed in binary form and have to be transferred to the internal RAM of the device
   by the host MCU (in this case Arduino).
   Since the RAM is volatile memory, the patch stored into the device gets lost when you turn off the system.
   Consequently, the content of the patch has to be transferred again to the device each time after turn on the
   system or reset the device.

   ATTENTION:
   The author of this sketch does not guarantee that this procedure will work in your development environment.
   Given this, it is at your own risk to continue with the procedures suggested here.
   This sketch works with the I2C communication protocol and is designed to apply a SSB extension PATCH to CI
   SI4735-D60. Once again, the author disclaims any liability for any damage this procedure may cause to your
   SI4735 or other devices that you are using. If you are sure about it and still want to apply the patch,
   set the variable "APPLY_PATCH to true".

   By Ricardo Lima Caratti, Nov 2019

*/

#include <Wire.h>
#include "patch_content.h"

#define SI473X_ADDR   0x11    // SI473X I2C buss address
#define POWER_UP      0x01    // Power up device and mode selection.
#define POWER_DOWN    0x11    // Power down device.
#define SET_PROPERTY  0x12    // Sets the value of a property.
#define SSB_TUNE_FREQ 0x40    // Tunes the SSB receiver to a frequency between 520 and 30 MHz in 1 kHz steps.

#define RESET_PIN 12


// Set the variable below to true if you want to apply the patch.
bool APPLY_PATCH = false;
bool FIRMWARE_OK = false;

const int size_content_initialization = sizeof ssb_patch_content_initialization;
const int size_content_full = sizeof ssb_patch_content_full;

byte firmwareInfo[8];

typedef union {
  struct {
      byte lowByte;
      byte highByte;
  } raw;
  unsigned value;
} unsigned_2_bytes;


 /*
 * SSB_TUNE_FREQ data type command
 * AN332 REV 0.8 UNIVERSAL PROGRAMMING GUIDE AMENDMENT FOR SI4735-D60 SSB AND NBFM PATCHES; 
 */
    typedef union {
    struct
    {
        byte DUMMY1 : 6; // Always set 0
        byte USBLSB : 2; // SSB Upper Side Band (USB) and Lower Side Band (LSB) Selection. 10 = USB is selected; 01 = LSB is selected.
        byte FREQH;   // ARG2 - Tune Frequency High Byte.
        byte FREQL;   // ARG3 - Tune Frequency Low Byte.
        byte ANTCAPH; // ARG4 - Antenna Tuning Capacitor High Byte. 
        byte ANTCAPL; // ARG5 - Antenna Tuning Capacitor Low Byte. Note used for FM.
    } arg;
    byte raw[5];
} si47x_set_frequency;


void setup() {

  Serial.begin(9600);
  while (!Serial);

  if ( !APPLY_PATCH ) {
    Serial.println("Set the APPLY_PATCH variable to true if you really want to run this sketch and upload it again!");
    while (1);
  } else {
    firmwarePowerUp();
    showFirmwareInformation();
    if ( !FIRMWARE_OK ) {
      Serial.println("Check if your firmware is compatible! If so, set FIRMWARE_OK to true and uplaod this sketch again.");
      while (1);
    }
    // Aplay the patch
    patchPowerUp();
    downloadPatch();
    // Is SSB alive?  
    ssbPowerUp()
    setSSB();

    
  }

}

/*
   Reset the device
*/
void reset() {
  pinMode(RESET_PIN, OUTPUT);
  delayMicroseconds(500);
  digitalWrite(RESET_PIN, LOW);
  delay(500);
  digitalWrite(RESET_PIN, HIGH);
  delay(500);
}


/*
 * Show firmware information
 */
void showFirmwareInformation() {
  Serial.println("Firmware Information"); 
  Serial.print("Final 2 digits of Part Number (HEX).: ");
  Serial.println(firmwareInfo[1],HEX);
  Serial.print("Firmware Major Revision (ASCII).....: ");
  Serial.println((char)firmwareInfo[2]);  
  Serial.print("Firmware Minor Revision (ASCII).....: ");
  Serial.println((char)firmwareInfo[3]);  
  Serial.print("Chip Revision (ASCII)...............: ");
  Serial.println((char)firmwareInfo[6]);  
  Serial.print("Library Revision (HEX)..............: ");
  Serial.println(firmwareInfo[7],HEX);    
}

/*
   Wait for the si473x gets ready (Clear to Send status bit have to be 1).
*/
inline void waitCTS() {
  do
  {
    delayMicroseconds(600);
    Wire.requestFrom(SI473X_ADDR, 1);
  } while (!(Wire.read() & B10000000));
}

void powerDown() {
  waitCTS();
  Wire.beginTransmission(SI473X_ADDR);
  Wire.write(POWER_DOWN);
  Wire.endTransmission();
}

/*
   Normal Start Up the SI4735 (AM FUNCTION)
   See Si47XX PROGRAMMING GUIDE; AN332; page 129
*/
void ssbPowerUp() {
  Wire.beginTransmission(SI473X_ADDR);
  Wire.write(POWER_UP);
  Wire.write(0b00010001); // Set to AM/SSB, disable interrupt; disable GPO2OEN; boot normaly; enable External Crystal Oscillator  .
  Wire.write(0b00000101); // Set to Analog Line Input.
  Wire.endTransmission();
  waitCTS();
}

/*
   The first command that is sent to the device is the POWER_UP command to confirm that
   the patch is compatible with the internal device library revision.
   See Si47XX PROGRAMMING GUIDE; AN332; pages 64 and 215-220.
*/
void firmwarePowerUp() {

  waitCTS();

  Wire.beginTransmission(SI473X_ADDR);
  Wire.write(POWER_UP);
  Wire.write(0b00011111); // Set to Read Library ID, disable interrupt; disable GPO2OEN; boot normaly; enable External Crystal Oscillator  .
  Wire.write(0b00000101); // Set to Analog Line Input.
  Wire.endTransmission();

  waitCTS();
  // Shows firmware information
  Wire.requestFrom(SI473X_ADDR, 8);
  for (int i = 0; i < 8; i++)
      firmwareInfo[i] = Wire.read(); 
}

/*
   Powerup the device by issuing the POWER_UP command with FUNC = 1 (AM/SW/LW Receive) together with the specific SSB patch.
   See Si47XX PROGRAMMING GUIDE; AN332; pages 64 and 215-220 and
   AN332 REV 0.8 UNIVERSAL PROGRAMMING GUIDE AMENDMENT FOR SI4735-D60 SSB AND NBFM PATCHES; page 7.
*/
void patchPowerUp() {
  Wire.beginTransmission(SI473X_ADDR);
  Wire.write(POWER_UP);
  Wire.write(0b00110001); // Set to AM, Enable External Crystal Oscillator; Set patch enable; GPO2 output disabled; CTS interrupt disabled.
  Wire.write(0b00000101); // Set to Analog Output
  Wire.endTransmission();
  waitCTS();
}

/*
   Transfer the content of the patch (ssb_patch_content_full) to the SI4735
*/
void downloadPatch() {
  byte content, cmd_status;
  int i, line, offset;
  Serial.println("Applying patch...");
  // Send patch for whole SSBRX full download
  for (offset = 0; offset < size_content_full; offset += 8)
  {
    line++;
    Wire.beginTransmission(SI473X_ADDR);
    for (i = 0; i < 8; i++)
    {
      content = pgm_read_byte_near(ssb_patch_content_full + (i + offset));
      Wire.write(content);
    }
    Wire.endTransmission();
    waitCTS();
    Wire.requestFrom(SI473X_ADDR, 1);
    cmd_status = Wire.read();
    if (cmd_status != 0x80) {
      Serial.print("Status/Error: ");
      Serial.print(cmd_status, BIN);
      Serial.print("; linha: ");
      Serial.print(line);
      Serial.print("; offset: ");
      Serial.println(offset + i);
    }
    waitCTS();
  }
  Serial.println("Patch applied!");
}

/*
    Sets the SI4735 to work on SSB
*/
void setSSB() {

    unsigned_2_bytes ssb_mode, property;

    waitCTS();

    // Set SSB MODE
    // AUDIOBW      = 2 - 3.0 kHz low-pass filter (4 bits).
    // SBCUTFLT     = 1 - Low pass filter to cutoff the unwanted side band (4 bits)
    // AVC divider  = 0 - for SSB mode, set divider = 0 (4 bits)
    // AVCEN        = 1 - Enable AVC (default) (1 bit)
    // SMUTESEL     = 0 - Soft-mute based on RSSI (default) (1 bit) 
    // Reserved     = 0 - Always 0 (1 bit)
    // DSP AFCDIS   = 1 - SSB mode, AFC disable (1 bit)
    ssb_mode.value = 0b1001000000010010;
    property.value = 0x0101; // SSB_MODE (page 24 of the AN332 REV 0.8 UNIVERSAL PROGRAMMING GUIDE AMENDMENT FOR SI4735-D60 SSB AND NBFM PATCHES;

    Wire.beginTransmission(SI473X_ADDR);
    Wire.write(SET_PROPERTY);
    Wire.write(0x00);                  // Always 0x00
    Wire.write(property.raw.highByte); // High byte first
    Wire.write(property.raw.lowByte);  // Low byte after
    Wire.write(ssb_mode.raw.highByte); // high byte first
    Wire.write(ssb_mode.raw.lowByte);  // low byte after

    Wire.endTransmission();
    delayMicroseconds(550);

}

/*
   Sets the frequency of the receiver
*/
void setFrequency(unsigned frequency, byte usblsb) {

     unsigned_2_bytes ssb_frequency;
     si47x_set_frequency set_freq; 

     ssb_frequency.value = frequency;

    set_freq.arg.DUMMY1 = 0;
    set_freq.arg.USBLSB = usblsb; // 1 = LSB and 2 = USB
    set_freq.arg.FREQH = ssb_frequency.raw.highByte;
    set_freq.arg.FREQL = ssb_frequency.raw.lowByte;
    set_freq.arg.ANTCAPH = 0;
    set_freq.arg.ANTCAPL = 1; 

    waitCTS();

    Wire.beginTransmission(SI473X_ADDR);
    Wire.write(SSB_TUNE_FREQ);
    Wire.write(set_freq.raw[0]); // Send byte with USBLSB (1 = LSB and 2 = USB)
    Wire.write(set_freq.arg.FREQH);
    Wire.write(set_freq.arg.FREQL);
    Wire.write(set_freq.arg.ANTCAPH);    
    Wire.write(set_freq.arg.ANTCAPL);
    Wire.endTransmission();
    delayMicroseconds(550);       


}

/*
 * Sets the BFO offset
 * AN332 REV 0.8 UNIVERSAL PROGRAMMING GUIDE AMENDMENT FOR SI4735-D60 SSB AND NBFM PATCHES; page 24.
*/
void setBFO(unsigned offset) {

    unsigned_2_bytes bfo_offset, property;

    property.value = 0x0100; 
    bfo_offset.value = offset;
    
    waitCTS();

    Wire.beginTransmission(SI473X_ADDR);
    Wire.write(SET_PROPERTY);
    Wire.write(0x00);                    // Always 0x00
    Wire.write(property.raw.highByte);   // High byte first
    Wire.write(property.raw.lowByte);    // Low byte after
    Wire.write(bfo_offset.raw.highByte); // High byte first
    Wire.write(bfo_offset.raw.lowByte);  // Low byte after    
    Wire.endTransmission();
    delayMicroseconds(550);
}


void loop() {


}
