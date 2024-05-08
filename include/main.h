#include <stm32f0xx.h>

#pragma once

#define GPIO_OUTPUT_CONFIG(port, pin) \
    port->MODER &= ~(GPIO_MODER_MODER##pin##_Msk); \
    port->MODER |= GPIO_MODER_MODER##pin##_0; \
    port->OTYPER &= ~GPIO_OTYPER_OT_##pin; \
    port->OSPEEDR &= ~GPIO_OSPEEDR_OSPEEDR##pin; \

#define GPIO_INPUT_CONFIG(port, pin) \
    port->MODER &= ~(GPIO_MODER_MODER##pin##_Msk); \
    port->MODER |= GPIO_MODER_MODER##pin##_0; \
    port->PUPDR &= ~GPIO_PUPDR_PUPDR##pin##_Msk; \
    port->PUPDR |= GPIO_PUPDR_PUPDR##pin##_1;