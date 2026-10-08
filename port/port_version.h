#pragma once

/* The real value is injected at compile time via xmake.lua's add_defines
 * (TMC_PC_VERSION="...") so the version is sourced from a single place
 * (top of xmake.lua). The string below is only a fallback for editor /
 * IDE indexers that don't see the build flags. */
#ifndef TMC_PC_VERSION
#define TMC_PC_VERSION "0.9.5"
#endif

/* Picori Co-op release version (xmake.lua: TMC_COOP_VERSION), shown after
 * the port version in the window title. */
#ifndef TMC_COOP_VERSION
#define TMC_COOP_VERSION "0.1.0"
#endif
#define TMC_COOP_TITLE " - Picori Co-op v" TMC_COOP_VERSION
