/*
 * log.h - debug output on the Feather's serial port (or stdout in the PC test).
 */
#ifndef PROGRAMMER_LOG_H
#define PROGRAMMER_LOG_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if DEBUG_SERIAL
void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(...) log_printf(__VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* PROGRAMMER_LOG_H */
