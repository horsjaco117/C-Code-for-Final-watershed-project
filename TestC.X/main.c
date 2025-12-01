// main.c - PIC16F1788 - MPLAB X v6.20
// Working with your exact (very old) MCC ADC driver

#include "mcc_generated_files/system/system.h"
#include <xc.h>
#include <stdio.h>

void EUSART_WriteString(const char *msg)
{
    while (*msg)
    {
        while (!UART.IsTxReady()) { ; }
        EUSART_Write(*msg++);
    }
}

void main(void)
{
    char buffer[32];

    SYSTEM_Initialize();      // This also calls ADC_Initialize() for you
    __delay_ms(100);

    while (1)
    {
        // 1. Hello World
        EUSART_WriteString("Hello World!\r\n");

        // 2. Start conversion on AN0 (channel 0) ? manual way (your MCC has no wrapper)
        ADCON0bits.CHS = 0;      // Select AN0
        __delay_us(5);           // Acquisition time (safe)
        ADCON0bits.GO = 1;       // Start conversion

        // 3. Wait for conversion to finish
        while (ADCON0bits.GO) { ; }   // GO bit clears automatically when done

        // 4. Read full 10-bit result
        uint16_t adc_value = ((uint16_t)ADRESH << 8) | ADRESL;   // 0?1023

        // 5. Send it
        sprintf(buffer, "ADC=%4u\r\n", adc_value);
        EUSART_WriteString(buffer);

        __delay_ms(100);         // Change delay as you wish
    }
}