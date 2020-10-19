#include <SdsDustSensor.h>
#include <LiquidCrystal.h>

LiquidCrystal lcd(7, 8, 9, 10, 11, 12);
SdsDustSensor sds(Serial);

#define LCD_ROWS 2
#define LCD_COLUMNS 16

void
setup()
{
	// Set up the LCD.
	lcd.begin(LCD_COLUMNS, LCD_ROWS);
	lcd.print("starting...");
	
	// Set up the SDS011 sensor.
	sds.begin();
}

int last;

void
loop()
{
	char buf[LCD_COLUMNS + 1];
	long timestamp = millis();
	
	PmResult pm = sds.readPm();
	if (!pm.isOk()) {
		delay(100);
		return;
	}

	lcd.setCursor(0, 0);
	(void) snprintf(buf, sizeof (buf), "PM2.5 = %s", String(pm.pm25, 2).c_str());
	lcd.print(buf);
	lcd.setCursor(0, 1);
	(void) snprintf(buf, sizeof (buf), "aqi = %3d", aqi_pm25(pm.pm25));
	lcd.print(buf);
	last = timestamp;
	delay(1000);
}

// Port of cbiffle's sds011-reader from Rust.
struct aqitbl_ent {
	float ae_pm25_max;
	float ae_aqi_min;
	float ae_aqi_max;
} PM25_AQI[] = {
	{ 12, 0, 50 },
	{ 35.4, 51, 100 },
	{ 55.4, 101, 150 },
	{ 150.4, 151, 200 },
	{ 250.4, 201, 300 },
	{ 500.4, 301, 500 },
};

int aqi_pm25(float conc)
{
	int nentries = sizeof (PM25_AQI) / sizeof (PM25_AQI[0]);
	int i;
	float conc_min = 0;

	for (i = 0; i < nentries; i++) {
		float conc_max = PM25_AQI[i].ae_pm25_max;
		float aqi_min = PM25_AQI[i].ae_aqi_min;
		float aqi_max = PM25_AQI[i].ae_aqi_max;

		if (conc <= conc_max) {
			float numerator = aqi_max - aqi_min;
			float denominator = conc_max - conc_min;
			float aqi_in_bucket =
			    numerator / denominator * (conc - conc_min);
			return (aqi_in_bucket + aqi_min);
		}

		conc_min = conc_max + 0.1;
	}

	return (-1);
}
