#ifndef DBONSAI_H
#define DBONSAI_H

#include <stdint.h>

void dnbInit(uint32_t seed);
void dnbTick(uint32_t dtMs);
uint8_t dnbCell(int x, int y);
void dnbStats(int *cells, int *leaves, int *retractions, int *components, int *violations);
int dnbSelfCheck(void);

#endif
