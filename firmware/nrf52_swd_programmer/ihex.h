/*
 * ihex.h - streaming Intel HEX parser.
 *
 * Feed it any number of bytes at a time; it calls the data callback once per
 * data record with the absolute address. No line buffer is needed beyond one
 * decoded record (max 255 data bytes).
 */
#ifndef IHEX_H
#define IHEX_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IHEX_OK            0
#define IHEX_DONE          1   /* EOF record seen; the rest of the input is ignored */
#define IHEX_ERR_SYNTAX   -1   /* bad character or record length */
#define IHEX_ERR_CHECKSUM -2
#define IHEX_ERR_TYPE     -3   /* unknown record type */
#define IHEX_ERR_CALLBACK -4   /* the data callback returned non-zero */
#define IHEX_ERR_NO_EOF   -5   /* input ended without an EOF record */

/* Return 0 to continue, non-zero to stop parsing (value kept in cb_result). */
typedef int (*ihex_data_cb)(void *ctx, uint32_t addr, const uint8_t *data, uint8_t len);

typedef struct {
  ihex_data_cb cb;
  void        *ctx;
  uint32_t     base;        /* from type 02 / 04 records */
  uint32_t     line;        /* current line number, for error messages */
  int          state;
  int          done;
  int          cb_result;
  uint16_t     nibbles;     /* hex digits received in the current record */
  uint8_t      hi;          /* pending high nibble */
  uint8_t      rec[5 + 255];/* len, addr hi, addr lo, type, data..., checksum */
} ihex_t;

void ihex_init(ihex_t *p, ihex_data_cb cb, void *ctx);
/* Returns IHEX_OK, IHEX_DONE or a negative error. */
int  ihex_feed(ihex_t *p, const uint8_t *buf, size_t n);
/* Call at end of input. Returns IHEX_DONE or IHEX_ERR_NO_EOF. */
int  ihex_finish(ihex_t *p);

#ifdef __cplusplus
}
#endif

#endif /* IHEX_H */
