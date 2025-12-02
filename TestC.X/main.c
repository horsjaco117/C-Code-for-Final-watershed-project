// main.c - PIC16F1788 - MPLAB X v6.20
// 5 analog channels + RA6 = SCRAM (IOC interrupt), RA7 = RESUME
// Sends only 2 bytes per channel: ID + high byte

#include <xc.h>
#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/adc/adc.h"
#include "mcc_generated_files/uart/eusart.h"

// Five dedicated 16-bit registers at even addresses
volatile __at(0x70) uint16_t AN0_value;
volatile __at(0x72) uint16_t AN1_value;
volatile __at(0x74) uint16_t AN2_value;
volatile __at(0x76) uint16_t AN3_value;
volatile __at(0x78) uint16_t AN5_value;
volatile __at(0x79) uint16_t HandShake;

#define HandShake 0
#define CH_AN0  1
#define CH_AN1  2
#define CH_AN2  3
#define CH_AN3  4
#define CH_AN5  5

const uint8_t ids[6]       = {0x24, 0xA0, 0xA1, 0xA2, 0xA3, 0xA5};
const uint8_t base_addr[6] = {0x79, 0x70, 0x72, 0x74, 0x76, 0x78};

// Fixed: use normal volatile uint8_t instead of "bit"
volatile uint8_t running = 1;        // 1 = running, 0 = paused by SCRAM

void __interrupt() ISR(void)
{
    if (IOCIF && IOCANbits.IOCAN6)      // RA6 falling edge (SCRAM pressed)
    {
        IOCAFbits.IOCAF6 = 0;
        running = 0;                    // Instantly pause everything
    }
    IOCIF = 0;
}

void main(void)
{
    uint8_t ch = 0;

    SYSTEM_Initialize();

    // RA6 interrupt-on-change setup (SCRAM)
    IOCANbits.IOCAN6 = 1;       // Interrupt on falling edge
    IOCAF = 0;
    IOCIE = 1;
    PEIE  = 1;
    GIE   = 1;

    __delay_ms(50);

    ADC_SetPositiveChannel(CH_AN0);
    ADC_StartConversion();

    while (1)
    {
        // RESUME button (RA7) - simple debounced poll
        if (!running && !PORTAbits.RA7)          // RA7 pressed
        {
            __delay_ms(30);
            if (!PORTAbits.RA7)                  // still pressed
            {
                running = 1;
                while (!PORTAbits.RA7);          // wait for release
            }
        }

        // Only do ADC + serial when running
        if (running && ADC_IsConversionDone())
        {
            uint16_t result     = ADC_GetConversionResult();
            uint8_t  high_byte  = result >> 8;

            // Store via indirect addressing
            FSR0L = base_addr[ch];
            INDF0 = (uint8_t)result;
            INDF0 = high_byte;

            // Send 2-byte packet
            while (!UART.IsTxReady());
            EUSART_Write(ids[ch]);

            while (!UART.IsTxReady());
            EUSART_Write(high_byte);

            // Next channel
            ch++;
            if (ch >= 5) ch = 0;

            // Select next channel
            switch (ch)
            {
                case 0: ADC_SetPositiveChannel(CH_AN0); break;
                case 1: ADC_SetPositiveChannel(CH_AN1); break;
                case 2: ADC_SetPositiveChannel(CH_AN2); break;
                case 3: ADC_SetPositiveChannel(CH_AN3); break;
                default: ADC_SetPositiveChannel(CH_AN5); break;
            }
            ADC_StartConversion();
        }

        __delay_us(10);
    }
}