#include "contiki.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include <stdio.h> /* For printf() */
#include <stdbool.h>
#define SECONDS 2

/*---------------------------------------------------------------------------*/
PROCESS(hello_world_process, "Hello world process");
AUTOSTART_PROCESSES(&hello_world_process);
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(hello_world_process, ev, data)
{
	PROCESS_BEGIN();
	SENSORS_ACTIVATE(button_sensor);
	static struct etimer et;
	static bool showGreen = false;
	static bool showRed = true;

	etimer_set(&et,CLOCK_SECOND*SECONDS);

   	while(1){
		PROCESS_WAIT_EVENT();
		//PROCESS_WAIT_EVENT_UNTIL((ev==sensors_event && data == &button_sensor) || etimer_expired(&et));

		unsigned long millis = clock_time()*1000UL/CLOCK_SECOND;
		unsigned long seconds = millis/1000UL;
		millis-=seconds*1000UL;

		if(ev==sensors_event && data==&button_sensor){
			showRed = !showRed;
			showGreen = true;
			if (showRed){
				leds_off(LEDS_ALL);
				leds_on(LEDS_RED);
			} else {
				leds_off(LEDS_ALL);
				leds_on(LEDS_BLUE);
			}
			printf("[%6lu.%3.3lu] button clicked: showGreen=%-5s showRed=%-5s\n", seconds, millis, showGreen?"true":"false", showRed?"true":"false");
		}
		
		if(etimer_expired(&et)){
			if(showGreen){
				leds_off(LEDS_ALL);
				leds_on(LEDS_GREEN);
			} else {
				if (showRed){
					leds_off(LEDS_ALL);
					leds_on(LEDS_RED);
				} else {
					leds_off(LEDS_ALL);
					leds_on(LEDS_BLUE);
				}
			}
			showGreen= !showGreen;
			printf("[%6lu.%3.3lu] timer expired:  showGreen=%-5s showRed=%-5s\n", seconds, millis, showGreen?"true":"false", showRed?"true":"false");
		}
		etimer_stop(&et);
		etimer_restart(&et);
	}
	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
