// main.c - PIC16F1788 - MPLAB X v6.20
// SCRAM MODE: Continuously blasts 0x24 + DigitalInputs (bit6 = 1)
// Normal mode: Full 5-channel cycle
#include <xc.h>
#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/adc/adc.h"
#include "mcc_generated_files/uart/eusart.h"

// Memory layout
volatile __at(0x70) uint16_t AN0_value;
volatile __at(0x72) uint16_t AN1_value;
volatile __at(0x74) uint16_t AN2_value;
volatile __at(0x76) uint16_t AN3_value;
volatile __at(0x78) uint16_t AN5_value;
volatile __at(0x7A) uint8_t DigitalInputs;   // Status register

#define CH_AN0 0
#define CH_AN1 1
#define CH_AN2 2
#define CH_AN3 3
#define CH_AN5 4

const uint8_t ids[6]       = {0x24, 0xA0, 0xA1, 0xA2, 0xA3, 0xA5};
const uint8_t base_addr[6] = {0x7A, 0x70, 0x72, 0x74, 0x76, 0x78};

volatile uint8_t ch = 0;
volatile uint8_t first_cycle = 1;

void __interrupt() ISR(void)
{
    if (IOCIF)
    {
        // RA6 = SCRAM pressed
        if (IOCAFbits.IOCAF6)
        {
            IOCAFbits.IOCAF6 = 0;
            __delay_ms(20);
            if (!PORTAbits.RA6)                  // confirmed pressed
            {
                DigitalInputs |=  (1 << 6);       // Set SCRAM active
                DigitalInputs &= ~(1 << 7);       // Clear any old RESUME flag
            }
        }

        // RA7 = RESUME pressed
        if (IOCAFbits.IOCAF7)
        {
            IOCAFbits.IOCAF7 = 0;
            __delay_ms(20);
            if (!PORTAbits.RA7)                  // confirmed pressed
            {
                DigitalInputs &= ~(1 << 6);       // Clear SCRAM
                DigitalInputs &= ~(1 << 7);       // Clear RESUME flag (one-shot)
            }
        }

        IOCIF = 0;
    }
}

void main(void)
{
    SYSTEM_Initialize();

    // Both buttons trigger on falling edge (active low)
    IOCANbits.IOCAN6 = 1;
    IOCANbits.IOCAN7 = 1;
    IOCAF = 0;
    IOCIE = 1;
    PEIE = 1;
    GIE = 1;

    DigitalInputs = 0x00;

    // Flush UART at startup
    while (!TXSTAbits.TRMT);
    TXREG = 0xFF;  while (!TXSTAbits.TRMT);
    TXREG = 0xFF;  while (!TXSTAbits.TRMT);

    __delay_ms(100);

    while (1)
    {
        uint8_t scram_active = (DigitalInputs & (1 << 6));

        // ============================================================
        // SCRAM MODE: Continuously blast 0x24 + status byte
        // ============================================================
        if (scram_active)
        {
            if (TXSTAbits.TRMT)                     // Shift register empty?
            {
                EUSART_Write(0x24);
                while (!TXSTAbits.TRMT);            // Wait for byte to finish
                EUSART_Write(DigitalInputs);        // Always has bit6 = 1
            }
            __delay_us(10);
            continue;
        }

        // ============================================================
        // NORMAL MODE: Full data cycle
        // ============================================================
        if (first_cycle || ch == 0)
        {
            while (!TXSTAbits.TRMT);
            EUSART_Write(0x24);
            while (!TXSTAbits.TRMT);
            EUSART_Write(DigitalInputs);            // Normal status (bit6 = 0)

            FSR0L = base_addr[0];
            INDF0 = DigitalInputs;

            first_cycle = 0;
            ch = 1;

            ADC_SetPositiveChannel(CH_AN0);
            ADC_StartConversion();
            continue;
        }

        if (ADC_IsConversionDone())
        {
            uint16_t result = ADC_GetConversionResult();
            uint8_t high_byte = result >> 8;

            FSR0L = base_addr[ch];
            INDF0 = (uint8_t)result;
            INDF0 = high_byte;

            while (!TXSTAbits.TRMT);
            EUSART_Write(ids[ch]);
            while (!TXSTAbits.TRMT);
            EUSART_Write(high_byte);

            ch++;
            if (ch >= 6) ch = 0;
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