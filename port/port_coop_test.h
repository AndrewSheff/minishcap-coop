#ifndef PORT_COOP_TEST_H
#define PORT_COOP_TEST_H

/* Coop-test mode: boot straight into a named room preset, skipping the title
 * and file select, with a mid-game save state. Presets live in
 * coop_tests.json next to the executable (source: assets/coop_tests.json).
 *
 * TMC_COOP_TEST=<preset name> enables it at startup; unset = normal game.
 * The F8 "Coop Test" tab lists the presets and can jump to one at runtime. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortCoopTestPreset {
    char name[48];
    char description[256];
    unsigned char area;
    unsigned char room;
    unsigned char layer;
    unsigned short x; /* room-relative pixels */
    unsigned short y;
    int all_items;      /* Port_DebugAction_GiveAllItems() */
    int mid_game_flags; /* story-skip + world-open globals */
    int safe_spawn;     /* nudge Link off collision / pits on arrival */
} PortCoopTestPreset;

/* port_coop_test_presets.cpp: parse coop_tests.json (exe dir, then cwd).
 * Returns the preset count (0 on missing/invalid file) and writes the path
 * that was read (or tried) into path_out. */
int Port_CoopTest_LoadPresets(PortCoopTestPreset* out, int max, char* path_out, int path_len);

/* port_coop_test.c */
void Port_CoopTest_Tick(unsigned int frame);
void Port_CoopTest_ReloadPresets(void);
int Port_CoopTest_PresetCount(void);
const PortCoopTestPreset* Port_CoopTest_Preset(int i);
const char* Port_CoopTest_PresetsPath(void);
/* Jump to preset i: warps immediately when in gameplay, otherwise boots into
 * it from the title / file select. Returns 1 if fired or queued. */
int Port_CoopTest_Go(int i);

#ifdef __cplusplus
}
#endif

#endif /* PORT_COOP_TEST_H */
