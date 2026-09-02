/*
 * flash_storage.h
 *
 * Purpose:
 *   A small, generic driver for reading/writing the one reserved page of
 *   internal MCU flash used for persistent Configuration storage.
 *
 * Layer:
 *   Driver (sits above the STM32 HAL, below the Application layer)
 *
 * Responsibilities:
 *   - Erase and rewrite the reserved flash page with a caller-supplied
 *     block of bytes.
 *   - Read the reserved page's current contents back into a caller-
 *     supplied buffer.
 *
 * This file does NOT:
 *   - Know anything about Configuration's struct layout, magic number, or
 *     version field - it only moves raw bytes. configuration.c owns that
 *     meaning.
 *   - Support more than one reserved page, wear leveling, or a backup
 *     copy - one fixed page, rewritten as a whole each time (Sec 12
 *     working rule: keep it simple, don't build for hypothetical needs).
 *   - Use a mutex - nothing concurrent calls into Configuration yet
 *     (see configuration.h for the note on when one will be needed).
 *
 * Hardware:
 *   Reserved page: address 0x080FF800, size 2048 bytes - the very last
 *   page of this chip's 1MB flash (Bank 2, Page 255). The linker script
 *   (STM32L476RGTX_FLASH.ld) was shrunk from 1024K to 1022K so normal
 *   program code/data can never be placed here by the linker.
 */

#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLASH_STORAGE_ADDRESS 0x080FF800u
#define FLASH_STORAGE_PAGE    255u          /* page index within Bank 2 */
#define FLASH_STORAGE_BANK    FLASH_BANK_2

/*
 * Erases the reserved page and writes "size" bytes from "data" into it.
 * "size" must be a multiple of 8 (flash is written 8 bytes/double-word at
 * a time) and must not exceed the page size (2048 bytes).
 * Returns 1 on success, 0 on failure (bad size, or a HAL flash error).
 */
int FlashStorage_Write(const void *data, uint32_t size);

/*
 * Reads "size" bytes from the reserved page into "out". Flash is memory-
 * mapped, so this is just a plain copy - always succeeds.
 */
void FlashStorage_Read(void *out, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_STORAGE_H */
