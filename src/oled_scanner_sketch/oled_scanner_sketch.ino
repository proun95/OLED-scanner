/*
This file is part of OLED 5.8ghz Scanner project.

    OLED 5.8ghz Scanner is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OLED 5.8ghz Scanner is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Nome-Programma.  If not, see <http://www.gnu.org/licenses/>.

    Copyright © 2016 Michele Martinelli
  */

#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>
#include "U8glib.h"
#include "PinChangeInterrupt.h"
#include "rx5808.h"
#include "const.h"

// devices with all constructor calls is here: http://code.google.com/p/u8glib/wiki/device
U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_DEV_0 | U8G_I2C_OPT_NO_ACK | U8G_I2C_OPT_FAST); // Fast I2C / TWI

RX5808 rx5808(rssi_pin);

byte data0 = 0;
byte data1 = 0;
byte data2 = 0;
byte data3 = 0;

#define SCANNER_MODE 0
#define RECEIVER_MODE 1

uint8_t curr_status = SCANNER_MODE;
uint8_t curr_screen = 6;
uint8_t curr_channel = 0;
uint32_t curr_freq;

//battery monitor
float volt;
uint16_t VoltDivider = 10.5;

uint32_t last_irq;
uint8_t changing_freq, changing_mode;

//default values used for calibration
uint16_t rssi_min = 1024;
uint16_t rssi_max = 0;

void irq_select_handle() {
  if (millis() - last_irq < 500) //debounce
    return;

  if (digitalRead(button_select) == HIGH && millis() - last_irq > 800) {
    if (curr_status == SCANNER_MODE) {
        curr_screen = 6;
    }
    else if (curr_status == RECEIVER_MODE) {
      curr_channel = rx5808.getNext(curr_channel);
      changing_freq = 1;
    }
  }
  else if (digitalRead(button_select) == LOW) {
    if (curr_status == SCANNER_MODE) {
        curr_screen = (curr_screen + 1) % 7;
    }
    else if (curr_status == RECEIVER_MODE) {
      curr_channel = (curr_channel / 8) * 8 + (curr_channel + 1) % 8;
      changing_freq = 1;
    }
  }

  rx5808.abortScan();
  last_irq = millis();
}

void irq_mode_handle() {
  if (millis() - last_irq < 500) //debounce
    return;

  if (digitalRead(button_mode) == HIGH && millis() - last_irq > 800) {
    rx5808.abortScan();
    changing_mode = 1;
    if (curr_status == RECEIVER_MODE) {
      curr_status = SCANNER_MODE;
    } else {
      curr_status = RECEIVER_MODE;
      curr_channel = rx5808.getMaxPos(); //next channel is the strongest
    }
  }
  else if (digitalRead(button_mode) == LOW) {
    if (curr_status == SCANNER_MODE) {
        curr_screen = curr_screen==0 ? 6 : (curr_screen - 1) % 7;
        rx5808.abortScan();
    }
    else if (curr_status == RECEIVER_MODE) {
        curr_channel = (curr_channel + 8 - curr_channel % 8) % CHANNEL_MAX;
        changing_freq = 1; ///////
        rx5808.abortScan(); //////
    }
  }

  last_irq = millis();
}

void setup() {
  //button init
  pinMode(button_select, INPUT_PULLUP);
  pinMode(button_mode, INPUT_PULLUP);

  //display init
  // assign default color value
  if ( u8g.getMode() == U8G_MODE_R3G3B2 ) {
    u8g.setColorIndex(255);     // white
  }
  else if ( u8g.getMode() == U8G_MODE_GRAY2BIT ) {
    u8g.setColorIndex(3);         // max intensity
  }
  else if ( u8g.getMode() == U8G_MODE_BW ) {
    u8g.setColorIndex(1);         // pixel on
  }
  else if ( u8g.getMode() == U8G_MODE_HICOLOR ) {
    u8g.setHiColorByRGB(255, 255, 255);
  }

  u8g.setDefaultForegroundColor();
  splashScr();

  // initialize SPI:
  pinMode (SSP, OUTPUT);
  SPI.begin();
  SPI.setBitOrder(LSBFIRST);
  rx5808.init();

  //power on calibration if button pressed
  while (digitalRead(button_select) == LOW || digitalRead(button_mode) == LOW) {
    rx5808.calibration();
  }

  //receiver init
  curr_channel = rx5808.getMaxPos();
  changing_freq = 1;
  changing_mode = 0;

  //rock&roll
  attachPCINT(digitalPinToPCINT(button_select), irq_select_handle, CHANGE);
  attachPCINT(digitalPinToPCINT(button_mode), irq_mode_handle, CHANGE);
}

void loop(void) {
  battery_measure();

  if (curr_status == SCANNER_MODE) {
    rx5808.scan(1, BIN_H);
  }

  u8g.firstPage();
  do {
    if (changing_mode) { //if changing mode "Loading..."
      wait_draw();
    } else {
      if (curr_status == RECEIVER_MODE) {
        receiver_draw(curr_channel);
      }
      else if (curr_status == SCANNER_MODE) {
        scanner_draw(curr_screen);
      }
    }
  } while ( u8g.nextPage() );

  if (curr_status == RECEIVER_MODE && changing_freq || changing_mode) {
    changing_freq = changing_mode = 0;
    rx5808.scan(1, BIN_H);
    curr_freq = pgm_read_word_near(channelFreqTable + curr_channel);
    rx5808.setFreq(curr_freq);
  }
}
