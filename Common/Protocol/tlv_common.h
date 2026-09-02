/*
 * tlv_common.h
 *
 * Purpose:
 *   Shared vocabulary for the TLV protocol: every Tag value that gives
 *   meaning to a TLV frame's Tag byte, and the small set of value enums
 *   (like the system Mode) both sides need to agree on the numbers for.
 *
 *   This is the ONE file both the LNC (C) and the Central Computer (C++)
 *   include directly, so the two sides can never quietly disagree about
 *   what a Tag number means. It is plain-C-compatible on purpose
 *   (extern "C" guarded) so it compiles unchanged in both languages.
 *
 * Layer:
 *   Shared vocabulary for the Message layer
 *   (Transport -> Protocol -> Message -> Application).
 *   This file does NOT do any encoding/decoding itself - see protocol.h
 *   (LNC) and tlv_codec.h (CC/GS) for that.
 *
 * Responsibilities:
 *   - Define every Tag value used on the wire, with its direction and
 *     payload shape documented in a comment.
 *   - Define the SystemMode enum (Normal/Warning/Error) shared by both sides.
 *
 * This file does NOT:
 *   - Encode or decode frames.
 *   - Know about UART, Ethernet, sockets, or any transport.
 *   - Depend on FreeRTOS, the STL, or anything specific to one side.
 *
 * NOTE: once a Tag number is assigned and in use, treat changing it as a
 * protocol-breaking change, exactly like SOF or the CRC algorithm.
 */

#ifndef TLV_COMMON_H
#define TLV_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    /* ---- Management "set" commands: Central Computer -> LNC ---- */
    TAG_SET_TEMP_NORMAL_RANGE      = 0x01, /* value: low + high */
    TAG_SET_TEMP_WARNING_RANGE     = 0x02, /* value: low + high */
    TAG_SET_HUMIDITY_NORMAL_LOWER  = 0x03, /* value: 1 number */
    TAG_SET_HUMIDITY_WARNING_LOWER = 0x04, /* value: 1 number */
    TAG_SET_LIGHT_NORMAL_LOWER     = 0x05, /* value: 1 number */
    TAG_SET_LIGHT_WARNING_LOWER    = 0x06, /* value: 1 number */
    TAG_SET_BATTERY_NORMAL_LOWER   = 0x07, /* value: 1 number */
    TAG_SET_BATTERY_WARNING_LOWER  = 0x08, /* value: 1 number */
    TAG_SET_RTC_DATETIME           = 0x09, /* value: DateTime (uint32) */

    /* ---- Requests: Central Computer -> LNC ---- */
    TAG_GET_SYSTEM_TIME_REQUEST           = 0x10, /* value: empty */
    TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST = 0x11, /* value: RequestId (uint8) + start DateTime + end DateTime */
    TAG_GET_EVENTS_BY_RANGE_REQUEST       = 0x12, /* value: RequestId (uint8) + start DateTime + end DateTime */

    /* ---- Responses: LNC -> Central Computer ---- */
    TAG_SYSTEM_TIME_RESPONSE       = 0x20, /* value: DateTime (uint32) */
    /* value: RequestId (uint8) + ChunkSeq (uint16 LE) + MoreDataFlag (uint8) +
       up to MAX_MEASUREMENTS_PER_CHUNK back-to-back 21-byte measurement records
       (same layout as TAG_KEEPALIVE/TAG_DATA_REPORT's value). RequestId echoes
       the value from the TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST that triggered
       this response, so the Central Computer can match chunks to requests. */
    TAG_MEASUREMENT_CHUNK_RESPONSE = 0x21,
    /* value: RequestId (uint8) + ChunkSeq (uint16 LE) + MoreDataFlag (uint8) +
       up to MAX_EVENTS_PER_CHUNK back-to-back event records, each
       EVENT_RECORD_SIZE bytes: timestamp (uint32 LE) + EventType (uint8, one
       of the TAG_EVENT_* values above) + description (EVENT_DESCRIPTION_SIZE
       bytes, ASCII, truncated to fit, always null-terminated within the field). */
    TAG_EVENT_CHUNK_RESPONSE       = 0x22,

    /* ---- Events: LNC -> Central Computer (unsolicited) ---- */
    TAG_EVENT_MODE_TRANSITION  = 0x30, /* value: timestamp + old_mode + new_mode */
    TAG_EVENT_OBJECT_DETECTION = 0x31, /* value: timestamp + detected_flag (1 = detected, 0 = cleared) */
    TAG_EVENT_CONFIG_CHANGED   = 0x32, /* value: timestamp */
    TAG_EVENT_STARTUP          = 0x33, /* value: timestamp + was_watchdog_reset (1 = yes, 0 = no) */

    /* ---- Data / Keep-alive: LNC -> Central Computer ---- */
    TAG_DATA_REPORT = 0x40, /* value: timestamp + temperature + humidity + light + battery + mode */
    TAG_KEEPALIVE   = 0x41  /* value: timestamp + temperature + humidity + light + battery + mode */
} MessageTag;

/*
 * The LNC's operating mode (Sec 2.10). Both sides must agree on these
 * numbers, since several Tag values above carry this as a raw byte.
 */
typedef enum
{
    MODE_NORMAL  = 0,
    MODE_WARNING = 1,
    MODE_ERROR   = 2
} SystemMode;

/*
 * Chunked bulk-transfer sizing (Sec 6, "bulk transfers"). Computed from the
 * frozen 256-byte total frame cap: kMaxValueSize (250) minus the 4-byte
 * chunk header (RequestId + ChunkSeq + MoreDataFlag) leaves 246 bytes for
 * records. Both sides must agree on these numbers - the LNC uses them to
 * decide how many records to pack into one chunk when building a response;
 * the Central Computer does NOT need to know them to parse a chunk (it just
 * divides the received value size by the record size), but they live here
 * as the single source of truth anyway.
 *
 *   Measurement record = 21 bytes (same layout as TAG_KEEPALIVE/TAG_DATA_REPORT).
 *   246 / 21 = 11 records/chunk (231 bytes used, 15 bytes unused - not worth
 *   a second, smaller size class just to use those 15 bytes).
 *
 *   Event record = EVENT_RECORD_SIZE bytes (see below).
 *   246 / 37 = 6 records/chunk (222 bytes used, 24 bytes unused).
 */
#define MAX_MEASUREMENTS_PER_CHUNK 11
#define MAX_EVENTS_PER_CHUNK       6

/*
 * An event record's description field: fixed-size on the wire (simpler to
 * build and parse than a variable-length field, and the LNC only ever
 * writes short, canned messages here - never arbitrary/user-supplied text).
 * Contract: up to EVENT_DESCRIPTION_SIZE-1 (31) characters, ALWAYS
 * null-terminated within the field - a longer source string is truncated,
 * a shorter one is null-padded.
 */
#define EVENT_DESCRIPTION_SIZE 32
#define EVENT_RECORD_SIZE (4 /* timestamp */ + 1 /* EventType */ + EVENT_DESCRIPTION_SIZE)

#ifdef __cplusplus
}
#endif

#endif /* TLV_COMMON_H */
