#ifndef DIG_H
#define DIG_H

#include <stdint.h>

enum {
    DG_SURFACE = 0,
    DG_DIG = 1,
    DG_CLIMB = 2,
    DG_CRUSH = 3,
    DG_BANK = 4
};

#ifdef __cplusplus
extern "C" {
#endif

void dgInit(uint32_t seed);
void dgTick(uint32_t dtMs);
void dgInteract(void);
uint8_t dgCell(int x, int y);
void dgStats(int *depth, int *haul, int *banked, int *crack, int *state, int *violations);
int dgSelfCheck(void);

#ifdef __cplusplus
}
#endif

#endif
