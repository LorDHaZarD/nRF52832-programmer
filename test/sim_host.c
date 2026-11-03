/*
 * sim_host.c - PC replacements for the Feather services: time, log, LEDs and
 * the HEX file source (a normal file instead of the SD card).
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include "hexsrc.h"
#include "swd_port.h"
#include "sim_host.h"
#include "sim_target.h"

unsigned long long sim_clocks_at_busy_off;

const char *sim_hex_path;
int sim_card_present = 1;
int sim_busy_led = 0;
int sim_busy_led_offs = 0;
int sim_quiet = 0;
static uint32_t now_ms;
static FILE *fp;
static uint32_t fsize;

uint32_t port_millis(void) { return now_ms++; }
void port_delay_ms(uint32_t ms) { now_ms += ms; }

void ui_busy_led(int on)
{
  if (sim_busy_led && !on) { sim_busy_led_offs++; sim_clocks_at_busy_off = sim.clocks; }
  sim_busy_led = on;
}

void log_printf(const char *fmt, ...)
{
  va_list ap;
  if (sim_quiet) return;
  printf("    | ");
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

int hexsrc_open(void)
{
  if (fp) { fclose(fp); fp = 0; }
  if (!sim_card_present) return HEXSRC_ERR_CARD;
  if (!sim_hex_path) return HEXSRC_ERR_FILE;
  fp = fopen(sim_hex_path, "rb");
  if (!fp) return HEXSRC_ERR_FILE;
  fseek(fp, 0, SEEK_END);
  fsize = (uint32_t)ftell(fp);
  fseek(fp, 0, SEEK_SET);
  return HEXSRC_OK;
}

int hexsrc_read(uint8_t *buf, uint16_t n, uint16_t *br)
{
  if (!fp) return HEXSRC_ERR_READ;
  *br = (uint16_t)fread(buf, 1, n, fp);
  return HEXSRC_OK;
}

uint32_t hexsrc_size(void) { return fsize; }
