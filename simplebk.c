#include "device.h"

#include <inttypes.h>

#include <avr/io.h>
#include <avr/pgmspace.h>

#include <util/delay.h>

#include <stdlib.h>

#include <avr/interrupt.h>

#include "init.h"
#include "disp1.h"
#include "colors.h"


#ifndef RXB8
#define RXB8 1
#endif

#ifndef TXB8
#define TXB8 0
#endif

#ifndef UPE
#define UPE 2
#endif

#ifndef DOR
#define DOR 3
#endif

#ifndef FE
#define FE 4
#endif

#ifndef UDRE
#define UDRE 5
#endif

#ifndef RXC
#define RXC 7
#endif

#define FRAMING_ERROR (1<<FE)
#define PARITY_ERROR (1<<UPE)
#define DATA_OVERRUN (1<<DOR)
#define DATA_REGISTER_EMPTY (1<<UDRE)
#define RX_COMPLETE (1<<RXC)

#define Connect      1
#define ReadData     2
#define ReadError    3
#define ReadADC      4
#define SpeedSample  5
#define Menu         6

// ����������� ���������
#define Y_minus       PORTC.0
#define Y_plus        PORTC.1
#define X_minus       PORTC.2
#define X_plus        PORTC.3

int mode;
int x,y;
uint8_t push, press, draw_rashod, fill_rashod = 0;
int press_count = 0;

unsigned int svetlii_color = 0x1A12;   // ���� ������� ���� (�������)
unsigned int temnii_color  = 0x218E;   // ���� ������� ���� (������)
unsigned int text_parametr = 0xFFE0;   // ���� ����������
unsigned int text_tablo    = 0xFFFF;   // ���� ��������

char convert[32];

#define BUFFER_SIZE 200
unsigned char buffer[BUFFER_SIZE];                       // �������� ������

const prog_uint8_t startCommunication[]                 = {0x81,0x10,0xf1,0x81,0x03};
const prog_uint8_t readDataByLocalIdentifier_RLI_ASS[]  = {0x82,0x10,0xF1,0x21,0x01,0xA5};
const prog_uint8_t readDTCByStatus[]                    = {0x84,0x10,0xF1,0x18,0x00,0x00,0x00,0x9D};
const prog_uint8_t clearDiagnosticInformation[]         = {0x83,0x10,0xf1,0x14,0x00,0x00,0x98};
const prog_uint8_t testerPresent[]                      = {0x82,0x10,0xf1,0x3E,0x01,0xC2};
const prog_uint8_t readDataByLocalIdentifier_RLI_FT[]   = {0x82,0x10,0xF1,0x21,0x03,0xA7};

volatile uint8_t  StartSend, first_draw, go, zamer_finish;
volatile unsigned char   counter;
volatile unsigned int    ms;
volatile unsigned int    dsec;
unsigned char            Connect_Delay;
float rashod = 0;
int rashod_middle = 0;

uint8_t ekran;

// USART Receiver interrupt service routine
ISR(USART_RX_vect) {
    char status,data;
    status = UCSR0A;
    data = UDR0;
    if ((status & (FRAMING_ERROR | PARITY_ERROR | DATA_OVERRUN)) == 0) {
	buffer[counter] = data;
	if (++counter == BUFFER_SIZE) {
	    counter = 0;
	}
    }
}

void SendCommand (prog_uint8_t *command, uint8_t length) {
    counter = 0;
    while (length--) {
	while(!(UCSR0A & (1 << UDRE)));  // ���� ��������� �������� �����
	UDR0 = *command++;
    }
}

// Timer 0 overflow interrupt service routine
ISR(TIMER0_OVF_vect) {
    // Reinitialize Timer 0 value
    TCNT0=0x06;
    if (++ms > 250) {
	ms = 0;
	StartSend = 1;       // ��������� �������� ������� ����� ������ 250 ��
    }
}

// Timer1 overflow interrupt service routine
ISR(TIMER1_OVF_vect) {
    // Reinitialize Timer1 value
    TCNT1H=0xE796 >> 8;           // ������ ����������� ������ 0,1 ���
    TCNT1L=0xE796 & 0xff;

    if (++dsec > 300) {           // ���� �� ������ 30 ���
	TCCR1B=0x00;
	go = 0;
	zamer_finish = 1;
    }
}

#define ADC_VREF_TYPE 0x40

// Read the AD conversion result
unsigned int read_adc(unsigned char adc_input) {
    ADMUX=adc_input | (ADC_VREF_TYPE & 0xff);
    _delay_us(10);                  // Delay needed for the stabilization of the ADC input voltage
    ADCSRA|=0x40;                  // Start the AD conversion
    while ((ADCSRA & 0x10)==0);    // Wait for the AD conversion to complete
    ADCSRA|=0x10;
    return ADCW;
}

void TouchScan (void) {
    push = 0;
    DDRC =  0b10100000;            //  X_minus, X_plus �� �����, ������ � ������ �� �����
    PORTC = 0b01011111;            //  ��������� ������ ��� ����� � ���������
    _delay_ms(1);

    if (read_adc(6) < 100) {       // ���� ���� ������� ( ��������� �� 0 ����� Y- )
	push = 1;
	press_count++;
	if (press_count > 1) {
	    press = 1;
	}

	PORTC = 0b01111111;        // c�������� X ����������, X_minus �� �����, X_plus �� +5 ����� 
	_delay_ms(1);
	x = abs((int) (232 - 0.276*read_adc(4)));

	// ���������� �������� ��� �� 2-� ������
	// ������������ ����������
	// � ����� ��������� ������ �� 2-� ������ ������� ������������� �������� ������� 
	// x = 232 - 0.276*ADC
	// y = 0.25*ADC - 67.5

	DDRC = 0b01011111;         // c�������� Y ����������, Y_minus, Y_plus �� �����  
	PORTC = 0b10111111;        //  Y_minus �� �����, Y_plus �� +5 �����
	_delay_ms(1);
	y = abs((int) (0.25*read_adc(5)-67.5));
    } else {
	press = 0;
	press_count = 0;
    }
}

int main(void) {
    // Declare your local variables here


/*
    // Input/Output Ports initialization
    // Port A initialization
    // Func7=In Func6=In Func5=In Func4=In Func3=In Func2=In Func1=In Func0=In 
    // State7=T State6=T State5=T State4=T State3=T State2=T State1=P State0=T 
    PORTC=0x02;
    DDRC=0x00;
*/

        // Port B initialization
    // Func7=Out Func6=In Func5=Out Func4=Out Func3=In Func2=In Func1=In Func0=In 
    // State7=0 State6=T State5=0 State4=0 State3=T State2=T State1=T State0=T 
    PORTB=0x00;
    DDRB=0xFF;

    // Port C initialization
    // Func7=In Func6=In Func5=In Func4=In Func3=In Func2=In Func1=In Func0=In 
    // State7=T State6=T State5=T State4=T State3=T State2=T State1=T State0=T 
    PORTC=0x00;
    DDRC=0x00;

    // Port D initialization
    // Func7=In Func6=In Func5=In Func4=In Func3=In Func2=In Func1=In Func0=In 
    // State7=T State6=T State5=T State4=T State3=T State2=T State1=T State0=T 
    PORTD=0x00;
    DDRD=0x00;


    port_init();



    // Timer/Counter 0 initialization
    // Clock source: System Clock
    // Clock value: 250,000 kHz
    // Mode: Normal top=0xFF
    // OC0 output: Disconnected
    TCCR0A=0x03;
    TCNT0=0x06;
    OCR0A=0x00;

    // Timer/Counter 1 initialization
    // Clock source: System Clock
    // Clock value: 62,500 kHz
    // Mode: Normal top=0xFFFF
    // OC1A output: Discon.
    // OC1B output: Discon.
    // Noise Canceler: Off
    // Input Capture on Falling Edge
    // Timer1 Overflow Interrupt: On
    // Input Capture Interrupt: Off
    // Compare A Match Interrupt: Off
    // Compare B Match Interrupt: Off
    TCCR1A=0x00;
    TCCR1B=0x00;
    //TCCR1B=0x04;
    TCNT1H=0xE7;
    TCNT1L=0x96;
    ICR1H=0x00;
    ICR1L=0x00;
    OCR1AH=0x00;
    OCR1AL=0x00;
    OCR1BH=0x00;
    OCR1BL=0x00;

    // Timer/Counter 2 initialization
    // Clock source: System Clock
    // Clock value: Timer2 Stopped
    // Mode: Normal top=0xFF
    // OC2 output: Disconnected
    ASSR=0x00;
    TCCR2A=0x00;
    TCNT2=0x00;
    OCR2A=0x00;

    // External Interrupt(s) initialization
    // INT0: Off
    // INT1: Off
    // INT2: Off
    MCUCR=0x00;
    MCUSR=0x00;

    // Timer(s)/Counter(s) Interrupt(s) initialization
    TIMSK0=0x05;

    // USART initialization
    // Communication Parameters: 8 Data, 1 Stop, No Parity
    // USART Receiver: On
    // USART Transmitter: On
    // USART Mode: Asynchronous
    // USART Baud Rate: 10400
    UCSR0A=0x00;
    UCSR0B=0x98;
    UCSR0C=0x86;
    UBRR0H=0x00;
    UBRR0L=0x5F;

    // Analog Comparator initialization
    // Analog Comparator: Off
    // Analog Comparator Input Capture by Timer/Counter 1: Off
    ACSR=0x80;
    ADCSRB = 0x00;

//    SFIOR=0x00;

    // ADC initialization
    // ADC Clock frequency: 125,000 kHz
    // ADC Voltage Reference: AVCC pin
    // ADC Auto Trigger Source: ADC Stopped
    ADMUX=ADC_VREF_TYPE & 0xff;
    ADCSRA=0x87;

    // SPI initialization
    // SPI Type: Master
    // SPI Clock Rate: 2*4000,000 kHz
    // SPI Clock Phase: Cycle Start
    // SPI Clock Polarity: Low
    // SPI Data Order: MSB First
    SPCR=0x50;
    SPSR=0x01;

    // TWI initialization
    // TWI disabled
    TWCR=0x00;

    // Global enable interrupts
    asm volatile("sei");

    s65_init();
//    fill_screen(bgcolor);
    mode = Connect;
    first_draw = 1;

    while (1) {
/*
	if (PINC.1 == 1) {                                 // ���� ������� ���������� �������,
	    delay_ms(5);                                   // �� ��������� ������� �������� ����������,
	    if (PINA.1 == 1) {                             // ����� �������� ���� ��� ������
		LCD_PowerOff();
	    }
	}
*/
	TouchScan();                                       // ������������ ��������

	switch (mode) {                                  // ������� ����� �����
	    case Connect:                                  // ���� ������ ��� ���������� => ������������ � ���
		if (StartSend == 1) {
		    put_string( 32, 60, "  Connecting...  ", 0xAFE0, 1);
		    _delay_ms(100);
		    SendCommand(startCommunication,5);
		    StartSend = 0;

		    // ������ 8 ������� ������������
		    if (++Connect_Delay > 8) {
			put_string(32,60,"No connection with ECU",0xAFE0,1);  // ���� ��� ������, �� ������� ��������������� ���������
			_delay_ms(2000);                                // � ����� 2 ��� ������� ������������ �����
			Connect_Delay = 0;
		    }
		} else {
		    // ������  ������ + ����� 
		    if (counter > 11) {
			unsigned char i = 0;
			unsigned int crc = 0;

			// ������ + ����� = 12 ����
			for (i=5;i<11;i++) crc = crc + buffer[i];               // ������� ����������� �����

			i =  crc & 0xFF;                         // ����� 2 ������� ������� 
			if ( buffer[11] == i ) {                 // ��������� ����������� �����
			    if ( buffer[8] == 0xC1 ) {           // ������������� ����� startCommunication
				if (ekran == 1) {                // ���� ��������� ��� ���� � ������ ������, ��
				    mode = ReadData;             // ��������� � ����� ������ ������ 
				    first_draw = 1;
				} else {                         // ���� ��������� ��� ���� � ������ ����������� �������, ��
				    mode = ReadData;
				    draw_rashod = 1;             // ��������� � ����� ����������� �������
				    fill_rashod = 1;
				    first_draw = 1;
				}
			    }
			}
			counter = 0;                             // ���������� counter ����� ����� ������� ��� ������� if (counter > 11)
		    }
		}
		break;

	    case ReadData:                                 // ���� ����� ������ ������
		if (first_draw == 1) {                       // ���� ������� � ���� �����, �� ������ ��� ������
		    draw_rect( 0, 0, 87, 32, svetlii_color);        // ������ ���, � ��� ��������� ������ ������������� 
		    draw_rect( 88, 0, 175, 32, temnii_color);       // ������ ������
		    bgcolor = temnii_color;
		    put_string( -3, 0, "Temp", text_tablo, 1);
		    bgcolor = svetlii_color;
		    put_string( 98, 0, "Throttle", text_tablo, 1);

		    draw_rect( 0, 33, 87, 65, temnii_color);
		    draw_rect( 88, 33, 175, 65, svetlii_color);
		    bgcolor = svetlii_color;
		    put_string( 12, 33, "RPM", text_tablo, 1);
		    bgcolor = temnii_color;
		    put_string( 98, 33, "Speed", text_tablo, 1);

		    draw_rect( 0, 66, 87, 98, svetlii_color);
		    draw_rect( 88, 66, 175, 98, temnii_color);
		    bgcolor = temnii_color;
		    put_string( 1, 66, "Power", text_tablo, 1);
		    bgcolor = svetlii_color;
		    put_string( 106, 66, "Injection", text_tablo, 1);

		    draw_rect( 0, 99, 175, 131, 0x0000);
		    bgcolor = 0x0000;
		    put_string( 17, 99,"Consumption", 0xFFFF, 1);

		    ekran = 1;                        // ���������� � eeprom ��� �� � ���� ������, ����� ��� ��������� ��������� �� � ���� � ���������
		    first_draw = 0;
		};
		if (StartSend == 1) {                          // ���� ������ 250 ��, �� ���� ���� ���������� �������� ������� ���
		    SendCommand(readDataByLocalIdentifier_RLI_ASS,6);
		    StartSend = 0;
		} else  {
		    if (counter>10) {                            //  ���� ������ ��������� �����
			unsigned char i = 0;
			unsigned int crc = 0;
			unsigned char drossel = 0;
			unsigned char otvet_length = 0;

			otvet_length = buffer[9];                  // ����� ������ ������ (4-� ���� � �������)
			if (counter > (otvet_length+10)) {         // ���� ���� �������� ��� �������
			    for (i=6;i<(otvet_length+10);i++) crc = crc + buffer[i];                 // ������� ����������� �����
			    i =  crc & 0xFF;                         // ����� 2 ������� ������� 
			    if ( buffer[(otvet_length+10)] == i ) {  // ��������� ����������� ����� 
				if (!draw_rashod) {
				    // ���� ���������� ����� ������
				    bgcolor = temnii_color;
				    i = buffer[20] - 0x28;                 // ��������� ����������� ���������
				    if (i & 0x80) {
					// ���� ��� ���� ����
					i = 256 - i;
					sprintf(convert,"-%d�",i);
				    } else sprintf(convert,"%d�",i);		// ���� ���� ����

				    // ������� ������ ������ �� ��������� � ������ ���� �� ���� ��������� ��� �������
				    if (strlen(convert) <= 3) put_string(25,11,strcat(convert, " "),text_parametr,2);
				    else put_string(25,11,convert,text_parametr,2);

				    drossel = buffer[22];                  // ��������� ����������� ��������
				    sprintf(convert,"%d%",drossel);
				    bgcolor = svetlii_color;
				    if (strlen(convert) < 3) put_string(117,11,strcat(convert, " "),text_parametr,2);
				    else put_string(117,11,convert,text_parametr,2);

				    bgcolor = svetlii_color;
				    if (drossel != 0) {                     // ������� ���� �� �������� ���
					sprintf(convert,"%d",((buffer[23])*40));
					if (strlen(convert) < 4) put_string(20,44,strcat(convert, " "),text_parametr,2);
					else put_string(20,44,convert,text_parametr,2);
				    } else {
					// ������� �� �������� ����
					sprintf(convert,"%d",((buffer[24])*10));
					if (strlen(convert) < 4) put_string(20,44,strcat(convert, " "),text_parametr,2);
					else put_string(20,44,convert,text_parametr,2);
				    }

				    bgcolor = temnii_color;
				    sprintf(convert,"%d",buffer[29]);     // ��������
				    if (strlen(convert) < 3) {
					put_string(125,44,strcat(convert, " "),text_parametr,2);
				    } else {
					put_string(125,44,convert,text_parametr,2);
				    }

				    // ���������� �������� sprintf => float � ���������� �������
				    bgcolor = temnii_color;
				    sprintf(convert,"%.1f",(5.2 + (float) buffer[30]/20));     // ���������� 
				    if (strlen(convert) < 4)  {
					put_string(16,77,strcat(convert, " "),text_parametr,2);
				    } else {
					put_string(16,77,convert,text_parametr,2);
				    }

				    bgcolor = svetlii_color;
				    sprintf(convert,"%.1f",(float) ((buffer[35] << 8) + buffer[34])/125 );     // ������������ �������  
				    if (strlen(convert) < 4)  {
					put_string(114,77,strcat(convert, "  "),text_parametr,2);
				    } else {
					put_string(114,77,convert,text_parametr,2);
				    }

				    bgcolor = 0x0000;
				    rashod = rashod + abs((float) ((buffer[43] << 8) + buffer[42])/128 );
				    if (++rashod_middle > 5) {
					rashod = rashod/6;
					if (rashod < 20) {
					    sprintf(convert,"%.1f",rashod);     // ������� ������ ������� 
					    if (strlen(convert) < 4)  {
						put_string(70,110,strcat(convert, " "),text_parametr,2);
					    } else {
						put_string(70,110,convert,text_parametr,2);
					    }
					} else {
					    put_string(70,110,"19.9",text_parametr,2);
					}
					rashod_middle = 0;
					rashod = 0;
				    }
				} else {
				    // ���� � ������ ����������� ������ �������
				    if (fill_rashod) {
					fill_screen(svetlii_color);
					bgcolor = svetlii_color;
					put_string(90,103,"L/100",0x07E0,2);
					fill_rashod = 0;
					ekran = 0;                           // ���������� � eeprom ��� �� � ������ ����������� ������ �������
				    }
				    bgcolor = svetlii_color;
				    rashod = rashod + abs((float) ((buffer[43] << 8) + buffer[42])/128 );
				    if (++rashod_middle > 5) {
					rashod = rashod/6;
					if (rashod < 20) {
					    sprintf(convert,"%.1f",rashod);     // ������� ������ ������� 
					    if (strlen(convert) < 4) {
						put_string(50,42,strcat(convert, " "),text_parametr,4);
					    } else {
						put_string(50,42,convert,text_parametr,4);
					    }
					} else {
					    put_string(50,42,"19.9",text_parametr,4);
					}
					rashod_middle = 0;
					rashod = 0;
				    }
				}
			    }
			    counter = 0;                               // ���������� counter ����� ����� ������� ��� ������� if (counter > (otvet_length+10))
			}
		    }
		}

		if (push && !press) {                       // ���� ������ ��������
		    if (!draw_rashod) {
			if (y > 90) {                           // � ������� � ������
			    draw_rashod = 1;                      // �� ������������� �� ����������� ������ ���
			    fill_rashod = 1;
			} else {
			    first_draw = 1;
			    mode = Menu;
			    draw_rashod = 0;
			}
		    } else {
			first_draw = 1;
			mode = ReadData;
			draw_rashod = 0;
		    }
		    rashod = 0;
		    rashod_middle = 0;
		}
		break;
	    case ReadADC:                                  // ���� ����� ������ ���
		if (first_draw == 1) {
		    fill_screen(bgcolor);
		    put_string(63,5,"MAF",text_tablo,1);
		    put_string(20,68,"Oxygen",text_tablo,1);
		    first_draw = 0;
		}
		if (StartSend == 1) {
		    SendCommand(readDataByLocalIdentifier_RLI_FT,6);
		    StartSend = 0;
		} else  {
		    if (counter>23) {                          //  6 ���� ������ + 18 ���� ����� (4-� ���� � ������ - ����� ������ � �������)
			unsigned char i = 0;                     // ���� ������������� ����� ��� ����, � ������ ���� ������� �� 4-�� ����� ��� � � �������
			unsigned int crc = 0;
			for (i=6;i<23;i++) {
			    crc = crc + buffer[i];                 // ������� ����������� �����
			}
			i =  crc & 0xFF;                         // ����� 2 ������� ������� 
			if ( buffer[23] == i ) {                 // ��������� ����������� �����  
			    sprintf(convert,"%.4f V", (float) (buffer[14]*5)/256);     // ���������� ����
			    put_string(30,30,convert,text_parametr,2);
			    sprintf(convert,"%.4f V", (float) (buffer[16]*5)/256);     // ���������� �� ������� ���������
			    put_string(30,90,convert,text_parametr,2);
			}
		    }
		}
		if (push && !press) {                        // ���� ������ � ����� ����� ������, �� ������� � ����
		    first_draw = 1;
		    mode = Menu;
		}
		break;

	    case ReadError:                                // ���� ����� ������ ������ 
		if (first_draw == 1) {
		    fill_screen(svetlii_color);
		    draw_rect(53,105,123,127,0xF800);
		    bgcolor = svetlii_color;
		    put_string(5,5,"Errors cnt:",text_tablo,1);
		    bgcolor = 0xF800;
		    put_string(65,112,"Reset",text_tablo,1);
		    first_draw = 0;
		}
		if (StartSend == 1) {
		    SendCommand(readDTCByStatus,8);
		    StartSend = 0;
		} else  {
		    // !!!!!!!!!!! ����� ����� ���� ������������ ������ !!!!!!!!!!!!!!
		    if (counter>12) {                          // ������  ������ + �����
			unsigned char cislo_oshibok = 0;
			unsigned char otvet_length = 0;
			cislo_oshibok = buffer[12];              // ������� ����� ������
			otvet_length = 13+(3*cislo_oshibok);     // ������ ������
			if (counter > otvet_length) {            // ���� ���� �������� ���� �����
			    unsigned char i = 0;
			    unsigned int crc = 0;

			    for (i=8;i<otvet_length;i++) crc = crc + buffer[i];

			    i =  crc & 0xFF;                       // ����� 2 ������� ������� 
			    if ( buffer[otvet_length] == i ) {     // ��������� ����������� �����
				bgcolor = svetlii_color;
				sprintf(convert,"%d",cislo_oshibok);
				put_string(150,5,convert,text_parametr,1);
				if (cislo_oshibok != 0) {
				    for (i=0;i < cislo_oshibok;i++) {  // �� ��� ������� ������� �� ����� �������� 15 ������ (3 �������� �� 5 ������)
					sprintf(convert,"P%02x%02x",buffer[(13+(i*3))],buffer[(14+(i*3))]);
					if (i < 5) {
					    put_string(5,25+(i*15),convert,0xAFE0,1);
					} else
					    if (i>=5 && i<10) {
						put_string(65,25+((i-5)*15),convert,0xAFE0,1);
					    } else
						if (i>=10 && i<15) put_string(125,25+((i-10)*15),convert,0xAFE0,1);
				    }
				}
			    }
			    counter = 0;                             // ���������� counter ����� ����� ������� ��� ������� if (counter>12)
			}
		    }
		}
		if (push && !press) {
		    if ((x > 61) && (x < 115) && (y > 108) && (y < 124)) {  // ���� ������ �� ������ "�����"
			draw_rect(53,105,123,127,0x07E0);            // ������ ���� ������ 
			bgcolor = 0x07E0;
			put_string(65,112,"Reset",text_tablo,1);
			SendCommand(clearDiagnosticInformation,7);   // ������� ������
			_delay_ms(200);                               // ��� ��� ����� �� �������� ������ (�� �����, �� ��� ���������� ��� ����� �����������)
			first_draw = 1;                              // �������������� �����
		    } else {
			first_draw = 1;
			mode = Menu;
		    }
		}
		break;
		
	    case SpeedSample:                              // ���� ����� ������ �������� 0-100 ��/�
		if (first_draw == 1) {
		    fill_screen(bgcolor);
		    put_string(15,5,"Acceleration 0-100 km/h",text_tablo,1);
		    put_string(65,25,"0.0",0xAFE0,2);
		    put_string(50,57,"Speed",text_tablo,1); 

		    draw_rect(53,105,123,127,0xF800);
		    bgcolor = 0xF800;
		    put_string(65,112,"Reset",text_tablo,1);

		    first_draw = 0;
		}
		if (StartSend == 1) {
		    SendCommand(readDataByLocalIdentifier_RLI_ASS,6);
		    StartSend = 0;
		} else {
		    if (counter>10) {                          //  ���� ������ ��������� �����
			unsigned char i = 0;
			unsigned int crc = 0;
			unsigned char speed = 0;
			unsigned char otvet_length = 0;
			otvet_length = buffer[9];                // ����� ������ ������ (4-� ���� � �������)
			if (counter > (otvet_length+10)) {       // ���� ���� �������� ��� �������
			    for (i=6;i<(otvet_length+10);i++) crc = crc + buffer[i];                 // ������� ����������� �����
			    i =  crc & 0xFF;                         // ����� 2 ������� ������� 
			    if ( buffer[(otvet_length+10)] == i ) {  // ��������� ����������� ����� 
				speed = buffer[29];
				if (speed == 0) go = 1;              // ���� ������ ����������� ��������� ����� ��������
				if (speed > 0 && go == 1) {            // ������ �����������
				    if (zamer_finish) {
					TCCR1B=0x00;
				    } else {
					TCCR1B=0x04;                       // ��������� ������ �������  
					if (speed >= 100) {                // ����� ������� �� 100 ��/�
					    zamer_finish = 1;
					    TCCR1B=0x00;
					}
				    }
				}
				sprintf(convert,"%.1f",((float) (dsec * 0.1)));  // ������� �����
				bgcolor = svetlii_color;
				put_string(65,25,convert,0xAFE0,2);
				sprintf(convert,"%d",speed);                     // ���������� ������� ��������
				if (strlen(convert) < 3) put_string(67,75,strcat(convert, " "),0xAFE0,2);
				else put_string(67,75,convert,0xAFE0,2);
			    }
			    counter = 0;                               // ���������� counter ����� ����� ������� ��� ������� if (counter > (otvet_length+10))
			}
		    }
		}
		if (push && !press) {
		    if ((x > 61) && (x < 115) && (y > 108) && (y < 124)) {  // ���� ������ �� ������ "�����"
			draw_rect(53,105,123,127,0x07E0);            // ������ ���� ������ 
			bgcolor = 0x07E0;
			put_string(65,112,"Reset",text_tablo,1);

			TCCR1B=0x00;
			dsec = 0;                                    // ���������� ����� ������� 
			go = 0;
			zamer_finish = 0;
			bgcolor = svetlii_color;
			first_draw = 1;                              // �������������� �����
		    } else {
			TCCR1B=0x00;
			dsec = 0;                                   // ���������� ����� ������� 
			go = 0;
			zamer_finish = 0;
			first_draw = 1;
			mode = Menu;
		    }
		}
		break;

	    case Menu:
		if (first_draw == 1) {
		    fill_screen(0xFFE0);
		    bgcolor = svetlii_color;
		    draw_rect(8,19,83,61,0x0000);
		    draw_rect(9,20,82,60,svetlii_color);
		    put_string(103,36,"Errors",text_tablo,1);
		    draw_rect(93,19,168,61,0x0000);
		    draw_rect(94,20,167,60,svetlii_color);
		    put_string(6,36,"Params",text_tablo,1);
		    draw_rect(8,71,83,113,0x0000);
		    draw_rect(9,72,82,112,svetlii_color);
		    put_string(107,80,"0-100",text_tablo,1);
		    put_string(111,96,"km/h",text_tablo,1);
		    draw_rect(93,71,168,113,0x0000);
		    draw_rect(94,72,167,112,svetlii_color);
		    put_string(31,87,"ADC",text_tablo,1);
		    first_draw = 0;
		}

		if (StartSend == 1) {
		    SendCommand(testerPresent,6);
		    StartSend = 0;
		}

		if (push && !press) {
		    if ((x > 10) && (x < 80) && (y > 21) && (y < 59) ) { first_draw = 1; mode = ReadData; }
		    if ((x > 84) && (x < 165) && (y > 21) && (y < 59) ) { first_draw = 1; mode = ReadError; }
		    if ((x > 10) && (x < 80) && (y > 73) && (y < 111) ) { first_draw = 1; mode = ReadADC; }
		    if ((x > 84) && (x < 165) && (y > 73) && (y < 111) ) { first_draw = 1; mode = SpeedSample; }
		}
		break;

	    default : 
		break;
	}
    }
}

// ��� ����������
/*
  lcd_circle(30,30,6,0xFC00); 
  lcd_circle(30,102,6,0xFC00);
  lcd_circle(146,102,6,0xFC00);
  lcd_circle(146,30,6,0xFC00);
  put_string(23,8,"1",text_tablo,1);
  put_string(23,115,"2",text_tablo,1);
  put_string(139,115,"3",text_tablo,1);
  put_string(139,8,"4",text_tablo,1); 

  sprintf(convert,"%d ",x);
  put_string(74,53,convert,text_tablo,1);
  sprintf(convert,"%d ",y);
  put_string(74,73,convert,text_tablo,1);
*/
