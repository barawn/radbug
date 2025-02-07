/////////////////////////////////////////////////////////////////////
// RADBUG V2
/////////////////////////////////////////////////////////////////////
//
// NO MORE GREENPAK

// hey look ATMegas have internal EEPROMs! who knew!
#include <EEPROM.h>

// 'K'
#define KEY             0x4B
#define KEY_ADDR        0x00
#define DFL_EN_ADDR     0x01
#define DFL_CTRLA_ADDR  0x02
#define DFL_CTRLB_ADDR  0x03

#define CTRLA_B 9
#define CTRLB_B 2

#define ENA_B 10
#define ENB_B 11

#define ARD_TCK 7
#define ARD_TDI 6

#define S0 5
#define S1 3
#define S2 4
#define S3 12

// 1G157: IF S = 0, Y = I0
//           S = 1, Y = I1

// OK: HERE'S HOW THIS WORKS
// CONNECTOR A ON, CONNECTOR B OFF
//  S3 = X
//  S2 = 0
// CONNECTOR A ON, CONNECTOR B ON
//  S3 = 1
//  S2 = 1
// CONNECTOR A OFF, CONNECTOR B ON
//  S3 = 0
//  S2 = 1
// THIS MEANS WE CAN MAP S3 = CONNECTOR A ON
//                       S2 = CONNECTOR B ON
// 
// N.B. IF CLOCK CONTROL, SET S3=0 THEN RESTORE
//
// S0/S1 ARE BOTH 0 = JTAG, 1 = ARDUINO

#define DEFAULT_EN_A 1
#define DEFAULT_EN_B 0
#define DEFAULT_EN_A_MASK 0x1
#define DEFAULT_EN_B_MASK 0x2
#define DEFAULT_ENABLE ((DEFAULT_EN_B << 1) | DEFAULT_EN_A)
uint8_t default_en;

uint8_t A_is_enabled = 0;
uint8_t B_is_enabled = 0;

uint8_t default_ctrlA;
uint8_t default_ctrlB;

#define DEFAULT_CTRL_A 0x81
#define DEFAULT_CTRL_B 0x81
uint8_t ctrlA_copy = 0;
uint8_t ctrlB_copy = 0;

void setup_A(bool val) {
  if (val) {
    // enable
    digitalWrite(S3, 1);
    digitalWrite(ENA_B, 0);
    A_is_enabled = 1;
  } else {
    digitalWrite(S3, 0);
    digitalWrite(ENA_B, 1);
    A_is_enabled = 0;
  }
}

void setup_B(bool val) {
  if (val) {
    digitalWrite(S2, 1);
    digitalWrite(ENB_B, 0);
    B_is_enabled = 1;
  } else {
    digitalWrite(S2, 0);
    digitalWrite(ENB_B, 1);
    B_is_enabled = 0;
  }
}

// the LED is on LED_BUILTIN
#include "cmdArduino.h"

void PrintStatusA(uint8_t val) {
  Serial.print(F("Connector A: "));
  if (val) Serial.println(F("ENABLED"));
  else Serial.println(F("DISABLED"));
}

void PrintStatusB(uint8_t val) {
  Serial.print(F("Connector B: "));
  if (val) Serial.println(F("ENABLED"));
  else Serial.println(F("DISABLED"));
}

void PrintSlot(uint8_t ctrlval) {
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
}

void PrintFullStatus(uint8_t enA, uint8_t enB, uint8_t ctrlA, uint8_t ctrlB) {
  PrintStatusA(enA);
  Serial.print(F("CTRLA: "));
  Serial.println(ctrlA, HEX);
  PrintStatusB(enB);
  Serial.print(F("CTRLB: "));
  Serial.println(ctrlB, HEX);
}

int status(int argc, char **argv) {
  // print out current config
  // like "Connector A: ENABLED"
  // like "Connector A CTRL: 0x"
  // etc.
  PrintFullStatus( A_is_enabled, B_is_enabled, ctrlA_copy, ctrlB_copy);
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
  rv = strtoul(*argv, NULL, 0);
  setup_A(rv);
  PrintStatusA(A_is_enabled);
  // if 0 enable A and print status A
  // like Connector A: ENABLED or DISABLED
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
  rv = strtoul(*argv, NULL, 0);
  setup_B(rv);
  PrintStatusB(B_is_enabled);
  // same as above
  return 0;
}

#define JTAG_ARDUINO 1
#define JTAG_FTDI 0

void jtagSelect(uint8_t me) {
  if (me) {
    digitalWrite(S3, 0);
    digitalWrite(S0, 1);
    digitalWrite(S1, 1);
  } else {
    if (A_is_enabled) digitalWrite(S3, 1);
    digitalWrite(S1, 0);
    digitalWrite(S0, 0);
  }
  delay(1);
}

// write the control value to a RACK CPLD
void clockControl(uint8_t val, int ctrlPin) {
  uint8_t rv;

  // start off LOW, we clock when high
  digitalWrite(ARD_TCK, 0);
  digitalWrite(ARD_TDI, 0);

  // SWITCH TO OUR OUTPUTS HERE
  jtagSelect(JTAG_ARDUINO);

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

  // SWITCH BACK TO JTAG HERE
  jtagSelect(JTAG_FTDI);
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
  ctrlA_copy = ctrlval;
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
  ctrlB_copy = ctrlval;
  return 0;
}

int decode(int argc, char **argv) {
  uint8_t ctrlval;
  
  argc--;
  argv++;
  if (!argc) {
    Serial.println(F("need a ctrl value to decode"));
    return;
  }
  ctrlval = strtoul(*argv, NULL, 0);
  PrintSlot(ctrlval);
  return 0;
}

int printdefault(int argc, char **argv) {
  PrintFullStatus( default_en & DEFAULT_EN_A_MASK,
                   default_en & DEFAULT_EN_B_MASK,
                   default_ctrlA,
                   default_ctrlB);
  return 0;                   
}

int savedefault(int argc, char **argv) {
  uint8_t en_val = 0;
  if (A_is_enabled) en_val |= DEFAULT_EN_A_MASK;
  if (B_is_enabled) en_val |= DEFAULT_EN_B_MASK;
  EEPROM.write(DFL_EN_ADDR, en_val);
  EEPROM.write(DFL_CTRLA_ADDR, ctrlA_copy);
  EEPROM.write(DFL_CTRLB_ADDR, ctrlB_copy);
  default_en = en_val;
  default_ctrlA = ctrlA_copy;
  default_ctrlB = ctrlB_copy;
  PrintFullStatus( default_en & DEFAULT_EN_A_MASK,
                   default_en & DEFAULT_EN_B_MASK,
                   default_ctrlA,
                   default_ctrlB);

  return 0;
}

void applyDefaults() {
  if (default_en & DEFAULT_EN_A_MASK) A_is_enabled = 1;
  else A_is_enabled = 0;
  if (default_en & DEFAULT_EN_B_MASK) B_is_enabled = 1;
  else B_is_enabled = 0;
  setup_A(A_is_enabled);
  setup_B(B_is_enabled);
  
  clockControl(default_ctrlA, CTRLA_B);
  ctrlA_copy = default_ctrlA;
  clockControl(default_ctrlB, CTRLB_B);
  ctrlB_copy = default_ctrlB;
  return 0;
}

int loaddefault(int argc, char **argv) {
  applyDefaults();
  PrintFullStatus( A_is_enabled, B_is_enabled, ctrlA_copy, ctrlB_copy);
  return 0;
}

int help(int argc, char **argv) {
  Serial.println(F("RADBUGv2 COMMANDS:"));
  Serial.println(F("a 0/1 : turns on (1) or off (0) connector A"));
  Serial.println(F("b 0/1 : turns on (1) or off (0) connector B"));
  Serial.println(F("ctrla # : writes ctrl value # to connector A"));
  Serial.println(F("ctrlb # : writes ctrl value # to connector B"));
  Serial.println(F("status : prints out the current crate config status"));
  Serial.println(F("blink : blinks the Arduino LED"));
  Serial.println(F("decode # : prints out what the ctrl value # means"));
  Serial.println(F("default : print current defaults"));
  Serial.println(F("save : store current values as the defaults"));
  Serial.println(F("load : reload default values"));
}

int led_blink(int argc, char **argv) {
  digitalWrite(LED_BUILTIN, LOW); // turns on LED
  delay(1000);
  digitalWrite(LED_BUILTIN, HIGH); // turns off LED
}

void setup() {
  // defaulty-defaulty
  if (EEPROM.read(KEY_ADDR) != KEY) {
    default_en = DEFAULT_ENABLE;
    default_ctrlA = DEFAULT_CTRL_A;
    default_ctrlB = DEFAULT_CTRL_B;
    // never been programmed
    EEPROM.write(KEY_ADDR, KEY);
    EEPROM.write(DFL_EN_ADDR, DEFAULT_ENABLE);
    EEPROM.write(DFL_CTRLA_ADDR, DEFAULT_CTRL_A);
    EEPROM.write(DFL_CTRLB_ADDR, DEFAULT_CTRL_B);
  } else {
    default_en = EEPROM.read(DFL_EN_ADDR);
    default_ctrlA = EEPROM.read(DFL_CTRLA_ADDR);
    default_ctrlB = EEPROM.read(DFL_CTRLB_ADDR);
  }
  
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(ARD_TCK, OUTPUT);
  digitalWrite(ARD_TCK, 0);
  pinMode(ARD_TDI, OUTPUT);
  digitalWrite(ARD_TDI, 0);
  pinMode(CTRLA_B, OUTPUT);
  pinMode(CTRLB_B, OUTPUT);

  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(ENA_B, OUTPUT);
  pinMode(ENB_B, OUTPUT);

  jtagSelect(0);  

  applyDefaults();

  digitalWrite(LED_BUILTIN, HIGH); // turns off LED
  // put your setup code here, to run once:
  cmd.begin(9600);
  Serial.println(F("RADBUGv2 Debugger Command Line Control"));
  cmd.add("blink", led_blink);
  cmd.add("help", help);
  cmd.add("status", status);
  cmd.add("a", seta);
  cmd.add("b", setb);
  cmd.add("ctrla", ctrla);
  cmd.add("ctrlb", ctrlb);
  cmd.add("decode", decode);
  cmd.add("default", printdefault);
  cmd.add("save", savedefault);
  cmd.add("load", loaddefault);
}

void loop() {
  cmd.poll();
}
