// Modified main.c for PIC16F1788 in MPLAB X IDE v6.20
/*

MAIN Generated Driver File

@file main.c

@defgroup main MAIN

@brief This is the generated driver implementation file for the MAIN driver.

@version MAIN Driver Version 1.0.2

@version Package Version: 3.1.2
/
/
© [2025] Microchip Technology Inc. and its subsidiaries.
Subject to your compliance with these terms, you may use Microchip
software and any derivatives exclusively with Microchip products.
You are responsible for complying with 3rd party license terms
applicable to your use of 3rd party software (including open source
software) that may accompany Microchip software. SOFTWARE IS ?AS IS.?
NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS
SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT,
MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY
KIND WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF
MICROCHIP HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE
FORESEEABLE. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP?S
TOTAL LIABILITY ON ALL CLAIMS RELATED TO THE SOFTWARE WILL NOT
EXCEED AMOUNT OF FEES, IF ANY, YOU PAID DIRECTLY TO MICROCHIP FOR
THIS SOFTWARE.
*/
#include "mcc_generated_files/system/system.h"
#include <string.h>
#include <xc.h>  // Added: Include for __delay_ms() if not already in system.h (explicit for delay macro).

void EUSART_WriteString(const char *message);  // Changed: Renamed from UART_WriteString to match EUSART API (no code removed).
void EUSART_WriteString(const char *message)  // Changed: Renamed from UART_WriteString.
{
for(int i = 0; i < (int)strlen(message); i++)
{
while(!UART.IsTxReady())  // Changed: Replaced UART.IsTxReady() with EUSART_is_tx_ready() (MCC's correct polling function).
{
;
};
EUSART_Write(message[i]);  // Changed: Replaced UART.Write() with EUSART_Write() (MCC's correct transmit function).
}
}
void main(void)
{
const char message[] = "Hello World!\r\n";
SYSTEM_Initialize();
EUSART_WriteString(message);  // Changed: Renamed call from UART_WriteString (removed redundant (void) cast).
while(1)
{
__delay_ms(10);  // Added: Simple 1-second delay for repeated testing (e.g., resend message); remove if not needed.
EUSART_WriteString(message);  // Added: Repeat send in loop for visibility on terminal (explicit for debugging serial).
}
}
// What was added:
// - #include <xc.h> for __delay_ms() (if your OSC is 4MHz or configured in MCC, adjust via #define _XTAL_FREQ if needed).
// - __delay_ms(1000) and repeated EUSART_WriteString in while(1) for debug (simple loop test).
// What was taken out:
// - Nothing fully removed; only renamed UART to EUSART and adjusted function calls for correctness (original UART. notation was invalid).