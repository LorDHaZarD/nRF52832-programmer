/*
 * ihex.c - streaming Intel HEX parser. See ihex.h.
 */
#include "ihex.h"

#define ST_IDLE    0   /* between records, waiting for ':' */
#define ST_RECORD  1   /* collecting hex digits */

void ihex_init(ihex_t *p, ihex_data_cb cb, void *ctx)
{
  p->cb = cb;
  p->ctx = ctx;
  p->base = 0;
  p->line = 1;
  p->state = ST_IDLE;
  p->done = 0;
  p->cb_result = 0;
  p->nibbles = 0;
  p->hi = 0;
}

static int hexval(uint8_t c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static int end_record(ihex_t *p)
{
  uint16_t count = p->nibbles / 2;
  uint8_t sum = 0, len, type;
  uint16_t i;

  p->state = ST_IDLE;
  if ((p->nibbles & 1) || count < 5) return IHEX_ERR_SYNTAX;
  len = p->rec[0];
  if (count != (uint16_t)len + 5) return IHEX_ERR_SYNTAX;
  for (i = 0; i < count; i++) sum = (uint8_t)(sum + p->rec[i]);
  if (sum != 0) return IHEX_ERR_CHECKSUM;

  type = p->rec[3];
  switch (type) {
    case 0x00: {                                   /* data */
      uint32_t addr = p->base + (((uint32_t)p->rec[1] << 8) | p->rec[2]);
      if (len == 0) return IHEX_OK;
      p->cb_result = p->cb(p->ctx, addr, &p->rec[4], len);
      return p->cb_result ? IHEX_ERR_CALLBACK : IHEX_OK;
    }
    case 0x01:                                     /* end of file */
      p->done = 1;
      return IHEX_DONE;
    case 0x02:                                     /* extended segment address */
      if (len != 2) return IHEX_ERR_SYNTAX;
      p->base = (((uint32_t)p->rec[4] << 8) | p->rec[5]) << 4;
      return IHEX_OK;
    case 0x04:                                     /* extended linear address */
      if (len != 2) return IHEX_ERR_SYNTAX;
      p->base = (((uint32_t)p->rec[4] << 8) | p->rec[5]) << 16;
      return IHEX_OK;
    case 0x03:                                     /* start segment address */
    case 0x05:                                     /* start linear address */
      return IHEX_OK;
    default:
      return IHEX_ERR_TYPE;
  }
}

int ihex_feed(ihex_t *p, const uint8_t *buf, size_t n)
{
  size_t i;
  if (p->done) return IHEX_DONE;

  for (i = 0; i < n; i++) {
    uint8_t c = buf[i];

    if (p->state == ST_IDLE) {
      if (c == ':') {
        p->state = ST_RECORD;
        p->nibbles = 0;
      } else if (c == '\n') {
        p->line++;
      } else if (c != '\r' && c != ' ' && c != '\t') {
        return IHEX_ERR_SYNTAX;
      }
      continue;
    }

    /* ST_RECORD */
    if (c == '\r' || c == '\n') {
      int rc = end_record(p);
      if (c == '\n') p->line++;
      if (rc != IHEX_OK) return rc;
      continue;
    } else {
      int v = hexval(c);
      if (v < 0) return IHEX_ERR_SYNTAX;
      if (p->nibbles >= 2 * sizeof(p->rec)) return IHEX_ERR_SYNTAX;
      if (p->nibbles & 1) {
        p->rec[p->nibbles / 2] = (uint8_t)((p->hi << 4) | (uint8_t)v);
      } else {
        p->hi = (uint8_t)v;
      }
      p->nibbles++;
    }
  }
  return IHEX_OK;
}

int ihex_finish(ihex_t *p)
{
  if (!p->done && p->state == ST_RECORD) {
    int rc = end_record(p);              /* last line without a newline */
    if (rc < 0) return rc;
  }
  return p->done ? IHEX_DONE : IHEX_ERR_NO_EOF;
}
