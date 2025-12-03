// main.c - PIC16F1788 - MPLAB X v6.20
// FINAL VERSION - AN3 100% CLEAN (no AN2 bleed)
// Dummy conversion on AN3 = bulletproof
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
volatile __at(0x7A) uint8_t DigitalInputs;   // Bit7=RUNNING, Bit6=SCRAM
volatile __at(0x7C) uint8_t DigitalOutputs;  // Controls PORTB (LEDs + outputs)

const uint8_t ids[7]       = {0x24, 0x25, 0xA0, 0xA1, 0xA2, 0xA3, 0xA5};
const uint8_t base_addr[7] = {0x7A, 0x7C, 0x70, 0x72, 0x74, 0x76, 0x78};

volatile uint8_t ch = 0;
volatile uint8_t first_cycle = 1;

void __interrupt() ISR(void)
{
    if (IOCIF)
    {
        if (IOCAFbits.IOCAF6)  // SCRAM pressed
        {
            IOCAFbits.IOCAF6 = 0;
            __delay_ms(20);
            if (!PORTAbits.RA6)
            {
                DigitalInputs |=  (1 << 6);  // Set SCRAM
                DigitalInputs &= ~(1 << 7);  // Clear RUNNING
            }
        }
        if (IOCAFbits.IOCAF7)  // RESUME pressed
        {
            IOCAFbits.IOCAF7 = 0;
            __delay_ms(20);
            if (!PORTAbits.RA7)
            {
                DigitalInputs &= ~(1 << 6);  // Clear SCRAM
                DigitalInputs |=  (1 << 7);  // Set RUNNING
            }
        }
        IOCIF = 0;
    }
}

void main(void)
{
    SYSTEM_Initialize();

    // PORTB: All digital outputs (debug LEDs + real outputs)
    ANSELB = 0x00;
    TRISB  = 0x00;
    PORTB  = 0x00;

    // IOC Setup
    IOCANbits.IOCAN6 = 1;
    IOCANbits.IOCAN7 = 1;
    IOCAF = 0;
    IOCIE = 1; PEIE = 1; GIE = 1;

    // Start in RUNNING state
    DigitalInputs  = 0x80;        // Bit7 = RUNNING
    DigitalOutputs = 0xFF;        // All LEDs ON

    // Flush UART
    while (!TXSTAbits.TRMT);
    TXREG = 0xFF; while (!TXSTAbits.TRMT);
    TXREG = 0xFF; while (!TXSTAbits.TRMT);

    __delay_ms(100);

    while (1)
    {
        uint8_t scram_active = (DigitalInputs & (1 << 6));

        // ============================================================
        // SCRAM MODE: Blast status + force outputs off
        // ============================================================
        if (scram_active)
        {
            PORTB = 0x00;

            if (TXSTAbits.TRMT)
            {
                EUSART_Write(0x24);
                while (!TXSTAbits.TRMT);
                EUSART_Write(DigitalInputs);        // 0x40

                EUSART_Write(0x25);
                while (!TXSTAbits.TRMT);
                EUSART_Write(0x00);
            }
            __delay_us(20);
            continue;
        }

        // ============================================================
        // NORMAL MODE: Running
        // ============================================================
        PORTB = DigitalOutputs;

        // 0x24 + DigitalInputs
        if (first_cycle || ch == 0)
        {
            while (!TXSTAbits.TRMT);
            EUSART_Write(0x24);
            while (!TXSTAbits.TRMT);
            EUSART_Write(DigitalInputs);    // 0x80 = RUNNING

            FSR0L = base_addr[0];
            INDF0 = DigitalInputs;

            first_cycle = 0;
            ch = 1;
            continue;
        }

        // 0x25 + DigitalOutputs
        if (ch == 1)
        {
            while (!TXSTAbits.TRMT);
            EUSART_Write(0x25);
            while (!TXSTAbits.TRMT);
            EUSART_Write(DigitalOutputs);

            FSR0L = base_addr[1];
            INDF0 = DigitalOutputs;

            ch = 2;

            // Start AN0 with proper delay
            ADC_SetPositiveChannel(0);
            __delay_us(10);
            ADC_StartConversion();
            continue;
        }

        // ADC Channels (ch 2 to 6)
        if (ADC_IsConversionDone())
        {
            uint16_t result = ADC_GetConversionResult();
            uint8_t high_byte = result >> 8;

            // Store & transmit current result
            FSR0L = base_addr[ch];
            INDF0 = (uint8_t)result;
            INDF0 = high_byte;

            while (!TXSTAbits.TRMT);
            EUSART_Write(ids[ch]);
            while (!TXSTAbits.TRMT);
            EUSART_Write(high_byte);

            // === NEXT CHANNEL ===
            ch++;
            if (ch >= 7) ch = 0;

            uint8_t next_channel;
            switch (ch)
            {
                case 2: next_channel = 0; break;  // AN0
                case 3: next_channel = 1; break;  // AN1
                case 4: next_channel = 2; break;  // AN2
                case 5: next_channel = 3; break;  // AN3 ? PROBLEMATIC
                case 6: next_channel = 5; break;  // AN5
                default: next_channel = 0; break;
            }

            // === CRITICAL FIX: Dummy read when switching to AN3 ===
            ADC_SetPositiveChannel(next_channel);
            __delay_us(10);

            if (next_channel == 3)  // AN3 = the cursed channel
            {
                ADC_StartConversion();
                while (!ADC_IsConversionDone());
                (void)ADC_GetConversionResult();  // Discard dummy result
            }

            __delay_us(10);
            ADC_StartConversion();
        }

        __delay_us(20);
    }
}