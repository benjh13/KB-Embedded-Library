/*
 * kb_spi.c
 *
 *  Created on: Oct 25, 2016
 *      Author: Bumsik Kim
 */

#include "kb_spi.h"
#include "kb_alternate_pins.h"

// base name change. Used with kb_msg(). See @kb_base.h
#ifdef KB_MSG_BASE
	#undef KB_MSG_BASE
	#define KB_MSG_BASE "SPI"
#endif

static uint32_t get_bus_freq_(kb_spi_t spi);
static SPI_HandleTypeDef *get_handler (kb_spi_t spi);
static void enable_spi_clk_ (kb_spi_t spi);

// forward declaration of constant variables
static const uint8_t prescaler_table_size_;
struct prescaler_ {
	uint32_t	divisor;
	uint32_t divisor_macro;
};
static const struct prescaler_ prescaler_table_ [];

#if defined(STM32F446xx)
	static SPI_HandleTypeDef spi_1_h_ = {.Instance = SPI1};
	static SPI_HandleTypeDef spi_2_h_ = {.Instance = SPI2};
	static SPI_HandleTypeDef spi_3_h_ = {.Instance = SPI3};
	static SPI_HandleTypeDef spi_4_h_ = {.Instance = SPI4};
#else
	#error "Please define device! " __FILE__ "\n"
#endif


int kb_spi_init(kb_spi_t spi, kb_spi_init_t *settings)
{
	// select handler
	SPI_HandleTypeDef* handler = get_handler(spi);
	if (NULL == handler)
	{
		return KB_ERROR;
	}
	enable_spi_clk_(spi);

	// basic setting
	handler->Init.Mode = SPI_MODE_MASTER;
	handler->Init.DataSize =  SPI_DATASIZE_8BIT;
	handler->Init.Direction = SPI_DIRECTION_2LINES;
	handler->Init.NSS = SPI_NSS_SOFT,			// No hardware NSS pin
	handler->Init.FirstBit = SPI_FIRSTBIT_MSB;
	handler->Init.TIMode = SPI_TIMODE_DISABLED;
	handler->Init.CRCCalculation = SPI_CRCCALCULATION_DISABLED;
	handler->Init.CRCPolynomial = 7;

	// frequency setting
	// use binary search
	uint32_t freq_bus = get_bus_freq_(spi);
	uint32_t prescale_target = freq_bus/settings->frequency;
	uint8_t front = prescaler_table_size_ - 1;
	uint8_t back = 0;
	uint8_t prescale_idx;
	uint32_t prescale_matched;
	uint32_t prescale_behind;
	if(prescale_target > prescaler_table_[front -1].divisor)
	{
		prescale_idx = front;
		prescale_matched = prescaler_table_[prescale_idx].divisor;
		kb_warning("Prescaler is touching its border vale\r\n");
		kb_warning("	You might want to double check\r\n");
	}
	else if (prescale_target <= prescaler_table_[0].divisor)
	{
		prescale_idx = 0;
		prescale_matched = prescaler_table_[prescale_idx].divisor;
		kb_warning("Prescaler is touching its border value");
		kb_warning("	You might want to double check\r\n");
	}
	else
	{
		while(1)
		{
			prescale_idx = (front+back)/2;
			prescale_matched = prescaler_table_[prescale_idx].divisor;
			prescale_behind = prescaler_table_[prescale_idx - 1].divisor;
			if(prescale_matched < prescale_target)
			{
				back = prescale_idx;
			}
			else if (prescale_behind < prescale_target)
			{
				break;
			}
			else
			{
				front = prescale_idx;
			}
		}
	}
	kb_msg("requested frequency :%lu\r\n", (unsigned long int)settings->frequency);
	kb_msg("selected divisor is %u\r\n", (unsigned int)prescaler_table_[prescale_idx].divisor);
	kb_msg("selected frequency is %lu\r\n", (unsigned long int)freq_bus/prescale_matched);

	handler->Init.BaudRatePrescaler = prescaler_table_[prescale_idx].divisor_macro;

	// polarity setting
	switch(settings->polarity)
	{
	case LEADING_RISING_EDGE:
		handler->Init.CLKPhase = SPI_PHASE_1EDGE;
		handler->Init.CLKPolarity = SPI_POLARITY_HIGH;
		break;
	case LEADING_FALLING_EDGE:
		handler->Init.CLKPhase = SPI_PHASE_1EDGE;
		handler->Init.CLKPolarity = SPI_POLARITY_LOW;
		break;
	case TRAILING_RISING_EDGE:
		handler->Init.CLKPhase = SPI_PHASE_2EDGE;
		handler->Init.CLKPolarity = SPI_POLARITY_HIGH;
		break;
	case TRAILING_FALLING_EDGE:
		handler->Init.CLKPhase = SPI_PHASE_2EDGE;
		handler->Init.CLKPolarity = SPI_POLARITY_LOW;
		break;
	default:
		kb_error("Wrong Polarity selected!.\r\n");
		return KB_ERROR;
	}

	kb_status_t result = HAL_SPI_Init(handler);;
	if (result != KB_OK)
	{
		kb_error("Error initializing.\r\n");
	}
	return	result;
}


int kb_spi_mosi_pin(kb_spi_t spi, kb_gpio_port_t port, kb_gpio_pin_t pin)
{
	uint32_t alternate = GPIO_SPI_MOSI_AF_(spi, port, pin);
	if (alternate == KB_WRONG_PIN)
	{
		kb_error("Wrong MOSI pin! Find a correct one.\r\n");
		return KB_ERROR;
	}
	kb_gpio_enable_clk(port);
	// Init GPIOs
	kb_gpio_init_t gpio_setting = {
		.Mode = GPIO_MODE_AF_PP,
		.Pull = GPIO_PULLUP,
		.Alternate = alternate,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH // 50MHz
	};
	kb_gpio_init(port, pin, &gpio_setting);
	return KB_OK;
}


int kb_spi_miso_pin(kb_spi_t spi, kb_gpio_port_t port, kb_gpio_pin_t pin)
{
	uint32_t alternate = GPIO_SPI_MISO_AF_(spi, port, pin);
	if (alternate == KB_WRONG_PIN)
	{
		kb_error("Wrong MISO pin! Find a correct one.\r\n");
		return KB_ERROR;
	}
	kb_gpio_enable_clk(port);
	// Init GPIOs
	kb_gpio_init_t gpio_setting = {
		.Mode = GPIO_MODE_AF_PP,
		.Pull = GPIO_PULLUP,
		.Alternate = alternate,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH // 50MHz
	};
	kb_gpio_init(port, pin, &gpio_setting);
	return KB_OK;
}


int kb_spi_sck_pin(kb_spi_t spi, kb_gpio_port_t port, kb_gpio_pin_t pin)
{
	uint32_t alternate = GPIO_SPI_SCK_AF_(spi, port, pin);
	if (alternate == KB_WRONG_PIN)
	{
		kb_error("Wrong SCK pin! Find a correct one.\r\n");
		return KB_ERROR;
	}
	kb_gpio_enable_clk(port);
	// Init GPIOs
	kb_gpio_init_t gpio_setting = {
		.Mode = GPIO_MODE_AF_PP,
		.Pull = GPIO_PULLUP,
		.Alternate = alternate,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH // 50MHz
	};
	kb_gpio_init(port, pin, &gpio_setting);
	return KB_OK;
}


inline int kb_spi_send(kb_spi_t spi, uint8_t* buf, uint16_t size)
{
	return kb_spi_send_timeout(spi, buf, size, TIMEOUT_MAX);
}


int kb_spi_send_timeout(kb_spi_t spi, uint8_t *buf, uint16_t size, uint32_t timeout)
{
	// select handler
	SPI_HandleTypeDef* handler = get_handler(spi);
	if (NULL == handler) {
		return KB_ERROR;
	}
	kb_status_t result = HAL_SPI_Transmit(handler, buf, size, timeout);
	if (result != KB_OK) {
		kb_error("Error in sending.\r\n");
	}
	return result;
}


static uint32_t get_bus_freq_(kb_spi_t spi)
{
	if ((spi == SPI2) || (spi == SPI3))
	{	// APB1: SPI2, SPI3
		return HAL_RCC_GetPCLK1Freq();
	}
	else if ((spi == SPI1) || (spi == SPI4))
	{	// APB2: SPI1, SPI4
		return HAL_RCC_GetPCLK2Freq();
	}
	kb_error("Wrong SPI device! Find a correct one.\r\n");
	return 0;
}


static SPI_HandleTypeDef *get_handler (kb_spi_t spi)
{
	if (spi == SPI1)
	{
		return &spi_1_h_;
	}
	else if (spi == SPI2)
	{
		return &spi_2_h_;
	}
	else if (spi == SPI3)
	{
		return &spi_3_h_;
	}
	else if (spi == SPI4)
	{
		return &spi_4_h_;
	}
	else
	{
		kb_error("Wrong SPI device selected!\r\n");
		return NULL;
	}
}


static void enable_spi_clk_ (kb_spi_t spi)
{
	if (spi == SPI1)
	{
		__SPI1_CLK_ENABLE();
	}
	else if (spi == SPI2)
	{
		__SPI2_CLK_ENABLE();
	}
	else if (spi == SPI3)
	{
		__SPI3_CLK_ENABLE();
	}
	else if (spi == SPI4)
	{
		__SPI4_CLK_ENABLE();
	}
	else
	{
		kb_error("Wrong SPI device selected!\r\n");
	}
	return;
}


#if defined(STM32F446xx)
	static const uint8_t prescaler_table_size_ = 8;
	static const struct prescaler_ prescaler_table_ [] =
	{
		{2, SPI_BAUDRATEPRESCALER_2},
		{4, SPI_BAUDRATEPRESCALER_4},
		{8, SPI_BAUDRATEPRESCALER_8},
		{16, SPI_BAUDRATEPRESCALER_16},
		{32, SPI_BAUDRATEPRESCALER_32},
		{64, SPI_BAUDRATEPRESCALER_64},
		{128, SPI_BAUDRATEPRESCALER_128},
		{256, SPI_BAUDRATEPRESCALER_256}
	};
#else
	#error "Please define device!"
#endif
