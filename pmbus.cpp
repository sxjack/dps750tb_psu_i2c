/* -*- tab-width: 2; mode: c; -*-
 * 
 * Minimal C++ class for interfacing with a PMBus PSU.
 *
 * Copyright (c) 2020, Steve Jack.
 *
 * Use at your own risk.
 *
 * Notes
 *
 * Tested with a Dell DPS-750TB.
 * 
 */

#pragma GCC diagnostic warning "-Wunused-variable"

#define DIAGNOSTICS  1

#include "pmbus.h"

/*
 *
 */

PMBus::PMBus() {

  int i;

  I2C = &Wire;

  memset(mfr_id,      0,sizeof(mfr_id));
  memset(mfr_model,   0,sizeof(mfr_model));
  memset(mfr_revision,0,sizeof(mfr_revision));
  memset(mfr_location,0,sizeof(mfr_location));
  memset(mfr_date,    0,sizeof(mfr_date));
  memset(mfr_serial,  0,sizeof(mfr_serial));

  i = 0;

  status_reg[i]   = 0x7a; status_byte[i] = &status_vout;
  status_reg[++i] = 0x7b; status_byte[i] = &status_iout;
  status_reg[++i] = 0x7c; status_byte[i] = &status_input;
  status_reg[++i] = 0x7d; status_byte[i] = &status_temperature;
  
  status_reg[++i] = 0x7e; status_byte[i] = &status_cml;
  status_reg[++i] = 0x7f; status_byte[i] = &status_other;
  status_reg[++i] = 0x80; status_byte[i] = &status_mfr_specific;
  status_reg[++i] = 0x81; status_byte[i] = &status_fans;

  while (++i < STATUS_REGISTERS) {

    status_reg[i] = 0;
  }

  return;
}

/*
 * 
 */

int PMBus::init(int pson,int i2c_enable,short int direction,uint8_t address,Stream *Debug_Serial) {

  return init(pson,i2c_enable,direction,address,Debug_Serial,NULL);
}

//

int PMBus::init(int pson,int i2c_enable,short int direction,uint8_t address,Stream *Debug_Serial,TwoWire *i2c) {

  int i;
  char text[128];
  
  i = text[0] = 0;

  pmbus_address    = address;
  PSON_do          = pson;
  I2C_enable_do    = i2c_enable;
  output_direction = direction;
  Debug            = Debug_Serial;

  if (i2c) {

    I2C = i2c;
  }
  
  pinMode(PSON_do,OUTPUT);
  pinMode(I2C_enable_do,OUTPUT);

  digitalWrite(I2C_enable_do,0 ^ output_direction);
  digitalWrite(PSON_do,0 ^ output_direction);

  //

  delay(500);

  //

  read_string(0x99,sizeof(mfr_id)       - 1,(uint8_t *) mfr_id);       delay(1);
  read_string(0x9c,sizeof(mfr_location) - 1,(uint8_t *) mfr_location); delay(1);
  read_string(0x9d,sizeof(mfr_date)     - 1,(uint8_t *) mfr_date);     delay(1);
  read_string(0x9e,sizeof(mfr_serial)   - 1,(uint8_t *) mfr_serial);   delay(1);

  check_model();

  //
  
  clear_faults();

#if DIAGNOSTICS

  if (Debug) {

    sprintf(text,"manf.:    \'%s\'\r\n",&mfr_id[1]);
    Debug->print(text);
    sprintf(text,"model:    \'%s\'\r\n",&mfr_model[1]);
    Debug->print(text);
    sprintf(text,"revision: \'%s\'\r\n",&mfr_revision[1]);
    Debug->print(text);
    sprintf(text,"date:     \'%s\'\r\n",&mfr_date[1]);
    Debug->print(text);
    sprintf(text,"serial:   \'%s\'\r\n",&mfr_serial[1]);
    Debug->print(text);

    if (total_power_on) {

      int  days, years;

      days  = (int) ((long int) total_power_on / 86400l);
      years = days / 365;

      sprintf(text,"on time:  %lu s (%08lx) ",total_power_on,total_power_on);
      Debug->print(text);
      sprintf(text,"%d days, %d years\r\n",days,years);
      Debug->print(text);
    }

    Debug->print("\r\nPMBus::init() complete\r\n");
  }

#endif

  return 0;
}

/*
 * 
 */

void PMBus::check_model() {

  read_string(0x9a,sizeof(mfr_model)    - 1,(uint8_t *) mfr_model);    delay(1);
  read_string(0x9b,sizeof(mfr_revision) - 1,(uint8_t *) mfr_revision); delay(1);

  pmbus_revision = read_byte(0x98); delay(1);
  vout_mode      = read_byte(0x20); delay(1);
  vout_command   = read_word(0x21); delay(1);
  
  // Model specific setup.

  if (strncmp(&mfr_model[1],"DPS750TB1",9) == 0) {

    status_reg[5] = 0;
    status_reg[6] = 0;

    temperatures  = 2;
    fans          = 1;

    if (pmbus_revision == 0) {

      vout_scale   = 1.0 / (float) 0x200;
    }

  } else if (strncmp(&mfr_model[1],"D1U86T-W-800-12-HB4C",20) == 0) {

  // muRata, unchecked, 0xe5 returns garbage on the DPS-750.

    int     i;
    uint8_t buffer[4];

    read_block(0xe5, 4,buffer); delay(1);

    for (i = 0; i < 4; ++i) {

      total_power_on <<= 8;
      total_power_on  |= buffer[i];
    }

  } else {

    status_reg[7] = 0;    
  }

  //

  return;
}

/*
 * 
 */

void PMBus::standby() {

  digitalWrite(PSON_do,1 ^ output_direction);

  return;
}

/*
 * 
 */

void PMBus::on() {

  digitalWrite(PSON_do,0 ^ output_direction);

  return;
}

/*
 * 
 */

int PMBus::scan() {

  int             i;
  float           tmp_f;
  uint32_t        msecs;
  static uint32_t last_scan = 0;  

  //

  msecs = millis();

  if ((msecs - last_scan) < 1000) {

    return 0;
  }

  last_scan = msecs;

  //

  if (mfr_model[0] == 0) {

    check_model();
  }

  //

  status_u8   = read_word(0x78); delay(1);
  status_word = read_word(0x79);

  for (i = 0; i < STATUS_REGISTERS; ++i) {

    if (status_reg[i]) {

      delay(1);
      
      *status_byte[i] = read_byte(status_reg[i]);
    }
  }
  
  //

  delay(1); read_linear(0x88,&V_in,90.0,264.0);
  delay(1); read_linear(0x89,&I_in,0.0,16.0);
  delay(1); read_linear(0x97,&W_in,0.0,750.0);

  delay(1); 
  
  if (vout_scale) {

    tmp_f = vout_scale * (float) read_word(0x8b);

    if (tmp_f < 16.0) {

      V_out = tmp_f;
    }
    
  } else {

    read_linear(0x8b,&V_out,0.0,9999.0);
  }

  delay(1); read_linear(0x8c,&I_out,0.0,70.0);

  delay(1); read_linear(0x96,&W_out,0.0,750.0);

  for (i = 0; i < temperatures; ++i) {

    delay(1); read_linear(0x8d + i,&T[i],-10.0,100.0);
  }

  //

  for (i = 0; i < fans; ++i) {

    delay(1); read_linear(0x90 + i,&fan[i],0.0,3000.0);
  }

  //

  return 1;
}

/*
 *
 */

void PMBus::clear_faults() {

  I2C->beginTransmission(pmbus_address);
  I2C->write(0x03);
  I2C->endTransmission(true);

  status_u8           =
  status_word         = 0;
 
  status_vout         =
  status_iout         =
  status_input        =
  status_temperature  =
  status_cml          =
  status_other        =
  status_mfr_specific =
  status_fans         = 0;

  return;
}

/*
 *
 */

void PMBus::write_byte(uint8_t reg,uint8_t value) {

  I2C->beginTransmission(pmbus_address);
  I2C->write(reg);
  I2C->write(value);
  I2C->endTransmission(true);

  return;
}

/*
 *
 */

uint8_t PMBus::read_byte(uint8_t reg) {

  I2C->beginTransmission(pmbus_address);
  I2C->write(reg);
  I2C->endTransmission(false);
  
  I2C->requestFrom(pmbus_address,(uint8_t) 1,(uint8_t) true);

  return I2C->read();
}

/*
 *
 */

uint16_t PMBus::read_word(uint8_t reg) {

  uint16_t value;

  I2C->beginTransmission(pmbus_address);
  I2C->write(reg);
  I2C->endTransmission(false);
  
  I2C->requestFrom(pmbus_address,(uint8_t) 2,(uint8_t) true);

  value  = I2C->read();
  value |= I2C->read() << 8;

  return value;
}

/*
 *
 */

int PMBus::read_string(uint8_t reg,int bytes,uint8_t *buffer) {

  int len = 0;

  len = (int) read_byte(reg);

  if ((len)&&(len < bytes)) {

    read_block(reg,len,buffer);
  }

  return len;
}

/*
 *
 */

void PMBus::read_block(uint8_t reg,int bytes, uint8_t *buffer) {

  int i;

  I2C->beginTransmission(pmbus_address);
  I2C->write(reg);
  I2C->endTransmission(false);
  
  I2C->requestFrom(pmbus_address,(uint8_t) bytes,(uint8_t) true);

  for (i = 0; i < bytes; ++i) {

    buffer[i] = I2C->read();
  }

  return;
}

/*
 * 
 */

void PMBus::read_linear(uint8_t reg,float *value,float min_f,float max_f) {

  float    tmp_f;
  uint16_t linear;

  linear = read_word(reg);
  tmp_f  = linear2float(linear);
  
  if ((tmp_f >= min_f)&&(tmp_f <= max_f)) {

    *value = tmp_f;
  }

#if 0

  if (Debug) {

    char text[64], text2[16];

    dtostrf(*value,6,1,text2);
    sprintf(text,"%02x %04x (%d,%d,%d) %s\n",
            reg,linear,(int) linear,(int) (linear >> 11),(int) (linear & 0x7ff),text2);
    Debug->print(text);
  }

#endif

  return; 
}

/*
 *
 */

float PMBus::linear2float(uint16_t u16) {

  float value = 0;
  union {linear_t linear; uint16_t u16;} conv;

  conv.u16 = u16;

  value = (float) conv.linear.y * pow(2.0,(float) conv.linear.n);

  return value;
}

/*
 *
 */
