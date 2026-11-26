#pragma once


/*
 *	Initializes I2C the display task
 */
void display_init(void);

// PAGE HOME
/*
 *	Displays the home page of the device which shows IAQ, temperature, humidity, pms reading and time
 */
void display_draw_home(void);
