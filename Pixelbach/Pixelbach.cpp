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

volatile uint32_t* buffer;
uint8_t row = 0;
volatile uint32_t* gpio_reg = NULL;
volatile uint32_t* set_reg = NULL;
volatile uint32_t* clr_reg = NULL;

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

//exit function
void my_handler(sig_atomic_t s) {
	free((void*)buffer);
	*clr_reg = -1;
	abort();
}

//initialise gpio register? for output
void Pixelbach::initialize_gpio_for_output(volatile uint32_t* gpio_registerset, int bit) {
	*(gpio_registerset + (bit / 10)) &= ~(7 << ((bit % 10) * 3));  // prepare: set as input
	*(gpio_registerset + (bit / 10)) |= (1 << ((bit % 10) * 3));  // set as output.
}

//converts full 24bit rgb(8:8:8) color to 16bit rgb(5:6:5) high color by chopping off the lower bits
inline uint32_t Pixelbach::fullToHighColor(int r, int g, int b) {
	return ((int)(((double)(r * 31) / 255.0) + 0.5) << 11) | ((int)(((double)(g * 63) / 255.0) + 0.5) << 5) | (int)(((double)(b * 31) / 255.0) + 0.5);
}

//initialise all register and stuff for drawing
int Pixelbach::init_drawFast() {
	std::cout << "mapping /sys/mem to memory" << std::endl;
	// Prepare GPIO
	volatile uint32_t* gpio_port = mmap_bcm_register(GPIO_REGISTER_BASE);
	//set all used gpio pins output
	fprintf(stderr, "MMapped to 0x%lx from base 0x%lx, offset 0x%lx\n",
		gpio_port, PERI_BASE, GPIO_REGISTER_BASE);
	for (uint8_t i = 0; i < 28; i++) {
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
	uint32_t* end = start + 6144;

	//outer most loop for "dimming"?
	for (uint8_t i = 22; i > 0; --i) {

		row = 0;

		for (const uint32_t* it = start; it < end; it++) {
			//select row 
			if (!((it - start) % 384)) {
				*clr_reg = ((~row) & 15 << A_Up);
				*set_reg = ((row - 1) & 15) << A_Up;

				*set_reg = LAT_MASK;
				for (uint8_t timedel = 0; timedel < 18; timedel++) {
					__asm__("nop");
				}
				*clr_reg = LAT_MASK;
				*set_reg = OE_MASK;
				*clr_reg = OE_MASK;
				row++;
			}

			/*
			* je höher der wert, desto heller <=> mehr an
			* z.b. bei 31 muss jeden cycle an sein, 0 nie,
			* 1 mal aus 32 = 1/32
			* 2 mal aus 32 = 1/16
			* 3 mal aus 32 = 3/32
			* 4 mal aus 32 = 1/8
			* 5 mal aus 32 = 5/32
			* 6 mal aus 32 = 3/16
			* 7 mal aus 32 = 7/32
			* 8 mal aus 32 = 1/4
			*
			*/

			//red
			if ((*it >> 11) > i) *set_reg = R1_Up_MASK;
			else *clr_reg = R1_Up_MASK;

			if ((*(it + 6144) >> 11) > i) *set_reg = R2_Up_MASK;
			else *clr_reg = R2_Up_MASK;

			if ((*(it + 12288) >> 11) > i) *set_reg = R1_Lo_MASK;
			else *clr_reg = R1_Lo_MASK;

			if ((*(it + 18432) >> 11) > i) *set_reg = R2_Lo_MASK;
			else *clr_reg = R2_Lo_MASK;

			//green
			if ((((*it) & GMASK) >> 5) > i) *set_reg = G1_Up_MASK;
			else *clr_reg = G1_Up_MASK;

			if ((((*(it + 6144)) & GMASK) >> 5) > i) *set_reg = G2_Up_MASK;
			else *clr_reg = G2_Up_MASK;

			if ((((*(it + 12288)) & GMASK) >> 5) > i) *set_reg = G1_Lo_MASK;
			else *clr_reg = G1_Lo_MASK;

			if ((((*(it + 18432)) & GMASK) >> 5) > i) *set_reg = G2_Lo_MASK;
			else *clr_reg = G2_Lo_MASK;

			//blue
			if (((*it) & BMASK) > i) *set_reg = B1_Up_MASK;
			else *clr_reg = B1_Up_MASK;

			if ((*(it + 6144) & BMASK) > i) *set_reg = B2_Up_MASK;
			else *clr_reg = B2_Up_MASK;

			if ((*(it + 12288) & BMASK) > i) *set_reg = B1_Lo_MASK;
			else *clr_reg = B1_Lo_MASK;

			if ((*(it + 18432) & BMASK) > i) *set_reg = B2_Lo_MASK;
			else *clr_reg = B2_Lo_MASK;

			//clock in data
			*set_reg = CLK_MASK;
			//budget timer™
			for (uint8_t timedel = 0; timedel < 32; timedel++) {
				__asm__("nop");
			}
			*clr_reg = CLK_MASK;
		}
	}
}

//returns the adress of the mapped buffer for other software to write to
uint32_t* Pixelbach::retBA() {
	//std::cout << (uint32_t*)buffer << std::endl;
	return (uint32_t*)buffer;
}
/*
int main() {
	Pixelbach pixie(1);
	pixie.start();
	return 0;
}
*/

//initializes everything, must be called before everything else
Pixelbach::Pixelbach(int arg) {

	buffer = (uint32_t*)calloc(30000, sizeof(uint32_t));

	signal(SIGINT, my_handler);

	if (init_drawFast()) std::cout << "init done" << std::endl;
	else abort();

	std::cout << "clearing pins" << std::endl;
	*clr_reg = -1;
	std::cout << "cleared all 32 pins" << std::endl;

	std::cout << "filling buffer with test pattern" << std::endl;
	for (int j = 0; j < 24576; j += 192) {
		int row = j / 192;
		for (int i = 0; i < 192; i++) {
			*(buffer + i + j) = ((row & 31) << 11) + ((row & 31) << 5) + (row & 31);
		}
	}
	std::cout << "everythings ready\n\n" << std::endl;
}


void Pixelbach::setPixel(int x, int y, int r, int g, int b) {

	//die ersten 6144 pixel sind für reihen 0-15 und 32-47
	//die pixel von 6144 bis 12287 sind für reihen 16-31 und 48-63
	if (x < 192) {
		if (y < 16) {
			*(buffer + x + (y * 384)) = fullToHighColor(r, g, b);
		}
		else if (y < 32) {
			y -= 16;
			*(buffer + x + 6144 + (y * 384)) = fullToHighColor(r, g, b);
		}
		else if (y < 48) {
			y -= 32;
			*(buffer + x + 192 + (y * 384)) = fullToHighColor(r, g, b);
		}
		else if (y < 64) {
			y -= 48;
			*(buffer + x + 6336 + (y * 384)) = fullToHighColor(r, g, b);
		}
		else if (y < 80) {
			y -= 64;
			*(buffer + x + 12288 + (y * 384)) = fullToHighColor(r, g, b);
		}
		else if (y < 96) {
			y -= 80;
			*(buffer + x + 18432 + (y * 384)) = fullToHighColor(r, g, b);
		}
		else if (y < 112) {
			y -= 96;
			*(buffer + x + 12480 + (y * 384)) = fullToHighColor(r, g, b);
		}
		else if (y < 128) {
			y -= 112;
			*(buffer + x + 18624 + (y * 384)) = fullToHighColor(r, g, b);
		}
	}
}

void Pixelbach::setPixelAlpha(int x, int y, int r, int g, int b, double a) {

	//die ersten 6144 pixel sind für reihen 0-15 und 32-47
	//die pixel von 6144 bis 12287 sind für reihen 16-31 und 48-63
	if (x < 192) {
		if (y < 16) {
			*(buffer + x + (y * 384)) = (fullToHighColor(r, g, b) * a) + (*(buffer + x + (y * 384))) * (1 - a) + 0.5;
		}
		else if (y < 32) {
			y -= 16;
			*(buffer + x + 6144 + (y * 384)) = (fullToHighColor(r, g, b) * a) + (*(buffer + x + 6144 + (y * 384))) * (1 - a) + 0.5;
		}
		else if (y < 48) {
			y -= 32;
			*(buffer + x + 192 + (y * 384)) = (fullToHighColor(r, g, b) * a) + (*(buffer + x + 192 + (y * 384))) * (1 - a) + 0.5;
		}
		else if (y < 64) {
			y -= 48;
			*(buffer + x + 6336 + (y * 384)) = (fullToHighColor(r, g, b) * a) + (*(buffer + x + 6336 + (y * 384))) * (1 - a) + 0.5;
		}
		else if (y < 80) {
			y -= 64;
			*(buffer + x + 12288 + (y * 384)) = (fullToHighColor(r, g, b) * a) + (*(buffer + x + 12288 + (y * 384))) * (1 - a) + 0.5;
		}
		else if (y < 96) {
			y -= 80;
			*(buffer + x + 18432 + (y * 384)) = (fullToHighColor(r, g, b) * a) + (*(buffer + x + 18432 +  (y * 384))) * (1 - a) + 0.5;
		}
		else if (y < 112) {
			y -= 96;
			*(buffer + x + 12480 + (y * 384)) = (fullToHighColor(r, g, b) * a) + (*(buffer + x + 12480 + (y * 384))) * (1 - a) + 0.5;
		}
		else if (y < 128) {
			y -= 112;
			*(buffer + x + 18624 + (y * 384)) = (fullToHighColor(r, g, b) * a) + (*(buffer + x + 18624 + (y * 384))) * (1 - a) + 0.5;
		}
	}
}

//returns the pixel data from a given pixel
int Pixelbach::getPixel(int x, int y) {

	//die ersten 6144 pixel sind für reihen 0-15 und 32-47
	//die pixel von 6144 bis 12287 sind für reihen 16-31 und 48-63
	if (x < 192) {
		if (y < 16) {
			return *(buffer + x + (y * 384));
		}
		else if (y < 32) {
			y -= 16;
			return *(buffer + x + 6144 + (y * 384));
		}
		else if (y < 48) {
			y -= 32;
			return *(buffer + x + 192 + (y * 384));
		}
		else if (y < 64) {
			y -= 48;
			return *(buffer + x + 6336 + (y * 384));
		}
		else if (y < 80) {
			y -= 64;
			return *(buffer + x + 12288 + (y * 384));
		}
		else if (y < 96) {
			y -= 80;
			return *(buffer + x + 18432 + (y * 384));
		}
		else if (y < 112) {
			y -= 96;
			return *(buffer + x + 12480 + (y * 384));
		}
		else if (y < 128) {
			y -= 112;
			return *(buffer + x + 18624 + (y * 384));
		}
	}
}

void Pixelbach::start() {
	std::cout << "displaying buffer till ended" << std::endl;
	while (true) {
		drawFast();
	}
}