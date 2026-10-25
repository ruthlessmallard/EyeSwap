#ifndef SHOOTER_H
#define SHOOTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Game states reported by csStats(). */
enum {
    CS_READY = 0,
    CS_WAIT  = 1,
    CS_DRAW  = 2,
    CS_SHOT  = 3,
    CS_SCORE = 4
};

void csInit(uint32_t seed);
void csTick(uint32_t dtMs);
void csInteract(void);
uint8_t csCell(int x, int y);
void csStats(int *round, int *streak, int *state, int *violations);
int csSelfCheck(void);

#ifdef __cplusplus
}
#endif

#endif /* SHOOTER_H */
