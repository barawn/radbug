// the LED is on LED_BUILTIN
#include "cmdArduino.h"
#include "Wire.h"

/////////////////////////////////////////////////////////////////////
// JTAG PINS
/////////////////////////////////////////////////////////////////////
#define CTRLB_B 2
#define ARD_TCK 3
#define ARD_TDI 4
#define CTRLA_B 9

/////////////////////////////////////////////////////////////////////
// NOTE: GREENPAK INTERFACING
/////////////////////////////////////////////////////////////////////
// The I2C outputs are at byte 0x7A and bit-reversed (OUT0 is bit 7, OUT7 is bit 0)
// yes I know they say 'Virtual Input' there, they're outputs
/////////////////////////////////////////////////////////////////////
#define GP_CTRL 0x7A
// bit mask for SEL_ARD (select Arduino Tck/Tdi control)   - OUT0 (bit 7)
#define GP_SEL_ARD 0x80
// bit mask for ENA_B (enable A-bar)                       - OUT1 (bit 6)
#define GP_ENA_B 0x40
// bit mask for ENB_B (enable B-bar)                       - OUT2 (bit 5)
#define GP_ENB_B 0x20

// this is our DEFAULT device address
uint8_t deviceAddress = 1;
// this is what we think connector A's config is
uint8_t ctrlA = 0x00;
// this is what we think connector B's config is
uint8_t ctrlB = 0x00;

/////////////////////////////////////////////////////////////////////
// NOTE: GREENPAK LED DEBUGGING DECODING
/////////////////////////////////////////////////////////////////////
// The LED pattern is
// LONG BLINK
// (blink if JTAG A is enabled)
// (blink if JTAG B is enabled)
// (blink if SEL_ARD is enabled)

// this is so violently dumb
// this whole thing is a 256-byte table (16 strings of 16 bytes worth of characters)
const char nvmstring0[] PROGMEM = "5A765B356A5F427207CA3D1DF651D45A"; // 00 - 0F
const char nvmstring1[] PROGMEM = "866919F6FF1F103E50A4651800380000"; // 10 - 1F
const char nvmstring2[] PROGMEM = "00000000000000000000000000000000"; // 20 - 2F
const char nvmstring3[] PROGMEM = "00000000C0480040FDF50400F60F0000"; // 30 - 3F
const char nvmstring4[] PROGMEM = "50FD0070FE0000000000000000000000"; // 40 - 4F
const char nvmstring5[] PROGMEM = "00000000000000000000000000000000"; // 50 - 5F
const char nvmstring6[] PROGMEM = "00303000808070000000808070000060"; // 60 - 6F
const char nvmstring7[] PROGMEM = "60006008000000000000000000000000"; // 70 - 7F
const char nvmstring8[] PROGMEM = "0000F902001422300C00000000000000"; // 80 - 8F
const char nvmstring9[] PROGMEM = "41840000ACACACAC8040000038060000"; // 90 - 9F
const char nvmstringA[] PROGMEM = "00010020000100002002011000070002"; // A0 - AF
const char nvmstringB[] PROGMEM = "00000201000002000100000201000002"; // B0 - BF
const char nvmstringC[] PROGMEM = "00010000020001000000010101000000"; // C0 - CF
const char nvmstringD[] PROGMEM = "00000000000000000000000000000000"; // D0 - DF
const char nvmstringE[] PROGMEM = "00000000000000000000000000000000"; // E0 - EF
const char nvmstringF[] PROGMEM = "000000000000000000000000000000A5"; // F0 - FF

// just so, so, so dumb
const char* const nvmString[16] PROGMEM = {
  nvmstring0,
  nvmstring1,
  nvmstring2,
  nvmstring3,
  nvmstring4,
  nvmstring5,
  nvmstring6,
  nvmstring7,
  nvmstring8,
  nvmstring9,
  nvmstringA,
  nvmstringB,
  nvmstringC,
  nvmstringD,
  nvmstringE,
  nvmstringF
};

////////////////////////////////////////////////////////////////////////////////
// print hex 8 
////////////////////////////////////////////////////////////////////////////////
void PrintHex8(uint8_t data) {
  if (data < 0x10) {
    Serial.print("0");
  }
  Serial.print(data, HEX);
}

// the default here shows up as device id 0001 (0x8) is present
int pingChip(int argc, char **argv) {
  for (int i=0;i<16;i++) {
    Wire.beginTransmission(i<<3);
    Serial.print(F("device 0x"));
    PrintHex8(i);
    Serial.print(F(" ("));
    PrintHex8(i<<3);
    Serial.print(F(") is "));
    if (Wire.endTransmission() != 0) {
      Serial.print(F("not "));
    }
    Serial.println(F("present."));
  }
  return 0;
}

int ackPoll(uint8_t addr) {
  int nack_count;
  nack_count = 0;
  while (nack_count < 1000) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return 0;
    nack_count++;
    delay(1);
  }
  return -1;
}

int progChip(int argc, char **argv) {
  // NOTE NOTE: The NVM configuration acts kindof like flash, except you can "write" a 1, but you cannot
  // "write" a zero (backwards from flash). So a chip erase sets all the contents of the NVM to zero.
  // store the string
  uint8_t addr;
  uint8_t nvmAddr;
  argc--;
  argv++;
  if (!argc) { 
    Serial.println(F("need a device addr"));
    return 0;
  }
  addr = atoi(*argv) << 3;
  nvmAddr = addr | 0x2;
  // First erase the chip. Note that we DO NOT EFFING POWER CYCLE so our address stays the SAME
  // Erase page at a time (16 bytes)
  for (uint8_t i=0;i<16;i++) {
    Serial.print(F("Erasing page: 0x"));
    PrintHex8(i);
    Wire.beginTransmission(addr);
    // ERSR register
    Wire.write(0xE3);
    // Set ERSE[7]=1 (enable), ERSE[4]=0 (NVM), ERSE[3:0] = page to erase
    Wire.write(0x80 | i);    
    // the SLG46824 effs up the ack here, so ignore it
    Wire.endTransmission();
    if (ackPoll(addr) != 0) {
      Serial.println(F(": failed (timeout)!"));
      return 0;
    }
    Serial.println(F(": okay"));
  }
  Serial.print(F("Now using address: 0x"));
  PrintHex8(nvmAddr);
  Serial.println();
  // ASCII maps 30-39 (48-57) to 0-9
  // and        41-46 (65-70) to A-F
  // if you subtract (val & 0x40 >> 3) + (val & 0x40 >> 6) and subtract '0' it maps the nybble without conditionals
  #define ASCII_NYBBLE(x) ( (x) - ( ((x) & 0x40) >> 3) + ( ((x) & 0x40 ) >> 6 ) - 0x30 )
  // the NVM is now entirely erased: so now we just program in the values
  for (int i=0;i<16;i++) {
    // 2 bytes per byte, 16 bytes = 32 total
    char buffer[32];
    uint8_t data[16];
    char *ptr = (char *) pgm_read_word(&nvmString[i]);
    
    strncpy_P(buffer, ptr, 32);
    for (int j=0;j<16;j++) {
      uint8_t upperNybble = ASCII_NYBBLE(buffer[2*j]);
      uint8_t lowerNybble = ASCII_NYBBLE(buffer[2*j+1]);
      data[j] = (upperNybble << 4) + lowerNybble;
    }
    Serial.print(F("Write 0x"));
    PrintHex8(i<<4);
    Serial.print(F(": "));
    // and just write it
    Wire.beginTransmission(nvmAddr);
    Wire.write(i<<4);
    for (int j=0;j<16;j++) {
      Wire.write(data[j]);
      PrintHex8(data[j]);
    }
    if (Wire.endTransmission() == 0) {
      Serial.print(F("- ack : "));
    } else {
      Serial.println(F(" - NACK : FAIL"));
      return 0;
    }
    if (ackPoll(addr) != 0) {
      Serial.println(F("TIMEOUT"));
      return 0;
    }
    Serial.println(F("okay"));
    delay(100);
  }
  // done-done
  return 0;
}

int readChip(int argc, char **argv) {
  uint8_t addr;
  argc--;
  argv++;

  if (!argc) { 
    Serial.println(F("need a device addr"));
    return 0;
  }

  // slave address start point
  // these guys actually take up 8 addresses for each device: so if it's 0x8, it'll take anything from 0x8-0xF.
  // The bottom three bits are a block address.
  addr = atoi(*argv) << 3;
  addr |= 0x2;
  for (int i=0;i<16;i++) {
    Wire.beginTransmission(addr);
    // address space
    Wire.write(i<<4);
    Wire.endTransmission(false);
    delay(10);
    Wire.requestFrom(addr, (uint8_t) 16);
    while (Wire.available()) {
      PrintHex8(Wire.read());
    }
    Serial.println();
  }
  return 0;
}


uint8_t gpGet() {
  uint8_t addr = deviceAddress << 3;
  Wire.beginTransmission(addr);
  Wire.write(GP_CTRL);
  if (Wire.endTransmission(false) != 0) {
    Serial.println(F("device nack?"));
    return;
  }
  if (Wire.requestFrom(addr, (uint8_t) 1) != 1) {
    Serial.println(F("no data?"));
    return;
  };
  return Wire.read();
}

void gpSet(uint8_t val) {
  uint8_t addr = deviceAddress << 3;
  Wire.beginTransmission(addr);
  Wire.write(GP_CTRL);
  Wire.write(val);
  if (Wire.endTransmission() != 0) {
    Serial.println(F("device nack?"));
    return;
  }
}

// int gpWrite(int argc, char **argv) {
//   uint8_t addr;
//   uint8_t val;
//   argc--;
//   argv++;
//   if (argc < 2) {
//     Serial.println(F("need device address and value"));
//     return;
//   }
//   addr = atoi(*argv);
//   argv++;
//   val = atoi(*argv);
//   Wire.beginTransmission(addr << 3);
//   Wire.write(GP_CTRL);
//   Wire.write(val);
//   if (Wire.endTransmission() != 0) {
//     Serial.println(F("nack?"));
//     return;
//   }
//   Serial.println(F("OK."));  
// }

int setAddress(int argc, char **argv) {
  argc--;
  argv++;
  if (!argc) {
    Serial.println(F("need a device address to change to"));
    return 0;
  }
  deviceAddress = atoi(*argv);
  return 0;
}

void printStatusA(uint8_t rv) {
  Serial.print("Connector A: ");
  if (rv & GP_ENA_B) Serial.println(F("DISABLED"));
  else Serial.println(F("ENABLED"));
}
void printStatusB(uint8_t rv) {
  Serial.print("Connector B: ");
  if (rv & GP_ENB_B) Serial.println(F("DISABLED"));
  else Serial.println(F("ENABLED"));
}


int status(int argc, char **argv) {
  uint8_t rv;
  uint8_t addr = deviceAddress << 3;

  rv = gpGet();
  printStatusA(rv);
  printStatusB(rv);
  Serial.print(F("Connector A CTRL: 0x"));
  PrintHex8(ctrlA);
  Serial.println();
  Serial.print(F("Connector B CTRL: 0x"));
  PrintHex8(ctrlB);
  Serial.println();
  Serial.print(F("SPLD Device Address: "));
  Serial.println(deviceAddress);
  return 0;
}

int seta(int argc, char **argv) {
  uint8_t rv;
  argc--;
  argv++;
  if (!argc) {
      Serial.println(F("need a 0/1 argument"));
      return 0;
  }
  rv = gpGet();
  if (atoi(*argv)) rv &= ~GP_ENA_B;
  else rv |= GP_ENA_B;
  gpSet(rv);
  printStatusA(rv);
  return 0;
}

int setb(int argc, char **argv) {
  uint8_t rv;
  argc--;
  argv++;
  if (!argc) {
      Serial.println(F("need a 0/1 argument"));
      return 0;
  }
  rv = gpGet();
  if (atoi(*argv)) rv &= ~GP_ENB_B;
  else rv |= GP_ENB_B;
  gpSet(rv);
  printStatusB(rv);
  return 0;
}

void clockControl(uint8_t val, int ctrlPin) {
  uint8_t rv;
  // start off LOW, we clock when high
  digitalWrite(ARD_TCK, 0);
  digitalWrite(ARD_TDI, 0);

  rv = gpGet();
  rv |= GP_SEL_ARD;
  gpSet(rv);
  // settle
  delay(1);
  // drive the TCTRL_B pin low
  digitalWrite(ctrlPin, 0);
  // settle
  delay(10);

  // Now shift: MSB first
  for (int i=0;i<8;i=i+1) {
      if (val & 0x80) digitalWrite(ARD_TDI, 1);
      else digitalWrite(ARD_TDI, 0);
      delay(1);
      digitalWrite(ARD_TCK, 1);
      delay(1);
      digitalWrite(ARD_TCK, 0);
      val = val << 1;
  }
  // wait a bit before tristating TCTRL_B
  delay(10);
  digitalWrite(ctrlPin, 1);
  // settle
  delay(10);
  // now we can release TCK/TDI
  rv &= ~GP_SEL_ARD;
  gpSet(rv);
}

int ctrla(int argc, char **argv) {
  uint8_t ctrlval;
  argc--;
  argv++;
  if (!argc) {
    Serial.println(F("need a ctrl value to write"));
    return 0;
  }
  ctrlval = strtoul(*argv, NULL, 0);
  clockControl(ctrlval, CTRLA_B);
  ctrlA = ctrlval;
  return 0;
}

int ctrlb(int argc, char **argv) {
  uint8_t ctrlval;
  argc--;
  argv++;
  if (!argc) {
    Serial.println(F("need a ctrl value to write"));
    return 0;
  }
  ctrlval = strtoul(*argv, NULL, 0);
  clockControl(ctrlval, CTRLB_B);
  ctrlB = ctrlval;
  return 0;
}

int decode(int argc, char **argv) {
  argc--;
  argv++;
  if (!argc) {
    Serial.println(F("need a ctrl value to decode"));
    return;
  }
  ctrlval = strtoul(*argv, NULL, 0);
  for (int i=0;i<8;i++) {
    if (i < 7) {
      Serial.print(F("Slot "));
      Serial.print(i+1);
      if (ctrlval & 0x1) {
        Serial.println(F(": ENABLED"));
      } else {
        Serial.println(F(": DISABLED"));
      }
    } else {
      Serial.print(F("LED: "));
      if (ctrlval & 0x1) {
        Serial.println(F(": ON"));        
      } else {
        Serial.println(F(": OFF"));
      }
    }
    ctrlval = ctrlval >> 1;
  }
  return 0;
}

int help(int argc, char **argv) {
  Serial.println(F("RADBUG COMMANDS:"));
  Serial.println(F("a 0/1 : turns on (1) or off (0) connector A"));
  Serial.println(F("b 0/1 : turns on (1) or off (0) connector B"));
  Serial.println(F("ctrla # : writes ctrl value # to connector A"));
  Serial.println(F("ctrlb # : writes ctrl value # to connector B"));
  Serial.println(F("status : prints out the current crate config status"));
  Serial.println(F("blink : blinks the Arduino LED"));
  Serial.println(F("read # : reads SPLD device address #"));
  Serial.println(F("prog # : programs SPLD device address #"));
  Serial.println(F("setdev # : sets the default device address to #"));
  Serial.println(F("decode # : prints out what the ctrl value # means"));
}

int led_blink(int argc, char **argv) {
  digitalWrite(LED_BUILTIN, LOW); // turns on LED
  delay(1000);
  digitalWrite(LED_BUILTIN, HIGH); // turns off LED
}

void setup() {
  Wire.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(ARD_TCK, OUTPUT);
  digitalWrite(ARD_TCK, 0);
  pinMode(ARD_TDI, OUTPUT);
  digitalWrite(ARD_TDI, 0);
  pinMode(CTRLA_B, OUTPUT);
  digitalWrite(CTRLA_B, 1);
  pinMode(CTRLB_B, OUTPUT);
  digitalWrite(CTRLB_B, 1);

  digitalWrite(LED_BUILTIN, HIGH); // turns off LED
  // put your setup code here, to run once:
  cmd.begin(9600);
  Serial.println(F("RADBUG Debugger Command Line Control"));
  cmd.add("blink", led_blink);
  cmd.add("ping", pingChip);
  cmd.add("read", readChip);
  cmd.add("prog", progChip);
  cmd.add("help", help);
  cmd.add("status", status);
  cmd.add("setdev", setAddress);
  cmd.add("a", seta);
  cmd.add("b", setb);
  cmd.add("ctrla", ctrla);
  cmd.add("ctrlb", ctrlb);
  cmd.add("decode", decode);
//  cmd.add("write", gpWrite);
}

void loop() {
  cmd.poll();
}
