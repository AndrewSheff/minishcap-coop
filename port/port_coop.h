#ifndef PORT_COOP_H
#define PORT_COOP_H

/* Local coop, stage 1: a second Link driven by its own input.
 *
 * Player 2 is a real PLAYER entity (gPortPlayerEntities[1]) in the player
 * list. While it — or anything it spawned (sword, items, effects) — updates,
 * the "current player" macros (include/port_player_ctx.h) point at player 2's
 * storage, so the unchanged player code runs for it. Everything else in the
 * world still sees player 1. */

#ifdef __cplusplus
extern "C" {
#endif

/* Port_UpdateInput: player 2's held GBA keys (active-high mask). */
void Port_Coop_SetP2Keys(unsigned short heldKeys);

/* Once per frame, between game frames: joining, re-spawning after an area
 * load, following player 1 into a new room. */
void Port_Coop_FrameTick(void);

/* Join player 2 at the next opportunity (coop-test mode). */
void Port_Coop_RequestJoin(void);
int Port_Coop_IsP2Active(void);

/* ram_UpdateEntities: switch to the entity's owner before its update and
 * collision, back to player 1 after. Returns the token for End;
 * PORT_COOP_SKIP_UPDATE = don't update (or draw) this entity this frame. */
#define PORT_COOP_SKIP_UPDATE 3
#define PORT_COOP_FOCUS 4 /* an enemy updating as "focused on" a player */
int Port_Coop_BeginEntityUpdate(void* entity);
void Port_Coop_EndEntityUpdate(int token);

/* AppendEntityToList hook: entities created while player 2 is current
 * belong to player 2. */
void Port_Coop_OnEntityAppended(void* entity);

/* 0 = player 1, 1 = player 2. */
int Port_Coop_CurrentPlayer(void);

/* Player 2's own A/B items (port_coop_items.c). */
void Port_CoopItems_Enter(void);
void Port_CoopItems_OnPauseMenuOpen(void);
int Port_CoopItems_MenuIsP2(void);
void Port_CoopItems_FrameTick(void);
void Port_CoopItems_Leave(void);
unsigned char Port_CoopItems_Equipped(int slot);

/* Stage 2 — the world sees player 2. */
int Port_Coop_IsPlayer2Entity(const void* entity);
int Port_Coop_PushPlayer(int idx);
void Port_Coop_PopPlayer(int prev);
int Port_Coop_PushOtherPlayer(void);
int Port_Coop_BeginCollision(const void* a, const void* b);
void Port_Coop_EndCollision(int token);
unsigned int Port_Coop_PlayerPalette(unsigned int paletteId);
int Port_Coop_IsP2Palette(unsigned int paletteId);
unsigned int Port_Coop_P2PaletteBase(unsigned int paletteId);
void Port_Coop_TintP2Palette(unsigned short* colors);

#ifdef __cplusplus
}
#endif

#endif /* PORT_COOP_H */
