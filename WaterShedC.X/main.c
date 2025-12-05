// main.c - PIC16F1788 @ 16 MHz internal OSC
// Per-case delays + C0-C6 serial + RA4 = Start/Pause toggle (bit 5)
#include <xc.h>
#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/adc/adc.h"
#include "mcc_generated_files/uart/eusart.h"

// =============================================================
// INDIRECT MEMORY (0x74-0x75 free)
// =============================================================
volatile __at(0x70) uint16_t DigitalInputs;     // 0x70-0x71
volatile __at(0x72) uint16_t DigitalOutputs;    // 0x72-0x73
volatile __at(0x76) uint16_t AN0_value;
volatile __at(0x78) uint16_t AN1_value;
volatile __at(0x7A) uint16_t AN2_value;
volatile __at(0x7C) uint16_t AN3_value;
volatile __at(0x7E) uint16_t AN5_value;

// =============================================================
// 14 PACKETS TOTAL (clean, no garbage)
// =============================================================
const uint8_t ids[14] = {
    0x24, 0x25, 0xA0, 0xA1, 0xA2, 0xA3, 0xA5,   // Digital + Analog
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6      // Delay values
};

const uint8_t base_addr[14] = {
    0x70, 0x72, 0x76, 0x78, 0x7A, 0x7C, 0x7E,
    0x80, 0x82, 0x84, 0x86, 0x88, 0x8A, 0x8C
};

// =============================================================
// GLOBALS
// =============================================================
volatile uint8_t  ch             = 0;
volatile uint8_t  first_cycle    = 1;
volatile uint8_t  timer_tick     = 0;
#define Tick_Control 38
volatile uint32_t second_counter = 0;
volatile uint8_t  one_second_flag = 0;
volatile uint8_t  current_case   = 0;

// Per-case delays (seconds) - change anytime!
uint16_t case_intervals[7] = {1, 1, 1, 1, 1, 1, 1};

// Countdown timers
volatile uint16_t case_timers[7] = {0};

// Output pattern per case
const uint8_t case_patterns[7] = {
    0b00000001, 0b00000011, 0b00000111, 0b00001111,
    0b00011111, 0b00111111, 0b01111111
};

// =============================================================
// TIMER INITIALISATIONS
// =============================================================
void Timer2_Init(void) {
    T2CON = 0; T2CONbits.T2CKPS = 0b10; PR2 = 249;
    TMR2 = 0; PIR1bits.TMR2IF = 0; PIE1bits.TMR2IE = 1; T2CONbits.TMR2ON = 1;
}
void Timer1_Heartbeat_Init(void) {
    T1CON = 0; T1CONbits.T1CKPS = 0b11; T1CONbits.TMR1CS = 0b01;
    TMR1H = TMR1L = 0; PIR1bits.TMR1IF = 0; PIE1bits.TMR1IE = 1; T1CONbits.TMR1ON = 1;
}
void Timer1_1Second_Perfect_Init(void) {
    T1CON = 0; T1CONbits.TMR1CS = 0b01; T1CONbits.T1CKPS = 0b11;
    TMR1H = 0x3C; TMR1L = 0xB0; PIR1bits.TMR1IF = 0; PIE1bits.TMR1IE = 1; T1CONbits.TMR1ON = 1;
}

// =============================================================
// INTERRUPT SERVICE ROUTINE
// =============================================================
void __interrupt() ISR(void)
{
    // 1-second tick
    if (PIE1bits.TMR1IE && PIR1bits.TMR1IF)
    {
        PIR1bits.TMR1IF = 0;
        TMR1H = 0x3C; TMR1L = 0xB0;
        second_counter++;
        if (second_counter >= Tick_Control)
        {
            second_counter = 0;
            one_second_flag = 1;
            for (uint8_t i = 0; i < 7; i++)
                if (case_timers[i]) case_timers[i]--;
        }
    }

    // 1ms tick
    if (PIE1bits.TMR2IE && PIR1bits.TMR2IF) { PIR1bits.TMR2IF = 0; timer_tick = 1; }

    // IOC: RA4 (Pause), RA6 (SCRAM), RA7 (RUN)
    if (IOCIF)
    {
        // RA4 = Start/Pause toggle
        if (IOCAFbits.IOCAF4)
        {
            IOCAFbits.IOCAF4 = 0;
            __delay_ms(20);                     // debounce
            if (!PORTAbits.RA4)
                DigitalInputs ^= (1<<5);        // toggle bit 5
        }

        // RA6 = SCRAM
        if (IOCAFbits.IOCAF6)
        {
            IOCAFbits.IOCAF6 = 0;
            __delay_ms(20);
            if (!PORTAbits.RA6)
            {
                DigitalInputs |= (1<<6);
                DigitalInputs &= ~(1<<7);
            }
        }

        // RA7 = Return to operation
        if (IOCAFbits.IOCAF7)
        {
            IOCAFbits.IOCAF7 = 0;
            __delay_ms(20);
            if (!PORTAbits.RA7)
            {
                DigitalInputs &= ~(1<<6);
                DigitalInputs |= (1<<7);
            }
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

    // Port setup
    ANSELB = 0; TRISB = 0; PORTB = 0;

    // IOC setup: RA4, RA6, RA7 (active low buttons)
    TRISAbits.TRISA4 = 1;
    TRISAbits.TRISA6 = 1;
    TRISAbits.TRISA7 = 1;

    IOCANbits.IOCAN4 = 1;
    IOCANbits.IOCAN6 = 1;
    IOCANbits.IOCAN7 = 1;

    IOCAF = 0;
    IOCIE = 1; PEIE = 1; GIE = 1;

    DigitalInputs = 0x80;           // Running flag
    DigitalOutputs = 0;

    // Prime all timers
    for (uint8_t i = 0; i < 7; i++)
        case_timers[i] = case_intervals[i];

    // Sync bytes
    while(!TXSTAbits.TRMT); EUSART_Write(0xFF);  // wait
    while(!TXSTAbits.TRMT); EUSART_Write(0xFF);  // wait

    Timer2_Init();
    Timer1_Heartbeat_Init();
    Timer1_1Second_Perfect_Init();
    __delay_ms(100);

    while(1)
    {
        if (!timer_tick) continue;
        timer_tick = 0;

        // === CASE TIMING (PAUSABLE via RA4) ===
        if (one_second_flag)
        {
            one_second_flag = 0;

            // If PAUSED (bit 5 set), freeze everything
            if (DigitalInputs & (1<<5))
            {
                // Do nothing - output stays frozen
            }
            else if (case_timers[current_case] == 0)
            {
                current_case = (current_case + 1) % 7;
                DigitalOutputs = case_patterns[current_case];
                case_timers[current_case] = case_intervals[current_case];
            }
        }

        // === SCRAM override ===
        if (DigitalInputs & (1<<6))
        {
            PORTB = 0;
            if (TXSTAbits.TRMT)
            {
                EUSART_Write(0x24); while(!TXSTAbits.TRMT);  // wait
                EUSART_Write(DigitalInputs);
                EUSART_Write(0x25); while(!TXSTAbits.TRMT);  // wait
                EUSART_Write(0x00);
            }
            continue;
        }

        PORTB = DigitalOutputs;

        // === PACKET STREAMING ===
        if (first_cycle || ch == 0)
        {
            while(!TXSTAbits.TRMT); EUSART_Write(0x24);  // wait
            while(!TXSTAbits.TRMT); EUSART_Write((uint8_t)DigitalInputs);  // wait
            FSR0L = base_addr[0]; INDF0 = (uint8_t)DigitalInputs; FSR0L++; INDF0 = DigitalInputs >> 8;
            first_cycle = 0; ch = 1; continue;
        }

        if (ch == 1)
        {
            while(!TXSTAbits.TRMT); EUSART_Write(0x25);  // wait
            while(!TXSTAbits.TRMT); EUSART_Write((uint8_t)DigitalOutputs);  // wait
            FSR0L = base_addr[1]; INDF0 = (uint8_t)DigitalOutputs; FSR0L++; INDF0 = DigitalOutputs >> 8;
            ch = 2; ADC_SetPositiveChannel(0); __delay_us(10); ADC_StartConversion(); continue;
        }

        // C0-C6: send delay values (low byte)
        if (ch >= 7 && ch <= 13)
        {
            uint8_t idx = ch - 7;
            uint16_t val = case_intervals[idx];
            uint8_t low = (uint8_t)val;

            FSR0L = base_addr[ch]; INDF0 = low; FSR0L++; INDF0 = val >> 8;
            while(!TXSTAbits.TRMT); EUSART_Write(ids[ch]);  // wait
            while(!TXSTAbits.TRMT); EUSART_Write(low);     // wait
            ch++; if (ch >= 14) ch = 0; continue;
        }

        // Analog channels
        if (ADC_IsConversionDone())
        {
            uint16_t r = ADC_GetConversionResult();
            FSR0L = base_addr[ch]; INDF0 = (uint8_t)r; FSR0L++; INDF0 = r >> 8;
            while(!TXSTAbits.TRMT); EUSART_Write(ids[ch]);  // wait
            while(!TXSTAbits.TRMT); EUSART_Write(r >> 8);   // wait
            ch++; if (ch >= 14) ch = 0;

            uint8_t next_ch;
            switch(ch)
            {
                case 4: next_ch = 0; break;
                case 5: next_ch = 1; break;
                case 6: next_ch = 2; break;
                case 7: next_ch = 3; break;
                case 8: next_ch = 4; break;
                default: next_ch = 0; break;
            }
            ADC_SetPositiveChannel(next_ch); __delay_us(10);
            if (next_ch == 5) { ADC_StartConversion(); while(ADC_IsConversionDone()); (void)ADC_GetConversionResult(); __delay_us(10); }
            ADC_StartConversion();
        }
    }
}