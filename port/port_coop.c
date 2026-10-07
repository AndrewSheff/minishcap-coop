/*
 * port/port_coop.c — local coop, stage 1: a second Link.
 *
 * Player 2 is gPortPlayerEntities[1], a real PLAYER entity in gEntityLists[1].
 * ram_UpdateEntities dispatches it to the ordinary PlayerUpdate like player 1;
 * around that call (and around every entity player 2 spawned) the
 * "current player" pointers behind gPlayerEntity / gPlayerState /
 * gActiveItems / gPlayerClones / gPossibleInteraction / gCarriedEntity
 * (include/port_player_ctx.h) point at player 2's storage, and gInput holds
 * player 2's buttons. The rest of the world keeps seeing player 1, which is
 * the stage-1 contract: enemies, buttons and the camera ignore player 2.
 *
 * Shared state player code writes but that belongs to player 1 is restored
 * after each player-2 update: the camera target, the R-button HUD hint.
 *
 * Lifecycle (Port_Coop_FrameTick, between frames):
 *   - join: any player-2 button (config coop_p2_join) or Port_Coop_RequestJoin
 *     spawns player 2 on player 1 through the game's own InitializePlayer;
 *   - player 1 enters another room: player 2 is moved next to player 1;
 *   - an area load erases the entity lists: player 2 is spawned again.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "main.h"
#include "game.h"
#include "common.h"
#include "entity.h"
#include "player.h"
#include "room.h"
#include "asm.h"
#include "structures.h"
#include "save.h"
#include "message.h"
#include "enemy.h"
#include "port_coop.h"
#include "port_runtime_config.h"

extern PlayerEntity gPortPlayerEntities[PORT_MAX_PLAYERS];
extern PlayerState gPortPlayerStates[PORT_MAX_PLAYERS];
extern Entity* gPortPlayerClones[PORT_MAX_PLAYERS][3];
extern ItemBehavior gPortActiveItems[PORT_MAX_PLAYERS][MAX_ACTIVE_ITEMS];
extern PossibleInteraction gPortPossibleInteractions[PORT_MAX_PLAYERS];
extern CarriedEntity gPortCarriedEntities[PORT_MAX_PLAYERS];
extern PlayerEntity* gPortCurPlayerEntity;
extern PlayerState* gPortCurPlayerState;
extern Entity* (*gPortCurPlayerClones)[3];
extern ItemBehavior (*gPortCurActiveItems)[MAX_ACTIVE_ITEMS];
extern PossibleInteraction* gPortCurPossibleInteraction;
extern CarriedEntity* gPortCurCarriedEntity;
extern GenericEntity gEntities[MAX_ENTITIES];
extern GenericEntity gAuxPlayerEntities[MAX_AUX_PLAYER_ENTITIES];
extern void UpdatePlayerInput(void);
extern UpdateContext gUpdateContext;
extern void UnloadOBJPalette2(Entity* ent);
extern void sub_0805EC60(Entity* ent);
extern void sub_08017744(Entity* ent);
extern void UnloadHitbox(Entity* ent);

#define P2 (&gPortPlayerEntities[1].base)

static int sCurrent;                 /* 0 = player 1, 1 = player 2 */
static int sP2Active;                /* player 2 has joined */
static int sJoinRequested;
static unsigned short sP2Held, sP2PrevHeld;
static unsigned char sLastArea = 0xff, sLastRoom = 0xff;
static int sHidden;                  /* player 1 is busy: player 2 is put away */
static u8 sHiddenDraw;               /* player 2's draw mode when put away */
static int sSpawnSettle;             /* frames until a fresh spawn has taken its form */
static s16 sP2PrevX, sP2PrevY;       /* player 2's position before its update (leash) */

/* The leash: how far apart the two Links may be — a native 240x160 screen
 * minus a margin. Game logic, not the camera: the same on any view size. */
#define LEASH_X (240 - 48)
#define LEASH_Y (160 - 48)
/* Player 1 walked further than this away: player 2 is brought along. */
#define LEASH_PULL_X (LEASH_X + 24)
#define LEASH_PULL_Y (LEASH_Y + 24)

/* Owner of each pooled entity: 1 = spawned while player 2 was current. */
static unsigned char sOwnerEntities[MAX_ENTITIES];
static unsigned char sOwnerAux[MAX_AUX_PLAYER_ENTITIES];
/* Enemies: the player each one is busy with (0 / 1, 0xff = not chosen yet). */
static unsigned char sFocus[MAX_ENTITIES];
static int sFocusContext; /* an enemy / enemy projectile is updating */

static unsigned char* OwnerSlot(const void* entity) {
    const GenericEntity* g = (const GenericEntity*)entity;
    if (g >= &gEntities[0] && g < &gEntities[MAX_ENTITIES])
        return &sOwnerEntities[g - gEntities];
    if (g >= &gAuxPlayerEntities[0] && g < &gAuxPlayerEntities[MAX_AUX_PLAYER_ENTITIES])
        return &sOwnerAux[g - gAuxPlayerEntities];
    return NULL;
}

static void SetCurrent(int idx) {
    sCurrent = idx;
    gPortCurPlayerEntity = &gPortPlayerEntities[idx];
    gPortCurPlayerState = &gPortPlayerStates[idx];
    gPortCurPlayerClones = &gPortPlayerClones[idx];
    gPortCurActiveItems = &gPortActiveItems[idx];
    gPortCurPossibleInteraction = &gPortPossibleInteractions[idx];
    gPortCurCarriedEntity = &gPortCarriedEntities[idx];
}

u32 Port_Coop_PlayerGfxSlot(Entity* player) {
    return player == P2 ? 0 /* any free slot */ : 2;
}

int Port_Coop_CurrentPlayer(void) {
    return sCurrent;
}

int Port_Coop_IsP2Active(void) {
    return sP2Active;
}

void Port_Coop_RequestJoin(void) {
    sJoinRequested = 1;
}

void Port_Coop_SetP2Keys(unsigned short heldKeys) {
    sP2PrevHeld = sP2Held;
    sP2Held = heldKeys;
}

void Port_Coop_OnEntityAppended(void* entity) {
    unsigned char* owner = OwnerSlot(entity);
    const GenericEntity* g = (const GenericEntity*)entity;
    /* Spawned by player 2 (sword, arrows...) -> player 2's. Not when an enemy
     * merely focused on player 2 spawned it: its shots stay the world's. */
    if (owner)
        *owner = (unsigned char)(sFocusContext ? 0 : sCurrent);
    if (g >= &gEntities[0] && g < &gEntities[MAX_ENTITIES])
        sFocus[g - gEntities] = 0xff;
}

/* ---- player-2 context ------------------------------------------------- */

typedef struct {
    Input input;
    Entity* cameraTarget;
    u8 rActionInteractTile, rActionGrabbing, rActionPlayerState;
    /* Shared timers HandlePlayerLife ticks once per player per frame: player
     * 2's tick is undone so charms / picolytes last as long as in 1P. */
    u8 charm, picolyteType;
    u16 charmTimer, picolyteTimer;
    /* World freeze: player code raises the event priority for its own
     * events (item get, death, captured). Player 2's must not stop the
     * world for player 1; a freeze player 1 raised still stops player 2. */
    u8 eventPriority, entPriority;
    /* The area's minish-portal state (gArea.portal_*) is computed by the
     * portal manager from player 1. Read by player 2's code it made player 2
     * jump onto the stump and keep the portal pose: player 2 sees no portal. */
    u8 portalMode;
} SavedP1;

static SavedP1 sSaved;
static Input sP2Input;

static void EnterP2(void) {
    sSaved.input = gInput;
    sSaved.cameraTarget = gRoomControls.camera_target;
    sSaved.rActionInteractTile = gHUD.rActionInteractTile;
    sSaved.rActionGrabbing = gHUD.rActionGrabbing;
    sSaved.rActionPlayerState = gHUD.rActionPlayerState;
    sSaved.charm = gSave.stats.charm;
    sSaved.picolyteType = gSave.stats.picolyteType;
    sSaved.charmTimer = gSave.stats.charmTimer;
    sSaved.picolyteTimer = gSave.stats.picolyteTimer;
    sSaved.eventPriority = gPriorityHandler.event_priority;
    sSaved.entPriority = gPriorityHandler.ent_priority;
    sSaved.portalMode = gArea.portal_mode;
    gArea.portal_mode = 0;
    gInput = sP2Input;
    SetCurrent(1);
    Port_CoopItems_Enter(); /* player 2's A/B choice in gSave.stats.equipped */
}

static void LeaveP2(void) {
    Port_CoopItems_Leave();
    SetCurrent(0);
    gInput = sSaved.input;
    /* Player code points the camera at "the player" (InitializePlayer, room
     * exits, ...): during player 2's turn that means player 2. Stage 1: the
     * camera stays on player 1. */
    if (gRoomControls.camera_target == P2)
        gRoomControls.camera_target = sSaved.cameraTarget;
    gHUD.rActionInteractTile = sSaved.rActionInteractTile;
    gHUD.rActionGrabbing = sSaved.rActionGrabbing;
    gHUD.rActionPlayerState = sSaved.rActionPlayerState;
    gSave.stats.charm = sSaved.charm;
    gSave.stats.picolyteType = sSaved.picolyteType;
    gSave.stats.charmTimer = sSaved.charmTimer;
    gSave.stats.picolyteTimer = sSaved.picolyteTimer;
    gPriorityHandler.event_priority = sSaved.eventPriority;
    gPriorityHandler.ent_priority = sSaved.entPriority;
    gArea.portal_mode = sSaved.portalMode;
}

static int P2Linked(void);
static int ChooseFocus(const Entity* e, unsigned char* focus);
static int FocusManaged(const Entity* e);

int Port_Coop_BeginEntityUpdate(void* entity) {
    if (!sP2Active || sCurrent != 0)
        return 0;
    if (entity == (void*)P2) {
        if (sHidden)
            return PORT_COOP_SKIP_UPDATE; /* put away while player 1 is busy */
        sP2PrevX = P2->x.HALF.HI;
        sP2PrevY = P2->y.HALF.HI;
        EnterP2();
        /* Player 1's UpdatePlayerInput ran at the top of UpdateEntities;
         * player 2's runs here, against player 2's gInput / gPlayerState. */
        UpdatePlayerInput();
        return 2;
    }
    {
        unsigned char* owner = OwnerSlot(entity);
        const GenericEntity* g = (const GenericEntity*)entity;
        if ((!owner || *owner == 0) && !sHidden && P2Linked() && g >= &gEntities[0] &&
            g < &gEntities[MAX_ENTITIES] && FocusManaged(&g->base)) {
            /* An enemy: while it updates, "the player" is the Link it is
             * busy with — the one it chases, hits, grabs or swallows. */
            sFocusContext = 1;
            if (ChooseFocus(&g->base, &sFocus[g - gEntities]) == 1)
                Port_Coop_PushPlayer(1);
            return PORT_COOP_FOCUS;
        }
        if (owner && *owner == 1) {
            /* ...unless player 1 is the one holding it now. */
            int i;
            if (gPortPlayerStates[0].heldObject != 0)
                for (i = 0; i < MAX_ACTIVE_ITEMS; i++)
                    if (gPortActiveItems[0][i].field_0x18 == (Entity*)entity) {
                        *owner = 0;
                        return 0;
                    }
            EnterP2();
            return 1;
        }
    }
    return 0;
}

static int Apart(s32 x, s32 y, s32 limX, s32 limY) {
    const Entity* p1 = &gPortPlayerEntities[0].base;
    s32 dx = x - p1->x.HALF.HI, dy = y - p1->y.HALF.HI;
    return dx > limX || dx < -limX || dy > limY || dy < -limY;
}

void Port_Coop_EndEntityUpdate(int token) {
    if (token == PORT_COOP_FOCUS) {
        sFocusContext = 0;
        if (sCurrent != 0)
            Port_Coop_PopPlayer(0);
        /* Enemy code may point the camera at "the player": keep it on 1. */
        if (gRoomControls.camera_target == P2)
            gRoomControls.camera_target = &gPortPlayerEntities[0].base;
        return;
    }
    if (token == 2) {
        /* Leash: player 2's own step may not take it further than a screen
         * from player 1 (it stops at the edge, like a wall). */
        Entity* p2 = P2;
        if (Apart(p2->x.HALF.HI, p2->y.HALF.HI, LEASH_X, LEASH_Y) &&
            !Apart(sP2PrevX, sP2PrevY, LEASH_X, LEASH_Y)) {
            p2->x.HALF.HI = sP2PrevX;
            p2->y.HALF.HI = sP2PrevY;
        }
    }
    if (token == 2 && gPortPlayerStates[1].heldObject != 0) {
        /* Player 2 lifted or grabbed something (pot, statue...): from now on
         * that object updates as player 2's, so its "am I still held" checks
         * read player 2's state. Done here, before the object's own update
         * this frame. */
        int i;
        for (i = 0; i < MAX_ACTIVE_ITEMS; i++) {
            unsigned char* owner = OwnerSlot(gPortActiveItems[1][i].field_0x18);
            if (owner)
                *owner = 1;
        }
    }
    if (token)
        LeaveP2();
}

/* ---- the world sees player 2 (stage 2) ---------------------------------- */

static int P2Linked(void);

/* Player 2 itself, or an entity it spawned (sword, arrows, effects). */
int Port_Coop_IsPlayer2Entity(const void* entity) {
    if (!sP2Active || entity == NULL)
        return 0;
    if (entity == (const void*)P2)
        return 1;
    {
        unsigned char* owner = OwnerSlot(entity);
        return owner != NULL && *owner == 1;
    }
}

/* Point the "current player" at idx (pointers only — gInput and the
 * player-1 HUD / camera stay as they are). Returns the previous index for
 * Port_Coop_PopPlayer. */
int Port_Coop_PushPlayer(int idx) {
    const int prev = sCurrent;
    SetCurrent(idx);
    return prev;
}

void Port_Coop_PopPlayer(int prev) {
    if (prev >= 0)
        SetCurrent(prev);
}

/* World objects that look for "the player" (floor buttons, pressure
 * plates) check the other player too: -1 when there is none, otherwise the
 * index to pass back to Port_Coop_PopPlayer. */
int Port_Coop_PushOtherPlayer(void) {
    if (!sP2Active || sHidden || !P2Linked())
        return -1;
    return Port_Coop_PushPlayer(1 - sCurrent);
}

/* Collision between two entities: run the handler as player 2 when either
 * side is player 2's (its damage, knockback and iframes land on player 2).
 * Returns the token for Port_Coop_EndCollision. */
int Port_Coop_BeginCollision(const void* a, const void* b) {
    if (sCurrent == 0 && (Port_Coop_IsPlayer2Entity(a) || Port_Coop_IsPlayer2Entity(b)))
        return Port_Coop_PushPlayer(1) + 1;
    return 0;
}

void Port_Coop_EndCollision(int token) {
    if (token)
        Port_Coop_PopPlayer(token - 1);
}

/* Enemy target filters (src/code_08049DF4.c): run the filter for each
 * player and keep the eligible one nearest to the enemy being updated. */
Entity* Port_Coop_PickEnemyTarget(Entity* (*filter)(void)) {
    extern Entity* gEnemyTarget;
    const Entity* self = gUpdateContext.current_entity;
    Entity* best = NULL;
    s32 bestD2 = 0x7fffffff;
    int prev, i;

    if (!sP2Active || sHidden || !P2Linked() || sFocusContext)
        return filter(); /* an enemy's focus player decides (EndEntityUpdate) */
    prev = sCurrent;
    for (i = 0; i < PORT_MAX_PLAYERS; i++) {
        Entity* t;
        SetCurrent(i);
        gEnemyTarget = NULL;
        t = filter();
        if (t != NULL) {
            s32 dx = self ? t->x.HALF.HI - self->x.HALF.HI : (i == prev ? 0 : 0x7fff);
            s32 dy = self ? t->y.HALF.HI - self->y.HALF.HI : 0;
            s32 d2 = dx * dx + dy * dy;
            if (d2 < bestD2) {
                bestD2 = d2;
                best = t;
            }
        }
    }
    SetCurrent(prev);
    gEnemyTarget = best;
    return best;
}

/* Player 2's tunic: its OBJ palette ids are the game's player palette ids
 * (normal 0x16, charms 0x17-0x19, hurt blink 0x1a-0x1b) + this offset; the
 * colours are the game's own palette with the green recoloured red. */
#define COOP_P2_PALETTE_OFFSET 0x400

unsigned int Port_Coop_PlayerPalette(unsigned int paletteId) {
    return sCurrent == 1 ? paletteId + COOP_P2_PALETTE_OFFSET : paletteId;
}

int Port_Coop_IsP2Palette(unsigned int paletteId) {
    return paletteId >= COOP_P2_PALETTE_OFFSET && paletteId < COOP_P2_PALETTE_OFFSET + 0x100;
}

unsigned int Port_Coop_P2PaletteBase(unsigned int paletteId) {
    return paletteId - COOP_P2_PALETTE_OFFSET;
}

/* Recolour one loaded 16-colour OBJ palette (GBA BGR555): green-dominant
 * colours (tunic, cap) swap their red and green channels. */
void Port_Coop_TintP2Palette(unsigned short* colors) {
    int i;
    for (i = 1; i < 16; i++) {
        u16 c = colors[i];
        u16 r = c & 0x1f, g = (c >> 5) & 0x1f, b = (c >> 10) & 0x1f;
        if (g > r + 2 && g > b + 2)
            colors[i] = (u16)(g | (r << 5) | (b << 10));
    }
}

/* ---- enemies that use "the player" directly (stage 4) ------------------- */

/* Can an enemy go after player idx right now? */
static int Targetable(int idx) {
    const PlayerState* ps = &gPortPlayerStates[idx];
    if (idx == 1 && (!sP2Active || sHidden))
        return 0;
    return ps->killed == 0 &&
           !(ps->flags & (PL_DROWNING | PL_USE_PORTAL | PL_HIDDEN | PL_FALLING | PL_IN_HOLE | PL_CLIMBING));
}

static s32 Dist2(const Entity* a, const Entity* b) {
    s32 dx = a->x.HALF.HI - b->x.HALF.HI, dy = a->y.HALF.HI - b->y.HALF.HI;
    return dx * dx + dy * dy;
}

/* Which player this enemy deals with: sticky while it holds that player
 * (captured / swallowed / beetles on it), otherwise the nearer one — with a
 * margin so it doesn't flip-flop between two Links at similar range. */
static int ChooseFocus(const Entity* e, unsigned char* focus) {
    int cur = *focus;
    if (cur > 1 || !Targetable(cur)) {
        cur = Targetable(0) ? 0 : (Targetable(1) ? 1 : 0);
    } else {
        const PlayerState* ps = &gPortPlayerStates[cur];
        const int held = (ps->flags & (PL_CAPTURED | PL_MOLDWORM_CAPTURED)) || ps->attachedBeetleCount != 0;
        const int other = 1 - cur;
        if (!held && Targetable(other) &&
            Dist2(e, &gPortPlayerEntities[other].base) + 24 * 24 < Dist2(e, &gPortPlayerEntities[cur].base))
            cur = other;
    }
    *focus = (unsigned char)cur;
    return cur;
}

/* Non-boss enemies and enemy projectiles follow their focus player. Boss
 * fights stay player 1's for now. */
static int FocusManaged(const Entity* e) {
    if (e->kind == ENEMY)
        return !(((const Enemy*)e)->enemyFlags & EM_FLAG_BOSS);
    if (e->kind == PROJECTILE) {
        const Entity* parent = e->parent;
        return !(parent != NULL && parent->kind == ENEMY && (((const Enemy*)parent)->enemyFlags & EM_FLAG_BOSS));
    }
    return 0;
}

/* ---- camera (stage 3) ------------------------------------------------- */

/* Scroll1's follow point: the middle of the two Links while both play
 * freely; otherwise (player 2 away or put away, a cutscene, a scripted
 * camera target) the game's own target. Display only — no game logic reads
 * it, so different screens may frame differently. */
void Port_Coop_CameraFocus(const Entity* target, s32* x, s32* y) {
    const Entity* p1 = &gPortPlayerEntities[0].base;
    if (!sP2Active || sHidden || target != p1 || !P2Linked())
        return;
    if (gPortPlayerStates[0].controlMode != CONTROL_ENABLED)
        return;
    *x = (p1->x.HALF.HI + P2->x.HALF.HI) / 2;
    *y = (p1->y.HALF.HI + P2->y.HALF.HI) / 2;
}

/* ---- lifecycle -------------------------------------------------------- */

static int P2Linked(void) {
    LinkedList* list = &gEntityLists[1];
    Entity* e = list->first;
    int steps = 0;
    while (e != NULL && e != (Entity*)list && steps++ < 64) {
        if (e == P2)
            return 1;
        e = e->next;
    }
    return 0;
}

static void UnlinkP2(void) {
    Entity* e = P2;
    if (P2Linked()) {
        e->prev->next = e->next;
        e->next->prev = e->prev;
        gEntCount--;
    }
    e->prev = NULL;
    e->next = NULL;
}

/* Is (x, y) — world pixels — walkable on layer for a standing Link? */
static int Walkable(int x, int y, unsigned layer) {
    int tx = (x - (int)gRoomControls.origin_x) >> 4;
    int ty = (y - (int)gRoomControls.origin_y) >> 4;
    if (tx < 0 || ty < 0 || tx >= gRoomControls.width / 16 || ty >= gRoomControls.height / 16)
        return 0;
    return GetCollisionDataAtTilePos((u32)((tx & 0x3f) | ((ty & 0x3f) << 6)), layer ? layer : 1) == 0;
}

/* Where player 2 appears: one tile beside player 1 if walkable, else on top. */
static void SpotNextToP1(s16* x, s16* y) {
    static const s8 kOffsets[][2] = { { 16, 0 }, { -16, 0 }, { 0, 16 }, { 0, -16 } };
    const Entity* p1 = &gPortPlayerEntities[0].base;
    *x = p1->x.HALF.HI;
    *y = p1->y.HALF.HI;
    for (unsigned i = 0; i < sizeof(kOffsets) / sizeof(kOffsets[0]); ++i) {
        int nx = p1->x.HALF.HI + kOffsets[i][0], ny = p1->y.HALF.HI + kOffsets[i][1];
        if (Walkable(nx, ny, p1->collisionLayer)) {
            *x = (s16)nx;
            *y = (s16)ny;
            return;
        }
    }
}

/* Free what player 2's last spawn took (VRAM slot, palettes, sprite parts,
 * hitbox) before spawning it again in the same area — what DeleteEntity
 * does, minus unlinking the list node, which UnlinkP2 handles. After an
 * area load those pools were reset and must not be touched. */
static void ReleaseP2(void) {
    Entity* e = P2;
    if (!P2Linked())
        return;
    UnloadGFXSlots(e);
    UnloadOBJPalette(e);
    UnloadOBJPalette2(e);
    sub_0805EC60(e);
    sub_08017744(e);
    UnloadHitbox(e);
}

static void SpawnP2As(int minish) {
    const Entity* p1 = &gPortPlayerEntities[0].base;
    PlayerRoomStatus saved = gRoomTransition.player_status;
    s16 x, y;

    ReleaseP2();
    UnlinkP2();
    if (minish) {
        /* Minish paths are a tile wide: stand on player 1. */
        x = p1->x.HALF.HI;
        y = p1->y.HALF.HI;
    } else {
        SpotNextToP1(&x, &y);
    }
    gRoomTransition.player_status.spawn_type = minish ? PL_SPAWN_MINISH : PL_SPAWN_DEFAULT;
    gRoomTransition.player_status.start_pos_x = x;
    gRoomTransition.player_status.start_pos_y = y;
    gRoomTransition.player_status.start_anim = p1->animationState;
    gRoomTransition.player_status.layer = p1->collisionLayer;

    sP2Input = (Input){ 0 };
    EnterP2();
    InitializePlayer(); /* the game's own player spawn, against player 2's storage */
    LeaveP2();
    /* InitializePlayer re-rooted the player hitbox list on player 2. */
    RegisterPlayerHitbox();
    gRoomTransition.player_status = saved;

    sP2Active = 1;
    sHidden = 0;
    /* A minish spawn only sets PL_MINISH on player 2's first update(s);
     * don't compare sizes until it has (no re-spawn loop). */
    sSpawnSettle = 30;
    fprintf(stderr, "[coop] player 2 %s at (%d,%d) area=0x%02x room=0x%02x\n",
            minish ? "spawned (minish)" : "spawned", (int)(x - gRoomControls.origin_x),
            (int)(y - gRoomControls.origin_y), (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
}

static void SpawnP2(void) {
    SpawnP2As((gPortPlayerStates[0].flags & PL_MINISH) != 0);
}

/* Player 1 changed rooms within the area: bring player 2 along. */
static void FollowP1(void) {
    Entity* p2 = P2;
    const Entity* p1 = &gPortPlayerEntities[0].base;
    s16 x, y;
    if (gPortPlayerStates[0].flags & PL_MINISH) {
        x = p1->x.HALF.HI; /* minish paths are a tile wide */
        y = p1->y.HALF.HI;
    } else {
        SpotNextToP1(&x, &y);
    }
    p2->x.HALF.HI = x;
    p2->y.HALF.HI = y;
    p2->z.WORD = 0;
    p2->collisionLayer = p1->collisionLayer;
    UpdateSpriteForCollisionLayer(p2);
    fprintf(stderr, "[coop] player 2 follows into area=0x%02x room=0x%02x\n", (unsigned)gRoomControls.area,
            (unsigned)gRoomControls.room);
}

static int GameplayLive(void) {
    const Entity* p1 = &gPortPlayerEntities[0].base;
    return gMain.task == TASK_GAME && gMain.state == GAMETASK_MAIN && gMain.substate == GAMEMAIN_UPDATE &&
           gRoomControls.camera_target != NULL && gSave.stats.health != 0 && p1->kind == PLAYER;
}

void Port_Coop_FrameTick(void) {
    /* Player 2's gInput for this frame. */
    sP2Input.heldKeys = sP2Held;
    sP2Input.newKeys = (u16)(sP2Held & ~sP2PrevHeld);

    if (!GameplayLive()) {
        if (gMain.task != TASK_GAME) {
            sP2Active = 0; /* back at the title / file select */
            sLastArea = sLastRoom = 0xff;
        }
        return;
    }

    if (getenv("TMC_COOP_DEBUG") && (gMain.ticks % 10) == 0) {
        const Entity* e;
        const PlayerState* ps;
        e = &gPortPlayerEntities[0].base;
        ps = &gPortPlayerStates[0];
        fprintf(stderr, "[coop-dbg]   p1=(%d,%d) action=%u sub=%u ctl=%u in=%08x fs=%u gIn=%04x dir=%u flags=%08x prio=%u ev=%u/%u msg=%x spd=%d edir=%u z=%d ani=%u kb=%u\n",
                (int)(e->x.HALF.HI - gRoomControls.origin_x), (int)(e->y.HALF.HI - gRoomControls.origin_y),
                e->action, e->subAction, ps->controlMode, (unsigned)ps->playerInput.heldInput, ps->framestate,
                gInput.heldKeys, ps->direction, (unsigned)ps->flags, e->updatePriority, gPriorityHandler.event_priority,
                gPriorityHandler.ent_priority, (unsigned)gMessage.state, e->speed, e->direction, (int)e->z.HALF.HI,
                (unsigned)ps->animation, e->knockbackDuration);
    }
    if (getenv("TMC_COOP_DEBUG") && (gMain.ticks % 10) == 0 && sP2Active) {
        const Entity* e = P2;
        const PlayerState* ps = &gPortPlayerStates[1];
        fprintf(stderr, "[coop-dbg] t=%u p2=(%d,%d) action=%u sub=%u ctl=%u held=%04x in=%08x fs=%u keys=%04x dir=%u edir=%u spd=%d flags=%08x anim=%u prio=%u/%u/%u\n",
                (unsigned)gMain.ticks, (int)(e->x.HALF.HI - gRoomControls.origin_x),
                (int)(e->y.HALF.HI - gRoomControls.origin_y), e->action, e->subAction, ps->controlMode,
                sP2Held, (unsigned)ps->playerInput.heldInput, ps->framestate, sP2Input.heldKeys, ps->direction, e->direction, e->speed,
                (unsigned)ps->flags, e->animationState, e->updatePriority, gPriorityHandler.event_priority,
                gPriorityHandler.ent_priority);
    }
    if (!sP2Active) {
        if (getenv("TMC_COOP_NOJOIN"))
            sJoinRequested = 0;
        if (sJoinRequested || (sP2Held != 0 && Port_Config_CoopP2JoinEnabled())) {
            sJoinRequested = 0;
            SpawnP2();
            sLastArea = gRoomControls.area;
            sLastRoom = gRoomControls.room;
        }
        return;
    }

    if (!P2Linked()) {
        /* An area load erased the entity lists (and player 2 with them). */
        SpawnP2();
    } else if (gRoomControls.area != sLastArea || gRoomControls.room != sLastRoom) {
        FollowP1();
    }
    sLastArea = gRoomControls.area;
    sLastRoom = gRoomControls.room;

    {
        const Entity* p1 = &gPortPlayerEntities[0].base;
        const PlayerState* p1s = &gPortPlayerStates[0];
        Entity* p2 = P2;
        /* Player 1 is in a cutscene, dialogue, item get, portal, transition:
         * player 2 is put away (not updated, drawn or hit) until it ends. */
        const int busy = p1s->controlMode != CONTROL_ENABLED || (gMessage.state & MESSAGE_ACTIVE) ||
                         gPriorityHandler.event_priority != 0 || (p1s->flags & PL_USE_PORTAL) ||
                         (p1->action != PLAYER_NORMAL && p1->action != PLAYER_MINISH && p1->action != PLAYER_ROLL &&
                          p1->action != PLAYER_JUMP && p1->action != PLAYER_PUSH && p1->action != PLAYER_PULL &&
                          p1->action != PLAYER_BOUNCE);
        if (busy) {
            if (!sHidden) {
                sHidden = 1;
                sHiddenDraw = p2->spriteSettings.draw;
                p2->spriteSettings.draw = 0;
                COLLISION_OFF(p2);
            }
            return;
        }
        if (sHidden) {
            sHidden = 0;
            if ((p1s->flags & PL_MINISH) != (gPortPlayerStates[1].flags & PL_MINISH)) {
                SpawnP2(); /* comes back at player 1's size */
            } else {
                FollowP1();
                p2->spriteSettings.draw = sHiddenDraw ? sHiddenDraw : 1;
                COLLISION_ON(p2);
            }
            return;
        }
        {
            /* Player 2's buttons for its next update: L (Y key) + B / A
             * steps its item slots instead of playing (port_coop_items.c). */
            static unsigned short sPrevFiltered;
            const unsigned short held = Port_CoopItems_FilterKeys(sP2Held, (u16)(sP2Held & ~sP2PrevHeld));
            sP2Input.heldKeys = held;
            sP2Input.newKeys = (u16)(held & ~sPrevFiltered);
            sPrevFiltered = held;
        }
        if (sSpawnSettle > 0) {
            sSpawnSettle--;
            return;
        }
        /* Minish: only together. Player 1 shrank or grew (portal, cap):
         * player 2 is spawned again on player 1 at the same size. */
        if ((p1s->flags & PL_MINISH) != (gPortPlayerStates[1].flags & PL_MINISH)) {
            SpawnP2();
            return;
        }
        /* Leash, player 1's side: walked off further than a screen away,
         * player 2 is brought along. */
        if (Apart(p2->x.HALF.HI, p2->y.HALF.HI, LEASH_PULL_X, LEASH_PULL_Y))
            FollowP1();
    }
}
