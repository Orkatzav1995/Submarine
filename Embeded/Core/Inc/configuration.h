/*
 * configuration.h
 *
 * Purpose:
 *   Owns all of the LNC's configurable limit values (Sec 2.6 of the
 *   spec): the temperature/humidity/light/battery boundaries Monitor
 *   will compare sensor readings against. Persists them to internal MCU
 *   flash (via flash_storage.c) so they survive a reset, and loads
 *   sensible defaults the first time the board ever boots.
 *
 * Layer:
 *   Application (uses the flash_storage driver; will be used by
 *   Monitor and Communication/dispatch later)
 *
 * Responsibilities:
 *   - Load configuration from flash at startup, or apply + save defaults
 *     if flash doesn't hold valid data yet (first boot).
 *   - Provide a getter and a setter for each of the 8 configurable
 *     values (matching the 8 "set limit" commands in Sec 2.5 1:1).
 *   - Save to flash immediately whenever a setter is called.
 *
 * This file does NOT:
 *   - Know about UART, TLV frames, or Communication - callers (later,
 *     whatever dispatches incoming CC commands) translate a parsed
 *     message into a call to one of these setters.
 *   - Decide Normal/Warning/Error mode - that is Monitor's job. This
 *     module only stores the boundary values Monitor will compare against.
 *   - Use a mutex - nothing concurrent calls into Configuration yet (no
 *     Communication-dispatch or Monitor module exists). Needed later once
 *     a second task can call this while Init is still loading, or once
 *     Monitor reads while a setter is mid-save - noted, not built yet.
 *   - Implement wear leveling or a CRC - a single fixed page rewritten as
 *     a whole is enough for this project's scope (Sec 12: don't build for
 *     hypothetical needs).
 *   - Run as its own FreeRTOS task.
 */

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Loads configuration from flash, or applies + saves defaults if flash
 * doesn't hold valid data yet. Call once at boot, before anything else
 * reads configuration values. */
void Configuration_Init(void);

/* Returns 1 if Configuration_Init() found valid data already in flash
 * (magic + version matched), or 0 if it had to apply and save defaults
 * (first boot, or flash held something unrecognized). Useful for
 * hardware testing/debugging. */
int Config_WasLoadedFromFlash(void);

/* --- Setters: update the value and immediately save to flash --- */
void Config_SetTempNormalRange(float low, float high);
void Config_SetTempWarningRange(float low, float high);
void Config_SetHumidityNormalLower(float value);
void Config_SetHumidityWarningLower(float value);
void Config_SetLightNormalLower(float value);
void Config_SetLightWarningLower(float value);
void Config_SetBatteryNormalLower(float value);
void Config_SetBatteryWarningLower(float value);

/* --- Getters: read the current in-memory value --- */
void  Config_GetTempNormalRange(float *outLow, float *outHigh);
void  Config_GetTempWarningRange(float *outLow, float *outHigh);
float Config_GetHumidityNormalLower(void);
float Config_GetHumidityWarningLower(void);
float Config_GetLightNormalLower(void);
float Config_GetLightWarningLower(void);
float Config_GetBatteryNormalLower(void);
float Config_GetBatteryWarningLower(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGURATION_H */
