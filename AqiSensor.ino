/*
 * AqiSensor: Simple AQI sensor project that emits data to an LCD display.
 * See README.adoc for more information.
 */

#include <SdsDustSensor.h>
#include <LiquidCrystal.h>

/*
 * LCD configuration.
 */
#define LCD_ROWS 2
#define LCD_COLUMNS 16
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

/*
 * Use software serial to avoid interfering with the hardware serial lines that
 * are used to reprogram the board.  (This also lets us use the hardware serial
 * UART for debug logging, if we want to.)  We might want to revisit this when
 * making this a permanent circuit, as the software serial implementation
 * probably uses more power.
 */
SoftwareSerial swserial(4, 5);
SdsDustSensor sds(swserial);

static int aqi_pm25(float);
static float pm25_adjust_aqandu(float);

void
setup()
{
	/* Set up the LCD. */
	lcd.begin(LCD_COLUMNS, LCD_ROWS);
	lcd.print("starting...");

	/* Set up the SDS011 sensor. */
	sds.begin();

#ifdef DISPLAY_DEMO
	/*
	 * On startup, run through a test loop that exercises the logic around
	 * formatting different values.  Use "#define DISPLAY_DEMO" somewhere
	 * above to enable this.
	 */
	displayDemo();
#endif
}

#ifdef DISPLAY_DEMO
/*
 * Prints fake updates to the LCD display for all reasonable values of pm2.5 and
 * wall time.  This is useful for verifying that the display does the right
 * thing with various ranges of values.  See setup().
 */
static void
displayDemo(void)
{
	unsigned long timestamp = 0;
	for (float f = 0.0; f < 510.0; f += 2.5) {
		updateDisplay(timestamp, f);
		timestamp += 1000;
		delay(50);
	}
}
#endif

/*
 * Every second, we take a reading from the SDS011 and update the display.
 */
void
loop()
{
	unsigned long timestamp = millis();

	PmResult pm = sds.readPm();
	if (pm.isOk()) {
		updateDisplay(timestamp, pm.pm25);
	} else {
		lcd.setCursor(0, 0);
		if (timestamp < 3000) {
			lcd.print("Synchronizing...");
			lcd.setCursor(0, 1);
			lcd.print("No readings yet.");
		} else {
			lcd.print("No sensor found.");
		}
	}

	delay(1000);
}

/*
 * Updates the LCD display to reflect the current timestamp (time since boot in
 * milliseconds) and the current pm2.5 value.  The current display format
 * assumes 2 rows and 16 columns and looks like this:
 *
 *          +-------------- measured pm2.5 concentration
 *          |         +---- minutes and seconds since boot
 *          v         v
 *    +----------------+
 *    |pm25 1.70   1:23|
 *    |AQI   7 AQ&U  17|
 *    +----------------+
 *           ^        ^ 
 *           |        |
 *           |        + AQI number based on pm2.5 concentration adjusted per
 *           |          using the AQ&U formula.
 *           + raw AQI number based on measured pm2.5 concentration
 *
 */
void
updateDisplay(unsigned long timestamp, float pm_raw)
{
	char buf[LCD_COLUMNS + 1];
	int aqi_raw = aqi_pm25(pm_raw);
	int aqi_aqu = aqi_pm25(pm25_adjust_aqandu(pm_raw));

	/*
	 * The desired precision for printing pm2.5 depends on how big the value
	 * is.  We only ever want to use 4 characters here.
	 */
	String pmstr;
	if (pm_raw >= 100) {
		pmstr = String(pm_raw, 0);
	} else if (pm_raw >= 10) {
		pmstr = String(pm_raw, 1);
	} else {
		pmstr = String(pm_raw, 2);
	}

	lcd.setCursor(0, 0);
	(void) snprintf(buf, sizeof (buf),
	    "pm25 %-4s %3lu:%02lu",
	    pmstr.c_str(),
	    (timestamp / 1000) / 60,
	    (timestamp / 1000) % 60);
	lcd.print(buf);

	lcd.setCursor(0, 1);
	(void) snprintf(buf, sizeof (buf), "AQI %3d AQ&U %3d",
	    aqi_raw, aqi_aqu);
	lcd.print(buf);
}

/*
 * Table describing the AQI curve as a function of pm2.5 concentration.
 *
 * You can find the parameters for this table in "Table 2" on page 3181 of the
 * Federal Register, Volume 78 No. 10 from Tuesday, January 15, 2013, available
 * from https://www.govinfo.gov/content/pkg/FR-2013-01-15/pdf/2012-30946.pdf.
 * This is referenced here:
 * https://www.epa.gov/pm-pollution/table-historical-particulate-matter-pm-national-ambient-air-quality-standards-naaqs
 *
 * Other useful references include TI Designs publication "PM2.5/PM10 Particle
 * Sensor Analog Front-End for Air Quality Monitoring Design", available here:
 * https://www.ti.com/lit/ug/tidub65c/tidub65c.pdf
 *
 * and cbiffle's sds011-reader in Rust.
 *
 * Each "struct aqitbl_ent" represents a bucket, and every reasonable pm2.5
 * concentration falls into one of these buckets.  PM25_AQI is an array of
 * disjoint buckets sorted by the maximum pm2.5 concentration for each bucket.
 *
 * For example, the first bucket (12, 0, 50) specifies that for pm2.5 values
 * below 12, the AQI is linear between 0 and 50.  The second bucket (35.4, 51,
 * 100) specifies that for pm2.5 values between 12 (the upper bound of the
 * previous bucket) and 35.4, the AQI is linear between 51 and 100.  And so on.
 */
struct aqitbl_ent {
	float ae_pm25_max;    /* maximum pm2.5 concentration for this bucket */
	float ae_aqi_min;     /* minimum AQI value for this bucket */
	float ae_aqi_max;     /* maximum AQI value for this bucket */
} PM25_AQI[] = {
	{ 12, 0, 50 },  	/* "Good" */
	{ 35.4, 51, 100 }, 	/* "Moderate" */
	{ 55.4, 101, 150 }, 	/* "Unhealthy for sensitive groups" */
	{ 150.4, 151, 200 }, 	/* "Unhealthy" */
	{ 250.4, 201, 300 }, 	/* "Very unhealthy */
	{ 350.4, 301, 400 }, 	/* "Hazardous */
	{ 500.4, 401, 500 }, 	/* "Hazardous */
};

/*
 * Given a measured or adjusted pm2.5 concentration (measured in micrograms /
 * meter ^ 3), compute the corresponding AQI.  This walks the PM25_AQI table as
 * described in the comment above.
 *
 * Returns -1 for values off the scale.
 */
static int
aqi_pm25(float conc)
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
			return (round(aqi_in_bucket + aqi_min));
		}

		conc_min = conc_max + 0.1;
	}

	return (-1);
}

/*
 * Given a measured pm2.5 concentration, compute the adjusted concentration
 * using the AQandU adjustment.  This formula comes from the tooltip on the
 * PurpleAir web site, but you can find more on the adjustment here:
 * https://www.aqandu.org/airu_sensor#calibrationSection
 */
static float
pm25_adjust_aqandu(float conc)
{
	return (conc * 0.778 + 2.65);
}
