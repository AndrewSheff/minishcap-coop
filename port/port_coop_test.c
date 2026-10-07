/*
 * port/port_coop_test.c — "coop test" quick-start mode.
 *
 * TMC_COOP_TEST=<preset> boots the game straight into the preset's room,
 * skipping the title and file select, with a synthesized mid-game save
 * (story-skip flags, optionally every item). Presets come from
 * coop_tests.json next to the executable (see port_coop_test_presets.cpp).
 * Unset = the game behaves exactly as before.
 *
 * Built from the existing harness pieces: the title -> file-select bootstrap
 * of port_repro_npc_talk.c, Port_DebugAction_Warp / WarpEntry and their
 * safe-spawn nudge, and Port_DebugAction_GiveAllItems.
 *
 * The synthesized save lives in memory only (slot 0 of the file-select
 * state); nothing is written to disk unless the player saves in-game, which
 * would overwrite slot 1 — don't save in coop-test mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "save.h"
#include "fileselect.h"
#include "room.h"
#include "flags.h"
#include "player.h"
#include "game.h"
#include "rando/rando.h"
#include "port_coop_test.h"
#include "port_debug_actions.h"
#include "port_runtime_config.h"

extern void SetActiveSave(u32 idx);

#define COOP_TEST_MAX_PRESETS 64

static PortCoopTestPreset sPresets[COOP_TEST_MAX_PRESETS];
static int sPresetCount = -1; /* -1 = not loaded yet */
static char sPresetsPath[512];

static int sPending = -1;       /* preset to boot into from title / file select */
static int sWarpPending = -1;   /* preset to warp to once gameplay is live */
static unsigned int sWarpFiredAt;
static int sForceWarp;          /* F8 jump: warp even when already in the room */
static int sLogged = -1;        /* preset whose arrival has been logged */
static int sSkipResumeHint;     /* boot in progress: skip Ezlo's file-resume hint */

void Port_CoopTest_ReloadPresets(void) {
    sPresetCount = Port_CoopTest_LoadPresets(sPresets, COOP_TEST_MAX_PRESETS, sPresetsPath, sizeof(sPresetsPath));
}

static void EnsureLoaded(void) {
    if (sPresetCount < 0)
        Port_CoopTest_ReloadPresets();
}

int Port_CoopTest_PresetCount(void) {
    EnsureLoaded();
    return sPresetCount;
}

const PortCoopTestPreset* Port_CoopTest_Preset(int i) {
    EnsureLoaded();
    return (i >= 0 && i < sPresetCount) ? &sPresets[i] : NULL;
}

const char* Port_CoopTest_PresetsPath(void) {
    EnsureLoaded();
    return sPresetsPath;
}

/* Mid-game story state. Same globals the randomizer's story skip and
 * world-open settings set (port/rando/rando_runtime.c): intro done, Ezlo met
 * (portals work), Minish Woods / Hyrule Town / castle intro scenes seen,
 * Cloud Tops / Tingle / Lake Hylia gates open. */
static void ApplyMidGameFlags(void) {
    SetGlobalFlag(START);
    SetGlobalFlag(EZERO_1ST);
    SetGlobalFlag(TABIDACHI);
    SetGlobalFlag(OUTDOOR);
    SetGlobalFlag(ENTRANCE_0);
    SetLocalFlagByBankB(FLAG_BANK_1, MORI_00_KOBITO);
    SetLocalFlagByBankB(FLAG_BANK_1, MORI_ENTRANCE_1ST);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_01_ZELDA);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_WAKAGI_1);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_WAKAGI_2);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_WAKAGI_3);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_AKINDO);
    SetLocalFlagByBankB(FLAG_BANK_1, CASTLE_04_MEZAME);
    SetLocalFlagByBankB(FLAG_BANK_1, MACHI_01_DEMO);
    SetLocalFlagByBankB(FLAG_BANK_2, MHOUSE15_OP1ST);
    SetLocalFlagByBankB(FLAG_BANK_2, M_PRIEST_TALK);
    SetLocalFlagByBankB(FLAG_BANK_2, M_ELDER_TALK1ST);
    SetLocalFlagByBankB(FLAG_BANK_2, M_PRIEST_MOVE);
    SetLocalFlagByBankB(FLAG_BANK_2, KOBITO_MORI_1ST);
    SetLocalFlagByBankB(FLAG_BANK_5, LV1_0B_WALK);
    SetGlobalFlag(KUMOTATSUMAKI);
    SetGlobalFlag(WARP_EVENT_END);
    SetGlobalFlag(TINGLE_TALK1ST);
    SetGlobalFlag(MIZUKAKI_START);
}

static void ApplySaveState(const PortCoopTestPreset* p) {
    if (p->mid_game_flags)
        ApplyMidGameFlags();
    if (p->all_items)
        Port_DebugAction_GiveAllItems();
}

static int FindPreset(const char* name) {
    for (int i = 0; i < Port_CoopTest_PresetCount(); ++i) {
        if (strcmp(sPresets[i].name, name) == 0)
            return i;
    }
    return -1;
}

static void ListPresets(void) {
    fprintf(stderr, "[coop-test] presets in %s:\n", sPresetsPath);
    for (int i = 0; i < sPresetCount; ++i)
        fprintf(stderr, "[coop-test]   %-12s %s\n", sPresets[i].name, sPresets[i].description);
}

static int InGameplay(void) {
    return gMain.task == TASK_GAME && gRoomControls.camera_target != NULL;
}

static int InPresetRoom(const PortCoopTestPreset* p) {
    unsigned char a = p->area, r = p->room;
    unsigned short x = p->x;
    Port_DebugAction_WarpEntry(&a, &r, &x);
    return (gRoomControls.area == p->area && gRoomControls.room == p->room) ||
           (gRoomControls.area == a && gRoomControls.room == r);
}

/* File select: replace slot 0 (in memory) with a fresh save that starts in
 * the preset room, then enter the game. ResetSaveFile() is not used: on the
 * PC port it also wipes slot 0's randomizer sidecar on disk. */
static void BootIntoPreset(int i, unsigned int frame) {
    const PortCoopTestPreset* p = &sPresets[i];
    SaveFile* sv = &gFileSelectState.saves[0];
    unsigned char a = p->area, r = p->room;
    unsigned short x = p->x;
    Port_DebugAction_WarpEntry(&a, &r, &x);

    memset(sv, 0, sizeof(*sv));
    sv->msg_speed = 1;
    sv->brightness = 1;
    sv->stats.health = 24;
    sv->stats.maxHealth = 24;
    sv->initialized = 1;
    memcpy(sv->name, "COOP", 4);
    sv->saved_status.area_next = a;
    sv->saved_status.room_next = r;
    sv->saved_status.start_pos_x = (s16)x;
    sv->saved_status.start_pos_y = (s16)p->y;
    sv->saved_status.layer = p->layer;
    gFileSelectState.saveStatus[0] = 1;
    SetActiveSave(0);
    /* A randomizer seed bound to the real slot 0 must not apply here. */
    if (Rando_IsActive())
        Rando_Reset();
    ApplySaveState(p);
    SetTask(TASK_GAME);
    sSkipResumeHint = 1;
    sWarpPending = i; /* fallback if the save start lands elsewhere */
    sWarpFiredAt = 0;
    fprintf(stderr, "[coop-test] frame %u: booted preset \"%s\" -> area=0x%02x room=0x%02x (%u,%u) layer=%u\n",
            frame, p->name, p->area, p->room, p->x, p->y, p->layer);
}

int Port_CoopTest_Go(int i) {
    const PortCoopTestPreset* p = Port_CoopTest_Preset(i);
    if (!p)
        return 0;
    if (gMain.task == TASK_GAME) {
        if (!InGameplay())
            return 0;
        ApplySaveState(p);
        sWarpPending = i;
        sWarpFiredAt = 0;
        sForceWarp = 1;
        sLogged = -1;
        return 1;
    }
    sPending = i;
    sLogged = -1;
    return 1;
}

void Port_CoopTest_Tick(unsigned int frame) {
    static int envRead = 0;
    if (!envRead) {
        envRead = 1;
        const char* env = getenv("TMC_COOP_TEST");
        if (env && *env) {
            int i = FindPreset(env);
            if (i < 0) {
                fprintf(stderr, "[coop-test] unknown preset \"%s\" — starting normally\n", env);
                ListPresets();
            } else {
                sPending = i;
                fprintf(stderr, "[coop-test] preset \"%s\": %s\n", sPresets[i].name, sPresets[i].description);
            }
        }
    }

    if (sPending >= 0) {
        if (gMain.task == TASK_TITLE && frame >= 30 && (frame & 0xF) < 3)
            Port_Config_TestForceEdge(PORT_INPUT_START);
        if (gMain.task == TASK_FILE_SELECT && frame > 60) {
            BootIntoPreset(sPending, frame);
            sPending = -1;
        }
        return;
    }

    /* USA: the first room after a file load spawns MiscManager type 15
     * (CreateMiscManager), which holds SELECT to make Ezlo recite the story
     * hint. gRoomTransition is cleared on load, so mark the manager as
     * already created until the room goes live: test rooms start playable. */
    if (sSkipResumeHint && gMain.task == TASK_GAME) {
        gRoomTransition.field31 = 1;
        if (gMain.state == GAMETASK_MAIN && gMain.substate == GAMEMAIN_UPDATE)
            sSkipResumeHint = 0;
    }

    if (sWarpPending < 0)
        return;
    const PortCoopTestPreset* p = &sPresets[sWarpPending];
    if (gMain.task != TASK_GAME)
        return;
    if (!sForceWarp && InPresetRoom(p) && gMain.substate == GAMEMAIN_UPDATE && sWarpFiredAt == 0) {
        /* The save start already put Link in the room. */
        sWarpPending = -1;
    } else if (InPresetRoom(p) && sWarpFiredAt != 0) {
        sWarpPending = -1;
    } else if ((sForceWarp || !InPresetRoom(p)) && frame % 20 == 0 && (sWarpFiredAt == 0 || frame - sWarpFiredAt > 180) &&
               Port_DebugAction_Warp(p->area, p->room, p->x, p->y, p->layer) == 1) {
        /* Re-firing mid-transition restarts the fade; give it 3s to land. */
        sWarpFiredAt = frame ? frame : 1;
        sForceWarp = 0;
        fprintf(stderr, "[coop-test] frame %u: warp fired -> area=0x%02x room=0x%02x\n", frame, p->area, p->room);
    }
    if (sWarpPending < 0 && sLogged != (int)(p - sPresets)) {
        sLogged = (int)(p - sPresets);
        fprintf(stderr, "[coop-test] frame %u: in room area=0x%02x room=0x%02x player=(%d,%d)\n", frame,
                (unsigned)gRoomControls.area, (unsigned)gRoomControls.room,
                (int)(gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x),
                (int)(gPlayerEntity.base.y.HALF.HI - gRoomControls.origin_y));
    }
}
