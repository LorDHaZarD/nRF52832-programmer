/*
 * nRF52832 SWD programmer
 *
 * Host : Adafruit Feather nRF52832 (Arduino board "Adafruit Feather nRF52832")
 * Target: Nordic nRF52832, connected by SWD (SWCLK, SWDIO, nRESET, GND, 3.3 V)
 *
 * Press the button: the HEX file named in config.h is read from the SD card,
 * the target is recovered (erased and unprotected), programmed in 512 byte
 * blocks (blank blocks skipped), the UICR values from config.h are written,
 * the busy LED turns off, and the flash is verified against the file.
 * PASS LED = success, FAIL LED = any error (details on the serial port).
 *
 * All pins, clocks, the file name and the UICR table live in config.h.
 */
#include <Arduino.h>
#include <stdarg.h>
#include "config.h"
#include "programmer.h"
#include "swd_port.h"

/* ------------------------------------------------------------------------ */
/* Services for the C modules                                               */
/* ------------------------------------------------------------------------ */

extern "C" uint32_t port_millis(void) { return millis(); }
extern "C" void port_delay_ms(uint32_t ms) { delay(ms); }

extern "C" void ui_busy_led(int on) { digitalWrite(LED_BUSY_PIN, on ? HIGH : LOW); }

#if DEBUG_SERIAL
extern "C" void log_printf(const char *fmt, ...)
{
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
}
#endif

/* ------------------------------------------------------------------------ */
/* Button                                                                   */
/* ------------------------------------------------------------------------ */

/* True once the button has read low for BUTTON_DEBOUNCE_MS without a break. */
static bool button_pressed(void)
{
  uint32_t t0 = millis();
  while (millis() - t0 < BUTTON_DEBOUNCE_MS) {
    if (digitalRead(BUTTON_PIN) != LOW) return false;
    delay(1);
  }
  return true;
}

static void wait_button_release(void)
{
  uint32_t t0 = millis();
  while (millis() - t0 < BUTTON_DEBOUNCE_MS) {
    if (digitalRead(BUTTON_PIN) == LOW) t0 = millis();
    delay(1);
  }
}

/* ------------------------------------------------------------------------ */
/* Arduino entry points                                                     */
/* ------------------------------------------------------------------------ */

void setup()
{
  pinMode(LED_BUSY_PIN, OUTPUT);
  pinMode(LED_PASS_PIN, OUTPUT);
  pinMode(LED_FAIL_PIN, OUTPUT);
  digitalWrite(LED_BUSY_PIN, LOW);
  digitalWrite(LED_PASS_PIN, LOW);
  digitalWrite(LED_FAIL_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  /* SWD lines and target reset stay high impedance while idle. */
  swd_port_release();
  target_reset_release();

#if DEBUG_SERIAL
  Serial.begin(DEBUG_BAUD);
  delay(100);
  Serial.println();
  Serial.println(F("nRF52832 SWD programmer ready"));
  Serial.print(F("HEX file: "));
  Serial.println(F(HEX_FILENAME));
  Serial.println(F("Press the button to program the target."));
#endif
}

void loop()
{
  /* Idle loop: wait for the push button (active low). */
  if (!button_pressed()) {
    delay(5);
    return;
  }

#if DEBUG_SERIAL
  Serial.println(F("\r\n--- Programming cycle started ---"));
#endif
  digitalWrite(LED_PASS_PIN, LOW);
  digitalWrite(LED_FAIL_PIN, LOW);
  digitalWrite(LED_BUSY_PIN, HIGH);

  prog_err_t err = prog_run();

  digitalWrite(LED_BUSY_PIN, LOW);
  if (err == PROG_OK) {
    digitalWrite(LED_PASS_PIN, HIGH);
  } else {
    digitalWrite(LED_FAIL_PIN, HIGH);
  }

  /* Do not start again while the button is still held. */
  wait_button_release();
}
