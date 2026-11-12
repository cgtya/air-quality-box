#pragma once

#include <stdint.h>
#include <stdatomic.h>

extern _Atomic uint8_t sleep_timer;

void view_task(void* arg);