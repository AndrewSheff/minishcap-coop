#ifndef PORT_PLAYER_CTX_H
#define PORT_PLAYER_CTX_H

/*
 * Coop: "current player" indirection (PC port only).
 *
 * The game knows exactly one Link through a handful of globals. On PC each
 * name below means "the player being processed right now": it expands to a
 * dereference of a pointer the coop layer (port/port_coop.c) switches while
 * it runs player 2's update. The ~3900 uses in src/ compile unchanged, and
 * existing `extern T gName;` declarations expand into matching pointer
 * declarations. The storage lives in port/port_linked_stubs.c as
 * gPortPlayer*[PORT_MAX_PLAYERS]; there is deliberately no symbol named
 * gPlayerEntity etc., so a stray direct reference fails to link.
 *
 * Text-only macros: no types needed here, so this header can be included
 * before player.h / entity.h / structures.h define them.
 */
#ifdef PC_PORT

#define PORT_MAX_PLAYERS 2

#define gPlayerEntity (*gPortCurPlayerEntity)
#define gPlayerState (*gPortCurPlayerState)
#define gActiveItems (*gPortCurActiveItems)
#define gPlayerClones (*gPortCurPlayerClones)
#define gPossibleInteraction (*gPortCurPossibleInteraction)
#define gCarriedEntity (*gPortCurCarriedEntity)

/* Player items (sword, shield, bow...) load their tiles into the fixed VRAM
 * slot 3. Player 2's items take a free slot of their own instead, so the
 * two Links' items don't overwrite each other's tiles (port_coop.c). */
extern unsigned int Port_Coop_PlayerItemGfxSlot(void);
#define PLAYER_ITEM_GFX_SLOT Port_Coop_PlayerItemGfxSlot()

#else
#define PLAYER_ITEM_GFX_SLOT 3
#endif /* PC_PORT */
#endif /* PORT_PLAYER_CTX_H */
