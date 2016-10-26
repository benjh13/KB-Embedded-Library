#include "stm32f4xx.h"
#include <stdio.h>

#include "HCMS-290X_display.h"
#include "kb_timer.h"
#include "kb_spi.h"

static const uint8_t fontTable[];

static inline void _SCK_set(int input)
{
	kb_gpio_set(HCMS_290X_SCK_PORT, HCMS_290X_SCK_PIN, (kb_gpio_state_t)input);
}
static inline void _CE_set(int input)
{
	kb_gpio_set(HCMS_290X_CE_PORT, HCMS_290X_CE_PIN, (kb_gpio_state_t)input);
}
static inline void _RS_set(int input)
{
	kb_gpio_set(HCMS_290X_RS_PORT, HCMS_290X_RS_PIN, (kb_gpio_state_t)input);
}
static inline void _RESET_set(int input)
{
	kb_gpio_set(HCMS_290X_RESET_PORT, HCMS_290X_RESET_PIN, (kb_gpio_state_t)input);
}

static void _wake_up(uint8_t enable);
//static void _set_brightness(uint8_t brightness);	// TODO: implement brightness
static void _write_ctrl_reg(uint8_t data);

/****************************************************************/
/*                    display four characters                   */
/*                this can be called during 1ms ISR             */
/****************************************************************/
static void _wake_up(uint8_t enable)
{
	uint8_t reg_num = 0; 	// 0 = control word 0, 1 = control word 1
	uint8_t sleep = enable & 0x01;	// 0 = sleep, 1 = wake up
	uint8_t pwm = 255 & 0x0F; // brightness by PWM, 4 bit
			// 0x0 ~ 0xF : 0% ~ 100%
	uint8_t peak_current = 0x00 & 0x03; // brightness by peak current, 2 bit.
			// 0b10 = 31%, 0b10 = 50%, 0b00 = 73%(default) , 0b11 = 100%

	uint8_t input = (reg_num << 7)|(sleep << 6)|(peak_current << 4)|(pwm);

	_write_ctrl_reg(input);
}


static void _write_ctrl_reg(uint8_t data)
{
	// initial pin setting
	_RS_set(1);	//select control register
	kb_timer_delay_us(1);
	_CE_set(0);	//enable data writing

	// write
	kb_spi_send(HCMS_290X_SPI, &data, 1, 0);

	//end
	kb_timer_delay_us(10);
	_CE_set(1);   //latch on
	uint8_t dummy = 0x00;
	kb_spi_send(HCMS_290X_SPI, &dummy, 1, 0);
}

void hcms_290x_init(void)
{
	// Init GPIOs
	//  CE PIN
	kb_gpio_init_t gpio_setting = {
		.Mode = GPIO_MODE_OUTPUT_PP,
		.Pull = GPIO_PULLDOWN,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH // 50MHz
	};
	kb_gpio_init(HCMS_290X_CE_PORT, HCMS_290X_CE_PIN, &gpio_setting);

	// RS pin
	gpio_setting = (kb_gpio_init_t ){
		.Mode = GPIO_MODE_OUTPUT_PP,
		.Pull = GPIO_NOPULL,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH // 50MHz
	};
	kb_gpio_init(HCMS_290X_RS_PORT, HCMS_290X_RS_PIN, &gpio_setting);

	// RESET pin
	gpio_setting = (kb_gpio_init_t){
		.Mode = GPIO_MODE_OUTPUT_PP,
		.Pull = GPIO_NOPULL,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH // 50MHz
	};
	kb_gpio_init(HCMS_290X_RESET_PORT, HCMS_290X_RESET_PIN, &gpio_setting);

	// MOSI pin
	kb_spi_mosi_init(HCMS_290X_SPI, HCMS_290X_MOSI_PORT, HCMS_290X_MOSI_PIN);

	// SCK pin
	kb_spi_sck_init(HCMS_290X_SPI, HCMS_290X_SCK_PORT, HCMS_290X_SCK_PIN);

	// then init SPI.
	/*
	 * Clock Signal. SPI1 is located in APB2. It runs at 22.5 MHz
	 * So the clock divided into 4. : 22.5/4 MHz
	 * HCMS-290x requirements:
	 * Fclk = 5MHz at Vlogic = 5V, 4MHz at Vlogic = 3V.
	 * Our project uses 5V for Vlogic.
	 */
	kb_spi_init(HCMS_290X_SPI, TRAILING_RISING_EDGE);	// TODO: frequency setting
	// it was originally SPI_BAUDRATEPRESCALER_16

	// set pins
	_RESET_set(1);
	_RS_set(1);
	_CE_set(1);

	// Reset device
	_RESET_set(0);
	kb_timer_delay_ms(10);
	_RESET_set(1);

	// clear the screen before waking up
	hcms_290x_clear();
	// wake up
	_wake_up(1);
}

void hcms_290x_matrix(char *s)
{
	int i, j;

	// initial pin setting
	uint8_t *ptr; //pointer for starting address of character s
	_RS_set(0);	//select dot register
	kb_timer_delay_us(1);
	_CE_set(0);	//enable data writing

	// write
	for(i=0; i<4; i++)
	{
		ptr = (uint8_t *)(fontTable + s[i]*5);
		for(j=0; j<5; j++)
		{	// Fix it to just write 5 bytes
			kb_spi_send(HCMS_290X_SPI, ptr, 1, 0);
			ptr++;
		}//for j
	}//for i

	// end
	kb_timer_delay_us(10);
	_CE_set(1);
	// We need to make falling edge to SCK pin to latch on
	uint8_t dummy = 0x00;
	kb_spi_send(HCMS_290X_SPI, &dummy, 1, 0);
}

void hcms_290x_err(int err) {
	char str[5];
	snprintf(str, 5, "E%03d", err);

	hcms_290x_matrix(str);
}

void hcms_290x_float(float f) {
	//a string with the format xx.x
	char str[5]; //need 5 because of null byte

	snprintf(str, 5, "%.1f", f);

	hcms_290x_matrix(str);
}

void hcms_290x_int(int i) {
	char str[5];

	snprintf(str, 5, "%04d", i);

	hcms_290x_matrix(str);
}

void hcms_290x_matrix_scroll(char* str) {
	int i;

	for(i = 0; i < 3; i++) {
		if(str[i] == '\0') {
			hcms_290x_matrix(str);
			return;
		}
	}

	i = 0;
	while(str[i+3] != '\0') {
		hcms_290x_matrix(&str[i]);
		kb_timer_delay_ms(1000);
		i++;
	}
}


/****************************************************************/
/*                       clear screen                           */
/*                 write 4 spaces into screen                   */
/****************************************************************/
void hcms_290x_clear(void)
{
	int i;
	uint8_t dummy = 0x00;

	// pin setup
	_RS_set(0);	//select dot register
	kb_timer_delay_us(1);
	_CE_set(0);	//enable data writing

	// write
	for(i=0; i<20; i++)
	{
		kb_spi_send(HCMS_290X_SPI, &dummy, 1, 0);
	}

	// end
	kb_timer_delay_us(10);
	_CE_set(1);   //latch on
	// We need to make falling edge to SCK pin to latch on
	kb_spi_send(HCMS_290X_SPI, &dummy, 1, 0);
}



static const uint8_t fontTable[] = {
	//0
	0x80, 0x80, 0x80, 0x80, 0x80, // (space)
	0x40, 0x40, 0x40, 0x40, 0x40, // (space)
	0x20, 0x20, 0x20, 0x20, 0x20, // (space)
	0x10, 0x10, 0x10, 0x10, 0x10, // (space)
	0x08, 0x08, 0x08, 0x08, 0x08, // (space)
	0x04, 0x04, 0x04, 0x04, 0x04, // (space)
	0x02, 0x02, 0x02, 0x02, 0x02, // (space)
	0x01, 0x01, 0x01, 0x01, 0x01, // (space)
	//8
	0x80, 0x80, 0x80, 0x80, 0x80, // (space)
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, // (space)
	0xe0, 0xe0, 0xe0, 0xe0, 0xe0, // (space)
	0xf0, 0xf0, 0xf0, 0xf0, 0xf0, // (space)
	0xf8, 0xf8, 0xf8, 0xf8, 0xf8, // (space)
	0xfc, 0xfc, 0xfc, 0xfc, 0xfc, // (space)
	0xfe, 0xfe, 0xfe, 0xfe, 0xfe, // (space)
	0xff, 0xff, 0xff, 0xff, 0xff, // (space)
	//16
	0x7f, 0x7f, 0x7f, 0x7f, 0x7f, // (space)
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, // (space)
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, // (space)
	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, // (space)
	0x07, 0x07, 0x07, 0x07, 0x07, // (space)
	0x03, 0x03, 0x03, 0x03, 0x03, // (space)
	0x01, 0x01, 0x01, 0x01, 0x01, // (space)
	0x00, 0x00, 0x00, 0x00, 0x00, // (space)

	//24
	0x7E, 0x42, 0x43, 0x42, 0x7E,
	0x7E, 0x62, 0x63, 0x62, 0x7E,
	0x7E, 0x72, 0x73, 0x72, 0x7E,
	0x7E, 0x7A, 0x7B, 0x7A, 0x7E,
	0x7E, 0x7E, 0x7F, 0x7E, 0x7E,
	0x04, 0x0E, 0x15, 0x04, 0x04,
	0x7C, 0x72, 0x72, 0x42, 0x7C,
	0x7C, 0x42, 0x72, 0x72, 0x7C,
	//32
	0x00, 0x00, 0x00, 0x00, 0x00, // space
	0x00, 0x00, 0x2f, 0x00, 0x00, // !
	0x00, 0x07, 0x00, 0x07, 0x00, // "
	0x14, 0x7f, 0x14, 0x7f, 0x14, // #
	0x24, 0x2a, 0x7f, 0x2a, 0x12, // $
	0xc4, 0xc8, 0x10, 0x26, 0x46, // %
	0x36, 0x49, 0x55, 0x22, 0x50, // &
	0x00, 0x05, 0x03, 0x00, 0x00, // '
	0x00, 0x1c, 0x22, 0x41, 0x00, // (
	0x00, 0x41, 0x22, 0x1c, 0x00, // )
	0x14, 0x08, 0x3E, 0x08, 0x14, // *
	0x08, 0x08, 0x3E, 0x08, 0x08, // +
	0x00, 0x00, 0x50, 0x30, 0x00, // ,
	0x10, 0x10, 0x10, 0x10, 0x10, // -
	0x00, 0x60, 0x60, 0x00, 0x00, // .
	0x20, 0x10, 0x08, 0x04, 0x02, // /
	0x3E, 0x51, 0x49, 0x45, 0x3E, // 0
	0x00, 0x42, 0x7F, 0x40, 0x00, // 1
	0x42, 0x61, 0x51, 0x49, 0x46, // 2
	0x21, 0x41, 0x45, 0x4B, 0x31, // 3
	0x18, 0x14, 0x12, 0x7F, 0x10, // 4
	0x27, 0x45, 0x45, 0x45, 0x39, // 5
	0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
	0x01, 0x71, 0x09, 0x05, 0x03, // 7
	0x36, 0x49, 0x49, 0x49, 0x36, // 8
	0x06, 0x49, 0x49, 0x29, 0x1E, // 9
	0x00, 0x36, 0x36, 0x00, 0x00, // :
	0x00, 0x56, 0x36, 0x00, 0x00, // ;
	0x08, 0x14, 0x22, 0x41, 0x00, // <
	0x14, 0x14, 0x14, 0x14, 0x14, // =
	0x00, 0x41, 0x22, 0x14, 0x08, // >
	0x02, 0x01, 0x51, 0x09, 0x06, // ?
	//64
	0x32, 0x49, 0x59, 0x51, 0x3E, // @
	0x7E, 0x11, 0x11, 0x11, 0x7E, // A
	0x7F, 0x49, 0x49, 0x49, 0x36, // B
	0x3E, 0x41, 0x41, 0x41, 0x22, // C
	0x7F, 0x41, 0x41, 0x22, 0x1C, // D
	0x7F, 0x49, 0x49, 0x49, 0x41, // E
	0x7F, 0x09, 0x09, 0x09, 0x01, // F
	0x3E, 0x41, 0x49, 0x49, 0x3A, // G
	0x7F, 0x08, 0x08, 0x08, 0x7F, // H
	0x00, 0x41, 0x7F, 0x41, 0x00, // I
	0x20, 0x40, 0x41, 0x3F, 0x01, // J
	0x7F, 0x08, 0x14, 0x22, 0x41, // K
	0x7F, 0x40, 0x40, 0x40, 0x40, // L
	0x7F, 0x02, 0x0C, 0x02, 0x7F, // M
	0x7F, 0x04, 0x08, 0x10, 0x7F, // N
	0x3E, 0x41, 0x41, 0x41, 0x3E, // O
	0x7F, 0x09, 0x09, 0x09, 0x06, // P
	0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
	0x7F, 0x09, 0x19, 0x29, 0x46, // R
	0x46, 0x49, 0x49, 0x49, 0x31, // S
	0x01, 0x01, 0x7F, 0x01, 0x01, // T
	0x3F, 0x40, 0x40, 0x40, 0x3F, // U
	0x1F, 0x20, 0x40, 0x20, 0x1F, // V
	0x3F, 0x40, 0x38, 0x40, 0x3F, // W
	0x63, 0x14, 0x08, 0x14, 0x63, // X
	0x07, 0x08, 0x70, 0x08, 0x07, // Y
	0x61, 0x51, 0x49, 0x45, 0x43, // Z
	0x00, 0x7F, 0x41, 0x41, 0x00, // [
	0x02, 0x04, 0x08, 0x10, 0x20, // '\'
	0x00, 0x41, 0x41, 0x7F, 0x00, // ]
	0x04, 0x02, 0x01, 0x02, 0x04, // ^
	0x40, 0x40, 0x40, 0x40, 0x40, // _
	//96
	0x00, 0x01, 0x02, 0x04, 0x00, // '
	0x20, 0x54, 0x54, 0x54, 0x78, // a
	0x7F, 0x48, 0x44, 0x44, 0x38, // b
	0x38, 0x44, 0x44, 0x44, 0x20, // c
	0x38, 0x44, 0x44, 0x48, 0x7F, // d
	0x38, 0x54, 0x54, 0x54, 0x18, // e
	0x08, 0x7E, 0x09, 0x01, 0x02, // f
	0x0C, 0x52, 0x52, 0x52, 0x3E, // g
	0x7F, 0x08, 0x04, 0x04, 0x78, // h
	0x00, 0x44, 0x7D, 0x40, 0x00, // i
	0x20, 0x40, 0x44, 0x3D, 0x00, // j
	0x7F, 0x10, 0x28, 0x44, 0x00, // k
	0x00, 0x41, 0x7F, 0x40, 0x00, // l
	0x7C, 0x04, 0x18, 0x04, 0x78, // m
	0x7C, 0x08, 0x04, 0x04, 0x78, // n
	0x38, 0x44, 0x44, 0x44, 0x38, // o
	0x7C, 0x14, 0x14, 0x14, 0x08, // p
	0x08, 0x14, 0x14, 0x18, 0x7C, // q
	0x7C, 0x08, 0x04, 0x04, 0x08, // r
	0x48, 0x54, 0x54, 0x54, 0x20, // s
	0x04, 0x3F, 0x44, 0x40, 0x20, // t
	0x3C, 0x40, 0x40, 0x20, 0x7C, // u
	0x1C, 0x20, 0x40, 0x20, 0x1C, // v
	0x3C, 0x40, 0x30, 0x40, 0x3C, // w
	0x44, 0x28, 0x10, 0x28, 0x44, // x
	0x0C, 0x50, 0x50, 0x50, 0x3C, // y
	0x44, 0x64, 0x54, 0x4C, 0x44, // z
	0x00, 0x08, 0x36, 0x41, 0x00, // {
	0x00, 0x00, 0x7F, 0x00, 0x00, // |
	0x00, 0x41, 0x36, 0x08, 0x00, // }
	//126
	0x08, 0x08, 0x2A, 0x1C, 0x08, // ->   //~
	0x08, 0x1C, 0x2A, 0x08, 0x08, // <-   //DEL
	//128 custmized
	0x44, 0x34, 0x0F, 0x34 ,0x44, //大
	0x2C, 0x40, 0x7F, 0x00 ,0x0C, //小
	0x40, 0x30, 0x0F, 0x30 ,0x40, //人
	0x7F, 0x55, 0x1D, 0x35 ,0x57, //民
	0x4A, 0x2A, 0x1F, 0x2A ,0x4A, //夫
    0x7F, 0x49, 0x49, 0x49 ,0x7F, //日
	0x40, 0x3F, 0x15, 0x15 ,0x7F, //月
	0x49, 0x29, 0x1F, 0x29 ,0x49, //天
	0x00, 0x21, 0x45, 0x4B ,0x31, //了
	0x02, 0x0F, 0x02, 0x0F ,0x02, //艹
	0x09, 0x09, 0x7F, 0x09 ,0x09, //干
	0x08, 0x08, 0x08, 0x08 ,0x08, //一
	0x14, 0x14, 0x14, 0x14 ,0x14, //二
	0x2A, 0x2A, 0x2A, 0x2A ,0x2A, //三
	0x7F, 0x49, 0x47, 0x49 ,0x7F, //四
	0x49, 0x79, 0x4F, 0x69 ,0x59, //五
	0x24, 0x1D, 0x06, 0x0C ,0x34, //六
	0x08, 0x3F, 0x48, 0x48 ,0x28, //七
	0x40, 0x7E, 0x00, 0x7E ,0x40, //八
	0x44, 0x3C, 0x07, 0x74 ,0x4C, //九
	0x04, 0x02, 0x7F, 0x02 ,0x04, //个
	0x04, 0x04, 0x7F, 0x04 ,0x04, //十
	0x7D, 0x55, 0x55, 0x57 ,0x7D, //百
	0x12, 0x12, 0x7E, 0x12 ,0x11, //千
	0x45, 0x25, 0x1F, 0x45 ,0x3D, //万
 	0x1F, 0x15, 0x7F, 0x15 ,0x1F, //甲
	0x31, 0x49, 0x45, 0x43 ,0x21, //乙
	0x7D, 0x15, 0x0F, 0x55 ,0x7D, //丙
	0x11, 0x21, 0x7F, 0x01 ,0x01, //丁
	0x66, 0x44, 0x7F, 0x44 ,0x66, //出
	0x7E, 0x22, 0x22, 0x22 ,0x7E, //口
	0x08, 0x0C, 0x7E, 0x0C ,0x80, //upper arrow
	0x10, 0x30, 0x7E, 0x30 ,0x10, //down arrow


	0x7F, 0x7F, 0x7F, 0x7F ,0x7F, //full filled block

};

