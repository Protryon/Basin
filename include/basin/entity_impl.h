#ifndef BASIN_ENTITY_IMPL_H
#define BASIN_ENTITY_IMPL_H

#include <basin/world.h>
#include <basin/entity.h>
#include <basin/player.h>
#include <basin/inventory.h>
#include <stdint.h>

void onSpawned_minecart(struct world* world, struct entity* entity);

void onTick_tnt(struct world* world, struct entity* entity);

void onTick_fallingblock(struct world* world, struct entity* entity);

void onInteract_cow(struct world* world, struct entity* entity, struct player* interacter, struct slot* item, int16_t slot_index);

void onInteract_mooshroom(struct world* world, struct entity* entity, struct player* interacter, struct slot* item, int16_t slot_index);

void tick_arrow(struct world* world, struct entity* entity);

void tick_itemstack(struct world* world, struct entity* entity);

#endif //BASIN_ENTITY_IMPL_H
