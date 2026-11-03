/*---------------------------------------------------------------------------/
/  Petit FatFs - Configuration file (settings for the nRF52832 SWD programmer)
/---------------------------------------------------------------------------*/

#ifndef PFCONF_DEF
#define PFCONF_DEF 8088  /* Revision ID */

/*---------------------------------------------------------------------------/
/ Function Configurations (0:Disable, 1:Enable)
/---------------------------------------------------------------------------*/

#define  PF_USE_READ   1  /* pf_read() function */
#define  PF_USE_DIR    0  /* pf_opendir() and pf_readdir() function */
#define  PF_USE_LSEEK  0  /* pf_lseek() function */
#define  PF_USE_WRITE  0  /* pf_write() function */

/* 1 GB cards usually ship as FAT16; FAT32 is what the brief names. Read both. */
#define PF_FS_FAT12    1  /* FAT12 */
#define PF_FS_FAT16    1  /* FAT16 */
#define PF_FS_FAT32    1  /* FAT32 */


/*---------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/---------------------------------------------------------------------------*/

/* 0: names must be upper case ASCII. hexsrc_sd.cpp upper-cases HEX_FILENAME
/  before calling pf_open(), so the name in config.h is case-insensitive. */
#define PF_USE_LCC    0

#define  PF_CODE_PAGE  437

#endif /* PF_CONF */
