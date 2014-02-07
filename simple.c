#include "device.h"

#include <inttypes.h>

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdlib.h>

#include "init.h"
#include "s65_lhp88.h"
#include "menu.h"
#include "ecu.h"

#include "vars.h"

#define _Y_minus	2
#define _Y_plus		3
#define _X_minus	4
#define _X_plus		5

#define def_TSdiff	8

int x, y;
uint8_t push = 0;


// Read the AD conversion result
unsigned int read_adc(unsigned char adc_input) {
    ADMUX = adc_input | (ADC_VREF_TYPE & 0xff);
    _delay_us(10);                  // Delay needed for the stabilization of the ADC input voltage
    ADCSRA |= 0x40;                  // Start the AD conversion
    while ((ADCSRA & 0x10) == 0);    // Wait for the AD conversion to complete
    ADCSRA |= 0x10;
    return ADCW;
}

void TS_scan (void) {
    push = 0;
    DDRC =  0b00101000;            //  X_minus, X_plus �� �����, ������ � ������ �� �����
    PORTC = 0b11010111;            //  ��������� ������ ��� ����� � ���������
    _delay_ms(1);

    if (read_adc(_X_minus) < 100) {       //(6 ??? X_minus) ���� ���� ������� ( ��������� �� 0 ����� Y- )
	push = 1;

	PORTC = 0b11011111;        // c�������� X ����������, X_minus �� �����, X_plus �� +5 ����� 
	_delay_ms(1);
	x = abs( (int)(240 - 0.276 * read_adc(_Y_minus)) );

	// ���������� �������� ��� �� 2-� ������
	// ������������ ����������
	// � ����� ��������� ������ �� 2-� ������ ������� ������������� �������� ������� 
	// x = 232 - 0.276*ADC
	// y = 0.25*ADC - 67.5

	DDRC =  0b00010100;        // c�������� Y ����������, Y_minus, Y_plus �� �����  
	PORTC = 0b11101111;        //  Y_minus �� �����, Y_plus �� +5 �����
	_delay_ms(1);
	y = abs((int) (0.25 * read_adc(_Y_plus) - 73.5));
    }
}

int main(void) {
    uint8_t i;
    int16_t j;

    while(1) {
	is_connected = 0;
	need_reset = 0;
	ecu_full_oil = 0;
	port_init();
	s65_init();

	sub_main();

	while (need_reset == 0) {
	    TS_scan();
	    if(push == 1){
		for(i = 0; i < menu_size; i++ )
		    if( ((x > pmenu[i].x1) && (x < pmenu[i].x2)) && ((y > pmenu[i].y1) && (y < pmenu[i].y2))) {
			pmenu[i].sub_menu();
			break;
		    };
	    }
	    if(worker.update == 1){ worker.sub_worker(); worker.update = 0; }
	}

	asm volatile("cli");
	timer0_stop();
	timer1_stop();
	usart_deinit();
    }


    return(0);
}


