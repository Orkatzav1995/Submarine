/*
 * flash_storage.c
 *
 * See flash_storage.h for purpose, layer, and responsibilities.
 */

#include "flash_storage.h"
#include "main.h"

void FlashStorage_Read(void *out, uint32_t size)
{
    const uint8_t *flashPtr = (const uint8_t *)FLASH_STORAGE_ADDRESS;
    uint8_t *outPtr = (uint8_t *)out;
    uint32_t i;

    for (i = 0; i < size; i++)
    {
        outPtr[i] = flashPtr[i];
    }
}

int FlashStorage_Write(const void *data, uint32_t size)
{
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError = 0;
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t offset;

    if (size == 0 || (size % 8) != 0 || size > 2048)
    {
        return 0;
    }

    HAL_FLASH_Unlock();

    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.Banks = FLASH_STORAGE_BANK;
    eraseInit.Page = FLASH_STORAGE_PAGE;
    eraseInit.NbPages = 1;

    if (HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 0;
    }

    for (offset = 0; offset < size; offset += 8)
    {
        uint64_t doubleWord;

        /* Copy 8 bytes at a time into a uint64_t - avoids assuming
         * "bytes + offset" is 8-byte aligned. */
        for (uint32_t b = 0; b < 8; b++)
        {
            ((uint8_t *)&doubleWord)[b] = bytes[offset + b];
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                               FLASH_STORAGE_ADDRESS + offset,
                               doubleWord) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return 0;
        }
    }

    HAL_FLASH_Lock();
    return 1;
}
