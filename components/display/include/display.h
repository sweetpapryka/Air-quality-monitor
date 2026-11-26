#pragma once


/*
 *	Initializes I2C the display task
 */
void display_init(void);

/*
 * Turns on the displays backlight
 */
void display_backlight_on(void);

/*
 * Turns off the displays backlight
 */
void display_backlight_off(void);

// PAGE HOME
/*
 *	Displays the home page of the device which shows IAQ, temperature, humidity, pms reading and time
 */
void display_draw_home(void);
