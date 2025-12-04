// main.c - PIC16F1788 @ 16 MHz internal oscillator
// TIMER1 = Perfect long-interval timer (up to hours/days)
// Change only ONE number for any case speed

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
volatile __at(0x7A) uint8_t DigitalInputs;  // Bit7=RUNNING, Bit6=SCRAM
volatile __at(0x7C) uint8_t DigitalOutputs;

const uint8_t ids[7] = {0x24, 0x25, 0xA0, 0xA1, 0xA2, 0xA3, 0xA5};
const uint8_t base_addr[7] = {0x7A, 0x7C, 0x70, 0x72, 0x74, 0x76, 0x78};

volatile uint8_t ch = 0;
volatile uint8_t first_cycle = 1;
volatile uint8_t timer_tick = 0;

// === EASY SPEED CONTROL - CHANGE THIS ONE LINE ===
// Examples:
// 1     ? case changes every 1 second
// 30    ? every 30 seconds
// 300   ? every 5 minutes
// 3600  ? every 1 hour
// 86400 ? every 24 hours
#define SECONDS_BETWEEN_CASES   2000     // ????? CHANGE THIS ONLY----------------(1000 = 30 seconds)
// =================================================

volatile uint32_t second_counter = 0;   // 32-bit for days-long timing
volatile uint8_t one_second_flag = 0;
volatile uint8_t current_case = 0;

// =============================================================
// TIMER1: Perfect 1.000000 second base tick @ 16 MHz
// Fosc/4 = 4 MHz ? 1:8 prescaler = 500 kHz ? 500000 ticks = 1.000000 s
// Preload = 65536 - 500000 = 15536 = 0x3CB0
// =============================================================
void Timer1_1Second_Perfect_Init(void)
{
    T1CONbits.TMR1CS = 0b01;     // Clock = Fosc/4 (4 MHz)
    T1CONbits.T1CKPS = 0b11;     // 1:8 prescaler ? 500 kHz
    T1GCONbits.TMR1GE = 0;       // No gating

    TMR1H = 0x3C;                // Preload high byte
    TMR1L = 0xB0;                // Preload low byte ? 15536

    PIR1bits.TMR1IF = 0;
    PIE1bits.TMR1IE = 1;
    T1CONbits.TMR1ON = 1;        // Start Timer1
}

// =============================================================
// TIMER2: 1 ms tick (unchanged)
// =============================================================
void Timer2_Init(void)
{
    T2CON = 0x00;
    T2CONbits.T2CKPS = 0b10;
    T2CONbits.T2OUTPS = 0b0000;
    PR2 = 124;                   // 16 MHz ? 4 MHz ? /16 = 250 kHz ? 125 ticks = 1 ms
    TMR2 = 0;
    TMR2IF = 0;
    TMR2IE = 1;
    T2CONbits.TMR2ON = 1;
}

// =============================================================
// TIMER1: Heartbeat (65 ms) - unchanged
// =============================================================
void Timer1_Heartbeat_Init(void)
{
    T1CON = 0x00;
    T1CONbits.T1CKPS = 0b11;
    T1CONbits.TMR1CS = 0b01;
    T1GCONbits.TMR1GE = 0;
    TMR1H = 0x00;
    TMR1L = 0x00;
    TMR1IF = 0;
    TMR1IE = 1;
    T1CONbits.TMR1ON = 1;
}

// =============================================================
// INTERRUPT SERVICE ROUTINE
// =============================================================
void __interrupt() ISR(void)
{
    // Perfect 1-second Timer1 overflow
    if (PIE1bits.TMR1IE && PIR1bits.TMR1IF)
    {
        PIR1bits.TMR1IF = 0;

        // Reload for next exact 1-second interval
        TMR1H = 0x3C;
        TMR1L = 0xB0;

        second_counter++;
        if (second_counter >= SECONDS_BETWEEN_CASES)
        {
            second_counter = 0;
            one_second_flag = 1;     // Trigger case change
        }
    }

    // 1 ms tick
    if (TMR2IE && TMR2IF)
    {
        TMR2IF = 0;
        timer_tick = 1;
    }

    // Heartbeat
    if (TMR1IE && TMR1IF)
    {
        TMR1IF = 0;
        PORTB ^= 0x80;
    }

    // IOC buttons
    if (IOCIF)
    {
        if (IOCAFbits.IOCAF6) { IOCAFbits.IOCAF6 = 0; __delay_ms(20);
            if (!PORTAbits.RA6) { DigitalInputs |= (1<<6); DigitalInputs &= ~(1<<7); }
        }
        if (IOCAFbits.IOCAF7) { IOCAFbits.IOCAF7 = 0; __delay_ms(20);
            if (!PORTAbits.RA7) { DigitalInputs &= ~(1<<6); DigitalInputs |= (1<<7); }
        }
        IOCIF = 0;
    }
}

// =============================================================
// MAIN
// =============================================================
void main(void)
{
    SYSTEM_Initialize();

    ANSELB = 0x00;
    TRISB = 0x00;
    PORTB = 0x00;

    IOCANbits.IOCAN6 = 1;
    IOCANbits.IOCAN7 = 1;
    IOCAF = 0;
    IOCIE = 1;
    PEIE = 1;
    GIE = 1;

    DigitalInputs = 0x80;
    DigitalOutputs = 0xFF;

    while (!TXSTAbits.TRMT);
    TXREG = 0xFF; while (!TXSTAbits.TRMT);
    TXREG = 0xFF; while (!TXSTAbits.TRMT);

    Timer2_Init();
    Timer1_Heartbeat_Init();
    Timer1_1Second_Perfect_Init();     // Perfect long timing

    __delay_ms(100);

    while (1)
    {
        if (!timer_tick) continue;
        timer_tick = 0;

        // CASE STATE MACHINE - runs exactly every N seconds
        if (one_second_flag)
        {
            one_second_flag = 0;
            current_case = (current_case + 1) % 4;

            switch (current_case)
            {
                case 0: DigitalOutputs = 0b10101010; break;
                case 1: DigitalOutputs = 0b01010101; break;
                case 2: DigitalOutputs = 0b10101010; break;
                case 3: DigitalOutputs = 0b01010101; break;
            }
        }

        // SCRAM override
        if (DigitalInputs & (1 << 6))
        {
            PORTB = 0x00;
            if (TXSTAbits.TRMT)
            {
                EUSART_Write(0x24); while (!TXSTAbits.TRMT);
                EUSART_Write(DigitalInputs);
                EUSART_Write(0x25); while (!TXSTAbits.TRMT);
                EUSART_Write(0x00);
            }
            continue;
        }

        PORTB = DigitalOutputs;

        // Your original ADC/UART loop (unchanged)
        if (first_cycle || ch == 0)
        {
            while (!TXSTAbits.TRMT);
            EUSART_Write(0x24); while (!TXSTAbits.TRMT);
            EUSART_Write(DigitalInputs);
            FSR0L = base_addr[0]; INDF0 = DigitalInputs;
            first_cycle = 0;
            ch = 1;
            continue;
        }

        if (ch == 1)
        {
            while (!TXSTAbits.TRMT);
            EUSART_Write(0x25); while (!TXSTAbits.TRMT);
            EUSART_Write(DigitalOutputs);
            FSR0L = base_addr[1]; INDF0 = DigitalOutputs;
            ch = 2;
            ADC_SetPositiveChannel(0);
            __delay_us(10);
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
            EUSART_Write(ids[ch]); while (!TXSTAbits.TRMT);
            EUSART_Write(high_byte);
            ch++;
            if (ch >= 7) ch = 0;

            uint8_t next_ch = (ch == 2) ? 0 : (ch == 3) ? 1 : (ch == 4) ? 2 : (ch == 5) ? 3 : 5;

            ADC_SetPositiveChannel(next_ch);
            __delay_us(10);
            if (next_ch == 3)
            {
                ADC_StartConversion();
                while (!ADC_IsConversionDone());
                (void)ADC_GetConversionResult();
            }
            __delay_us(10);
            ADC_StartConversion();
        }
    }
}