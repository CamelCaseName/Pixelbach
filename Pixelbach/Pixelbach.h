#pragma once

#ifndef Pixelbach_h
#define Pixelbach_h

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <chrono>
#include <iostream>
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <bitset>
#include <fstream>

/*
*
*
* using a lot of hacked code from the awesome hzeller https://github.com/hzeller/rpi-gpio-dma-demo/
* http://www.raspberrypi.org/wp-content/uploads/2012/02/BCM2835-ARM-Peripherals.pdf
* https://datasheets.raspberrypi.org/bcm2711/bcm2711-peripherals.pdf
* two rows of panels, each 6 panels long
* arranged in a 3x4 grid, so one row goes from bottom right to bottom left, then back to higher right, over to higher left
*
* ###\these two are connected
* ###/
* ###\
* ###/and these ones
*
* 654
* 321 upper
* 654
* 321 lower
*
* panel size each 32x64, so one row is 32x384
* two rows => 64x384 is the number of pixels = 24576 pixels
*/

//pinout for pi and leds
//original went from 0 to 25, now for this hat: https://www.reichelt.com/de/en/raspberry-pi-shield-for-rgb-led-matrix-debo-matrixctrl-p262582.html?&trstct=pos_0&nbc=1
#define A_Up 22
#define B_Up 23
#define C_Up 24
#define D_Up 25
#define R1_Up 11
#define R2_Up 8
#define G1_Up 27
#define G2_Up 9
#define B1_Up 7
#define B2_Up 10
#define CLK_Up 17
#define LAT_Up 4
#define OE_Up 18
#define R1_Lo 12
#define R2_Lo 19
#define G1_Lo 5
#define G2_Lo 13
#define B1_Lo 6
#define B2_Lo 20
#define A_Lo 22
#define B_Lo 23
#define C_Lo 24
#define D_Lo 25
#define CLK_Lo 17
#define LAT_Lo 4
#define OE_Lo 18
//use GND for GND

//bitmasks for first iteration of storage, 555rgb
#define GMASK 0x7E0 //00000 111111 00000 //only use 555 color, is easier, scrap lsb
#define BMASK 0x1F  //00000 000000 11111

//Pi4 peripheral memory base adress (the manuals differ on that one)
#define PERI_BASE  0xFE000000 //0x7E000000 

//pi4 gpio base adress (https://datasheets.raspberrypi.org/bcm2711/bcm2711-peripherals.pdf page 90)
#define GPIO_REGISTER_BASE 0x200000

//memory offset for the first 32 gpio set registers (https://datasheets.raspberrypi.org/bcm2711/bcm2711-peripherals.pdf page 90)
#define GPIO_SET_OFFSET 0x1C

//memory offset for the first 32 gpio clear registers (https://datasheets.raspberrypi.org/bcm2711/bcm2711-peripherals.pdf page 90)
#define GPIO_CLR_OFFSET 0x28

//default memory page size
#define PAGE_SIZE 4096

//Pixelbach
class Pixelbach {
	public:
		uint32_t* retBA();
		inline uint32_t fullToHighColor(int r, int g, int b);
		Pixelbach();
		void start();
		void setPixel(int x, int y, int r, int g, int b);
		volatile uint32_t* buffer;
		uint8_t row = 0;
		volatile uint32_t* gpio_reg = NULL;
		volatile uint32_t* set_reg = NULL;
		volatile uint32_t* clr_reg = NULL;
		void initialize_gpio_for_output(volatile uint32_t* gpio_registerset, int bit);
		int init_drawFast();
		void drawFast();
};

#endif