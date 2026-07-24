#pragma once

#include <kifu-client.h>

#include <stdint.h>

struct kifu_dice_stabilizer;

struct kifu_dice_stabilizer *kifu_dice_stabilizer_create(void);
void kifu_dice_stabilizer_destroy(struct kifu_dice_stabilizer *stabilizer);
void kifu_dice_stabilizer_reset(struct kifu_dice_stabilizer *stabilizer);
uint32_t kifu_dice_stabilizer_update(struct kifu_dice_stabilizer *stabilizer,
					 const struct kifu_dice_result *detections,
					 uint32_t detection_count,
					 uint64_t now_ns,
					 struct kifu_dice_result *out_dice,
					 bool *out_valid,
					 uint32_t out_capacity);
