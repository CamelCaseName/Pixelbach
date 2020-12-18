#include <stdio.h>
#include <stdlib.h>
#include <pigpio.h>
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
#include <bcm2835.h>
using namespace std;


//https://datasheets.raspberrypi.org/bcm2711/bcm2711-peripherals.pdf
//or
//http://www.raspberrypi.org/wp-content/uploads/2012/02/BCM2835-ARM-Peripherals.pdf

//using a lot of hacked code from the awesome hzeller https://github.com/hzeller/rpi-gpio-dma-demo/blob/master/gpio-dma-test.c

/*
* two rows of panels, each 6 panels long
*
* arranged in a 3x4 grid, so one row goes from bottom right to bottom left, then back to higher right, over to higher left
*
* ###\these wto are connected
* ###/
* ###\
* ###/and these ones
*
*
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
#define A_Up 0
#define B_Up 1
#define C_Up 2
#define D_Up 3
#define R1_Up 4
#define R2_Up 5
#define G1_Up 6
#define G2_Up 7
#define B1_Up 8
#define B2_Up 9
#define CLK_Up 10
#define LAT_Up 11
#define OE_Up 12
#define R1_Lo 13
#define R2_Lo 14
#define G1_Lo 15
#define G2_Lo 16
#define B1_Lo 17
#define B2_Lo 18
#define A_Lo 19
#define B_Lo 20
#define C_Lo 21
#define D_Lo 22
#define CLK_Lo 23
#define LAT_Lo 24
#define OE_Lo 25
//use GND for GND

//generate bitmasks for the gpio registers
uint32_t OE_MASK = (1 << OE_Lo) + (1 << OE_Up);
uint32_t CLK_MASK = (1 << CLK_Lo) + (1 << CLK_Up);
uint32_t LAT_MASK = (1 << LAT_Lo) + (1 << LAT_Up);
uint32_t ROW_UP_MASK = (1 << A_Up) + (1 << B_Up) + (1 << C_Up) + (1 << D_Up);
uint32_t ROW_LO_MASK = (1 << A_Lo) + (1 << B_Lo) + (1 << C_Lo) + (1 << D_Lo);
uint32_t R1_Up_MASK = (1 << R1_Up);
uint32_t R2_Up_MASK = (1 << R2_Up);
uint32_t R1_Lo_MASK = (1 << R1_Lo);
uint32_t R2_Lo_MASK = (1 << R2_Lo);
uint32_t G1_Up_MASK = (1 << G1_Up);
uint32_t G2_Up_MASK = (1 << G2_Up);
uint32_t G1_Lo_MASK = (1 << G1_Lo);
uint32_t G2_Lo_MASK = (1 << G2_Lo);
uint32_t B1_Up_MASK = (1 << B1_Up);
uint32_t B2_Up_MASK = (1 << B2_Up);
uint32_t B1_Lo_MASK = (1 << B1_Lo);
uint32_t B2_Lo_MASK = (1 << B2_Lo);

//bitmasks for first iteration of storage, 555rgb
#define gmask 0x7C0 //00000 111110 00000 //only use 555 color, is easier, scrap lsb
#define bmask 0x1F  //00000 000000 11111

//Pi4 peripheral memory base adress 
#define PERI_BASE  0xFE00000000 //0x7E000000 

//pi4 gpio base adress (https://datasheets.raspberrypi.org/bcm2711/bcm2711-peripherals.pdf page 90)
#define GPIO_REGISTER_BASE 0x200000

//memory offset for the first 32 gpio set registers (https://datasheets.raspberrypi.org/bcm2711/bcm2711-peripherals.pdf page 90)
#define GPIO_SET_OFFSET 0x1C

//memory offset for the first 32 gpio clear registers (https://datasheets.raspberrypi.org/bcm2711/bcm2711-peripherals.pdf page 90)
#define GPIO_CLR_OFFSET 0x28

//default memory page size
#define PAGE_SIZE 4096

//offsets to the gpio function select registers, set to 1 for gpio use
//each register is 3 bits wide
#define GPIO_FSEL_0TO9_OFFSET   0x0 //0x7E200000 
#define GPIO_FSEL_10TO19_OFFSET 0x4 //0x7E200004 
#define GPIO_FSEL_20TO29_OFFSET 0x8 //0x7E200008 


// Return a pointer to a periphery subsystem register.
static uint32_t* mmap_bcm_register(off_t register_offset) {
    const off_t base = PERI_BASE;

    int mem_fd;
    if ((mem_fd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
        perror("can't open /dev/mem: ");
        fprintf(stderr, "You need to run this as root!\n");
        return NULL;
    }

    uint32_t* result =
        (uint32_t*)mmap(NULL,		// Any adddress in our space will do
            PAGE_SIZE,
            PROT_READ | PROT_WRITE,  // Enable r/w on GPIO registers.
            MAP_SHARED,
            mem_fd,                // File to map
            base + register_offset // Offset to bcm register
        );
    close(mem_fd);

    if (result == MAP_FAILED) {
        fprintf(stderr, "mmap error %p\n", result);
        return NULL;
    }
    return result;
}



class Timer {
private:
    // Type aliases to make accessing nested type easier
    using clock_t = std::chrono::high_resolution_clock;
    using second_t = std::chrono::duration<double, std::ratio<1> >;

    std::chrono::time_point<clock_t> m_beg;

public:
    Timer() : m_beg(clock_t::now()) {
    }

    void reset() {
        m_beg = clock_t::now();
    }

    double elapsed() const {
        return std::chrono::duration_cast<second_t>(clock_t::now() - m_beg).count();
    }
};

uint32_t buffer[24576];
uint8_t row = 0;
Timer t;
volatile uint32_t* gpio_reg = NULL;
volatile uint32_t* set_reg = NULL;
volatile uint32_t* clr_reg = NULL;
fstream gpio[27];

/*			Code from https://www.iot-programmer.com/index.php/books/22-raspberry-pi-and-the-iot-in-c/chapters-raspberry-pi-and-the-iot-in-c/59-raspberry-pi-and-the-iot-in-c-memory-mapped-gpio?start=3

#include <stdio.h>
#include <stdlib.h>
//#include <bcm2835.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <map>
#include <string.h>

int main(int argc, char** argv) {
    int memfd = open("/dev/mem", O_RDWR | O_SYNC);
    uint32_t* map = (uint32_t*)mmap(
        NULL,
        4 * 1024,
        (PROT_READ | PROT_WRITE),
        MAP_SHARED,
        memfd,
        0x7e200000);
    if (map == MAP_FAILED)
        printf("bcm2835_init: %s mmap failed: %s\n", strerror(errno));
    //close(memfd); //doesnt work?

    volatile uint32_t* fsel = map;
    *fsel = 0x1000; //pin 1 to out
    volatile uint32_t* set = map + 0x1C / 4;
    volatile uint32_t* clr = map + 0x28 / 4;
    for (;;) {
        *set = 0x10; //set pin 1 high
        *clr = 0x10; //clr pin 1 to low
    };
    return (EXIT_SUCCESS);
}

*/

//exit function
void my_handler(sig_atomic_t s) {
    //int i = deinit_drawFastFile();
    abort();
}



//initialise gpio register? for output
void initialize_gpio_for_output(volatile uint32_t* gpio_registerset, int bit) {
    *(gpio_registerset + (bit / 10)) &= ~(7 << ((bit % 10) * 3));  // prepare: set as input
    *(gpio_registerset + (bit / 10)) |= (1 << ((bit % 10) * 3));  // set as output.
}

//just to steal code
void run_cpu_from_memory_masked() {
    // Prepare GPIO
    volatile uint32_t* gpio_port = mmap_bcm_register(GPIO_REGISTER_BASE);
    initialize_gpio_for_output(gpio_port, 10);
    volatile uint32_t* set_reg = gpio_port + (GPIO_SET_OFFSET / sizeof(uint32_t));
    volatile uint32_t* clr_reg = gpio_port + (GPIO_CLR_OFFSET / sizeof(uint32_t));

    // Prepare data.
    const int n = 256;
    uint32_t* gpio_data = (uint32_t*)malloc(n * sizeof(*gpio_data));
    for (int i = 0; i < n; ++i) {
        // To toggle our pin, alternate between set and not set.
        gpio_data[i] = (i % 2 == 0) ? (1 << 10) : 0;
    }

    // Do it. Endless loop: reading, writing.
    printf("2) CPU: reading word from memory, write masked to GPIO set/clr.\n"
        "== Press Ctrl-C to exit.\n");
    const uint32_t mask = (1 << 10);
    const uint32_t* start = gpio_data;
    const uint32_t* end = start + n;
    for (;;) {
        for (const uint32_t* it = start; it < end; ++it) {
            if ((*it & mask) != 0) *set_reg = *it & mask;
            if ((~*it & mask) != 0) *clr_reg = ~*it & mask;
        }
    }

    free(gpio_data);  // (though never reached due to Ctrl-C)
}

inline void bitWrite(volatile uint32_t* reg, uint32_t mask, uint32_t value) {

    uint32_t           regval = *reg;
    const uint32_t     width = mask & 0xff;
    const uint32_t     bitno = mask >> 16;
    regval &= ~(((1 << width) - 1) << bitno);
    regval |= value << bitno;
    *reg = regval;
    //cout << *(gpio_reg + 0x34) << endl;
}

int init_draw() {

    if (gpioInitialise() < 0) exit(1);

    for (int i = 0; i < 26; i++) {
        gpioSetMode(i, 1);
    }

    return 1;
}

void draw() {
    //6 bits for green

    for (uint8_t i = 0; i < 16; i++) {

        row = 0;

        //initial zero rows plus oe low
        gpioWrite(OE_Lo, 0);
        gpioWrite(OE_Up, 0);

        //only fourth because we set 4 pixels at once
        for (uint16_t bufId = 0; bufId < 6144; bufId++) {

            //color selection takes the longest...

            //first version

            //if the set number for this color is smaller than the current iteration (half of it because we have one bit less), we set it according to the eval
            gpioWrite(R1_Up, (buffer[bufId + 0] >> 11) > i);
            //r2 is offset by 16x384 pixels, half a panel in height, equals 24576
            gpioWrite(R2_Up, (buffer[bufId + 6144] >> 11) > i);
            //lower half is offset by 49152 pixels, half of all leds
            gpioWrite(R1_Lo, (buffer[bufId + 12288] >> 11) > i);
            //r2 off lower half is offset twice, by 73728 pixels
            gpioWrite(R2_Lo, (buffer[bufId + 18432] >> 11) > i);

            //same for green
            //and for getting only the bits i want, with a prepared mask
            gpioWrite(G1_Up, ((buffer[bufId + 0] & gmask) >> 5) > i);
            gpioWrite(G2_Up, ((buffer[bufId + 6144] & gmask) >> 5) > i);
            gpioWrite(G1_Lo, ((buffer[bufId + 12288] & gmask) >> 5) > i);
            gpioWrite(G2_Lo, ((buffer[bufId + 18432] & gmask) >> 5) > i);

            //same for blue
            //and for getting only the bits i want, with a prepared mask
            gpioWrite(B1_Up, (buffer[bufId + 0] & bmask) > i);
            gpioWrite(B2_Up, (buffer[bufId + 6144] & bmask) > i);
            gpioWrite(B1_Lo, (buffer[bufId + 12288] & bmask) > i);
            gpioWrite(B2_Lo, (buffer[bufId + 18432] & bmask) > i);


            //second version
            /*
            gpioWrite(R1_Up, Sbuffer[bufId].r > i);
            gpioWrite(R2_Up, Sbuffer[bufId + 24576].r > i);
            gpioWrite(R1_Lo, Sbuffer[bufId + 49152].r > i);
            gpioWrite(R2_Lo, Sbuffer[bufId + 73728].r > i);
            gpioWrite(G1_Up, Sbuffer[bufId].g > i);
            gpioWrite(G2_Up, Sbuffer[bufId + 24576].g > i);
            gpioWrite(G1_Lo, Sbuffer[bufId + 49152].g > i);
            gpioWrite(G2_Lo, Sbuffer[bufId + 73728].g > i);
            gpioWrite(B1_Up, Sbuffer[bufId].b > i);
            gpioWrite(B2_Up, Sbuffer[bufId + 24576].b > i);
            gpioWrite(B1_Lo, Sbuffer[bufId + 49152].b > i);
            gpioWrite(B2_Lo, Sbuffer[bufId + 73728].b > i);
            */



            //clock once
            gpioWrite(CLK_Lo, 1);
            gpioWrite(CLK_Up, 1);
            gpioWrite(CLK_Lo, 0);
            gpioWrite(CLK_Up, 0);

            //after clocking in 384 pixels, we change row 
            if (!(bufId % 384)) {
                ++row;
                //set rows correctly
                gpioWrite(A_Lo, row & 1);
                gpioWrite(A_Up, row & 1);
                gpioWrite(B_Lo, (row >> 1) & 1);
                gpioWrite(B_Up, (row >> 1) & 1);
                gpioWrite(C_Lo, (row >> 2) & 1);
                gpioWrite(C_Up, (row >> 2) & 1);
                gpioWrite(D_Lo, (row >> 3) & 1);
                gpioWrite(D_Up, (row >> 3) & 1);
            }
        }

        //after setting allpixels, oe and lat high, the low again
        gpioWrite(LAT_Lo, 1);
        gpioWrite(LAT_Up, 1);
        gpioWrite(LAT_Lo, 0);
        gpioWrite(LAT_Up, 0);
        gpioWrite(OE_Up, 1);
        gpioWrite(OE_Up, 1);


    }

}

int init_drawFast() {

    // Prepare GPIO
    volatile uint32_t* gpio_port = mmap_bcm_register(GPIO_REGISTER_BASE);
    //set all used gpio pins output
    for (uint8_t i = 0; i < 26; i++) {
        initialize_gpio_for_output(gpio_port, i);
    }
    //define registers for use later
    set_reg = gpio_port + (GPIO_SET_OFFSET / sizeof(uint32_t));
    clr_reg = gpio_port + (GPIO_CLR_OFFSET / sizeof(uint32_t));

    if (!errno) return 1;
}

void drawFast() {
    //start and end pointers for going through the array
    const uint32_t* start = buffer;
    const uint32_t* end = start + (24576 / 4) - 1;

    //outer most loop for "dimming"?
    for (uint8_t i = 1; i < 32; i++) {

        row = 0;

        //OE_Up and OE_Lo to 0
        *clr_reg |= OE_MASK;

        for (const uint32_t* it = start; it < end; ++it) {

            //R1_Up
            //cout << (it - start) << endl;

            //kann ich immer einfach so  ORen? |=
            //oder muss ich erst n dummy agregieren, dann nur schreiben?

            if ((*(it + 0) >> 11)& i) *set_reg |= R1_Up_MASK;
            else *clr_reg |= R1_Up_MASK;

            if ((*(it + 6144) >> 11)& i) *set_reg |= R2_Up_MASK;
            else *clr_reg |= R2_Up_MASK;

            if ((*(it + 12288) >> 11)& i) *set_reg |= R1_Lo_MASK;
            else *clr_reg |= R1_Lo_MASK;

            if ((*(it + 18432) >> 11)& i) *set_reg |= R2_Lo_MASK;
            else *clr_reg |= R1_Lo_MASK;

            /*
            //R2_Up
            if ((((*it + 24576) & (i << 11)) & R2_Up_MASK) != 0) *set_reg = ((*it + 24576) & (i << 11)) & R2_Up_MASK;
            if ((~((*it + 24576) & (i << 11)) & R2_Up_MASK) != 0) *clr_reg = ~((*it + 24576) & (i << 11)) & R2_Up_MASK;
            */

            //clock in data
            *set_reg |= CLK_MASK;
            //*set_reg &= ~CLK_MASK;
            *clr_reg |= CLK_MASK;

            //after clocking in 384 pixels, we change row 
            if (!((it - start) % 384)) {

                *set_reg |= (row << A_Up);
                *clr_reg &= ~(row << A_Up);

                *set_reg |= (row << A_Lo);
                *clr_reg &= ~(row << A_Lo);

                ++row;
            }
        }

        *set_reg |= LAT_MASK;
        *clr_reg |= LAT_MASK;
        *set_reg |= OE_MASK;
    }
}

int init_drawFastDIY() {

    
    //init mapped adress plus offsets
    gpio_reg = mmap_bcm_register(GPIO_REGISTER_BASE);
    set_reg = gpio_reg + (GPIO_SET_OFFSET / 4);
    clr_reg = gpio_reg + (GPIO_CLR_OFFSET / 4);

    //fsel0, fsel1 and fsel2
    *(gpio_reg + 0)= 0x9249249; //1001001001001001001001001001
    *(gpio_reg + 4)= 0x9249249;
    *(gpio_reg + 8)= 0x9249249;

    msync((void*)gpio_reg, 0xFF,MS_SYNC);

    //bitset<32> t1(*gpio_reg);
    bitset<32> t2(*(gpio_reg + (0x34 / 4)));
    cout << t2 << endl;
    /*

    // Prepare GPIO
    volatile uint32_t* gpio_port = mmap_bcm_register(GPIO_REGISTER_BASE);
    //set all used gpio pins output
    for (uint8_t i = 0; i < 26; i++) {
        initialize_gpio_for_output(gpio_port, i);
    }

    */
    //define registers for use later
    //set_reg = gpio_port + (GPIO_SET_OFFSET / sizeof(uint32_t));
    //clr_reg = gpio_port + (GPIO_CLR_OFFSET / sizeof(uint32_t));

    if (!errno) return 1;
}

void drawFastDIY() {
    //start and end pointers for going through the array
    const uint32_t* start = buffer;
    const uint32_t* end = start + 24576;



    //TODO
    //register als define mit pointer zu adressen und dann wie beim ardiuno

    //outer most loop for "dimming"?
    for (uint8_t i = 0; i < 32; i++) {

        row = 0;

        //OE_Up and OE_Lo to 0
        //*gpio_reg = ~OE_MASK;
        bitWrite(set_reg, OE_MASK, 0);
        bitWrite(clr_reg, OE_MASK, 1);

        for (const uint32_t* it = start; it < end; ++it) {

            //R1_Up
            bitWrite(set_reg, R1_Up_MASK, (*it & (i << 11)));
            bitWrite(clr_reg, R1_Up_MASK, ~(*it & (i << 11)));
            bitWrite(set_reg, R2_Up_MASK, (*(it + 24576) & (i << 11)));
            bitWrite(clr_reg, R2_Up_MASK, ~(*(it + 24576) & (i << 11)));
            bitWrite(set_reg, R1_Lo_MASK, (*(it + 49152) & (i << 11)));
            bitWrite(clr_reg, R1_Lo_MASK, ~(*(it + 49152) & (i << 11)));
            bitWrite(set_reg, R2_Lo_MASK, (*(it + 73728) & (i << 11)));
            bitWrite(clr_reg, R2_Lo_MASK, ~(*(it + 73728) & (i << 11)));


            //clock in data
            bitWrite(set_reg, CLK_MASK, 1);
            bitWrite(set_reg, CLK_MASK, 0);
            bitWrite(clr_reg, CLK_MASK, 1);
            bitWrite(clr_reg, CLK_MASK, 0);
            //after clocking in 384 pixels, we change row 
            if (!((it - start) % 384)) {
                ++row;
                bitWrite(set_reg, ROW_UP_MASK, row);
                bitWrite(clr_reg, ROW_UP_MASK, ~row);

                bitWrite(set_reg, ROW_LO_MASK, (row) << A_Lo);
                bitWrite(clr_reg, ROW_LO_MASK, ~(row << A_Lo));
            }
        }

        bitWrite(set_reg, LAT_MASK, 1);
        bitWrite(clr_reg, LAT_MASK, 0);

        bitWrite(set_reg, LAT_MASK, 0);
        bitWrite(clr_reg, LAT_MASK, 1);

        bitWrite(set_reg, OE_MASK, 1);
        bitWrite(clr_reg, OE_MASK, 0);
    }
}

int init_drawFastFile() {
    //export all gpios
    gpio[26].open("/sys/class/gpio/export", ios::out | ios::binary);
    //makes all pins accessible
    for (int i = 0; i < 26; i++) {
        gpio[26] << i;
        gpio[26].sync();
    }
    gpio[26].close();

    //sets all pin modes to output
    for (int i = 0; i < 26; i++) {
        string temp = "/sys/class/gpio/gpio";
        switch (i / 10)
        {
        case 0:
            temp += (int)i + 48;
            break;
        case 1:
            temp += 49;
            temp += (int)i + 38;
            break;
        case 2:
            temp += 50;
            temp += (int)i + 28;
            break;
        default:
            break;
        }
        //temp += (int)i + 48;
        temp += "/direction";
        
        gpio[26].open((temp), ios::out | ios::binary);
        gpio[26] << "out";
        gpio[26].close();
    }
    //gpio[26].close();

    //create a fs for every gpio value file
    for (int i = 0; i < 26; i++) {
        string temp = "/sys/class/gpio/gpio";
        switch (i / 10)
        {
        case 0:
            temp += (int)i + 48;
            break;
        case 1:
            temp += 49;
            temp += (int)i + 38;
            break;
        case 2:
            temp += 50;
            temp += (int)i + 28;
            break;
        default:
            break;
        }
        //temp += (int)i + 48;
        temp += "/value";
        cout << "pin nr. " << i << " | directory: " << temp << endl;
        gpio[i].open((temp), ios::out | ios::binary);
    }

    if (!errno) return 1;
}

int deinit_drawFastFile() {
    //unexport all gpios
    gpio[26].open("/sys/class/gpio/unexport", ios::out | ios::binary);
    //sets all pin modes to output
    for (int i = 0; i < 26; i++) {
        gpio[i].close();
        gpio[26] << i;
        gpio[26].sync();
    }
    gpio[26].close();
    return 1;
}

void drawFastFile() {
    //start and end pointers for going through the array
    const uint32_t* start = buffer;
    const uint32_t* end = start + (24576 / 4) - 1;

    //outer most loop for "dimming"?
    for (uint8_t i = 0; i < 32; i++) {

        row = 0;

        cout << "dimmed " << (int)i << " time" << endl;

        //OE_Up and OE_Lo to 0
        gpio[OE_Up] << 0;
        gpio[OE_Lo] << 0;

        for (const uint32_t* it = start; it < end; ++it) {

            //R1_Up
            gpio[R1_Up] << ((*(it + 0) >> 11)& i);
            gpio[R2_Up] << ((*(it + 6144) >> 11)& i);
            gpio[R1_Lo] << ((*(it + 12288) >> 11)& i);
            gpio[R2_Lo] << ((*(it + 18432) >> 11)& i);


            gpio[G1_Up] << ((*(it + 0) & gmask) & (i << 6));
            gpio[G2_Up] << ((*(it + 6144) & gmask) & (i << 6));
            gpio[G1_Lo] << ((*(it + 12288) & gmask) & (i << 6));
            gpio[G2_Lo] << ((*(it + 18432) & gmask) & (i << 6));


            gpio[B1_Up] << ((*(it + 0) & bmask) & i);
            gpio[B2_Up] << ((*(it + 6144) & bmask) & i);
            gpio[B1_Lo] << ((*(it + 12288) & bmask) & i);
            gpio[B2_Lo] << ((*(it + 18432) & bmask) & i);


            //clock in data
            gpio[CLK_Lo] << 1;
            gpio[CLK_Up] << 1;
            gpio[CLK_Lo] << 0;
            gpio[CLK_Up] << 0;

            //after clocking in 384 pixels, we change row 
            if (!((it - start) % 384)) {
                ++row;

                cout << "current row: " << (int)row << endl;


                gpio[A_Lo] << (row);
                gpio[A_Up] << (row);
                gpio[B_Lo] << (row >> 1);
                gpio[B_Up] << (row >> 1);
                gpio[C_Lo] << (row >> 2);
                gpio[C_Up] << (row >> 2);
                gpio[D_Lo] << (row >> 3);
                gpio[D_Up] << (row >> 3);
            }
        }

        gpio[LAT_Lo] << 1;
        gpio[LAT_Up] << 1;
        gpio[LAT_Lo] << 0;
        gpio[LAT_Up] << 0;
        gpio[OE_Lo] << 1;
        gpio[OE_Up] << 1;

    }
}



int main() {

    /*
        uint32_t* sysfs_gpio;
        cout << "trying to map gpio folder" << endl;
        int gpio_export;
        gpio_export = open("/sys/class/gpio/export", O_RDWR | O_SYNC);

        cout << "folder returned: " << gpio_export << " | errno: " << errno << endl;
        sysfs_gpio = (uint32_t*)mmap(NULL, 4096, (PROT_READ | PROT_WRITE), MAP_SHARED, gpio_export, 0);
        cout << "mapped gpio folder at: " << sysfs_gpio << ", errno: " << errno << endl;
               */

    signal(SIGINT, my_handler);

    //gpioInitialise();
    //int tt = deinit_drawFastFile();
    if (init_drawFastDIY()) cout << "init done" << endl;
    else abort(); 
    //if (init_drawFastFile()) cout << "init done" << endl;
    //else abort();

    cout << "clearing pins" << endl;

    *clr_reg = -1;

    cout << "cleared all 32 pins" << endl;
    //gpioDelay(5);
    cout << "flipping pin 1" << endl;
    while (true)
    {
        //gpio[1] << 1;
        //gpio[1].sync();
        //gpio[1] << 0;
        //gpio[1].sync();
        *set_reg = -1;
        *clr_reg = -1;
        //gpioWrite(1, 1);
        //gpioWrite(1, 0);

    }

    /*

    cout << "setting all pins to output" << endl;

    //initialize accordingly

    //if (!init_draw()) abort();
    //if (!init_drawFast()) abort();
    //if (!init_drawFastDIY()) abort();
    //if(!init_drawFastFile()) abort();

    cout << "all GPIOFSEL set to 1" << endl;

    //fill buffer with only red
    for (uint16_t j = 0; j < 24576; j++) {
        buffer[j] = 0xF800;
    }

    //reset timer for speed comparison
    t.reset();

    cout << "running 25 iterations timed" << endl;
    for (uint16_t k = 0; k < 25; k++) {
        //draw();
        //drawFast();
        //drawFastDIY();
        //drawFastFile();
    }

    cout << t.elapsed() << endl;

    //deinit_drawFastFile();

         */
}


/*
int main(int argc, char** argv) {
    int memfd = open("/dev/mem", O_RDWR | O_SYNC);
    uint32_t* map = (uint32_t*)mmap(
        NULL,
        4 * 1024,
        (PROT_READ | PROT_WRITE),
        MAP_SHARED,
        memfd,
        0x7e200000);
    if (map == MAP_FAILED)
        printf("bcm2835_init: %s mmap failed: %s\n", strerror(errno));
    //close(memfd); //doesnt work?

    volatile uint32_t* fsel = map;
    *fsel = 0x1000; //pin 1 to out
    volatile uint32_t* set = map + 0x1C / 4;
    volatile uint32_t* clr = map + 0x28 / 4;
    for (;;) {
        *set = 0x10; //set pin 1 high
        *clr = 0x10; //clr pin 1 to low
    };
    return (EXIT_SUCCESS);
}
*/