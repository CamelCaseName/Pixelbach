#include "Pixelbach.h"

//generate bitmasks for the gpio registers
uint32_t OE_MASK = (1 << OE_Lo);
uint32_t CLK_MASK = (1 << CLK_Lo);
uint32_t LAT_MASK = (1 << LAT_Lo);
uint32_t ROW_MASK = (1 << A_Up) + (1 << B_Up) + (1 << C_Up) + (1 << D_Up);
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

volatile uint32_t buffer[24576];
uint8_t row = 0;
Timer t;
volatile uint32_t* gpio_reg = NULL;
volatile uint32_t* set_reg = NULL;
volatile uint32_t* clr_reg = NULL;

// Return a pointer to a periphery subsystem register.
static uint32_t* Pixelbach::mmap_bcm_register(off_t register_offset) {
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

//exit function
void Pixelbach::my_handler(sig_atomic_t s) {
    *clr_reg = -1;
    abort();
}

//initialise gpio register? for output
void Pixelbach::initialize_gpio_for_output(volatile uint32_t* gpio_registerset, int bit) {
    *(gpio_registerset + (bit / 10)) &= ~(7 << ((bit % 10) * 3));  // prepare: set as input
    *(gpio_registerset + (bit / 10)) |= (1 << ((bit % 10) * 3));  // set as output.
}

//converts full 24bit rgb(8:8:8) color to 16bit rgb(5:6:5) high color by chopping off the lower bits
inline int Pixelbach::fullToHighColor(int r, int g, int b) {
    return ((r >> 3) << 11) + ((g >> 2) << 5) + (b >> 3);
}

//initialise all register and stuff for drawing
int Pixelbach::init_drawFast() {
    std::cout << "mapping /sys/mem to memory" << std::endl;
    // Prepare GPIO
    volatile uint32_t* gpio_port = mmap_bcm_register(GPIO_REGISTER_BASE);
    //set all used gpio pins output
    fprintf(stderr, "MMapped to 0x%lx from base 0x%lx, offset 0x%lx\n",
        gpio_port, PERI_BASE, GPIO_REGISTER_BASE);
    for (uint8_t i = 0; i < 26; i++) {
        initialize_gpio_for_output(gpio_port, i);
    }
    std::cout << "all GPIOFSEL set to 1" << std::endl;
    //define registers for use later
    set_reg = gpio_port + (GPIO_SET_OFFSET / sizeof(uint32_t));
    clr_reg = gpio_port + (GPIO_CLR_OFFSET / sizeof(uint32_t));

    if (!errno) return 1;
}

//makes led go colorful
void Pixelbach::drawFast() {
    //start and end pointers for going through the array
    uint32_t* start = (uint32_t*)buffer;
    uint32_t* end = start + (24576 / 4) - 1;

    //outer most loop for "dimming"?
    for (uint8_t i = 1; i < 64; i++) {

        row = 0;

        //OE_Up and OE_Lo to 0
        *clr_reg |= OE_MASK;

        for (const uint32_t* it = start; it < end; ++it) {

            //select row 
            if (!((it - start) % 384)) {
                *clr_reg = ((~row) & 15 << A_Up);
                *set_reg = (row << A_Up);

                ++row;
            }

            //red
            if ((*(it + 0) >> 11)& i) *set_reg = R1_Up_MASK;
            else *clr_reg = R1_Up_MASK;

            if ((*(it + 6144) >> 11)& i) *set_reg = R2_Up_MASK;
            else *clr_reg = R2_Up_MASK;

            if ((*(it + 12288) >> 11)& i) *set_reg = R1_Lo_MASK;
            else *clr_reg = R1_Lo_MASK;

            if ((*(it + 18432) >> 11)& i) *set_reg = R2_Lo_MASK;
            else *clr_reg = R2_Lo_MASK;

            //green
            if (((*(it + 0)) & GMASK >> 5) & i) *set_reg = G1_Up_MASK;
            else *clr_reg = G1_Up_MASK;

            if (((*(it + 0)) & GMASK >> 5) & i) *set_reg = G2_Up_MASK;
            else *clr_reg = G2_Up_MASK;

            if (((*(it + 0)) & GMASK >> 5) & i) *set_reg = G1_Lo_MASK;
            else *clr_reg = G1_Lo_MASK;

            if (((*(it + 0)) & GMASK >> 5) & i) *set_reg = G2_Lo_MASK;
            else *clr_reg = G2_Lo_MASK;

            //blue
            if (*(it + 0) & i) *set_reg = B1_Up_MASK;
            else *clr_reg = B1_Up_MASK;

            if (*(it + 6144) & i) *set_reg = B2_Up_MASK;
            else *clr_reg = B2_Up_MASK;

            if (*(it + 12288) & i) *set_reg = B1_Lo_MASK;
            else *clr_reg = B1_Lo_MASK;

            if (*(it + 18432) & i) *set_reg = B2_Lo_MASK;
            else *clr_reg = B1_Lo_MASK;

            //clock in data
            *set_reg = CLK_MASK;
            *clr_reg = CLK_MASK;
        }

        *set_reg = LAT_MASK;
        *clr_reg = LAT_MASK;
        *set_reg = OE_MASK;
    }
}

//returns the adress of the mapped buffer for other software to write to
uint32_t* Pixelbach::retBA() {
    return (uint32_t*)buffer;
}

//initializes everything, must be called before everything else
int Pixelbach::init() {
    signal(SIGINT, my_handler);

    if (init_drawFast()) std::cout << "init done" << std::endl;
    else abort(); 

    std::cout << "clearing pins" << std::endl;

    *clr_reg = -1;

    std::cout << "cleared all 32 pins" << std::endl;

    //fill buffer with only red
    for (uint16_t j = 0; j < 24576; j++) {
        buffer[j] = 0xFFFF;
    }

    std::cout << "displaying buffer till ended" << std::endl;
    while(true) {
        drawFast();
    }
}