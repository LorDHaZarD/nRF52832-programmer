/*
 * swd.h - ARM Serial Wire Debug (ADIv5) host, bit-banged.
 *
 * Layers:
 *   swd_line_*      wire level: line reset, JTAG-to-SWD switch
 *   swd_dp_*        debug port registers
 *   swd_ap_*        access port registers (any AP, SELECT handled here)
 *   swd_mem_*       32-bit memory access through the AHB-AP (AP 0)
 */
#ifndef SWD_H
#define SWD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes */
#define SWD_OK            0
#define SWD_ERR_WAIT      1   /* target kept answering WAIT */
#define SWD_ERR_FAULT     2   /* target answered FAULT (sticky error cleared) */
#define SWD_ERR_NOACK     3   /* no valid ACK: nothing connected or line out of sync */
#define SWD_ERR_PARITY    4   /* read data parity error */
#define SWD_ERR_TIMEOUT   5   /* a polled condition never became true */

/* DP register addresses (A[3:2] << 2) */
#define DP_IDCODE         0x00   /* read */
#define DP_ABORT          0x00   /* write */
#define DP_CTRL_STAT      0x04
#define DP_SELECT         0x08
#define DP_RDBUFF         0x0C

#define DP_ABORT_CLEAR_ALL       0x0000001EUL  /* ORUNERRCLR|WDERRCLR|STKERRCLR|STKCMPCLR */
#define DP_CTRL_CDBGPWRUPREQ     (1UL << 28)
#define DP_CTRL_CDBGPWRUPACK     (1UL << 29)
#define DP_CTRL_CSYSPWRUPREQ     (1UL << 30)
#define DP_CTRL_CSYSPWRUPACK     (1UL << 31)

/* MEM-AP register addresses */
#define AP_CSW            0x00
#define AP_TAR            0x04
#define AP_DRW            0x0C
#define AP_IDR            0xFC

#define AHB_AP            0

void swd_init(void);
void swd_release(void);

/* Line reset + JTAG-to-SWD + line reset + read IDCODE. */
int  swd_connect(uint32_t *idcode);

/* Power up the debug and system domains (CTRL/STAT handshake). */
int  swd_power_up(void);
/* Drop the power-up requests so the target can leave debug interface mode. */
void swd_power_down(void);

int  swd_dp_read(uint8_t addr, uint32_t *value);
int  swd_dp_write(uint8_t addr, uint32_t value);
int  swd_ap_read(uint8_t apsel, uint8_t addr, uint32_t *value);
int  swd_ap_write(uint8_t apsel, uint8_t addr, uint32_t value);

/* Memory access through the AHB-AP. swd_mem_init() sets CSW to 32-bit,
 * auto-increment. Block functions must not cross a 1 KB boundary. */
int  swd_mem_init(void);
int  swd_mem_read32(uint32_t addr, uint32_t *value);
int  swd_mem_write32(uint32_t addr, uint32_t value);
int  swd_mem_read_block(uint32_t addr, uint32_t *words, uint32_t count);

/* Statistics, handy in the debug log */
extern uint32_t swd_wait_count;

#ifdef __cplusplus
}
#endif

#endif /* SWD_H */
