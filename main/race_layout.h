// The data-driven module/layout interpreter (device side). The PC app uploads a
// list of MODULE SPECS — each carries its render kind, the telemetry field it
// binds, label, value format+unit, color rules, and zone placement. The firmware
// renders any layout generically from these specs; adding a module needs no
// firmware change. Wire format is `@RS{"v":1,"mods":[...]}` (see race_layout.c).
#pragma once
#include <stdint.h>
#include "format_common.h"   // fmt_type_t / rule_op_t / pal_token_t

#ifdef __cplusplus
extern "C" {
#endif

// Render kinds. RK_STAT / RK_BAR are fully data-bound (field+label+fmt+color);
// the rest are composite renderers that read a fixed set of telemetry fields.
typedef enum {
    RK_NONE = 0,
    RK_STAT, RK_GEAR, RK_GEARSPEED, RK_RPMSTRIP, RK_TYREGRID,
    RK_TCDUAL, RK_SECTORS, RK_LAPPAIR, RK_BAR, RK_MAP, RK_FLAG, RK_POSITION,
    RK_COUNT
} race_kind_t;

// Screen zones (regions of the main display).
typedef enum { RZ_TOP = 0, RZ_LEFT, RZ_CENTER, RZ_RIGHT, RZ_BOTTOM, RZ_ZONES } race_zone_t;

#define RACE_MAX_MODS   24
#define RACE_MAX_RULES   4
#define RACE_LABEL_LEN  12
#define RACE_UNIT_LEN    6
#define RACE_SCHEMA_VERSION 1

typedef struct { uint8_t op; uint8_t color; int32_t v; } race_rule_t;   // op/color are enums

typedef struct {
    uint8_t  kind;        // race_kind_t
    uint8_t  field;       // field-registry id (0 = none / composite reads fixed fields)
    uint8_t  fmt_t;       // fmt_type_t
    uint8_t  base;        // pal_token_t (base color)
    int16_t  scale;       // value divisor
    uint8_t  zone;        // race_zone_t
    uint8_t  order;       // sort order within the zone
    uint8_t  rule_n;      // active color rules
    uint8_t  size_pct;    // text size as % of box height (0 = auto-fit)
    char     label[RACE_LABEL_LEN];
    char     unit[RACE_UNIT_LEN];
    race_rule_t rules[RACE_MAX_RULES];
} race_mod_spec_t;

typedef struct {
    uint16_t version;
    uint8_t  count;
    uint8_t  valid;
    race_mod_spec_t mods[RACE_MAX_MODS];
} race_layout_t;

void race_layout_init(void);                            // restore NVS or default
const race_layout_t *race_layout_get(void);
int  race_layout_load_json(const char *json, int len);  // @RS handler; persists
const char *race_layout_json(void);                     // last-pushed @RS JSON (for @RG read-back)

#ifdef __cplusplus
}
#endif
