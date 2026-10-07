/*
 * port/port_coop_items.c — coop player 2's own A/B items.
 *
 * Both players share one inventory (gSave); each has its own A/B choice.
 * Player 1's lives in the save as always (gSave.stats.equipped). Player 2's
 * lives here and in a sidecar file, tmc.coop, next to tmc.sav — the save
 * format is untouched. While player 2's code runs (port_coop.c EnterP2 /
 * LeaveP2) its choice is swapped into gSave.stats.equipped, so every item
 * site reads player 2's items with no change in src/; ammo and bombs come
 * from the shared counts.
 *
 * Choosing: player 2 has its own Start. Pressed in play, it opens the game's
 * own pause menu in "player 2 mode": the menu is driven by player 2's buttons
 * (port_bios.c), shows and edits player 2's A/B (its choice stays in
 * gSave.stats.equipped while the menu is open) and names items from the
 * ROM. On close player 1's choice is put back. Saving from that menu writes
 * player 1's choice into the save file (Port_CoopItems_SaveImage).
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "game.h"
#include "item.h"
#include "save.h"
#include "player.h"
#include "port_coop.h"

#define COOP_FILENAME "tmc.coop"
static const char kMagic[6] = { 'T', 'M', 'C', 'O', 'P', '1' };

static u8 sEquip[2];   /* player 2's SLOT_A, SLOT_B */
static u8 sSavedP1[2]; /* player 1's, while player 2's are swapped in */
static int sLoaded;
static int sSwapped;
/* Player 2's pause menu: 1 = opened, waiting for the menu to start;
 * 2 = the menu is running. */
static int sMenuP2;
static int sMenuWait;

static void Save(void) {
    FILE* f = fopen(COOP_FILENAME ".tmp", "wb");
    int ok;
    if (!f)
        return;
    ok = fwrite(kMagic, 1, sizeof(kMagic), f) == sizeof(kMagic) && fwrite(sEquip, 1, sizeof(sEquip), f) == sizeof(sEquip);
    if (fclose(f) != 0)
        ok = 0;
    if (ok)
        ok = rename(COOP_FILENAME ".tmp", COOP_FILENAME) == 0;
    if (!ok) {
        remove(COOP_FILENAME ".tmp");
        fprintf(stderr, "[coop] could not write %s\n", COOP_FILENAME);
    }
}

static void Load(void) {
    FILE* f;
    char magic[sizeof(kMagic)];
    sLoaded = 1;
    f = fopen(COOP_FILENAME, "rb");
    if (!f) {
        /* First time: start with player 1's choice. */
        sEquip[0] = gSave.stats.equipped[SLOT_A];
        sEquip[1] = gSave.stats.equipped[SLOT_B];
        return;
    }
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) || memcmp(magic, kMagic, sizeof(magic)) != 0 ||
        fread(sEquip, 1, sizeof(sEquip), f) != sizeof(sEquip)) {
        sEquip[0] = gSave.stats.equipped[SLOT_A];
        sEquip[1] = gSave.stats.equipped[SLOT_B];
    }
    fclose(f);
}

/* Is item (as stored in an equip slot) still owned? Bottles and the
 * lantern change id with their contents / state, so they count by kind. */
static int Owned(u8 item) {
    extern u32 GetInventoryValue(u32 item);
    if (item == ITEM_NONE)
        return 1;
    if (item == ITEM_LANTERN_ON)
        item = ITEM_LANTERN_OFF;
    return GetInventoryValue(item) == 1;
}

void Port_CoopItems_Enter(void) {
    if (!sLoaded)
        Load();
    if (sSwapped || sMenuP2)
        return;
    sSwapped = 1;
    sSavedP1[0] = gSave.stats.equipped[SLOT_A];
    sSavedP1[1] = gSave.stats.equipped[SLOT_B];
    gSave.stats.equipped[SLOT_A] = Owned(sEquip[0]) ? sEquip[0] : ITEM_NONE;
    gSave.stats.equipped[SLOT_B] = Owned(sEquip[1]) ? sEquip[1] : ITEM_NONE;
}

void Port_CoopItems_Leave(void) {
    if (!sSwapped || sMenuP2)
        return; /* player 2's menu keeps player 2's choice in place */
    sSwapped = 0;
    /* Keep what player 2's code changed (lantern on/off, emptied bottle). */
    sEquip[0] = gSave.stats.equipped[SLOT_A];
    sEquip[1] = gSave.stats.equipped[SLOT_B];
    gSave.stats.equipped[SLOT_A] = sSavedP1[0];
    gSave.stats.equipped[SLOT_B] = sSavedP1[1];
}

/* InitPauseMenu hook (src/player.c CheckInitPauseMenu): player 2 pressed its
 * Start during its own update. */
void Port_CoopItems_OnPauseMenuOpen(void) {
    if (Port_Coop_CurrentPlayer() != 1 || !sSwapped)
        return;
    sMenuP2 = 1;
    sMenuWait = 60;
}

int Port_CoopItems_MenuIsP2(void) {
    return sMenuP2 != 0;
}

/* Once per frame (Port_Coop_FrameTick, between frames): notice the menu
 * starting and closing; on close store player 2's pick, restore player 1's. */
void Port_CoopItems_FrameTick(void) {
    const int inGame = gMain.task == TASK_GAME && gMain.state == GAMETASK_MAIN && gMain.substate == GAMEMAIN_UPDATE;
    if (!sMenuP2)
        return;
    if (sMenuP2 == 1) {
        if (!inGame) {
            sMenuP2 = 2; /* the menu took over */
        } else if (--sMenuWait <= 0) {
            sMenuP2 = 2; /* never opened: close it out below */
        } else {
            return;
        }
    }
    if (!inGame && gMain.task == TASK_GAME)
        return; /* still in the menu */
    sMenuP2 = 0;
    sSwapped = 0;
    sEquip[0] = gSave.stats.equipped[SLOT_A];
    sEquip[1] = gSave.stats.equipped[SLOT_B];
    gSave.stats.equipped[SLOT_A] = sSavedP1[0];
    gSave.stats.equipped[SLOT_B] = sSavedP1[1];
    Save();
}

/* WriteSaveFile hook: saving from player 2's menu must not store player
 * 2's A/B as player 1's. Returns 1 and fills *out with the image to write. */
int Port_CoopItems_SaveImage(const SaveFile* in, SaveFile* out) {
    if (!sMenuP2 && !sSwapped)
        return 0;
    *out = *in;
    out->stats.equipped[SLOT_A] = sSavedP1[0];
    out->stats.equipped[SLOT_B] = sSavedP1[1];
    return 1;
}

unsigned char Port_CoopItems_Equipped(int slot) {
    if (!sLoaded)
        Load();
    if (slot != 0 && slot != 1)
        return 0;
    if (sSwapped || sMenuP2)
        return gSave.stats.equipped[slot]; /* player 2's are in place */
    return Owned(sEquip[slot]) ? sEquip[slot] : ITEM_NONE;
}
