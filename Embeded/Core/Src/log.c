/*
 * log.c
 *
 * See log.h for purpose, layer, and responsibilities.
 */

#include "log.h"
#include "fatfs.h"
#include <stdio.h>
extern SPI_HandleTypeDef hspi1;

/* TEMPORARY - defined in main.c, set here as proof Log_Init() was
 * actually reached. See main.c's Includes/PV comments. */
extern volatile int g_log_init_called;

#define SECONDS_PER_DAY (86400UL)
#define RETENTION_DAYS  (7UL)

static int s_mounted = 0;

/* TEMPORARY diagnostics - see the getters in log.h. */
static FRESULT s_lastMountResult = FR_OK;
static FRESULT s_lastOpenResult = FR_OK;
static FRESULT s_lastWriteResult = FR_OK;
static FRESULT s_lastCloseResult = FR_OK;
static UINT s_lastBytesWritten = 0;

/* Is "year" a leap year? (Gregorian calendar rule) */
static int IsLeapYear(uint16_t year)
{
    if (year % 400 == 0) return 1;
    if (year % 100 == 0) return 0;
    return (year % 4 == 0);
}

/* Converts a Unix epoch (seconds since 1970-01-01 UTC) into a calendar
 * year/month/day. Written as two simple counting loops (count whole
 * years, then count whole months) rather than a clever formula, so it's
 * easy to follow. */
static void EpochToDate(uint32_t epochSeconds, uint16_t *outYear, uint8_t *outMonth, uint8_t *outDay)
{
    static const uint8_t daysInMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint32_t days = epochSeconds / SECONDS_PER_DAY;
    uint16_t year = 1970;
    uint8_t month;

    for (;;)
    {
        uint16_t daysInThisYear = IsLeapYear(year) ? 366 : 365;
        if (days < daysInThisYear)
        {
            break;
        }
        days -= daysInThisYear;
        year++;
    }

    for (month = 0; month < 12; month++)
    {
        uint8_t dim = daysInMonth[month];
        if (month == 1 && IsLeapYear(year)) /* February */
        {
            dim = 29;
        }
        if (days < dim)
        {
            break;
        }
        days -= dim;
    }

    *outYear = year;
    *outMonth = (uint8_t)(month + 1);   /* 1-12 */
    *outDay = (uint8_t)(days + 1);      /* 1-31 */
}

/* Writes "YYYYMMDD.LOG\0" into outFilename for the given timestamp's
 * date. outFilename must be at least FILENAME_BUFFER_SIZE bytes -
 * realistic dates only ever need 13, but the buffer is sized a bit
 * larger so the compiler's format-length check (which conservatively
 * assumes a uint16_t year/uint8_t month/day could need more digits than
 * they realistically will) doesn't warn. */
#define FILENAME_BUFFER_SIZE 16

static void BuildFilename(uint32_t epochSeconds, char *outFilename)
{
    uint16_t year;
    uint8_t month, day;

    EpochToDate(epochSeconds, &year, &month, &day);
    snprintf(outFilename, FILENAME_BUFFER_SIZE, "%04u%02u%02u.LOG", year, month, day);
}

/* Formats a float with 2 decimal digits into "out", without relying on
 * printf's %f (this project's nano.specs build doesn't include
 * float-in-printf support). Simple truncation, not rounding - fine for a
 * log file. */
static void FormatFloat2dp(float value, char *out, size_t outSize)
{
    int whole = (int)value;
    int frac = (int)((value - (float)whole) * 100.0f);

    if (frac < 0)
    {
        frac = -frac;
    }
    snprintf(out, outSize, "%d.%02d", whole, frac);
}

/*int Log_Init(void)
{
    g_log_init_called = 1;

	HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
	HAL_Delay(1000);
    s_lastMountResult = f_mount(&USERFatFS, USERPath, 1);
    s_mounted = (s_lastMountResult == FR_OK);
    return s_mounted;
}*/
int Log_Init(void)
{
    g_log_init_called = 1;

    uint8_t dummy = 0xFF;

    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
    HAL_Delay(1000);

    /* Send 80 clock cycles (10 bytes of 0xFF) with CS high,
     * required by SD cards to enter SPI mode before CMD0. */
    for (int i = 0; i < 10; i++)
    {
        HAL_SPI_Transmit(&hspi1, &dummy, 1, 100);
    }

    s_lastMountResult = f_mount(&USERFatFS, USERPath, 1);
    s_mounted = (s_lastMountResult == FR_OK);
    return s_mounted;
}

static void DeleteOldFile(uint32_t currentTimestamp)
{
    char oldFilename[FILENAME_BUFFER_SIZE];

    /* Guard against underflow if currentTimestamp is small (e.g. during
     * early testing before a real time source exists) - nothing to
     * delete yet if we're not even 7 days past the epoch. */
    if (currentTimestamp < RETENTION_DAYS * SECONDS_PER_DAY)
    {
        return;
    }

    BuildFilename(currentTimestamp - (RETENTION_DAYS * SECONDS_PER_DAY), oldFilename);
    f_unlink(oldFilename); /* ignore result - fine if the file doesn't exist */
}

void Log_Write(const MeasurementSample *sample)
{
    char filename[FILENAME_BUFFER_SIZE];
    char tempStr[16], humidityStr[16], lightStr[16], batteryStr[16];
    char line[128];
    FIL file;
    UINT bytesWritten;
    int lineLength;

    if (!s_mounted)
    {
        return;
    }

    BuildFilename(sample->timestamp, filename);

   // s_lastOpenResult = f_open(&file, filename, FA_OPEN_APPEND | FA_WRITE);
    s_lastOpenResult = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (s_lastOpenResult != FR_OK)
    {
        return;
    }

    FormatFloat2dp(sample->temperature, tempStr, sizeof(tempStr));
    FormatFloat2dp(sample->humidity, humidityStr, sizeof(humidityStr));
    FormatFloat2dp(sample->light, lightStr, sizeof(lightStr));
    FormatFloat2dp(sample->battery, batteryStr, sizeof(batteryStr));

    lineLength = snprintf(line, sizeof(line), "%lu,%s,%s,%s,%s,%u\r\n",
                           (unsigned long)sample->timestamp,
                           tempStr, humidityStr, lightStr, batteryStr,
                           (unsigned int)sample->mode);

    s_lastWriteResult = f_write(&file, line, (UINT)lineLength, &bytesWritten);
    s_lastBytesWritten = bytesWritten;
    s_lastCloseResult = f_close(&file);

    DeleteOldFile(sample->timestamp);
}

int Log_GetLastMountResult(void)
{
    return (int)s_lastMountResult;
}

int Log_GetLastOpenResult(void)
{
    return (int)s_lastOpenResult;
}

int Log_GetLastWriteResult(void)
{
    return (int)s_lastWriteResult;
}

int Log_GetLastCloseResult(void)
{
    return (int)s_lastCloseResult;
}

unsigned int Log_GetLastBytesWritten(void)
{
    return (unsigned int)s_lastBytesWritten;
}
