/*
 * log.h
 *
 * Purpose:
 *   Writes one log entry (timestamp + measurement data + mode) to a file
 *   named by date on the SD card, and keeps only the last 7 days of
 *   files (Sec 2.4 of the spec).
 *
 * Layer:
 *   Application (uses FatFs; will be called by Monitor later, Sec 2.1)
 *
 * Responsibilities:
 *   - Mount the SD card's filesystem (Log_Init).
 *   - Build one log line from a MeasurementSample and append it to
 *     today's file, creating the file if it doesn't exist yet.
 *   - Delete the file for the day exactly 7 days ago, so at most 7 files
 *     ever exist.
 *
 * This file does NOT:
 *   - Read the RTC or know what "now" is - the caller supplies a
 *     timestamp (same pattern as every message struct in message.h).
 *     This keeps Log completely independent of the not-yet-built RTC/
 *     Init work.
 *   - Decide Normal/Warning/Error mode - it just writes whatever mode
 *     value it's given.
 *   - Run as its own FreeRTOS task.
 *   - Use a mutex - nothing concurrent calls Log yet (no Monitor/Event
 *     module exists). Needed later once a second task can call this
 *     while another write is in progress - noted, not built yet.
 *
 * Filename format:
 *   YYYYMMDD.LOG (e.g. 20260831.LOG) - an 8.3-compatible short filename,
 *   since this project's FatFs config has long filenames disabled
 *   (_USE_LFN = 0 in ffconf.h).
 */

#ifndef LOG_H
#define LOG_H

#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mounts the SD card's filesystem. Call once at boot, before the first
 * Log_Write(). Returns 1 on success, 0 on failure (e.g. no card, or the
 * card isn't formatted as FAT). */
int Log_Init(void);

/* Appends one line built from "sample" to today's file (named by
 * sample->timestamp's date), creating the file if needed, then deletes
 * the file for the day exactly 7 days before that. Does nothing if
 * Log_Init() was never called or failed. */
void Log_Write(const MeasurementSample *sample);

/* TEMPORARY diagnostics - the result of the last f_open()/f_write()/
 * f_close() call made by Log_Write(), so real-hardware testing can see
 * exactly where a write is failing. Each is a FatFs FRESULT value cast
 * to int: 0 = FR_OK (success); nonzero = a specific error (see ff.h's
 * FRESULT enum - common ones: 1=FR_DISK_ERR, 3=FR_NOT_READY,
 * 7=FR_DENIED, 10=FR_WRITE_PROTECTED, 13=FR_NO_FILESYSTEM). Remove once
 * Log is confirmed working. */
int Log_GetLastMountResult(void);
int Log_GetLastOpenResult(void);
int Log_GetLastWriteResult(void);
int Log_GetLastCloseResult(void);
unsigned int Log_GetLastBytesWritten(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
