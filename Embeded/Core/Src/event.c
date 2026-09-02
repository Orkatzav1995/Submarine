/*
 * event.c
 *
 * See event.h for purpose, layer, and responsibilities.
 */

#include "event.h"
#include "fatfs.h"
#include "led.h"
#include "buzzer.h"
#include "button.h"
#include <stdio.h>

#define SECONDS_PER_DAY (86400UL)
#define RETENTION_DAYS  (7UL)

/* 1 while the alarm (buzzer) is active because of an Error-mode
 * transition or a detected object - lets Event_CheckAlarmButton() know
 * whether a button press should do anything. */
static int s_alarmActive = 0;

/* TEMPORARY diagnostics - see the getters in event.h. */
static FRESULT s_lastOpenResult = FR_OK;
static FRESULT s_lastWriteResult = FR_OK;
static FRESULT s_lastCloseResult = FR_OK;

/*
 * BuildFilename()/EpochToDate()/IsLeapYear() below are intentionally
 * duplicated from log.c rather than shared: log.c's versions are static
 * (private to that file), and Log is already hardware-verified and
 * marked "do not revisit" in PROJECT_GUIDE.md - exporting them would
 * mean touching finished, verified code just to avoid ~20 lines of
 * duplication here.
 */

static int IsLeapYear(uint16_t year)
{
    if (year % 400 == 0) return 1;
    if (year % 100 == 0) return 0;
    return (year % 4 == 0);
}

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

/* "YYYYMMDD.TXT\0" - same 8.3-compatible shape as log.c's YYYYMMDD.LOG,
 * a different base pattern so the two files never collide (Log keeps its
 * own ".LOG" extension - not touched here). Plain ".TXT" chosen so the
 * file opens directly in Notepad etc. for manual SD-card inspection -
 * the content is plain human-readable text either way, so there was no
 * technical reason to prefer a different extension. Buffer sized like
 * log.c's FILENAME_BUFFER_SIZE, for the same format-length-warning
 * reason documented there. */
#define FILENAME_BUFFER_SIZE 16

static void BuildFilename(uint32_t epochSeconds, char *outFilename)
{
    uint16_t year;
    uint8_t month, day;

    EpochToDate(epochSeconds, &year, &month, &day);
    snprintf(outFilename, FILENAME_BUFFER_SIZE, "%04u%02u%02u.TXT", year, month, day);
}

static void DeleteOldFile(uint32_t currentTimestamp)
{
    char oldFilename[FILENAME_BUFFER_SIZE];

    if (currentTimestamp < RETENTION_DAYS * SECONDS_PER_DAY)
    {
        return;
    }

    BuildFilename(currentTimestamp - (RETENTION_DAYS * SECONDS_PER_DAY), oldFilename);
    f_unlink(oldFilename); /* ignore result - fine if the file doesn't exist */
}

/* Appends one "timestamp,eventType,description\r\n" line to today's
 * events file, creating it if needed, then applies the same 7-day
 * retention as Log. Uses FA_OPEN_APPEND (not Log's current
 * FA_CREATE_ALWAYS) because an events file must accumulate every event
 * from the day, not just keep the last one written. */
static void WriteEventRecord(uint32_t timestamp, uint8_t eventType, const char *description)
{
    char filename[FILENAME_BUFFER_SIZE];
    char line[64];
    FIL file;
    UINT bytesWritten;
    int lineLength;

    BuildFilename(timestamp, filename);

    s_lastOpenResult = f_open(&file, filename, FA_OPEN_APPEND | FA_WRITE);
    if (s_lastOpenResult != FR_OK)
    {
        return;
    }

    lineLength = snprintf(line, sizeof(line), "%lu,%u,%s\r\n",
                           (unsigned long)timestamp, (unsigned int)eventType, description);

    s_lastWriteResult = f_write(&file, line, (UINT)lineLength, &bytesWritten);
    s_lastCloseResult = f_close(&file);

    DeleteOldFile(timestamp);
}

static const char *ModeName(uint8_t mode)
{
    switch (mode)
    {
        case MODE_NORMAL:  return "NORMAL";
        case MODE_WARNING: return "WARNING";
        case MODE_ERROR:   return "ERROR";
        default:           return "UNKNOWN";
    }
}

void Event_Init(void)
{
    s_alarmActive = 0;
}

int Event_IsAlarmActive(void)
{
    return s_alarmActive;
}

uint16_t Event_OnModeTransition(const ModeTransitionMessage *transition, uint8_t *out_frame, uint16_t out_frame_size)
{
    char description[EVENT_DESCRIPTION_SIZE];
    LedColor color;

    snprintf(description, sizeof(description), "MODE %s->%s",
             ModeName(transition->oldMode), ModeName(transition->newMode));

    if (transition->newMode == MODE_ERROR)
    {
        /* ->Error (Sec 2.3.1): LED red, alarm on. "Suppress non-essential
         * operations" is deliberately not implemented here - see this
         * file's header comment and PROJECT_GUIDE.md Open Question #3. */
        color = LED_COLOR_RED;
        Buzzer_On();
        s_alarmActive = 1;
    }
    else if (transition->newMode == MODE_WARNING)
    {
        /* Normal->Warning or Error->Warning: LED yellow. Only the
         * Error->Warning case also needs to stop an active alarm and
         * "resume full operation" - nothing is currently suspended, so
         * there is nothing to resume (see header comment). */
        color = LED_COLOR_YELLOW;
        if (transition->oldMode == MODE_ERROR && s_alarmActive)
        {
            Buzzer_Off();
            s_alarmActive = 0;
        }
    }
    else /* MODE_NORMAL */
    {
        /* Warning->Normal or Error->Normal: LED green, same alarm-stop
         * rule as Error->Warning above. */
        color = LED_COLOR_GREEN;
        if (transition->oldMode == MODE_ERROR && s_alarmActive)
        {
            Buzzer_Off();
            s_alarmActive = 0;
        }
    }

    Led_SetColor(color);
    WriteEventRecord(transition->timestamp, TAG_EVENT_MODE_TRANSITION, description);

    return Message_BuildEventModeTransition(transition, out_frame, out_frame_size);
}

uint16_t Event_OnObjectDetection(const TimestampWithFlagMessage *detection, uint8_t *out_frame, uint16_t out_frame_size)
{
    const char *description;

    if (detection->flag)
    {
        Led_SetColor(LED_COLOR_RED);
        Buzzer_On();
        s_alarmActive = 1;
        description = "OBJECT DETECTED";
    }
    else
    {
        Led_SetColor(LED_COLOR_GREEN);
        if (s_alarmActive)
        {
            Buzzer_Off();
            s_alarmActive = 0;
        }
        description = "OBJECT CLEARED";
    }

    WriteEventRecord(detection->timestamp, TAG_EVENT_OBJECT_DETECTION, description);

    return Message_BuildEventObjectDetection(detection, out_frame, out_frame_size);
}

uint16_t Event_OnConfigurationChanged(const TimestampMessage *change, uint8_t *out_frame, uint16_t out_frame_size)
{
    /* Sec 2.3's own bullet for this case only says "write timestamped
     * message to events file" - no LED/buzzer action, and per this
     * session's decision, also send it to the CC (TAG_EVENT_CONFIG_CHANGED
     * already exists as a real wire tag/builder). */
    WriteEventRecord(change->timestamp, TAG_EVENT_CONFIG_CHANGED, "CONFIGURATION CHANGED");

    return Message_BuildEventConfigChanged(change, out_frame, out_frame_size);
}

uint16_t Event_OnInitStartup(const TimestampWithFlagMessage *startup, uint8_t *out_frame, uint16_t out_frame_size)
{
    const char *description = startup->flag ? "STARTUP (WDT RESET)" : "STARTUP (NORMAL)";

    WriteEventRecord(startup->timestamp, TAG_EVENT_STARTUP, description);

    return Message_BuildEventStartup(startup, out_frame, out_frame_size);
}

void Event_CheckAlarmButton(void)
{
    if (s_alarmActive && Button_Read() == BUTTON_PRESSED)
    {
        Buzzer_Off();
        s_alarmActive = 0;
    }
}

int Event_GetLastOpenResult(void)
{
    return (int)s_lastOpenResult;
}

int Event_GetLastWriteResult(void)
{
    return (int)s_lastWriteResult;
}

int Event_GetLastCloseResult(void)
{
    return (int)s_lastCloseResult;
}
