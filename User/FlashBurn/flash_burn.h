#ifndef __FLASH_BURN_H
#define __FLASH_BURN_H

#include <stdint.h>
#include <stdbool.h>

extern volatile bool flash_burn_active;

void FlashBurn_Init(void);
void FlashBurn_Task(void);

#endif
