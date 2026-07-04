#include <lvgl.h>
#include <Wire.h>
#include <Arduino.h>
#include "pin_config.h"
#include "XPowersLib.h"
#include "lv_conf.h"
#include "Arduino_GFX_Library.h"

XPowersPMU power;

bool pmu_flag = false;
bool adc_switch = false;
lv_obj_t *info_label;
bool backlight_on = true;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LCD_WIDTH * LCD_HEIGHT / 10];

#define EXAMPLE_LVGL_TICK_PERIOD_MS 2

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS /* CS */, LCD_SCLK /* SCK */, LCD_SDIO0 /* SDIO0 */, LCD_SDIO1 /* SDIO1 */,
  LCD_SDIO2 /* SDIO2 */, LCD_SDIO3 /* SDIO3 */);

Arduino_CO5300 *gfx = new Arduino_CO5300(
  bus, LCD_RESET /* RST */, 0 /* rotation */, LCD_WIDTH /* width */, LCD_HEIGHT /* height */, 0, 0, 0, 0);


void setFlag(void) {
  pmu_flag = true;
}


void adcOn() {
  power.enableTemperatureMeasure();
  // Enable internal ADC detection
  power.enableBattDetection();
  power.enableVbusVoltageMeasure();
  power.enableBattVoltageMeasure();
  power.enableSystemVoltageMeasure();
}

void adcOff() {
  power.disableTemperatureMeasure();
  // Enable internal ADC detection
  power.disableBattDetection();
  power.disableVbusVoltageMeasure();
  power.disableBattVoltageMeasure();
  power.disableSystemVoltageMeasure();
}

#if LV_USE_LOG != 0
/* Serial debugging */
void my_print(const char *buf) {
  Serial.printf(buf);
  Serial.flush();
}
#endif

void example_lvgl_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area) {
  if (area->x1 % 2 != 0) area->x1--;
  if (area->y1 % 2 != 0) area->y1--;
  // 变为奇数(如果是偶数就加 1)
  if (area->x2 % 2 == 0) area->x2++;
  if (area->y2 % 2 == 0) area->y2++;
}


/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif

  lv_disp_flush_ready(disp);
}

void example_increase_lvgl_tick(void *arg) {
  /* Tell LVGL how many milliseconds has elapsed */
  lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static uint8_t count = 0;
void example_increase_reboot(void *arg) {
  count++;
  if (count == 30) {
    esp_restart();
  }
}

void setup() {
  Serial.begin(115200); /* prepare for possible serial debug */

  delay(1000);

  Wire.begin(IIC_SDA, IIC_SCL);

  bool result = power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);

  if (result == false) {
    Serial.println("PMU is not online...");
    while (1) delay(50);
  }

  setFlag();

  power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  power.setChargeTargetVoltage(3);
  // Clear all interrupt flags
  power.clearIrqStatus();
  // Enable the required interrupt function
  power.enableIRQ(
    XPOWERS_AXP2101_PKEY_SHORT_IRQ  //POWER KEY
  );

  adcOn();

  gfx->begin();
  bus->writeC8D8(0x36, 0xA0);

  String LVGL_Arduino = "Hello Arduino! ";
  LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();

  Serial.println(LVGL_Arduino);
  Serial.println("I am LVGL_Arduino");

  lv_init();

#if LV_USE_LOG != 0
  lv_log_register_print_cb(my_print); /* register print function for debugging */
#endif


  lv_disp_draw_buf_init(&draw_buf, buf, NULL, LCD_WIDTH * LCD_HEIGHT / 10);

  /*Initialize the display*/
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  /*Change the following line to your display resolution*/
  disp_drv.hor_res = LCD_WIDTH;
  disp_drv.ver_res = LCD_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.rounder_cb = example_lvgl_rounder_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  /*Initialize the (dummy) input device driver*/
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  lv_indev_drv_register(&indev_drv);

  const esp_timer_create_args_t lvgl_tick_timer_args = {
    .callback = &example_increase_lvgl_tick,
    .name = "lvgl_tick"
  };

  const esp_timer_create_args_t reboot_timer_args = {
    .callback = &example_increase_reboot,
    .name = "reboot"
  };

  esp_timer_handle_t lvgl_tick_timer = NULL;
  esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);

  info_label = lv_label_create(lv_scr_act());
  lv_label_set_text(info_label, "Initializing...");
  lv_obj_align(info_label, LV_ALIGN_CENTER, 0, 0);

  Serial.println("Setup done");
  pinMode(0, INPUT);
}

void loop() {
  lv_timer_handler(); /* let the GUI do its work */
  delay(5);

  if (pmu_flag) {
    pmu_flag = false;
    // Get PMU Interrupt Status Register
    uint32_t status = power.getIrqStatus();
    if (power.isPekeyShortPressIrq()) {
      if (adc_switch) {
        adcOn();
        Serial.println("Enable ADC\n\n\n");
      } else {
        adcOff();
        Serial.println("Disable ADC\n\n\n");
      }
      adc_switch = !adc_switch;
    }
    power.clearIrqStatus();
  }

  String info = "";

  uint8_t charge_status = power.getChargerStatus();

  info += "power Temperature: " + String(power.getTemperature()) + "*C\n";
  info += "isCharging: " + String(power.isCharging() ? "YES" : "NO") + "\n";
  info += "isDischarge: " + String(power.isDischarge() ? "YES" : "NO") + "\n";
  info += "isStandby: " + String(power.isStandby() ? "YES" : "NO") + "\n";
  info += "isVbusIn: " + String(power.isVbusIn() ? "YES" : "NO") + "\n";
  info += "isVbusGood: " + String(power.isVbusGood() ? "YES" : "NO") + "\n";

  switch (charge_status) {
    case XPOWERS_AXP2101_CHG_TRI_STATE:
      info += "Charger Status: tri_charge\n";
      break;
    case XPOWERS_AXP2101_CHG_PRE_STATE:
      info += "Charger Status: pre_charge\n";
      break;
    case XPOWERS_AXP2101_CHG_CC_STATE:
      info += "Charger Status: constant charge\n";
      break;
    case XPOWERS_AXP2101_CHG_CV_STATE:
      info += "Charger Status: constant voltage\n";
      break;
    case XPOWERS_AXP2101_CHG_DONE_STATE:
      info += "Charger Status: charge done\n";
      break;
    case XPOWERS_AXP2101_CHG_STOP_STATE:
      info += "Charger Status: not charging\n";
      break;
  }

  info += "Battery Voltage: " + String(power.getBattVoltage()) + "mV\n";
  info += "Vbus Voltage: " + String(power.getVbusVoltage()) + "mV\n";
  info += "System Voltage: " + String(power.getSystemVoltage()) + "mV\n";

  if (power.isBatteryConnect()) {
    info += "Battery Percent: " + String(power.getBatteryPercent()) + "%\n";
  }

  lv_label_set_text(info_label, info.c_str());
  lv_obj_set_style_text_font(info_label, &lv_font_montserrat_20, LV_PART_MAIN);
  delay(20);
}