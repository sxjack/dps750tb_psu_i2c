/*
 * Minimal C++ class for interfacing with a PMBus PSU.
 *
 * Copyright (c) 2020, Steve Jack.
 *
 * Use at your own risk.
 *
 *
 */

#ifndef PMBUS_H
#define PMBUS_H

#include <Arduino.h>
#include <Wire.h>

#define STATUS_REGISTERS 10

/*
 *
 */

class PMBus {

 public:
                PMBus();
  int           init(int,int,short int,uint8_t,Stream *);
  int           init(int,int,short int,uint8_t,Stream *,TwoWire *);
  void          check_model(void);
  int           scan(void);
  void          clear_faults(void);
  void          standby(void);
  void          on(void);

  char          mfr_id[8], mfr_model[24], mfr_revision[4], mfr_location[8], mfr_date[8], mfr_serial[16];
  float         V_in = 240.0, I_in = 0.0, V_out = 0.0, I_out = 0.0, T[3] = {0.0,0.0,0.0}, fan[2] = {0.0,0.0},
                W_in = 0.0, W_out = 0.0;
  uint8_t       pmbus_revision = 0, vout_mode = 0;
  uint8_t       status_u8 = 0, status_vout = 0, status_iout = 0, status_input = 0, status_temperature = 0, 
                status_cml = 0, status_other = 0, status_mfr_specific = 0, status_fans = 0, *status_byte[STATUS_REGISTERS];
  uint16_t      status_word = 0, vout_command = 0;
  uint32_t      total_power_on = 0;

  typedef struct linear {int y:11; int n:5;} linear_t;
 
 private:

  void          write_byte(uint8_t,uint8_t);
  uint8_t       read_byte(uint8_t);
  uint16_t      read_word(uint8_t);
  int           read_string(uint8_t,int,uint8_t *);
  void          read_block(uint8_t,int,uint8_t *);
  void          read_linear(uint8_t,float *,float,float);
  float         linear2float(uint16_t);

  int           fans = 2, temperatures = 3;
  short int     PSON_do = 2, I2C_enable_do = 25, output_direction = 0, status_reg[STATUS_REGISTERS];
  float         vout_scale;
  uint8_t       pmbus_address = 0x58;
  Stream       *Debug = NULL;
  TwoWire      *I2C = NULL;
};

#endif
