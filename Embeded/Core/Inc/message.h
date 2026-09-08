/*
 * message.h
 *
 * Purpose:
 *   Maps between typed, meaningful application messages and raw TLV
 *   frames, in whichever direction the LNC actually uses each tag:
 *     - BUILDS a ready-to-send frame for the 7 tags the LNC sends to the
 *       Central Computer (KeepAlive, DataReport, the 4 event types, the
 *       system-time response).
 *     - PARSES an incoming frame for the 12 tags the LNC receives FROM
 *       the Central Computer (the "set limit" commands and "get"
 *       requests) - the LNC never sends these, so it needs parsers here,
 *       not builders. (An earlier version of this file had builders for
 *       these 12 instead, which was backwards - fixed.)
 *   Application code either fills in a struct and calls a builder, or
 *   gets a filled-in struct back from a parser - either way, it never
 *   touches SOF/CRC/Tag details directly.
 *
 *   Covers 19 of the 21 tags in Common/Protocol/tlv_common.h. NOT covered
 *   yet: TAG_MEASUREMENT_CHUNK_RESPONSE and TAG_EVENT_CHUNK_RESPONSE - see
 *   PROJECT_GUIDE.md for why (the "~20 records per chunk" recommendation
 *   does not fit the 256-byte frame cap with the current record size, and
 *   an event's "description" field has no defined maximum length yet).
 *   Wherever multiple tags carry identical fields, they share one struct
 *   (e.g. MeasurementSample for TAG_KEEPALIVE and TAG_DATA_REPORT) instead
 *   of duplicating it under a different name.
 *
 * Layer:
 *   Message (Transport -> Protocol -> Message -> Application)
 *
 * Responsibilities:
 *   - Define typed structs for each application message.
 *   - Build a ready-to-send TLV frame from a struct (for messages the LNC sends).
 *   - Parse an incoming TLV frame into a struct, rejecting a frame with the
 *     wrong tag or an unexpected payload length (for messages the LNC receives).
 *
 * This file does NOT:
 *   - Know about UART, Ethernet, or any transport.
 *   - Use FreeRTOS.
 *   - Decide when to send anything, or what values to put in a message, or
 *     what to DO with a received command - that is Application code's job.
 */

#ifndef MESSAGE_H
#define MESSAGE_H

#include "protocol.h"
#include "tlv_common.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Data / Keep-alive
 * ============================================================ */

/*
 * A single sensor sample: timestamp, the four measured values, and the
 * resulting mode. Shared by two message types (TAG_KEEPALIVE, Sec 2.8, and
 * TAG_DATA_REPORT, Sec 2.1/2.5) because both carry exactly these fields -
 * only the Tag differs.
 */
typedef struct
{
    uint32_t timestamp;   /* Unix epoch UTC */
    float temperature;
    float humidity;
    float light;
    float battery;
    uint8_t mode;         /* a SystemMode value from tlv_common.h */
} MeasurementSample;

/*
 * Builds a complete, ready-to-send TAG_KEEPALIVE frame from "sample" into
 * out_buffer. Returns the number of bytes written, or 0 on failure (same
 * failure meaning as Protocol_EncodeFrame: buffer too small).
 */
uint16_t Message_BuildKeepAlive(const MeasurementSample *sample,
                                uint8_t *out_buffer,
                                uint16_t out_buffer_size);

/*
 * Builds a complete, ready-to-send TAG_DATA_REPORT frame from "sample".
 * Same payload layout and same failure meaning as Message_BuildKeepAlive -
 * only the Tag written into the frame differs.
 */
uint16_t Message_BuildDataReport(const MeasurementSample *sample,
                                 uint8_t *out_buffer,
                                 uint16_t out_buffer_size);

/* ============================================================
 * Single-timestamp messages
 * ============================================================ */

/*
 * Just one Unix epoch UTC timestamp. Shared by three message types that
 * each carry nothing but a single point in time:
 *   - TAG_SET_RTC_DATETIME: the new time the LNC should set its clock to.
 *   - TAG_SYSTEM_TIME_RESPONSE: the LNC's current time, in reply to a request.
 *   - TAG_EVENT_CONFIG_CHANGED: when the configuration changed (Sec 2.3.2).
 */
typedef struct
{
    uint32_t timestamp;
} TimestampMessage;

/* CC -> LNC: the new time the LNC should set its clock to. LNC receives this, so it parses, not builds. */
int Message_ParseSetRtcDateTime(const ProtocolFrame *frame, TimestampMessage *out_message);

uint16_t Message_BuildSystemTimeResponse(const TimestampMessage *message, uint8_t *out_buffer, uint16_t out_buffer_size);
uint16_t Message_BuildEventConfigChanged(const TimestampMessage *message, uint8_t *out_buffer, uint16_t out_buffer_size);

/*
 * Phase 1 (LNC Timestamp/RTC Hardening): CC -> LNC, in reply to the LNC's
 * own boot-time TAG_GET_SYSTEM_TIME_REQUEST (built below) - CC's real
 * current time. LNC receives this, so it parses, reusing the same
 * TimestampMessage every other single-timestamp tag already shares.
 */
int Message_ParseSystemTimeResponse(const ProtocolFrame *frame, TimestampMessage *out_message);

/*
 * CC -> LNC request with no payload at all: TAG_GET_SYSTEM_TIME_REQUEST.
 * Nothing to put in an output struct - just confirms the frame is this type.
 * Returns 1 if frame matches (right tag, zero-length payload), 0 otherwise.
 */
int Message_ParseGetSystemTimeRequest(const ProtocolFrame *frame);

/*
 * Phase 1 (LNC Timestamp/RTC Hardening): LNC -> CC, sent once at boot
 * (Init_Start()/StartTask02) to actively request CC's real time - the
 * LNC-initiated counterpart to the CC-initiated TAG_GET_SYSTEM_TIME_REQUEST
 * above (same tag, opposite direction; CC already has its own
 * buildGetSystemTimeRequest() for the reverse case). No payload at all.
 */
uint16_t Message_BuildGetSystemTimeRequest(uint8_t *out_buffer, uint16_t out_buffer_size);

/* ============================================================
 * Timestamp + one flag byte
 * ============================================================ */

/*
 * A timestamp plus a single 0/1 flag byte. Shared by two event types
 * whose meaning differs only in what the flag means:
 *   - TAG_EVENT_OBJECT_DETECTION: flag = 1 (object detected) or 0 (cleared).
 *   - TAG_EVENT_STARTUP: flag = 1 (startup followed a watchdog reset) or 0 (did not).
 */
typedef struct
{
    uint32_t timestamp;
    uint8_t flag;
} TimestampWithFlagMessage;

uint16_t Message_BuildEventObjectDetection(const TimestampWithFlagMessage *message, uint8_t *out_buffer, uint16_t out_buffer_size);
uint16_t Message_BuildEventStartup(const TimestampWithFlagMessage *message, uint8_t *out_buffer, uint16_t out_buffer_size);

/* ============================================================
 * Date/time ranges - the two "retrieve by range" requests
 * CC -> LNC: the LNC receives these, so it parses, not builds.
 * ============================================================ */

typedef struct
{
    uint8_t requestId;   /* echoed back in every chunk of the response, so
                            the Central Computer can match chunks to requests */
    uint32_t startTime;  /* Unix epoch UTC */
    uint32_t endTime;    /* Unix epoch UTC */
} TimeRangeMessage;

int Message_ParseGetMeasurementsByRangeRequest(const ProtocolFrame *frame, TimeRangeMessage *out_message);
int Message_ParseGetEventsByRangeRequest(const ProtocolFrame *frame, TimeRangeMessage *out_message);

/* ============================================================
 * Chunked bulk-transfer responses (LNC -> Central Computer)
 * See Common/Protocol/tlv_common.h for the sizing rationale
 * (MAX_MEASUREMENTS_PER_CHUNK, MAX_EVENTS_PER_CHUNK, EVENT_DESCRIPTION_SIZE).
 * ============================================================ */

/*
 * One stored event, as retrieved in reply to TAG_GET_EVENTS_BY_RANGE_REQUEST.
 * "eventType" is one of the TAG_EVENT_* values from tlv_common.h - reusing
 * the same vocabulary used for live event reporting, instead of a second enum.
 * "description" is always null-terminated within the fixed-size array - see
 * EVENT_DESCRIPTION_SIZE's contract in tlv_common.h.
 */
typedef struct
{
    uint32_t timestamp;
    uint8_t eventType;
    char description[EVENT_DESCRIPTION_SIZE];
} EventRecord;

/*
 * Builds one TAG_MEASUREMENT_CHUNK_RESPONSE frame carrying "sampleCount"
 * records (must be <= MAX_MEASUREMENTS_PER_CHUNK). Returns the number of
 * bytes written, or 0 on failure (sampleCount too large, or buffer too small).
 */
uint16_t Message_BuildMeasurementChunkResponse(uint8_t requestId, uint16_t chunkSeq, uint8_t moreDataFlag,
                                                const MeasurementSample *samples, uint8_t sampleCount,
                                                uint8_t *out_buffer, uint16_t out_buffer_size);

/*
 * Builds one TAG_EVENT_CHUNK_RESPONSE frame carrying "eventCount" records
 * (must be <= MAX_EVENTS_PER_CHUNK). Returns the number of bytes written,
 * or 0 on failure (eventCount too large, or buffer too small).
 */
uint16_t Message_BuildEventChunkResponse(uint8_t requestId, uint16_t chunkSeq, uint8_t moreDataFlag,
                                          const EventRecord *events, uint8_t eventCount,
                                          uint8_t *out_buffer, uint16_t out_buffer_size);

/* ============================================================
 * Configuration "set limit" commands
 * CC -> LNC: the LNC receives these, so it parses, not builds.
 * ============================================================ */

/*
 * Temperature is the only sensor with both a low AND a high bound per mode
 * (Sec 2.5) - the other three sensors only get a single lower bound.
 */
typedef struct
{
    float low;
    float high;
} TemperatureRangeMessage;

int Message_ParseSetTempNormalRange(const ProtocolFrame *frame, TemperatureRangeMessage *out_message);
int Message_ParseSetTempWarningRange(const ProtocolFrame *frame, TemperatureRangeMessage *out_message);

/*
 * A single limit value. Shared by the 6 humidity/light/battery "set lower
 * bound" commands - each sensor gets one Normal and one Warning bound.
 */
typedef struct
{
    float limitValue;
} SingleLimitMessage;

int Message_ParseSetHumidityNormalLower(const ProtocolFrame *frame, SingleLimitMessage *out_message);
int Message_ParseSetHumidityWarningLower(const ProtocolFrame *frame, SingleLimitMessage *out_message);
int Message_ParseSetLightNormalLower(const ProtocolFrame *frame, SingleLimitMessage *out_message);
int Message_ParseSetLightWarningLower(const ProtocolFrame *frame, SingleLimitMessage *out_message);
int Message_ParseSetBatteryNormalLower(const ProtocolFrame *frame, SingleLimitMessage *out_message);
int Message_ParseSetBatteryWarningLower(const ProtocolFrame *frame, SingleLimitMessage *out_message);

/* ============================================================
 * Mode transition event
 * ============================================================ */

typedef struct
{
    uint32_t timestamp;
    uint8_t oldMode; /* a SystemMode value from tlv_common.h */
    uint8_t newMode; /* a SystemMode value from tlv_common.h */
} ModeTransitionMessage;

uint16_t Message_BuildEventModeTransition(const ModeTransitionMessage *message, uint8_t *out_buffer, uint16_t out_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* MESSAGE_H */
