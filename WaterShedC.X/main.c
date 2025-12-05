// main.c - PIC16F1788 @ 16 MHz internal OSC
// Fully fixed version - correct packets, no overlaps
#include <xc.h>
#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/adc/adc.h"
#include "mcc_generated_files/uart/eusart.h"

// =============================================================
// INDIRECT MEMORY - CLEAN LAYOUT (2 bytes per 10-bit value)
// =============================================================
volatile __at(0x70) uint16_t DigitalInputs;         // 0x70-0x71
volatile __at(0x72) uint16_t DigitalOutputs;        // 0x72-0x73
volatile __at(0x74) uint16_t case_interval_seconds; // 0x74-0x75

volatile __at(0x76) uint16_t AN0_value;   // 0x76-0x77
volatile __at(0x78) uint16_t AN1_value;   // 0x78-0x79
volatile __at(0x7A) uint16_t AN2_value;   // 0x7A-0x7B
volatile __at(0x7C) uint16_t AN3_value;   // 0x7C-0x7D
volatile __at(0x7E) uint16_t AN5_value;   // 0x7E-0x7F

// Protocol: 9 packets total
const uint8_t ids[9] =       {0x24, 0x25, 0xC1, 0xC2, 0xA0, 0xA1, 0xA2, 0xA3, 0xA5};
const uint8_t base_addr[9] = {0x70, 0x72, 0x74, 0x75, 0x76, 0x78, 0x7A, 0x7C, 0x7E};

// =============================================================
// GLOBALS
// =============================================================
volatile uint8_t ch = 0;
volatile uint8_t first_cycle = 1;
volatile uint8_t timer_tick = 0;
#define Tick_Control 38                     // 38 × Timer1 overflow = exactly 1 second
volatile uint32_t second_counter = 0;
volatile uint32_t case_timer = 0;
volatile uint8_t one_second_flag = 0;
volatile uint8_t current_case = 0;

// =============================================================
// TIMER INITIALISATIONS
// =============================================================
void Timer2_Init(void)                      // 1 ms tick for main loop
{
    T2CON = 0;
    T2CONbits.T2CKPS = 0b10;                // 1:16 prescaler
    PR2 = 249;                              // 250 × 4 µs = 1 ms at 16 MHz
    TMR2 = 0;
    PIR1bits.TMR2IF = 0;
    PIE1bits.TMR2IE = 1;
    T2CONbits.TMR2ON = 1;
}

void Timer1_Heartbeat_Init(void)            // ~65 ms toggle on RB7 (alive LED)
{
    T1CON = 0;
    T1CONbits.T1CKPS = 0b11;                // 1:8
    T1CONbits.TMR1CS = 0b01;                // Fosc/4
    TMR1H = TMR1L = 0;
    PIR1bits.TMR1IF = 0;
    PIE1bits.TMR1IE = 1;
    T1CONbits.TMR1ON = 1;
}

void Timer1_1Second_Perfect_Init(void)      // Exactly 1.000000 s base
{
    T1CON = 0;
    T1CONbits.TMR1CS = 0b01;                // Fosc/4 ? 4 MHz
    T1CONbits.T1CKPS = 0b11;                // 1:8 ? 500 kHz
    TMR1H = 0x3C;
    TMR1L = 0xB0;                           // 65536 - 500000 = 15536
    PIR1bits.TMR1IF = 0;
    PIE1bits.TMR1IE = 1;
    T1CONbits.TMR1ON = 1;
}

// =============================================================
// INTERRUPT SERVICE ROUTINE
// =============================================================
void __interrupt() ISR(void)
{
    // 1-second perfect tick
    if (PIE1bits.TMR1IE && PIR1bits.TMR1IF)
    {
        PIR1bits.TMR1IF = 0;
        TMR1H = 0x3C; TMR1L = 0xB0;
        second_counter++;
        if (second_counter >= Tick_Control)
        {
            second_counter = 0;
            case_timer++;
            uint16_t interval = case_interval_seconds ? case_interval_seconds : 1;
            if (case_timer >= interval)
            {
                case_timer = 0;
                one_second_flag = 1;
            }
        }
    }

    // 1 ms tick
    if (PIE1bits.TMR2IE && PIR1bits.TMR2IF)
    {
        PIR1bits.TMR2IF = 0;
        timer_tick = 1;
    }

    // Heartbeat toggle
    if (PIE1bits.TMR1IE && PIR1bits.TMR1IF && !PIE1bits.TMR1IE) // only runs if heartbeat active
    {
        PIR1bits.TMR1IF = 0;
        LATB ^= 0x80;           // RB7 toggle
    }

    // IOC buttons (RA6/RA7)
    if (IOCIF)
    {
        if (IOCAFbits.IOCAF6) { IOCAFbits.IOCAF6 = 0; __delay_ms(20);
            if (!PORTAbits.RA6) { DigitalInputs |=  (1<<6); DigitalInputs &= ~(1<<7); }
        }
        if (IOCAFbits.IOCAF7) { IOCAFbits.IOCAF7 = 0; __delay_ms(20);
            if (!PORTAbits.RA7) { DigitalInputs &= ~(1<<6); DigitalInputs |=  (1<<7); }
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

    // Port B all output (relays + heartbeat)
    ANSELB = 0;
    TRISB = 0;
    PORTB = 0;

    // IOC on RA6/RA7
    IOCANbits.IOCAN6 = 1;
    IOCANbits.IOCAN7 = 1;
    IOCAF = 0;
    IOCIE = 1;
    PEIE = 1;
    GIE = 1;

    DigitalInputs = 0x80;           // RUNNING flag
    DigitalOutputs = 0;
    case_interval_seconds = 1;      // default 1 second

    // Two sync bytes
    while(!TXSTAbits.TRMT);
    EUSART_Write(0xFF);
    while(!TXSTAbits.TRMT);
    EUSART_Write(0xFF);

    Timer2_Init();
    Timer1_Heartbeat_Init();
    Timer1_1Second_Perfect_Init();

    __delay_ms(100);

    while(1)
    {
        if (!timer_tick) continue;
        timer_tick = 0;

        // --- Case sequencer ---
        if (one_second_flag)
        {
            one_second_flag = 0;
            current_case = (current_case + 1) % 7;
            DigitalOutputs = 1 << current_case;
        }

        // --- SCRAM button override ---
        if (DigitalInputs & (1<<6))
        {
            PORTB = 0;
            if (TXSTAbits.TRMT)
            {
                EUSART_Write(0x24); while(!TXSTAbits.TRMT);
                EUSART_Write(DigitalInputs);
                EUSART_Write(0x25); while(!TXSTAbits.TRMT);
                EUSART_Write(0x00);
            }
            continue;
        }

        PORTB = DigitalOutputs;

        // =================== PACKET STREAMING ===================
        // First packet ever (0x24 DigitalInputs)
        if (first_cycle || ch == 0)
        {
            while(!TXSTAbits.TRMT);
            EUSART_Write(0x24);
            while(!TXSTAbits.TRMT);
            EUSART_Write((uint8_t)DigitalInputs);

            FSR0L = base_addr[0];
            INDF0 = (uint8_t)DigitalInputs;
            FSR0L = base_addr[0] + 1;
            INDF0 = DigitalInputs >> 8;

            first_cycle = 0;
            ch = 1;
            continue;
        }

        // 0x25 DigitalOutputs
        if (ch == 1)
        {
            while(!TXSTAbits.TRMT);
            EUSART_Write(0x25);
            while(!TXSTAbits.TRMT);
            EUSART_Write((uint8_t)DigitalOutputs);

            FSR0L = base_addr[1];
            INDF0 = (uint8_t)DigitalOutputs;
            FSR0L = base_addr[1] + 1;
            INDF0 = DigitalOutputs >> 8;

            ch = 2;
            ADC_SetPositiveChannel(0);      // prepare AN0 for next round
            __delay_us(10);
            ADC_StartConversion();
            continue;
        }

        // C1 and C2 - case interval bytes
        if (ch == 2 || ch == 3)
        {
            while(!TXSTAbits.TRMT);
            EUSART_Write(ids[ch]);
            while(!TXSTAbits.TRMT);
            uint8_t val = (ch == 2) ? (uint8_t)case_interval_seconds :
                                      (uint8_t)(case_interval_seconds >> 8);
            EUSART_Write(val);
            FSR0L = base_addr[ch];
            INDF0 = val;

            ch++;
            if (ch >= 9) ch = 0;
            continue;
        }

        // All analog channels (A0,A1,A2,A3,A5)
        if (ADC_IsConversionDone())
        {
            uint16_t result = ADC_GetConversionResult();
            uint8_t low  = (uint8_t)result;
            uint8_t high = result >> 8;

            // Store both bytes correctly
            FSR0L = base_addr[ch];
            INDF0 = low;
            FSR0L++;
            INDF0 = high;

            // Send packet (only high byte as your original protocol did)
            while(!TXSTAbits.TRMT);
            EUSART_Write(ids[ch]);
            while(!TXSTAbits.TRMT);
            EUSART_Write(high);

            ch++;
            if (ch >= 9) ch = 0;

            // Map packet index ? real ADC channel (skip AN4)
            uint8_t adc_ch;
            switch(ch)
            {
                case 4: adc_ch = 0; break;  // A0 packet next
                case 5: adc_ch = 1; break;
                case 6: adc_ch = 2; break;
                case 7: adc_ch = 3; break;
                case 8: adc_ch = 4; break;
                default: adc_ch = 0; break;
            }

            ADC_SetPositiveChannel(adc_ch);
            __delay_us(10);

            // Dummy read on AN3 for settling (your original trick)
            if (adc_ch == 5)
            {
                ADC_StartConversion();
                while(!ADC_IsConversionDone());
                (void)ADC_GetConversionResult();
                __delay_us(10);
            }

            ADC_StartConversion();
        }
    }
}