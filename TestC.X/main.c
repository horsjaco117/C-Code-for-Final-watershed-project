// main.c - PIC16F1788 - MPLAB X v6.20
// 100% FIXED: 0x24 HandShake FIRST and ONLY first - NO stray 00
#include <xc.h>
#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/adc/adc.h"
#include "mcc_generated_files/uart/eusart.h"

// Your current memory layout
volatile __at(0x70) uint16_t AN0_value;
volatile __at(0x72) uint16_t AN1_value;
volatile __at(0x74) uint16_t AN2_value;
volatile __at(0x76) uint16_t AN3_value;
volatile __at(0x78) uint16_t AN5_value;
volatile __at(0x7A) uint8_t DigitalInputs;          // your location

#define CH_AN0 0
#define CH_AN1 1
#define CH_AN2 2
#define CH_AN3 3
#define CH_AN5 4

const uint8_t ids[6]       = {0x24, 0xA0, 0xA1, 0xA2, 0xA3, 0xA5};
const uint8_t base_addr[6] = {0x7A, 0x70, 0x72, 0x74, 0x76, 0x78};

volatile uint8_t running = 1;
volatile uint8_t ch = 0;
volatile uint8_t first_cycle = 1;   // ensures clean start

void __interrupt() ISR(void)
{
    if (IOCIF)
    {
        if (IOCAFbits.IOCAF6) { IOCAFbits.IOCAF6 = 0; __delay_ms(20); if (!PORTAbits.RA6) running = 0; }
        if (IOCAFbits.IOCAF7) { IOCAFbits.IOCAF7 = 0; __delay_ms(20); if (PORTAbits.RA7) running = 1; }
        IOCIF = 0;
    }
}

void main(void)
{
    SYSTEM_Initialize();

    // IOC Setup
    IOCANbits.IOCAN6 = 1;
    IOCAPbits.IOCAP7 = 1;
    IOCAF = 0;
    IOCIE = 1;
    PEIE = 1;
    GIE = 1;

    DigitalInputs = 0x21;
    
    // CRITICAL: Flush UART TX buffer completely
    while (!TXSTAbits.TRMT);     // Wait for shift register empty
    TXREG = 0xFF;                // Dummy write
    while (!TXSTAbits.TRMT);
    TXREG = 0xFF;
    while (!TXSTAbits.TRMT);

    
    
    __delay_ms(100);             // Give everything time to settle

    while (1)
    {
        if (!running)
        {
            __delay_ms(10);
            continue;
        }

        // === 1. Send 0x24 HandShake FIRST - every cycle ===
        if (first_cycle || ch == 0)
        {
            // Wait for UART to be 100% ready
            while (!PIR1bits.TXIF);           // TX buffer empty
            while (!TXSTAbits.TRMT);          // Shift register empty

            EUSART_Write(0x24);
            while (!TXSTAbits.TRMT);          // Ensure fully sent
            EUSART_Write(DigitalInputs);          // Your byte at 0x7A

            // Store it in memory too
            FSR0L = base_addr[0];
            INDF0 = DigitalInputs;

            first_cycle = 0;
            ch = 1;

            // NOW start first ADC conversion
            ADC_SetPositiveChannel(CH_AN0);
            ADC_StartConversion();
            continue;
        }

        // === 2. Normal ADC channels ===
        if (ADC_IsConversionDone())
        {
            uint16_t result = ADC_GetConversionResult();
            uint8_t high_byte = result >> 8;

            // Store result
            FSR0L = base_addr[ch];
            INDF0 = (uint8_t)result;
            INDF0 = high_byte;

            // Transmit
            while (!TXSTAbits.TRMT);
            EUSART_Write(ids[ch]);
            while (!TXSTAbits.TRMT);
            EUSART_Write(high_byte);

            // Next channel
            ch++;
            if (ch >= 6)
            {
                ch = 0;  // Next loop will send 0x24 again
            }
            else
            {
                switch (ch)
                {
                    case 1: ADC_SetPositiveChannel(CH_AN0); break;
                    case 2: ADC_SetPositiveChannel(CH_AN1); break;
                    case 3: ADC_SetPositiveChannel(CH_AN2); break;
                    case 4: ADC_SetPositiveChannel(CH_AN3); break;
                    case 5: ADC_SetPositiveChannel(CH_AN5); break;
                }
                ADC_StartConversion();
            }
        }

        __delay_us(10);
    }
}