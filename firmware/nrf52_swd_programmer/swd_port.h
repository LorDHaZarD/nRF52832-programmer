/*
 * swd_port.h - pin-level access for the bit-banged SWD interface and the
 * target reset line, plus the few timing services the C modules need.
 *
 * On the Feather nRF52832 the GPIO registers are written directly, which is
 * far faster than digitalWrite(). When PROGRAMMER_SIM is defined (PC test
 * build) the same calls are routed to a software model of the target.
 */
#ifndef SWD_PORT_H
#define SWD_PORT_H

#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Timing services, implemented in the sketch (.ino) or the PC test. */
uint32_t port_millis(void);
void     port_delay_ms(uint32_t ms);

#ifdef PROGRAMMER_SIM
/* ---------------------------- PC simulation ---------------------------- */
void sim_swclk(int level);
void sim_swdio_out(int level);
void sim_swdio_dir_out(int out);
int  sim_swdio_in(void);
void sim_target_reset(int asserted);

static inline void swd_port_init(void)        { sim_swdio_dir_out(1); sim_swclk(0); }
static inline void swd_port_release(void)     { sim_swdio_dir_out(0); }
static inline void swclk_lo(void)             { sim_swclk(0); }
static inline void swclk_hi(void)             { sim_swclk(1); }
static inline void swdio_set(int v)           { sim_swdio_out(v); }
static inline int  swdio_get(void)            { return sim_swdio_in(); }
static inline void swdio_drive(void)          { sim_swdio_dir_out(1); }
static inline void swdio_float(void)          { sim_swdio_dir_out(0); }
static inline void swd_half_delay(void)       { }
static inline void target_reset_assert(void)  { sim_target_reset(1); }
static inline void target_reset_release(void) { sim_target_reset(0); }

#else
/* ------------------------- Feather nRF52832 ---------------------------- */
#include "nrf.h"

/* Arduino pin number -> GPIO number table from the board variant. */
extern const uint32_t g_ADigitalPinMap[];

extern uint32_t swd_clk_mask;
extern uint32_t swd_dio_mask;
extern uint32_t swd_dio_pin;

#define PORT_PIN_CNF(dir, input, pull)                    \
  (((uint32_t)(dir)   << GPIO_PIN_CNF_DIR_Pos)   |        \
   ((uint32_t)(input) << GPIO_PIN_CNF_INPUT_Pos) |        \
   ((uint32_t)(pull)  << GPIO_PIN_CNF_PULL_Pos)  |        \
   ((uint32_t)GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos))

static inline void swd_half_delay(void)
{
#if SWD_HALF_PERIOD_DELAY > 0
  for (volatile uint32_t i = SWD_HALF_PERIOD_DELAY; i > 0; i--) { }
#endif
}

static inline void swclk_lo(void)   { NRF_P0->OUTCLR = swd_clk_mask; }
static inline void swclk_hi(void)   { NRF_P0->OUTSET = swd_clk_mask; }
static inline void swdio_set(int v) { if (v) NRF_P0->OUTSET = swd_dio_mask; else NRF_P0->OUTCLR = swd_dio_mask; }
static inline int  swdio_get(void)  { return (NRF_P0->IN & swd_dio_mask) ? 1 : 0; }
static inline void swdio_drive(void){ NRF_P0->DIRSET = swd_dio_mask; }
static inline void swdio_float(void){ NRF_P0->DIRCLR = swd_dio_mask; }

/* Configure SWCLK as output low and SWDIO as output high with its input
 * buffer connected, so it can be read after DIRCLR without reconfiguring. */
static inline void swd_port_init(void)
{
  uint32_t clk = g_ADigitalPinMap[SWD_PIN_SWCLK];
  swd_dio_pin  = g_ADigitalPinMap[SWD_PIN_SWDIO];
  swd_clk_mask = 1UL << clk;
  swd_dio_mask = 1UL << swd_dio_pin;
  NRF_P0->OUTCLR = swd_clk_mask;
  NRF_P0->OUTSET = swd_dio_mask;
  NRF_P0->PIN_CNF[clk] = PORT_PIN_CNF(GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect,
                                      GPIO_PIN_CNF_PULL_Disabled);
  NRF_P0->PIN_CNF[swd_dio_pin] = PORT_PIN_CNF(GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Connect,
                                              GPIO_PIN_CNF_PULL_Pullup);
}

/* Put both SWD lines in high impedance so the target runs undisturbed. */
static inline void swd_port_release(void)
{
  uint32_t clk = g_ADigitalPinMap[SWD_PIN_SWCLK];
  uint32_t dio = g_ADigitalPinMap[SWD_PIN_SWDIO];
  NRF_P0->PIN_CNF[clk] = PORT_PIN_CNF(GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect,
                                      GPIO_PIN_CNF_PULL_Disabled);
  NRF_P0->PIN_CNF[dio] = PORT_PIN_CNF(GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect,
                                      GPIO_PIN_CNF_PULL_Disabled);
}

/* nRESET is driven open-drain style: low to assert, high impedance to release
 * (the target has its own pull-up on the reset pin). */
static inline void target_reset_assert(void)
{
  uint32_t p = g_ADigitalPinMap[TARGET_PIN_RESET];
  NRF_P0->OUTCLR = 1UL << p;
  NRF_P0->PIN_CNF[p] = PORT_PIN_CNF(GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect,
                                    GPIO_PIN_CNF_PULL_Disabled);
}

static inline void target_reset_release(void)
{
  uint32_t p = g_ADigitalPinMap[TARGET_PIN_RESET];
  NRF_P0->PIN_CNF[p] = PORT_PIN_CNF(GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect,
                                    GPIO_PIN_CNF_PULL_Disabled);
}
#endif /* PROGRAMMER_SIM */

#ifdef __cplusplus
}
#endif

#endif /* SWD_PORT_H */
