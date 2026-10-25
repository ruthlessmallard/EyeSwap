#ifndef SLAB_H
#define SLAB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SB_SURFACE = 0,
    SB_DIG = 1,
    SB_BACKOUT = 2,
    SB_CRUSH = 3,
    SB_BANK = 4
};

void sbInit(uint32_t seed);
void sbTick(uint32_t dtMs);
void sbInteract(void);
void sbCell(int x, int y);
uint8_t sbCellAt(int x, int y);
void sbStats(int *depth, int *haul, int *banked, int *crack,
             int *state, int *ai, int *violations);
int sbSelfCheck(void);

#ifdef __cplusplus
}
#endif

#endif
