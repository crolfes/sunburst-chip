#pragma once

// CPU clock is 250 Mhz but Uart uses CPU_TIMER_HZ to set timing parameters
// based upon BAUD_RATE and the Uart runs at the 50 MHz peripheral clock
// frequency
#define CPU_TIMER_HZ ( 50'000'000)
#define BAUD_RATE    (  1'500'000)

#define SRAM_ADDRESS (0x0020'0000)
#define SRAM_BOUNDS  (0x0004'0000)

#define UART_NUM 2
#define UART_BOUNDS (0x0000'0034)
#define UART_ADDRESS (0x4030'0000)
#define UART_RANGE (0x0000'1000)

#define I2C_NUM 2
#define I2C_ADDRESS (0x4010'0000)
#define I2C_BOUNDS (0x0000'0080)
#define I2C_RANGE (0x0000'1000)

#define SPI_NUM 2
#define SPI_ADDRESS (0x4020'0000)
#define SPI_BOUNDS (0x30)
#define SPI_RANGE (0x0000'1000)

#define USBDEV_BOUNDS  (0x0000'1000)
#define USBDEV_ADDRESS (0x4040'0000)

#define GPIO_ADDRESS (0x4000'2000)
#define GPIO_BOUNDS  (0x0000'0040)

#define PATTGEN_ADDRESS (0x4050'0000)
#define PATTGEN_BOUNDS (0x0000'0030)

#define PLIC_ADDRESS (0x8800'0000)
#define PLIC_BOUNDS (0x0400'0000)

#define PWM_ADDRESS (0x4060'0000)
#define PWM_BOUNDS (0x0000'005C)
