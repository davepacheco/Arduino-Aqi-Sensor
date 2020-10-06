#include <SdsDustSensor.h>
#include <LiquidCrystal.h>

LiquidCrystal lcd(7, 8, 9, 10, 11, 12);
SdsDustSensor sds(Serial);

#define LCD_ROWS 2
#define LCD_COLUMNS 16

void setup() {
  // Set up the LCD.
  lcd.begin(LCD_COLUMNS, LCD_ROWS);
  lcd.print("starting...");

  // Set up the SDS011 sensor.
  sds.begin();
}

int last;

void loop() {
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
  (void) snprintf(buf, sizeof (buf), "dt = %4dms", timestamp - last);
  lcd.print(buf);
  last = timestamp;
  delay(1000);
}
