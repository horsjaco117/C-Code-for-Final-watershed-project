// main.c - PIC16F1788 - MPLAB X v6.20
// DigitalInputs register at 0x7A reflects real digital inputs
// RA6 = SCRAM (press ? bit6 = 1 ? stops comms)
// RA7 = RESUME (press ? bit7 = 1 ? resumes comms)
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
volatile __at(0x7A) uint8_t DigitalInputs;   // Now a real status register

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
        // RA6 pressed (SCRAM) ? set bit 6
        if (IOCAFbits.IOCAF6)
        {
            IOCAFbits.IOCAF6 = 0;
            __delay_ms(20);                    // debounce
            if (!PORTAbits.RA6)                // still pressed
            {
                DigitalInputs |= (1 << 6);      // Set bit 6 = SCRAM active
            }
        }

        // RA7 pressed (RESUME) ? set bit 7
        if (IOCAFbits.IOCAF7)
        {
            IOCAFbits.IOCAF7 = 0;
            __delay_ms(20);                    // debounce
            if (!PORTAbits.RA7)                // still pressed (active low)
            {
                DigitalInputs |= (1 << 7);      // Set bit 7 = RESUME requested
                DigitalInputs &= ~(1 << 6);     // Clear SCRAM bit when resuming
            }
        }

        IOCIF = 0;
    }
}

void main(void)
{
    SYSTEM_Initialize();

    // IOC Setup: both pins trigger on falling edge (button press)
    IOCANbits.IOCAN6 = 1;   // RA6 falling edge
    IOCANbits.IOCAN7 = 1;   // RA7 falling edge (both active low)

    IOCAF = 0;
    IOCIE = 1;
    PEIE = 1;
    GIE = 1;

    DigitalInputs = 0x00;   // Start clean

    // Flush UART at startup
    while (!TXSTAbits.TRMT);
    TXREG = 0xFF;
    while (!TXSTAbits.TRMT);
    TXREG = 0xFF;
    while (!TXSTAbits.TRMT);

    __delay_ms(100);

    while (1)
    {
        // Communication is ON only if SCRAM bit (bit6) is NOT set
        uint8_t comms_active = !(DigitalInputs & (1 << 6));

        // === 1. Send 0x24 + DigitalInputs register FIRST ===
        if (first_cycle || ch == 0)
        {
            while (!TXSTAbits.TRMT);
            EUSART_Write(0x24);
            while (!TXSTAbits.TRMT);
            EUSART_Write(DigitalInputs);        // Real status byte

            FSR0L = base_addr[0];
            INDF0 = DigitalInputs;

            first_cycle = 0;
            ch = 1;

            if (comms_active)
            {
                ADC_SetPositiveChannel(CH_AN0);
                ADC_StartConversion();
            }
            continue;
        }

        // === 2. Send ADC data only if communication is active ===
        if (comms_active && ADC_IsConversionDone())
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