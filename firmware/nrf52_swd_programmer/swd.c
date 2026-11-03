/*
 * swd.c - bit-banged ARM Serial Wire Debug host (ADIv5, SWD protocol v1).
 *
 * Wire timing (matches CMSIS-DAP): the host changes SWDIO while SWCLK is
 * high and drops SWCLK; the target samples SWDIO on the rising edge. When
 * the target drives SWDIO it changes it after a rising edge, and the host
 * samples it while SWCLK is low, before raising SWCLK again.
 */
#include "swd.h"
#include "swd_port.h"

#ifndef PROGRAMMER_SIM
uint32_t swd_clk_mask;
uint32_t swd_dio_mask;
uint32_t swd_dio_pin;
#endif

#define ACK_OK     0x1
#define ACK_WAIT   0x2
#define ACK_FAULT  0x4

uint32_t swd_wait_count;
static uint32_t select_cache;

/* ------------------------------------------------------------------------ */
/* Bit level                                                                */
/* ------------------------------------------------------------------------ */

static inline void write_bit(uint32_t bit)
{
  swdio_set((int)(bit & 1));
  swclk_lo();
  swd_half_delay();
  swclk_hi();
  swd_half_delay();
}

static inline uint32_t read_bit(void)
{
  uint32_t bit;
  swclk_lo();
  swd_half_delay();
  bit = (uint32_t)swdio_get();
  swclk_hi();
  swd_half_delay();
  return bit;
}

static inline void clock_cycle(void)
{
  swclk_lo();
  swd_half_delay();
  swclk_hi();
  swd_half_delay();
}

static void write_bits(uint32_t value, uint32_t count)
{
  while (count--) {
    write_bit(value);
    value >>= 1;
  }
}

static uint32_t read_bits(uint32_t count)
{
  uint32_t value = 0;
  uint32_t i;
  for (i = 0; i < count; i++) {
    value |= read_bit() << i;
  }
  return value;
}

static uint32_t parity32(uint32_t v)
{
  v ^= v >> 16;
  v ^= v >> 8;
  v ^= v >> 4;
  v ^= v >> 2;
  v ^= v >> 1;
  return v & 1;
}

static void idle_cycles(uint32_t n)
{
  swdio_set(0);
  while (n--) clock_cycle();
}

/* ------------------------------------------------------------------------ */
/* Packet level                                                             */
/* ------------------------------------------------------------------------ */

/*
 * One SWD transaction with WAIT retry.
 * ap: 0 = DP, 1 = AP. rnw: 1 = read. addr: register address (bits 3:2 used).
 */
static int transfer(uint32_t ap, uint32_t rnw, uint8_t addr, uint32_t *data)
{
  uint32_t retries = SWD_WAIT_RETRIES;
  uint32_t a23 = ((uint32_t)addr >> 2) & 3;
  uint32_t req = 0x81                          /* start (bit 0) and park (bit 7) */
               | (ap << 1) | (rnw << 2) | (a23 << 3)
               | (parity32((ap) | (rnw << 1) | (a23 << 2)) << 5);

  for (;;) {
    uint32_t ack;

    swdio_drive();
    write_bits(req, 8);
    swdio_float();
    clock_cycle();                     /* turnaround host -> target */
    ack = read_bits(3);

    if (ack == ACK_OK) {
      if (rnw) {
        uint32_t value = read_bits(32);
        uint32_t par   = read_bit();
        clock_cycle();                 /* turnaround target -> host */
        swdio_drive();
        idle_cycles(2);
        if (par != parity32(value)) return SWD_ERR_PARITY;
        if (data) *data = value;
      } else {
        clock_cycle();                 /* turnaround target -> host */
        swdio_drive();
        write_bits(*data, 32);
        write_bit(parity32(*data));
        idle_cycles(2);
      }
      return SWD_OK;
    }

    if (ack == ACK_WAIT || ack == ACK_FAULT) {
      clock_cycle();                   /* turnaround target -> host */
      swdio_drive();
      idle_cycles(2);
      if (ack == ACK_FAULT) {
        uint32_t clr = DP_ABORT_CLEAR_ALL;
        /* Writing ABORT is always accepted, even after FAULT. */
        transfer(0, 0, DP_ABORT, &clr);
        return SWD_ERR_FAULT;
      }
      swd_wait_count++;
      if (--retries == 0) {
        uint32_t abort = 0x1;          /* DAPABORT: cancel the stuck transaction */
        transfer(0, 0, DP_ABORT, &abort);
        return SWD_ERR_WAIT;
      }
      continue;
    }

    /* No valid ACK. Clock out a full data phase so the target (if it did
     * see the request) returns to idle, then give up. */
    read_bits(33);
    clock_cycle();
    swdio_drive();
    idle_cycles(2);
    return SWD_ERR_NOACK;
  }
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

void swd_init(void)
{
  swd_port_init();
  select_cache = 0xFFFFFFFFUL;
}

void swd_release(void)
{
  swd_port_release();
}

int swd_connect(uint32_t *idcode)
{
  uint32_t id = 0;
  int rc;

  swd_init();
  swdio_drive();

  write_bits(0xFFFFFFFFUL, 32);        /* line reset: >= 50 cycles high */
  write_bits(0xFFFFFFFFUL, 24);
  write_bits(0xE79E, 16);              /* JTAG-to-SWD select sequence */
  write_bits(0xFFFFFFFFUL, 32);        /* line reset again */
  write_bits(0xFFFFFFFFUL, 24);
  idle_cycles(8);

  rc = transfer(0, 1, DP_IDCODE, &id);  /* IDCODE read leaves the reset state */
  if (rc == SWD_OK) {
    uint32_t clr = DP_ABORT_CLEAR_ALL;
    rc = transfer(0, 0, DP_ABORT, &clr);
  }
  if (idcode) *idcode = id;
  select_cache = 0xFFFFFFFFUL;
  return rc;
}

int swd_dp_read(uint8_t addr, uint32_t *value)
{
  return transfer(0, 1, addr, value);
}

int swd_dp_write(uint8_t addr, uint32_t value)
{
  if (addr == DP_SELECT) select_cache = value;
  return transfer(0, 0, addr, &value);
}

int swd_power_up(void)
{
  uint32_t t0, stat = 0;
  int rc = swd_dp_write(DP_CTRL_STAT, DP_CTRL_CDBGPWRUPREQ | DP_CTRL_CSYSPWRUPREQ);
  if (rc) return rc;
  t0 = port_millis();
  for (;;) {
    rc = swd_dp_read(DP_CTRL_STAT, &stat);
    if (rc) return rc;
    if ((stat & (DP_CTRL_CDBGPWRUPACK | DP_CTRL_CSYSPWRUPACK)) ==
        (DP_CTRL_CDBGPWRUPACK | DP_CTRL_CSYSPWRUPACK)) {
      return SWD_OK;
    }
    if (port_millis() - t0 > 100) return SWD_ERR_TIMEOUT;
  }
}

void swd_power_down(void)
{
  swd_dp_write(DP_CTRL_STAT, 0);
}

static int ap_select(uint8_t apsel, uint8_t addr)
{
  uint32_t sel = ((uint32_t)apsel << 24) | ((uint32_t)addr & 0xF0);
  if (sel == select_cache) return SWD_OK;
  return swd_dp_write(DP_SELECT, sel);
}

int swd_ap_write(uint8_t apsel, uint8_t addr, uint32_t value)
{
  int rc = ap_select(apsel, addr);
  if (rc) return rc;
  return transfer(1, 0, addr, &value);
}

int swd_ap_read(uint8_t apsel, uint8_t addr, uint32_t *value)
{
  int rc = ap_select(apsel, addr);
  if (rc) return rc;
  rc = transfer(1, 1, addr, 0);        /* posted read, result arrives in RDBUFF */
  if (rc) return rc;
  return transfer(0, 1, DP_RDBUFF, value);
}

int swd_mem_init(void)
{
  uint32_t csw;
  int rc = swd_ap_read(AHB_AP, AP_CSW, &csw);
  if (rc) return rc;
  csw = (csw & ~0x37UL) | 0x12UL;      /* Size = 32 bit, AddrInc = single */
  return swd_ap_write(AHB_AP, AP_CSW, csw);
}

int swd_mem_write32(uint32_t addr, uint32_t value)
{
  int rc = swd_ap_write(AHB_AP, AP_TAR, addr);
  if (rc) return rc;
  return swd_ap_write(AHB_AP, AP_DRW, value);
}

int swd_mem_read32(uint32_t addr, uint32_t *value)
{
  int rc = swd_ap_write(AHB_AP, AP_TAR, addr);
  if (rc) return rc;
  return swd_ap_read(AHB_AP, AP_DRW, value);
}

int swd_mem_read_block(uint32_t addr, uint32_t *words, uint32_t count)
{
  uint32_t i;
  int rc;

  if (count == 0) return SWD_OK;
  rc = swd_ap_write(AHB_AP, AP_TAR, addr);
  if (rc) return rc;
  rc = ap_select(AHB_AP, AP_DRW);
  if (rc) return rc;

  /* AP reads are posted: each read returns the result of the previous one. */
  rc = transfer(1, 1, AP_DRW, 0);
  if (rc) return rc;
  for (i = 1; i < count; i++) {
    rc = transfer(1, 1, AP_DRW, &words[i - 1]);
    if (rc) return rc;
  }
  return transfer(0, 1, DP_RDBUFF, &words[count - 1]);
}
