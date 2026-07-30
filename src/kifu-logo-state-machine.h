#pragma once

#include <stdbool.h>
#include <stdint.h>

struct kifu_logo_state_machine;

struct kifu_logo_state_machine *kifu_logo_state_machine_create(void);
void kifu_logo_state_machine_destroy(struct kifu_logo_state_machine *machine);
void kifu_logo_state_machine_activate(struct kifu_logo_state_machine *machine, uint64_t now_ns);
bool kifu_logo_state_machine_update(struct kifu_logo_state_machine *machine, uint64_t now_ns, uint32_t latest_dice_count);
float kifu_logo_state_machine_opacity(const struct kifu_logo_state_machine *machine, uint64_t now_ns);
