// main.c - PIC16F1788 - MPLAB X v6.20
// Sends only: 0xA0 + low_byte   0xA2 + low_byte   etc.
// High byte removed ? 2 bytes per packet, ~4?5 kHz total rate

#include <xc.h>
#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/adc/adc.h"
#include "mcc_generated_files/uart/eusart.h"

// Four dedicated 16-bit registers (only low byte will be sent)
volatile __at(0x70) uint16_t AN0_value;
volatile __at(0x71) uint16_t AN2_value;
volatile __at(0x72) uint16_t AN3_value;
volatile __at(0x73) uint16_t AN5_value;

#define CH_AN0  0
#define CH_AN2  1
#define CH_AN3  2
#define CH_AN5  3

void main(void)
{
    uint8_t ch = 0;
    const uint8_t base_addr[4] = { 0x70, 0x71, 0x72, 0x73 };

    SYSTEM_Initialize();
    __delay_ms(50);

    ADC_SetPositiveChannel(CH_AN0);
    ADC_StartConversion();

    while (1)
    {
        if (ADC_IsConversionDone())
        {
            uint16_t result = ADC_GetConversionResult();
            uint8_t  low   = (uint8_t)result;           // lower 8 bits

            // Store full 16-bit result into dedicated register (indirect)
            FSR0L = base_addr[ch];
            INDF0 = low;                    // low byte
            INDF0 = result >> 8;            // high byte (still saved, just not sent)

            // Send only 2 bytes: ID + low_byte
            while (!UART.IsTxReady());
            EUSART_Write(0xA0 + ch);        // 0xA0, A2, A3, A5

            while (!UART.IsTxReady());
            EUSART_Write(low);

            // Next channel
            ch++;
            if (ch >= 4) ch = 0;

            // Start next conversion
            ADC_SetPositiveChannel(ch==0 ? CH_AN0 :
                                  ch==1 ? CH_AN2 :
                                  ch==2 ? CH_AN3 : CH_AN5);
            ADC_StartConversion();
        }

        // Optional tiny delay ? remove completely for maximum speed
        // __delay_us(5);
    }
}