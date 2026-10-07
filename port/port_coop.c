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

#define P2 (&gPortPlayerEntities[1].base)

static int sCurrent;                 /* 0 = player 1, 1 = player 2 */
static int sP2Active;                /* player 2 has joined */
static int sJoinRequested;
static unsigned short sP2Held, sP2PrevHeld;
static unsigned char sLastArea = 0xff, sLastRoom = 0xff;

/* Owner of each pooled entity: 1 = spawned while player 2 was current. */
static unsigned char sOwnerEntities[MAX_ENTITIES];
static unsigned char sOwnerAux[MAX_AUX_PLAYER_ENTITIES];

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
    if (owner)
        *owner = (unsigned char)sCurrent;
}

/* ---- player-2 context ------------------------------------------------- */

typedef struct {
    Input input;
    Entity* cameraTarget;
    u8 rActionInteractTile, rActionGrabbing, rActionPlayerState;
} SavedP1;

static SavedP1 sSaved;
static Input sP2Input;

static void EnterP2(void) {
    sSaved.input = gInput;
    sSaved.cameraTarget = gRoomControls.camera_target;
    sSaved.rActionInteractTile = gHUD.rActionInteractTile;
    sSaved.rActionGrabbing = gHUD.rActionGrabbing;
    sSaved.rActionPlayerState = gHUD.rActionPlayerState;
    gInput = sP2Input;
    SetCurrent(1);
}

static void LeaveP2(void) {
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
}

int Port_Coop_BeginEntityUpdate(void* entity) {
    if (!sP2Active || sCurrent != 0)
        return 0;
    if (entity == (void*)P2) {
        EnterP2();
        /* Player 1's UpdatePlayerInput ran at the top of UpdateEntities;
         * player 2's runs here, against player 2's gInput / gPlayerState. */
        UpdatePlayerInput();
        return 1;
    }
    {
        unsigned char* owner = OwnerSlot(entity);
        if (owner && *owner == 1) {
            EnterP2();
            return 1;
        }
    }
    return 0;
}

void Port_Coop_EndEntityUpdate(int token) {
    if (token)
        LeaveP2();
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

static void SpawnP2(void) {
    const Entity* p1 = &gPortPlayerEntities[0].base;
    PlayerRoomStatus saved = gRoomTransition.player_status;
    s16 x, y;

    UnlinkP2();
    SpotNextToP1(&x, &y);
    gRoomTransition.player_status.spawn_type = PL_SPAWN_DEFAULT;
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
    fprintf(stderr, "[coop] player 2 joined at (%d,%d) area=0x%02x room=0x%02x\n",
            (int)(x - gRoomControls.origin_x), (int)(y - gRoomControls.origin_y), (unsigned)gRoomControls.area,
            (unsigned)gRoomControls.room);
}

/* Player 1 changed rooms within the area: bring player 2 along. */
static void FollowP1(void) {
    Entity* p2 = P2;
    const Entity* p1 = &gPortPlayerEntities[0].base;
    s16 x, y;
    SpotNextToP1(&x, &y);
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
}
