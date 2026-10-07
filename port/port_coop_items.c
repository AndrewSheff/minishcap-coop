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
 * Player 2 has no pause menu (START is player 1's): holding its L (Y key)
 * and tapping B (U) or A (O) steps that slot through the owned items, in
 * pause-menu order, with a toast naming the item.
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "item.h"
#include "itemMetaData.h"
#include "save.h"
#include "player.h"
#include "pauseMenu.h"
#include "port_coop.h"

extern u32 GetInventoryValue(u32 item);
extern void Port_DebugMenu_ToastFromExternal(const char* msg);

#define COOP_FILENAME "tmc.coop"
static const char kMagic[6] = { 'T', 'M', 'C', 'O', 'P', '1' };

static u8 sEquip[2];     /* player 2's SLOT_A, SLOT_B */
static u8 sSavedP1[2];   /* player 1's, while player 2's are swapped in */
static int sLoaded;
static int sSwapped;

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

/* The pause-menu grid: owned equippable items, one per menu slot (an
 * upgrade replaces its base item, as in the menu). Returns the count. */
static int BuildGrid(u8* out) {
    u8 grid[MENU_SLOT_COUNT];
    u32 item;
    int i, n = 0;
    memset(grid, 0, sizeof(grid));
    for (item = ITEM_SMITH_SWORD; item < ITEM_BOTTLE_EMPTY; item++) {
        u32 slot = gItemMetaData[item].menuSlot;
        if (GetInventoryValue(item) != 1 || slot >= MENU_SLOT_COUNT)
            continue;
        if (item == ITEM_OCARINA)
            continue; /* warps the world: player 1's */
        grid[slot] = (u8)((item == ITEM_LANTERN_OFF || item == ITEM_LANTERN_ON) ? gPauseMenuOptions.unk15 : item);
    }
    for (i = 0; i < MENU_SLOT_COUNT; i++)
        if (grid[i] != 0)
            out[n++] = grid[i];
    return n;
}

static int Owned(u8 item) {
    u8 grid[MENU_SLOT_COUNT];
    int n = BuildGrid(grid), i;
    for (i = 0; i < n; i++)
        if (grid[i] == item)
            return 1;
    return 0;
}

static const char* ItemName(u8 id) {
    switch (id) {
        case ITEM_NONE: return "(пусто)";
        case ITEM_SMITH_SWORD: case ITEM_GREEN_SWORD: case ITEM_RED_SWORD: case ITEM_BLUE_SWORD:
        case ITEM_FOURSWORD: return "меч";
        case ITEM_BOMBS: return "бомбы";
        case ITEM_REMOTE_BOMBS: return "бомбы с пультом";
        case ITEM_BOW: return "лук";
        case ITEM_LIGHT_ARROW: return "лук (свет)";
        case ITEM_BOOMERANG: return "бумеранг";
        case ITEM_MAGIC_BOOMERANG: return "волшебный бумеранг";
        case ITEM_SHIELD: return "щит";
        case ITEM_MIRROR_SHIELD: return "зеркальный щит";
        case ITEM_LANTERN_OFF: case ITEM_LANTERN_ON: return "фонарь";
        case ITEM_GUST_JAR: return "кувшин ветра";
        case ITEM_PACCI_CANE: return "трость Пачи";
        case ITEM_MOLE_MITTS: return "кротовые рукавицы";
        case ITEM_ROCS_CAPE: return "плащ Рок";
        case ITEM_PEGASUS_BOOTS: return "сапоги Пегаса";
        case ITEM_FIRE_ROD: return "огненный жезл";
        case ITEM_BOTTLE1: case ITEM_BOTTLE2: case ITEM_BOTTLE3: case ITEM_BOTTLE4: return "бутылка";
        default: return "предмет";
    }
}

/* Step slot (0 = A, 1 = B) to the next owned item, skipping the one in
 * player 2's other slot (as the menu would swap them). */
static void Cycle(int slot) {
    u8 grid[MENU_SLOT_COUNT];
    int n = BuildGrid(grid), i, cur = -1;
    char msg[96];
    if (n == 0)
        return;
    for (i = 0; i < n; i++)
        if (grid[i] == sEquip[slot])
            cur = i;
    for (i = 1; i <= n; i++) {
        u8 cand = grid[(cur + i + n) % n];
        if (cand != sEquip[1 - slot] || n == 1) {
            sEquip[slot] = cand;
            break;
        }
    }
    Save();
    snprintf(msg, sizeof(msg), "Игрок 2 · %s: %s", slot == 0 ? "A" : "B", ItemName(sEquip[slot]));
    Port_DebugMenu_ToastFromExternal(msg);
}

unsigned short Port_CoopItems_FilterKeys(unsigned short held, unsigned short newKeys) {
    if (!sLoaded)
        Load();
    /* L is player 2's item-select modifier, never a game input (kinstone
     * fusion is player 1's). With L held, A / B pick items instead. */
    if (held & L_BUTTON) {
        if (newKeys & B_BUTTON)
            Cycle(1);
        if (newKeys & A_BUTTON)
            Cycle(0);
        held &= (unsigned short)~(A_BUTTON | B_BUTTON);
    }
    return (unsigned short)(held & ~L_BUTTON);
}

void Port_CoopItems_Enter(void) {
    if (!sLoaded)
        Load();
    if (sSwapped)
        return;
    sSwapped = 1;
    sSavedP1[0] = gSave.stats.equipped[SLOT_A];
    sSavedP1[1] = gSave.stats.equipped[SLOT_B];
    gSave.stats.equipped[SLOT_A] = Owned(sEquip[0]) ? sEquip[0] : ITEM_NONE;
    gSave.stats.equipped[SLOT_B] = Owned(sEquip[1]) ? sEquip[1] : ITEM_NONE;
}

void Port_CoopItems_Leave(void) {
    if (!sSwapped)
        return;
    sSwapped = 0;
    /* Keep what player 2's code changed (lantern on/off, emptied bottle). */
    if (gSave.stats.equipped[SLOT_A] != ITEM_NONE)
        sEquip[0] = gSave.stats.equipped[SLOT_A];
    if (gSave.stats.equipped[SLOT_B] != ITEM_NONE)
        sEquip[1] = gSave.stats.equipped[SLOT_B];
    gSave.stats.equipped[SLOT_A] = sSavedP1[0];
    gSave.stats.equipped[SLOT_B] = sSavedP1[1];
}

unsigned char Port_CoopItems_Equipped(int slot) {
    if (!sLoaded)
        Load();
    return (slot == 0 || slot == 1) ? sEquip[slot] : 0;
}
