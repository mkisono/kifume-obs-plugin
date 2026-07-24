#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <obs-module.h>

struct kifu_source;

struct kifu_source *kifu_source_create(obs_data_t *settings, obs_source_t *source);
void kifu_source_destroy(void *data);
void kifu_source_update(void *data, obs_data_t *settings);
void kifu_source_activate(void *data);
void kifu_source_deactivate(void *data);
void kifu_source_render(void *data, gs_effect_t *effect);
uint32_t kifu_source_get_width(void *data);
uint32_t kifu_source_get_height(void *data);
obs_properties_t *kifu_source_properties(void *data);
void kifu_source_defaults(obs_data_t *settings);
void kifu_source_save(void *data, obs_data_t *settings);
void kifu_source_load(void *data, obs_data_t *settings);
const char *kifu_source_get_name(void *type_data);

extern struct obs_source_info kifu_source_info;
