#!/usr/bin/env python3
# Single source of truth -> generated headers.
#
# Reads main/field_registry.json and emits:
#   main/field_registry_gen.h                 (device, C)  — table + field_value() resolver
#   pith-dashboard/src/field_registry_gen.hpp (app, C++)   — table for the field dropdown
#
# Run with no arguments (paths are resolved relative to this file). Editing the
# JSON updates both sides so the app resolves/formats exactly like the device.
import json, os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
JSON = os.path.join(ROOT, "main", "field_registry.json")
OUT_H = os.path.join(ROOT, "main", "field_registry_gen.h")
OUT_HPP = os.path.join(ROOT, "pith-dashboard", "src", "field_registry_gen.hpp")

BANNER = ("// AUTO-GENERATED from main/field_registry.json by tools/gen_field_registry.py.\n"
          "// Do not edit by hand.")


def fmt_token(f):
    return "FMT_" + f.upper()


def enum_name(name):
    return "FIELD_" + name.upper()


def main():
    with open(JSON) as f:
        fields = json.load(f)["fields"]

    enum_lines = ["enum {", "    FIELD_NONE = 0,"]
    for fd in fields:
        enum_lines.append("    " + enum_name(fd["name"]) + ",")
    enum_lines.append("    FIELD_COUNT")
    enum_lines.append("};")
    enum_block = "\n".join(enum_lines)

    def reg_rows():
        rows = ['    { "", FMT_INT, 1, "" },']
        for fd in fields:
            rows.append('    {{ "{name}", {fmt}, {sc}, "{label}" }},'.format(
                name=fd["name"], fmt=fmt_token(fd["fmt"]), sc=fd.get("sc", 1), label=fd["label"]))
        return "\n".join(rows)

    # ---- device header (C) ----
    h = [BANNER,
         "#pragma once",
         '#include "simhub_proto.h"',
         '#include "format_common.h"',
         "#include <string.h>",
         "",
         "typedef struct { const char *name; uint8_t fmt; int16_t scale; const char *label; } field_def_t;",
         "",
         enum_block,
         "",
         "static const field_def_t FIELD_REGISTRY[FIELD_COUNT] = {",
         reg_rows(),
         "};",
         "",
         "static inline uint8_t field_id_from_str(const char *s) {",
         "    if (!s) return FIELD_NONE;",
         "    for (int i = 1; i < FIELD_COUNT; i++)",
         "        if (!strcmp(s, FIELD_REGISTRY[i].name)) return (uint8_t)i;",
         "    return FIELD_NONE;",
         "}",
         "",
         "// Resolve a field id to its current raw integer value from telemetry.",
         "static inline int field_value(const simhub_telemetry_t *t, uint8_t id) {",
         "    switch (id) {"]
    for fd in fields:
        h.append("    case {e}: return {acc};".format(e=enum_name(fd["name"]), acc=fd["accessor"]))
    h += ["    default: return 0;",
          "    }",
          "}",
          ""]
    with open(OUT_H, "w") as f:
        f.write("\n".join(h))

    # ---- app header (C++) ----
    hpp = [BANNER,
           "#pragma once",
           '#include "format_common.h"',
           "#include <string>",
           "#include <vector>",
           "",
           "struct FieldDef { const char *name; int fmt; int scale; const char *label; };",
           "",
           enum_block,
           "",
           "static const FieldDef FIELD_REGISTRY[FIELD_COUNT] = {",
           reg_rows(),
           "};",
           "",
           "static inline int field_id_from_str(const std::string &s) {",
           "    for (int i = 1; i < FIELD_COUNT; i++)",
           "        if (s == FIELD_REGISTRY[i].name) return i;",
           "    return FIELD_NONE;",
           "}",
           ""]
    with open(OUT_HPP, "w") as f:
        f.write("\n".join(hpp))

    print("generated field_registry_gen.h + field_registry_gen.hpp ({} fields)".format(len(fields)))


if __name__ == "__main__":
    main()
