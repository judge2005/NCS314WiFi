#ifndef _WIFI_H
#define _WIFI_H
enum struct I2CCommands {
	time = 1,
	time_or_date,
	date_format,
	time_format,	//false = 24 hour
	leading_zero,
	display_on,
	display_off,

	backlight,
	hue_cycling,
	cycle_time,
	hue,
	saturation,
	brightness,

	alarm_set,
	alarm_time
};
#endif
