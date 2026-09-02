/*
 * configuration.c
 *
 * See configuration.h for purpose, layer, and responsibilities.
 */

#include "configuration.h"
#include "flash_storage.h"
#include <stdint.h>

#define CONFIG_MAGIC   0x434F4E46u  /* ASCII "CONF" */
#define CONFIG_VERSION 1u

typedef struct
{
    uint32_t magic;
    uint32_t version;

    float tempNormalLow;
    float tempNormalHigh;
    float tempWarningLow;
    float tempWarningHigh;

    float humidityNormalLower;
    float humidityWarningLower;

    float lightNormalLower;
    float lightWarningLower;

    float batteryNormalLower;
    float batteryWarningLower;
} ConfigurationData;

static ConfigurationData s_config;
static int s_loadedFromFlash;

static void ApplyDefaults(void)
{
    s_config.magic = CONFIG_MAGIC;
    s_config.version = CONFIG_VERSION;

    s_config.tempNormalLow = 15.0f;
    s_config.tempNormalHigh = 30.0f;
    s_config.tempWarningLow = 10.0f;
    s_config.tempWarningHigh = 35.0f;

    s_config.humidityNormalLower = 40.0f;
    s_config.humidityWarningLower = 30.0f;

    s_config.lightNormalLower = 30.0f;
    s_config.lightWarningLower = 20.0f;

    s_config.batteryNormalLower = 30.0f;
    s_config.batteryWarningLower = 20.0f;
}

static void Save(void)
{
    FlashStorage_Write(&s_config, sizeof(s_config));
}

void Configuration_Init(void)
{
    ConfigurationData fromFlash;

    FlashStorage_Read(&fromFlash, sizeof(fromFlash));

    if (fromFlash.magic == CONFIG_MAGIC && fromFlash.version == CONFIG_VERSION)
    {
        s_config = fromFlash;
        s_loadedFromFlash = 1;
    }
    else
    {
        ApplyDefaults();
        Save();
        s_loadedFromFlash = 0;
    }
}

int Config_WasLoadedFromFlash(void)
{
    return s_loadedFromFlash;
}

void Config_SetTempNormalRange(float low, float high)
{
    s_config.tempNormalLow = low;
    s_config.tempNormalHigh = high;
    Save();
}

void Config_SetTempWarningRange(float low, float high)
{
    s_config.tempWarningLow = low;
    s_config.tempWarningHigh = high;
    Save();
}

void Config_SetHumidityNormalLower(float value)
{
    s_config.humidityNormalLower = value;
    Save();
}

void Config_SetHumidityWarningLower(float value)
{
    s_config.humidityWarningLower = value;
    Save();
}

void Config_SetLightNormalLower(float value)
{
    s_config.lightNormalLower = value;
    Save();
}

void Config_SetLightWarningLower(float value)
{
    s_config.lightWarningLower = value;
    Save();
}

void Config_SetBatteryNormalLower(float value)
{
    s_config.batteryNormalLower = value;
    Save();
}

void Config_SetBatteryWarningLower(float value)
{
    s_config.batteryWarningLower = value;
    Save();
}

void Config_GetTempNormalRange(float *outLow, float *outHigh)
{
    *outLow = s_config.tempNormalLow;
    *outHigh = s_config.tempNormalHigh;
}

void Config_GetTempWarningRange(float *outLow, float *outHigh)
{
    *outLow = s_config.tempWarningLow;
    *outHigh = s_config.tempWarningHigh;
}

float Config_GetHumidityNormalLower(void)
{
    return s_config.humidityNormalLower;
}

float Config_GetHumidityWarningLower(void)
{
    return s_config.humidityWarningLower;
}

float Config_GetLightNormalLower(void)
{
    return s_config.lightNormalLower;
}

float Config_GetLightWarningLower(void)
{
    return s_config.lightWarningLower;
}

float Config_GetBatteryNormalLower(void)
{
    return s_config.batteryNormalLower;
}

float Config_GetBatteryWarningLower(void)
{
    return s_config.batteryWarningLower;
}
