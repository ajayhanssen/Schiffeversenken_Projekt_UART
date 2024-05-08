#include <stm32f091xc.h>


#define GPIO_INPUT_MODE 0
#define GPIO_ALTFN_MODE 2
#define GPIO_OUTPUT_MODE 1
#define GPIO_ANALOG_MODE 3
//...

#define GPIO_SPEED_LOW 0
#define GPIO_SPEED_MEDIUM 1
#define GPIO_SPEED_HIGH 3

#define GPIO_PUPD_NO 0
#define GPIO_PUPD_UP 1
#define GPIO_PUPD_DOWN 2

#define GPIO_OPT_PP 0
#define GPIO_OPT_OD 1

#define PIN_SET 1
#define PIN_RESET 0
//...

typedef struct {
	uint8_t pin_number;
	uint8_t pin_mode;
	uint8_t pin_speed ;
	uint8_t pin_pupdc;
	uint8_t pin_opt;
	uint8_t pin_alt_fun_mode;
}PinConfig_t;

typedef struct {
	GPIO_TypeDef *pGPIOx; 
	PinConfig_t GPIO_PinConfig ; 
}GPIO_Handle_t;


int GPIO_Init(GPIO_Handle_t* pGPIOHandle);

int GPIO_Write(GPIO_TypeDef *port, uint32_t pin, uint32_t value);

int GPIO_Read(GPIO_TypeDef *port, uint32_t pin);
