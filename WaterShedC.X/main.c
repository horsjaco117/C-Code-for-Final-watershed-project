// main.c - PIC16F1788 @ 16 MHz internal OSC
// FINAL: SCRAM automatically activates PAUSE (bit 5) ? must press RA4 to resume
#include <xc.h>
#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/adc/adc.h"
#include "mcc_generated_files/uart/eusart.h"

// INDIRECT MEMORY
volatile __at(0x70) uint16_t DigitalInputs;
volatile __at(0x72) uint16_t DigitalOutputs;
volatile __at(0x76) uint16_t AN0_value;
volatile __at(0x78) uint16_t AN1_value;
volatile __at(0x7A) uint16_t AN2_value;
volatile __at(0x7C) uint16_t AN3_value;
volatile __at(0x7E) uint16_t AN5_value;

// 14 CLEAN PACKETS
const uint8_t ids[14] = {
    0x24, 0x25, 0xA0, 0xA1, 0xA2, 0xA3, 0xA5,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6
};

const uint8_t base_addr[14] = {
    0x70, 0x72, 0x76, 0x78, 0x7A, 0x7C, 0x7E,
    0x80, 0x82, 0x84, 0x86, 0x88, 0x8A, 0x8C
};

// GLOBALS
volatile uint8_t  ch             = 0;
volatile uint8_t  first_cycle    = 1;
volatile uint8_t  timer_tick     = 0;
#define Tick_Control 38
volatile uint32_t second_counter = 0;
volatile uint8_t  one_second_flag = 0;
volatile uint8_t  current_case   = 0;

uint16_t case_intervals[7] = {1, 1, 1, 1, 1, 1, 1};
volatile uint16_t case_timers[7] = {0};

const uint8_t case_patterns[7] = {
    0b00000001, 0b00000011, 0b00000111, 0b00001111,
    0b00011111, 0b00111111, 0b01111111
};

// TIMER INIT
void Timer2_Init(void) { T2CON = 0; T2CONbits.T2CKPS = 0b10; PR2 = 249; TMR2 = 0; PIR1bits.TMR2IF = 0; PIE1bits.TMR2IE = 1; T2CONbits.TMR2ON = 1; }
void Timer1_Heartbeat_Init(void) { T1CON = 0; T1CONbits.T1CKPS = 0b11; T1CONbits.TMR1CS = 0b01; TMR1H = TMR1L = 0; PIR1bits.TMR1IF = 0; PIE1bits.TMR1IE = 1; T1CONbits.TMR1ON = 1; }
void Timer1_1Second_Perfect_Init(void) { T1CON = 0; T1CONbits.TMR1CS = 0b01; T1CONbits.T1CKPS = 0b11; TMR1H = 0x3C; TMR1L = 0xB0; PIR1bits.TMR1IF = 0; PIE1bits.TMR1IE = 1; T1CONbits.TMR1ON = 1; }

// INTERRUPT
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
        // RA4 = Toggle Pause (only works if not in SCRAM)
        if (IOCAFbits.IOCAF4) { IOCAFbits.IOCAF4 = 0; __delay_ms(20);
            if (!PORTAbits.RA4)
            {
                if (DigitalInputs & (1<<6)) {
                    // If in SCRAM, pressing RA4 clears BOTH SCRAM and PAUSE
                    DigitalInputs &= ~((1<<6) | (1<<5));
                    DigitalInputs |= (1<<7);  // Set RUN flag
                } else {
                    DigitalInputs ^= (1<<5);  // Normal pause toggle
                }
            }
        }

        // RA6 = SCRAM (forces PAUSE too!)
        if (IOCAFbits.IOCAF6) { IOCAFbits.IOCAF6 = 0; __delay_ms(20);
            if (!PORTAbits.RA6)
            {
                DigitalInputs |= (1<<6);   // SCRAM
                DigitalInputs |= (1<<5);   // ? FORCE PAUSE
                DigitalInputs &= ~(1<<7);  // Clear RUN
            }
        }

        // RA7 = Clear SCRAM only (does NOT clear pause!)
        if (IOCAFbits.IOCAF7) { IOCAFbits.IOCAF7 = 0; __delay_ms(20);
            if (!PORTAbits.RA7)
            {
                DigitalInputs &= ~(1<<6);  // Clear SCRAM
                DigitalInputs |= (1<<7);   // Set RUN
                // Pause bit (bit 5) remains unchanged!
            }
        }

        IOCIF = 0;
    }
}

// MAIN
void main(void)
{
    SYSTEM_Initialize();
    ANSELB = 0; TRISB = 0; PORTB = 0;

    TRISAbits.TRISA4 = 1; TRISAbits.TRISA6 = 1; TRISAbits.TRISA7 = 1;
    IOCANbits.IOCAN4 = 1; IOCANbits.IOCAN6 = 1; IOCANbits.IOCAN7 = 1;
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

        // CASE TIMING ? only runs if NOT paused
        if (one_second_flag)
        {
            one_second_flag = 0;
            if (!(DigitalInputs & (1<<5)) && case_timers[current_case] == 0)
            {
                current_case = (current_case + 1) % 7;
                DigitalOutputs = case_patterns[current_case];
                case_timers[current_case] = case_intervals[current_case];
            }
        }

        // SCRAM: turns off outputs + forces pause
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

        // PACKET STREAMING (unchanged)
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

        if (ch >= 7 && ch <= 13)
        {
            uint8_t idx = ch - 7;
            uint16_t val = case_intervals[idx];
            uint8_t low = (uint8_t)val;
            FSR0L = base_addr[ch]; INDF0 = low; FSR0L++; INDF0 = val >> 8;
            while(!TXSTAbits.TRMT); EUSART_Write(ids[ch]);
            while(!TXSTAbits.TRMT); EUSART_Write(low);
            ch++; if (ch >= 14) ch = 0; continue;
        }

        // ANALOG + AN3/AN5 overrange ? SCRAM + PAUSE
        if (ch >= 2 && ch <= 6)
        {
            if (ADC_IsConversionDone())
            {
                uint16_t r = ADC_GetConversionResult();
                FSR0L = base_addr[ch]; INDF0 = (uint8_t)r; FSR0L++; INDF0 = r >> 8;
                while(!TXSTAbits.TRMT); EUSART_Write(ids[ch]);
                while(!TXSTAbits.TRMT); EUSART_Write(r >> 8);

                // AN3 or AN5 = 0xFF ? trigger SCRAM + PAUSE
                if ((ch == 5 || ch == 6) && (r >> 8) == 0xFF)
                {
                    DigitalInputs |= (1<<6);   // SCRAM
                    DigitalInputs |= (1<<5);   // ? FORCE PAUSE
                    DigitalInputs &= ~(1<<7);  // Clear RUN
                }

                ch++; if (ch >= 14) ch = 0;

                uint8_t next_ch = (ch == 2) ? 0 : (ch == 3) ? 1 : (ch == 4) ? 2 : (ch == 5) ? 3 : 4;
                ADC_SetPositiveChannel(next_ch); __delay_us(10);
                if (next_ch == 4) { ADC_StartConversion(); while(ADC_IsConversionDone()); (void)ADC_GetConversionResult(); __delay_us(10); }
                ADC_StartConversion();
            }
            continue;
        }
    }
}