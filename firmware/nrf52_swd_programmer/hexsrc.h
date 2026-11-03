// SPDX-License-Identifier: MIT
// Copyright (c) 2026 LorDHaZarD
/*
 * hexsrc.h - where the HEX file comes from. On the Feather this is the SD
 * card through Petit FatFs (hexsrc_sd.cpp); the PC test reads a normal file.
 */
#ifndef HEXSRC_H
#define HEXSRC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HEXSRC_OK         0
#define HEXSRC_ERR_CARD   1   /* card missing or not answering */
#define HEXSRC_ERR_FS     2   /* no FAT file system found */
#define HEXSRC_ERR_FILE   3   /* HEX_FILENAME not in the root directory */
#define HEXSRC_ERR_READ   4

/* Initialise the card, mount and open HEX_FILENAME from the start. */
int      hexsrc_open(void);
/* Read up to n bytes; *br == 0 means end of file. */
int      hexsrc_read(uint8_t *buf, uint16_t n, uint16_t *br);
uint32_t hexsrc_size(void);

#ifdef __cplusplus
}
#endif

#endif /* HEXSRC_H */
