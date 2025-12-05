// main.c - PIC16F1788 @ 16 MHz internal OSC
// Per-case delays + custom outputs + C0-C6 delay packets on serial
#include <xc.h>
#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/adc/adc.h"
#include "mcc_generated_files/uart/eusart.h"

// =============================================================
// INDIRECT MEMORY (0x74-0x75 now free)
// =============================================================
volatile __at(0x70) uint16_t DigitalInputs;     // 0x70-0x71
volatile __at(0x72) uint16_t DigitalOutputs;    // 0x72-0x73
volatile __at(0x76) uint16_t AN0_value;         // 0x76-0x77
volatile __at(0x78) uint16_t AN1_value;
volatile __at(0x7A) uint16_t AN2_value;
volatile __at(0x7C) uint16_t AN3_value;
volatile __at(0x7E) uint16_t AN5_value;

// =============================================================
// 16 PACKETS TOTAL: original 9 + new C0-C6
// =============================================================
const uint8_t ids[16] = {
    0x24, 0x25, 0xA0, 0xA1, 0xA2, 0xA3, 0xA5,   // 0-8
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6                  // 9-15 (delay values)
};

const uint8_t base_addr[16] = {
    0x70, 0x72, 0x76, 0x78, 0x7A, 0x7C, 0x7E,   // original
    0x80, 0x82, 0x84, 0x86, 0x88, 0x8A, 0x8C                // free bank 1 area
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

// Your per-case delays in seconds (change any time!)
uint16_t case_intervals[7] = {1, 1, 1, 1, 1, 1, 1};

// Countdown timers
volatile uint16_t case_timers[7] = {0};

// Your custom output pattern for each case (binary as you wanted)
const uint8_t case_patterns[7] = {
    0b00000001,   // Case 0
    0b00000011,   // Case 1
    0b00000111,   // Case 2
    0b00001111,   // Case 3
    0b00011111,   // Case 4
    0b00111111,   // Case 5
    0b01111111    // Case 6
};

uint16_t case_interval_seconds = 1;  // legacy fallback for old C1/C2

// =============================================================
// TIMER INITIALISATIONS (unchanged)
// =============================================================
void Timer2_Init(void) { T2CON = 0; T2CONbits.T2CKPS = 0b10; PR2 = 249; TMR2 = 0; PIR1bits.TMR2IF = 0; PIE1bits.TMR2IE = 1; T2CONbits.TMR2ON = 1; }
void Timer1_Heartbeat_Init(void) { T1CON = 0; T1CONbits.T1CKPS = 0b11; T1CONbits.TMR1CS = 0b01; TMR1H = TMR1L = 0; PIR1bits.TMR1IF = 0; PIE1bits.TMR1IE = 1; T1CONbits.TMR1ON = 1; }
void Timer1_1Second_Perfect_Init(void) { T1CON = 0; T1CONbits.TMR1CS = 0b01; T1CONbits.T1CKPS = 0b11; TMR1H = 0x3C; TMR1L = 0xB0; PIR1bits.TMR1IF = 0; PIE1bits.TMR1IE = 1; T1CONbits.TMR1ON = 1; }

// =============================================================
// INTERRUPT SERVICE ROUTINE
// =============================================================
void __interrupt() ISR(void)
{
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

    if (PIE1bits.TMR2IE && PIR1bits.TMR2IF) { PIR1bits.TMR2IF = 0; timer_tick = 1; }

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
    ANSELB = 0; TRISB = 0; PORTB = 0;
    IOCANbits.IOCAN6 = 1; IOCANbits.IOCAN7 = 1;
    IOCAF = 0; IOCIE = 1; PEIE = 1; GIE = 1;

    DigitalInputs = 0x80;
    DigitalOutputs = 0;

    for (uint8_t i = 0; i < 7; i++) case_timers[i] = case_intervals[i];

    while(!TXSTAbits.TRMT); EUSART_Write(0xFF);
    while(!TXSTAbits.TRMT); EUSART_Write(0xFF);

    Timer2_Init();
    Timer1_Heartbeat_Init();
    Timer1_1Second_Perfect_Init();
    __delay_ms(100);

    while(1)
    {
        if (!timer_tick) continue;
        timer_tick = 0;

        // Per-case timing
        if (one_second_flag)
        {
            one_second_flag = 0;
            if (case_timers[current_case] == 0)
            {
                current_case = (current_case + 1) % 7;
                DigitalOutputs = case_patterns[current_case];   // Your custom pattern
                case_timers[current_case] = case_intervals[current_case];
            }
        }

        // SCRAM override
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
        if (first_cycle || ch == 0)
        {
            while(!TXSTAbits.TRMT); EUSART_Write(0x24);
            while(!TXSTAbits.TRMT); EUSART_Write((uint8_t)DigitalInputs);
            FSR0L = base_addr[0]; INDF0 = (uint8_t)DigitalInputs; FSR0L++; INDF0 = DigitalInputs >> 8;
            first_cycle = 0; ch = 1; continue;
        }

        if (ch == 1)
        {
            while(!TXSTAbits.TRMT); EUSART_Write(0x25);
            while(!TXSTAbits.TRMT); EUSART_Write((uint8_t)DigitalOutputs);
            FSR0L = base_addr[1]; INDF0 = (uint8_t)DigitalOutputs; FSR0L++; INDF0 = DigitalOutputs >> 8;
            ch = 2; ADC_SetPositiveChannel(0); __delay_us(10); ADC_StartConversion(); continue;
        }

        // Legacy C1/C2 (keeps old host happy)
        if (ch == 2 || ch == 3)
        {
            while(!TXSTAbits.TRMT); EUSART_Write(ids[ch]);
            while(!TXSTAbits.TRMT);
            uint8_t v = (ch == 2) ? (uint8_t)case_interval_seconds : (case_interval_seconds >> 8);
            EUSART_Write(v);
            FSR0L = base_addr[ch]; INDF0 = v;
            ch++; if (ch >= 16) ch = 0; continue;
        }

       // NEW: C0-C6 ? low byte of each case delay (perfect for 1?255 seconds)
if (ch >= 9 && ch <= 15)
{
    uint8_t idx = ch - 9;
    uint16_t val = case_intervals[idx];
    uint8_t low = (uint8_t)val;                     // low byte (0?255)

    // Still write both bytes to indirect memory (for consistency)
    FSR0L = base_addr[ch];
    INDF0 = low;
    FSR0L++;
    INDF0 = val >> 8;                               // high byte (usually 0)

    // Send packet ID + low byte (just like analog channels)
    while(!TXSTAbits.TRMT); EUSART_Write(ids[ch]);  // e.g. 0xC0
    while(!TXSTAbits.TRMT); EUSART_Write(low);      // actual delay in seconds

    ch++;
    if (ch >= 16) ch = 0;
    continue;
}

        // Analog channels (100% unchanged)
        if (ADC_IsConversionDone())
        {
            uint16_t r = ADC_GetConversionResult();
            FSR0L = base_addr[ch]; INDF0 = (uint8_t)r; FSR0L++; INDF0 = r >> 8;
            while(!TXSTAbits.TRMT); EUSART_Write(ids[ch]);
            while(!TXSTAbits.TRMT); EUSART_Write(r >> 8);
            ch++; if (ch >= 16) ch = 0;

            uint8_t next_ch;
            switch(ch)
            {
                case 4: next_ch = 0; break;
                case 5: next_ch = 1; break;
                case 6: next_ch = 2; break;
                case 7: next_ch = 3; break;
                case 8: next_ch = 4; break;  // AN5
                default: next_ch = 0; break;
            }
            ADC_SetPositiveChannel(next_ch); __delay_us(10);
            if (next_ch == 5) { ADC_StartConversion(); while(ADC_IsConversionDone()); (void)ADC_GetConversionResult(); __delay_us(10); }
            ADC_StartConversion();
        }
    }
}