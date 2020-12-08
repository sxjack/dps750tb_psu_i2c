/* -*- tab-width: 2; mode: c; -*-
 * 
 * Program to extract some basic data from a power supply that supports PMBus and present it. 
 *
 * Copyright (c) 2020, Steve Jack.
 *
 * MIT Licence.
 *
 * Use at your own risk.
 *
 * Notes
 *
 * Developed with an ESP32 (ARDUINO_ARCH_ESP32), a Nano (ARDUINO_AVR_NANO)
 * and a Dell DSP-750TB. Should work with an STM32F1 or a ESP8266.
 * 
 * Use an ESP if you want a web interface, the STM32 or Nano if you just want a display.
 * 
 * Notes
 *
 * STM32duino: "Generic STM32F1 series", "BluePill F103CB", "Enabled (no generic 'Serial')".
 * If you don't set the Serial support correctly, you get a strange linker error.
 * 
 *
 *
 *
 */

#pragma GCC diagnostic warning "-Wunused-variable"

#define DIAGNOSTICS  1
#define WEBSERVER    1

/*
 *
 */

#include <Arduino.h>
#include <Wire.h>

#include "pmbus.h"

// Processor specific setup.

#if defined(ARDUINO_BLUEPILL_F103C8) || defined(ARDUINO_BLUEPILL_F103CB) // STM32F1

#define _STM32F103C

#define LCD_DISPLAY      11
#define OUTPUT_DIRECTION  0
#define DebugSerial    Serial2

HardwareSerial Serial1(PA10,PA9);
HardwareSerial Serial2(PA3,PA2);
HardwareSerial Serial3(PB11,PB10);

void yield(void);

static const int blink_do = PC13, PSON_do = PB14, I2C_enable_do = PB15;

#elif defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)       // ESP

#define OUTPUT_DIRECTION  0
#define DebugSerial    Serial

#define WIFI_SSID     "ssid" 
#define WIFI_PASSWORD "secret"

#if defined(ARDUINO_ARCH_ESP32)

#define LCD_DISPLAY       6 // 11
#define WS2812           27

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

static const int blink_do =  2, PSON_do =  5, I2C_enable_do = 15;

#else

#define LCD_DISPLAY      16

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

static const int blink_do = D5, PSON_do = D6, I2C_enable_do = D7;

#endif

#include <WiFiUdp.h>

int  syslog(IPAddress,char *);
int  connectWiFi(void);
void get_addresses(void);
void webserver(void);

static char        gateway_s[18], my_ip_s[18];
static IPAddress   my_ip, gateway_ip;
static WiFiUDP     udp;
#if WEBSERVER
static WiFiServer  server(80);
#endif

#elif defined(ARDUINO_AVR_NANO)                                          // AVR Nano

#define LCD_DISPLAY       1 // 1 uses a lot of I/O.
// #define WS2812           10
#define OUTPUT_DIRECTION  1
#define DebugSerial    Serial

static const int blink_do = 13, PSON_do = 12, I2C_enable_do = 11;

#else                                                                    //

#error "No configuration for this processor."

#endif // Processor specific configuration.

/*
 *  Displays.
 */

#if LCD_DISPLAY && (LCD_DISPLAY < 10)

//
// 16x2 LCD displays.
// The NewLiquidCrystal library is used for AVR and ESP32 Arduinos.
//

#define LCD_PAGES 2

static char               lcd_line_0[18], lcd_line_1[18];

#if LCD_DISPLAY == 1

#include <LiquidCrystal.h>

static const short int    contrast_ao = 3, contrast = 120; // Contrast may need tuning for particular displays.

LiquidCrystal lcd(9,8,7,6,5,4);

#elif LCD_DISPLAY == 6

#include <LiquidCrystal_I2C.h>

static const uint8_t      display_address = 0x27;
static LiquidCrystal_I2C  lcd(display_address,2,1,0,4,5,6,7);

#endif

#elif (LCD_DISPLAY > 10) && (LCD_DISPLAY < 20)

//
// OLED displays on the I2C interface.
// The U8g2 library is used.
//

#include <U8x8lib.h>

#if LCD_DISPLAY == 11
const int display_address = 0x78;
U8X8_SH1106_128X64_NONAME_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE);
#elif LCD_DISPLAY == 12
const int display_address = 0x78;
U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(U8X8_PIN_NONE);
#elif LCD_DISPLAY == 16
const int display_address = 0x78; // 0x3c?
U8X8_SSD1306_64X48_ER_HW_I2C u8x8(U8X8_PIN_NONE);
#endif

#endif // LCD_DISPLAY type

//

#if defined(WS2812)

#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel status_ws2812(8,WS2812,NEO_GRB + NEO_KHZ800);

#endif

/*
 * 
 */

static const char     *title = "PMBus", *build_date = __DATE__;
static PMBus           psu;

/*
 * 
 */

void setup() {

  int status, i;

  status = i = 0;

  pinMode(blink_do,OUTPUT);

#if defined(ARDUINO_ARCH_ESP32)

  Wire.begin();
  // Using Wire1 on 12 & 14 crashes the program.
  Wire1.begin(18,19,100000);

#elif defined(ARDUINO_ARCH_ESP8266)

  Wire.begin(D2,D1);

#elif defined(_STM32F103C)

  Wire.begin();
  // Wire2.begin();

#else 

  Wire.begin();

#endif

#if defined(ARDUINO_ARCH_AVR)
  DebugSerial.begin(57600);
#else
  DebugSerial.begin(115200);
#endif

  Wire.setClock(100000);

#if DIAGNOSTICS

  DebugSerial.print("\r\n");
  DebugSerial.print((char *) title);
  DebugSerial.print("\r\n");
  DebugSerial.print((char *) build_date);
  DebugSerial.print("\r\n\n");

#endif

  //

  delay(100);

#if defined(ARDUINO_ARCH_ESP32)
  psu.init(PSON_do,I2C_enable_do,OUTPUT_DIRECTION,0x58,&DebugSerial,&Wire1);
#else
  psu.init(PSON_do,I2C_enable_do,OUTPUT_DIRECTION,0x58,&DebugSerial);
#endif

  yield();

#if (LCD_DISPLAY) && (LCD_DISPLAY < 10)

  for (i = 0; i < 16; ++i) {

    lcd_line_0[i] = lcd_line_1[i] = ' ';
  }
  
  lcd_line_0[16] = lcd_line_1[16] = 0;

#if LCD_DISPLAY == 1

  analogWrite(contrast_ao,contrast);

  lcd.begin(16,2);

#elif LCD_DISPLAY > 3

  lcd.begin(16,2);
  lcd.setBacklightPin(3,POSITIVE);
  lcd.setBacklight(HIGH);

#endif

  lcd.setCursor(5,0);
  lcd.print(title);   

  lcd.setCursor(2,1);
  lcd.print(build_date);

#elif (LCD_DISPLAY > 10) && (LCD_DISPLAY < 20)

  u8x8.setI2CAddress(display_address);
  u8x8.begin();
  u8x8.setPowerSave(0);

  u8x8.setFont(u8x8_font_chroma48medium8_r);
 
  u8x8.refreshDisplay();

#if LCD_DISPLAY < 16
  u8x8.drawString(5,0,(char *) title);
  u8x8.drawString(3,1,(char *) build_date);
#elif LCD_DISPLAY == 16
  u8x8.drawString(2,0,(char *) title);
#endif

#endif

  yield();

  //

#if defined(WS2812)

  status_ws2812.begin();
  status_ws2812.clear();
  status_ws2812.show();

#endif

  //

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)

  status = connectWiFi();

  get_addresses();

  syslog(gateway_ip,(char *) "setup() complete");

#if WEBSERVER

  server.begin();

#endif

#endif

  psu.on();

#if DIAGNOSTICS
  DebugSerial.print("setup() complete\r\n");
#endif

  return;
}

/*
 * 
 */

void loop() {

  int              i;
  char             text[128];
  uint32_t         run_msecs, run_secs;
  static int       blink = 0;
  static char      I_out_s[8] = {0}, V_out_s[8] = {0};
  static uint32_t  last_display_update = 0, last_scan = 0;
#if LCD_DISPLAY && (LCD_DISPLAY < 11)
  static int       lcd_counter = 0;
#elif LCD_DISPLAY > 10
  static int       display_phase = 0;
#endif
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
static int         last_hour = -1;
static time_t      ticks;
struct tm         *local_tm;
#endif

  text[0] = i = 0; // Because I can't be bothered to sort out #if's

  //

  run_msecs = millis(); // run time
  run_secs  = run_msecs / 1000;

  if (psu.scan()) {

    digitalWrite(blink_do,blink ^= 1);

    last_scan = run_secs;

    dtostrf(psu.V_out,5,2,V_out_s);
    dtostrf(psu.I_out,5,2,I_out_s);

#if defined(WS2812)

    for (i = 0; i < 8; ++i) {

       status_ws2812.setPixelColor(i,((psu.status_u8 >> i) & 0x01) ? 0xff0000: 0x003f00); 
    }

    status_ws2812.show();

#endif

#if defined(DebugSerial)

    sprintf(text,"{ \"run time\": %lu, \"Vout\": %s, \"Iout\": %s }\r\n",
            run_secs,V_out_s,I_out_s);
    DebugSerial.print(text);

#endif

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)

    time(&ticks);
    local_tm = localtime(&ticks);

    if (local_tm->tm_hour != last_hour) {

      last_hour = local_tm->tm_hour;

      sprintf(text,"%sV, %sA, %dC",V_out_s,I_out_s,(int) psu.T[0]);
      syslog(gateway_ip,text);
    }

#endif
  }

  yield();

  //

  if ((run_secs > 3)&&
      ((run_msecs - last_display_update) > 50)) {

#if LCD_DISPLAY && (LCD_DISPLAY < 10)

    if (++lcd_counter >= (LCD_PAGES * 80)) {

      lcd_counter = 0;
    }

    if ((lcd_counter % 20) == 0) {

      sprintf(lcd_line_0,"%4sV %4sA   ",V_out_s,I_out_s);

      switch (lcd_counter / 80) { // 4 seconds on a page

      case 0:

        sprintf(lcd_line_1,"%2dC %2dC %4drpm ",(int) psu.T[0],(int) psu.T[1],(int) psu.fan[0]);

        break;

      case 1:

        for (i = 0; i < 16; ++i) {

          lcd_line_1[15 - i] = ((psu.status_word >> i) & 0x01) ? '1': '0';
        }

        break;
      }

      lcd.setCursor(0,0);
      lcd.print(lcd_line_0);   

      lcd.setCursor(0,1);
      lcd.print(lcd_line_1);
    }
    
#elif LCD_DISPLAY == 11

    switch (display_phase) {

    case 0:

      break;

    case 1:
      for (i = 0; i < 16; ++i) {

        text[i] = ' ';
      }

      text[i] = 0;
      u8x8.drawString(0,1,text);
      break;

    case 2:
   
      sprintf(text,"   %3dV  %4dW  ",(int) psu.V_in,(int) psu.W_out);
      u8x8.drawString(0,display_phase,text);
      break;

    case 3:
   
      sprintf(text," %sV %sA  ",V_out_s,I_out_s);
      u8x8.drawString(0,display_phase,text);
      break;

      break;

    case 4:
   
      sprintf(text,"  %2dC  %4drpm  ",(int) psu.T[0],(int) psu.fan[0]);
      u8x8.drawString(0,display_phase,text);
      break;

    case 5:

      for (i = 0; i < 16; ++i) {

        text[15 - i] = ((psu.status_word >> i) & 0x01) ? '1': '0';
      }

      text[i] = 0;
      u8x8.drawString(0,display_phase,text);
      break;

    case 6:
   
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
      sprintf(text," %3ddB          ",WiFi.RSSI());
      u8x8.drawString(0,display_phase,text);
#endif
      break;

    case 7: // This is just for detecting memory leaks.

#if defined(ARDUINO_ARCH_ESP32)
      sprintf(text,"%-6u  %08x",ESP.getFreeHeap(),text);
#else
      sprintf(text,"%08x",text);
#endif
      u8x8.drawString(0,display_phase,text);
      break;
    }

    if (++display_phase > 7) {

      display_phase = 0;
    }

    u8x8.refreshDisplay();

#elif LCD_DISPLAY == 16

    switch (display_phase) {

    case 0:
   
      break;

    case 1:

      sprintf(text," %sV ",V_out_s);
      u8x8.drawString(0,display_phase,text);
      break;

    case 2:

      sprintf(text," %sA ",I_out_s);
      u8x8.drawString(0,display_phase,text);
      break;

    case 3:
 
      sprintf(text,"%2dC %4d",(int) psu.T[0],(int) psu.fan[0]);
      u8x8.drawString(0,display_phase,text);
      break;

    case 4:

      for (i = 0; i < 8; ++i) {

        text[7 - i] = ((psu.status_word >> i) & 0x01) ? '1': '0';
      }

      text[i] = 0;
      u8x8.drawString(0,display_phase,text);
      break;

    default:

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
      sprintf(text,"%ddB",WiFi.RSSI());
      u8x8.drawString(1,5,text);
#endif
      break;
    }

    if (++display_phase > 5) {

      display_phase = 0;
    }

    u8x8.refreshDisplay();

#endif  

    last_display_update = run_msecs;

  }

#if (defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)) && WEBSERVER

  webserver();

#endif

  return;
}

/*
 *  Processor specific functions.
 */

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)

int connectWiFi() {

  int                status, i;
  static const char *ssid     = WIFI_SSID,
                    *password = WIFI_PASSWORD;

  if ((status = WiFi.status()) != WL_CONNECTED) {

    WiFi.mode(WIFI_STA);

    WiFi.begin(ssid,password);

    for (i = 0; i < 20; ++i) {

      if ((status = WiFi.status()) == WL_CONNECTED) {

        udp.begin(2380);

        break;
      }

      delay(500);
    }
  }

  return status;
}

/*
 *
 */

#if WEBSERVER

void webserver() {

  int         i;
  char        text[128], text2[16], text3[16], text4[16];
  uint16_t    u16;
  const char *request_s;
  WiFiClient  client;
  String      request;
  static const char *http_header[]     = {"HTTP/1.1 200 OK", "Content-Type: text/html",
                                          "Connection: close", NULL},
                    *html_header[]     = {"<html>", "<head>", 
                                          "<meta http-equiv=\"Refresh\" content=\"5\"/>",
                                          "</head>", "<body>", NULL},
                    *html_footer[]     = {"</body", "</html>", NULL},
                    *status_word_s[16] = {"OTHER", "CML", "TEMPERATURE", "VIN_UV", "IOUT_OC", "VOUT_OV", "OFF", "BUSY",
                                          "UNKNOWN", "OTHER", "FANS", "POWER_GOOD#", "MFR", "INPUT", "IOUT", "VOUT",},
                    *crlf = "\r\n", *on_s = "ON", *off_s = "OFF";

  if (!(client = server.available())) {

    return;
  }

  request   = client.readStringUntil('\r');
  request_s = request.c_str();

  if ((request_s[5] == 'o')&&(request_s[6] == 'n')) {

    psu.on();

  } else if ((request_s[5] == 'o')&&(request_s[6] == 'f')&&(request_s[7] == 'f')) {

    psu.standby();
  }

  for (i = 0; http_header[i]; ++i) {

    client.print(http_header[i]);
    client.print(crlf);
  }

  client.print(crlf);

  for (i = 0; html_header[i]; ++i) {

    client.print(html_header[i]);
    client.print("\n");
  }

  sprintf(text,"<h3 align=\"center\">%s</h3>\n",
          (psu.mfr_model[0]) ? &psu.mfr_model[1]: title);
  client.print(text);

  client.print("<table align=\"center\">\n");

  dtostrf(psu.I_in,5,2,text3);
  dtostrf(psu.W_in,6,1,text4);
  sprintf(text,"<tr><td>Input (V/A/W)</td><td>%d</td><td>%s</td><td>%s</td></tr>\n",
          (int) psu.V_in,text3,text4);
  client.print(text);

  dtostrf(psu.V_out,5,2,text2);
  dtostrf(psu.I_out,5,2,text3);
  dtostrf(psu.W_out,6,1,text4);
  sprintf(text,"<tr><td>Output (V/A/W)</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",
          text2,text3,text4);
  client.print(text);

  dtostrf(psu.T[0],4,1,text2);
  dtostrf(psu.T[1],4,1,text3);
  dtostrf(psu.T[2],4,1,text4);
  sprintf(text,"<tr><td>Temperatures (C)</td><td>%s</td><td>%s</td></tr>\n",
          text2,text3);
  client.print(text);

  sprintf(text,"<tr><td>Fan (rpm)</td><td>%d</td></tr>\n",(int) psu.fan[0]);
  client.print(text);

  client.print("<tr><td align=\"center\" colspan=\"3\"><b>Status</b></td></tr>\n");

  for (i = 0, u16 = psu.status_word; i < 16; ++i) {

    sprintf(text,"<tr><td>%s</td><td>%s</td>",
            status_word_s[i],((u16 & 0x01) ? on_s: off_s));
    client.print(text);

    text[0] = 0;

    switch (i) {

    case  0:

      sprintf(text,"<td>%02x</td>",psu.status_other);
      break;      

    case  1:

      sprintf(text,"<td>%02x</td>",psu.status_cml);
      break;      

    case  2:

      sprintf(text,"<td>%02x</td>",psu.status_temperature);
      break;      

    case  3:
    case 13:

      sprintf(text,"<td>%02x</td>",psu.status_input);
      break;      

    case  4:
    case 14:

      sprintf(text,"<td>%02x</td>",psu.status_iout);
      break;      

    case 5:
    case 15:

      sprintf(text,"<td>%02x</td>",psu.status_vout);
      break;      

    case 12:

      sprintf(text,"<td>%02x</td>",psu.status_mfr_specific);
      break;      
    }

    if (text[0]) {

      client.print(text);
    }
    
    sprintf(text,"</tr>\n");
    client.print(text);

    u16 >>= 1;
  }

  client.print("</table>\n");

  sprintf(text,"<!-- pmbus revision %02x -->\n",psu.pmbus_revision);
  client.print(text);
  sprintf(text,"<!-- vout mode      %02x -->\n",psu.vout_mode);
  client.print(text);
  sprintf(text,"<!-- vout command   %04x -->\n",psu.vout_command);
  client.print(text);
  sprintf(text,"<!-- model          \'%s\' -->\n",&psu.mfr_model[1]);
  client.print(text);
  sprintf(text,"<!-- revision       \'%s\' -->\n",&psu.mfr_revision[1]);
  client.print(text);
#if 0
  sprintf(text,"<!-- total power on %lu s, %d days -->\n",
          psu.total_power_on,(int) (psu.total_power_on / 86400l));
  client.print(text);
#endif
  sprintf(text,"<!-- %s -->\n",build_date);
  client.print(text);
  sprintf(text,"<!-- %s -->\n",request_s);
  client.print(text);

  for (i = 0; html_footer[i]; ++i) {

    client.print(html_footer[i]);
    client.print("\n");
  }

  return;
}

#endif

/*
 *
 */

void get_addresses() {

  my_ip      = WiFi.localIP();
  gateway_ip = WiFi.gatewayIP(); 

  sprintf(my_ip_s,"%d.%d.%d.%d",my_ip[0],my_ip[1],my_ip[2],my_ip[3]);
  sprintf(gateway_s,"%d.%d.%d.%d",gateway_ip[0],gateway_ip[1],gateway_ip[2],gateway_ip[3]);

  return;
}

/*
 *
 */

int syslog(IPAddress server,char *message) {

  char       buffer[128];
  WiFiClient syslog;

  if (strlen(message) > 64) {

    message[64] = 0;
  }

  // <166> is local4, informational

  sprintf(buffer,"<166>1 %s: %s",
          ((psu.mfr_model[0]) ? &psu.mfr_model[1]: "pmbus"),message);

  if (server) {

    udp.beginPacket(server,514);
    udp.print(buffer);
    udp.endPacket();    

    yield();
  }

  return 0;
}

#elif defined(_STM32F103C)

void yield(void) {

  return;
}

#endif

/*
 *
 */
