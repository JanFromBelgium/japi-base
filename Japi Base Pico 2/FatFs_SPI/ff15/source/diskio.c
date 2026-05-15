#include "ff.h"
#include "diskio.h"
#include "sd_card.h"

// We forceren de declaratie zodat de compiler niet meer klaagt
extern sd_card_t *sd_get_by_num(size_t num);

#define DEV_SD 0

DSTATUS disk_status (BYTE pdrv) {
    if (pdrv != DEV_SD) return STA_NOINIT;
    sd_card_t *pSD = sd_get_by_num(pdrv);
    if (!pSD) return STA_NOINIT;
    return 0; 
}

DSTATUS disk_initialize (BYTE pdrv) {
    if (pdrv != DEV_SD) return STA_NOINIT;
    sd_card_t *pSD = sd_get_by_num(pdrv);
    if (!pSD) return STA_NOINIT;
    
    // Roep de functie aan VIA het object
    if (pSD->init) {
        return (pSD->init(pSD) == 0) ? 0 : STA_NOINIT;
    }
    return STA_NOINIT;
}

DRESULT disk_read (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != DEV_SD) return RES_PARERR;
    sd_card_t *pSD = sd_get_by_num(pdrv);
    if (!pSD || !pSD->read_blocks) return RES_PARERR;
    
    // Roep read_blocks aan VIA het object
    return (pSD->read_blocks(pSD, buff, sector, count) == 0) ? RES_OK : RES_ERROR;
}

DRESULT disk_write (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != DEV_SD) return RES_PARERR;
    sd_card_t *pSD = sd_get_by_num(pdrv);
    if (!pSD || !pSD->write_blocks) return RES_PARERR;
    
    // Roep write_blocks aan VIA het object met de juiste cast
    return (pSD->write_blocks(pSD, (const uint8_t*)buff, sector, count) == 0) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != DEV_SD) return RES_PARERR;
    return RES_OK;
}