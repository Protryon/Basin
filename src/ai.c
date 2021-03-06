#include <basin/ai.h>
#include <basin/entity.h>
#include <basin/world.h>
#include <basin/game.h>
#include <avuna/list.h>
#include <avuna/hash.h>
#include <avuna/string.h>
#include <math.h>

void initai_zombie(struct world* world, struct entity* entity) {
    struct aitask* task = ai_initTask(entity, 4, &ai_swimming, &ai_shouldswimming, NULL);
    struct ai_nearestattackabletarget_data* and = pcalloc(task->pool, sizeof(struct ai_nearestattackabletarget_data));
    and->chance = 10;
    and->type = ENT_PLAYER;
    and->checkSight = 1;
    and->unseenTicks = 0;
    and->targetSearchDelay = 0;
    and->targetDist = 16.;
    ai_initTask(entity, 1, &ai_nearestattackabletarget, &ai_shouldnearestattackabletarget, and);
    and = pcalloc(task->pool, sizeof(struct ai_nearestattackabletarget_data));
    and->chance = 10;
    and->type = ENT_VILLAGER;
    and->checkSight = 0;
    and->unseenTicks = 0;
    and->targetSearchDelay = 0;
    and->targetDist = 16.;
    ai_initTask(entity, 1, &ai_nearestattackabletarget, &ai_shouldnearestattackabletarget, and);
    and = pcalloc(task->pool, sizeof(struct ai_nearestattackabletarget_data));
    and->chance = 10;
    and->type = ENT_VILLAGERGOLEM;
    and->checkSight = 1;
    and->unseenTicks = 0;
    and->targetSearchDelay = 0;
    and->targetDist = 16.;
    ai_initTask(entity, 1, &ai_nearestattackabletarget, &ai_shouldnearestattackabletarget, and);
    struct ai_attackmelee_data* amd = pcalloc(task->pool, sizeof(struct ai_attackmelee_data));
    amd->attackInterval = 20;
    amd->speed = 1.;
    amd->longMemory = 0;
    ai_initTask(entity, 4, &ai_zombieattack, &ai_shouldzombieattack, amd);
}

void ai_stdcancel(struct world* world, struct entity* entity, struct aitask* ai) {
    if (!ai->ai_running) return;
    entity->ai->mutex &= ~ai->mutex;
    ai->ai_running = 0;
}

struct aitask* ai_initTask(struct entity* entity, uint16_t mutex, int32_t (*ai_tick)(struct world* world, struct entity* entity, struct aitask* ai), int (*ai_should)(struct world* world, struct entity* entity, struct aitask* ai), void* data) {
    struct mempool* ai_pool = mempool_new();
    pchild(entity->pool, ai_pool);
    struct aitask* ai = pcalloc(ai_pool, sizeof(struct aitask));
    ai->pool = ai_pool;
    ai->ai_running = 0;
    ai->ai_waiting = 0;
    ai->mutex = mutex;
    ai->ai_should = ai_should;
    ai->ai_tick = ai_tick;
    ai->ai_cancel = &ai_stdcancel;
    ai->data = data;
    hashmap_putptr(entity->ai->tasks, ai, ai);
    return ai;
}

int32_t ai_handletasks(struct world* world, struct entity* entity) {
    if (entity->ai == NULL) return 0;
    int32_t mw = 0;
    ITER_MAP(entity->ai->tasks) {
        struct aitask* ai = value;
        if (ai->ai_running) {
            if (ai->ai_waiting == -1) (*ai->ai_cancel)(world, entity, ai);
            else if (ai->ai_waiting > 0) ai->ai_waiting--;
            else if (ai->ai_tick != NULL) {
                if (!(*ai->ai_should)(world, entity, ai)) (*ai->ai_cancel)(world, entity, ai);
                else ai->ai_waiting = (*ai->ai_tick)(world, entity, ai);
            }
        } else if (ai->ai_should != NULL && !(ai->mutex & entity->ai->mutex) && (*ai->ai_should)(world, entity, ai)) {
            ai->ai_waiting = 0;
            entity->ai->mutex |= ai->mutex;
            ai->ai_waiting = (*ai->ai_tick)(world, entity, ai);
            ai->ai_running = 1;
        }
        if (ai->ai_waiting > mw) mw = ai->ai_waiting;
        ITER_MAP_END();
    }
    return mw;
}

float wrapAngle(float angle) {
    angle = fmod(angle, 360.);
    if (angle >= 180.) angle -= 360.;
    if (angle < -180.) angle += 360.;
    return angle;
}

void lookHelper_tick(struct entity* entity) {
    if (entity->ai == NULL || entity->ai->lookHelper_speedPitch == 0. || entity->ai->lookHelper_speedYaw == 0.) return;
    double dx = entity->ai->lookHelper_x - entity->x;
    struct boundingbox bb;
    entity_collision_bounding_box(entity, &bb);
    double dy = entity->ai->lookHelper_y - entity->y - ((bb.maxY - bb.minY) * .9); // TODO: real eye height
    double dz = entity->ai->lookHelper_z - entity->z;
    double horiz_dist = sqrt(dx * dx + dz * dz);
    float dp = -(atan2(dy, horiz_dist) * 180. / M_PI);
    float dya = (atan2(dz, dx) * 180. / M_PI) - 90.;
    dp = wrapAngle(dp - entity->pitch);
    if (dp > entity->ai->lookHelper_speedPitch) dp = entity->ai->lookHelper_speedPitch;
    if (dp < -entity->ai->lookHelper_speedPitch) dp = -entity->ai->lookHelper_speedPitch;
    entity->pitch += dp;
    dya = wrapAngle(dya - entity->yaw);
    if (dya > entity->ai->lookHelper_speedYaw) dya = entity->ai->lookHelper_speedYaw;
    if (dya < -entity->ai->lookHelper_speedYaw) dya = -entity->ai->lookHelper_speedYaw;
    entity->yaw += dya;
    if (fabs(dp) < 0.5 && fabs(dya) < .5) {
        entity->ai->lookHelper_speedPitch = 0.;
        entity->ai->lookHelper_speedYaw = 0.;
    }
}

float aipath_manhattan(struct aipath_point* p1, struct aipath_point* p2) {
    return fabs(p1->x - p2->x) + fabs(p1->y - p2->y) + fabs(p1->y - p2->y);
}

void aipath_sortback(struct list* path, int32_t index) {
    struct aipath_point* point = path->data[index];
    int32_t i = 0;
    for (float lowest_distance = point->target_dist; index > 0; index = i) {
        i = (index - 1) >> 1;
        struct aipath_point* point2 = path->data[i];
        if (lowest_distance >= point2->target_dist) break;
        path->data[index] = point2;
        point2->index = index;
    }
    path->data[index] = point;
    point->index = index;
}

void aipath_sortforward(struct list* path, int32_t index) {
    struct aipath_point* pp = path->data[index];
    float ld = pp->target_dist;
    while (1) {
        int i = 1 + (index << 1);
        int j = i + 1;
        if (i >= path->count) break;
        struct aipath_point* pp2 = path->data[i];
        float ld2 = pp2->target_dist;
        struct aipath_point* pp3 = NULL;
        float ld3 = 0.;
        if (j >= path->count)  {
            ld3 = (float) 0x7f800000; // positive infinity
        } else {
            pp3 = path->data[j];
            ld3 = pp3->target_dist;
        }
        if (ld2 < ld3) {
            if (ld2 >= ld) break;
            path->data[index] = pp2;
            pp2->index = index;
            index = i;
        } else {
            if (ld3 >= ld) break;
            path->data[index] = pp3;
            pp3->index = index;
            index = j;
        }
    }
    path->data[index] = pp;
    pp->index = index;
}

struct aipath_point* aipath_addpoint(struct list* path, struct aipath_point* point) {
    struct aipath_point* new_point = pmalloc(path->pool, sizeof(struct aipath_point));
    memcpy(new_point, point, sizeof(struct aipath_point));
    new_point->index = path->count;
    list_append(path, new_point);
    aipath_sortback(path, new_point->index);
    return new_point;
}

struct aipath_point* aipath_dequeue(struct list* path) {
    if (path->count == 0) return NULL;
    struct aipath_point* point = path->data[0];
    --path->count;
    path->data[0] = path->data[--path->size];
    if (path->count > 0) aipath_sortforward(path, 0);
    point->index = -1;
    return point;
}

struct list* aipath_find(struct mempool* pool, int32_t sx, int32_t sy, int32_t sz, int32_t ex, int32_t ey, int32_t ez) {
    struct aipath_point end;
    memset(&end, 0, sizeof(struct aipath_point));
    end.x = ex;
    end.y = ey;
    end.z = ez;
    struct aipath_point start;
    memset(&start, 0, sizeof(struct aipath_point));
    start.x = sx;
    start.y = sy;
    start.z = sz;
    struct list* path = list_new(32, pool);
    start.x = sx;
    start.y = sy;
    start.z = sz;
    start.next_dist = aipath_manhattan(&start, &end);
    start.target_dist = start.next_dist;
    struct aipath_point* current_point = aipath_addpoint(path, &start);
    int i = 0;
    while (path->count > 0) {
        if (++i >= 200) break;
        struct aipath_point* point2 = aipath_dequeue(path);
        if (point2->x == end.x && point2->y == end.y && point2->z == end.z) {
            current_point = point2;
            break;
        }
        if (aipath_manhattan(point2, &end) < aipath_manhattan(current_point, &end)) current_point = point2;
        point2->visited = 1;

    }
    return path;
}

int32_t ai_attackmelee(struct world* world, struct entity* entity, struct aitask* ai) {
    struct ai_attackmelee_data* amd = ai->data;
    entity->ai->lookHelper_speedYaw = 30.;
    entity->ai->lookHelper_speedPitch = 30.;
    entity->ai->lookHelper_x = entity->attacking->x;
    struct boundingbox bb;
    entity_collision_bounding_box(entity->attacking, &bb);
    entity->ai->lookHelper_y = entity->attacking->y + ((bb.maxY - bb.minY) * .9); // TODO: real eye height
    entity->ai->lookHelper_z = entity->attacking->z;
    double dist = entity_distsq(entity->attacking, entity);
    amd->delayCounter--;
    //TODO: cansee
    if ((amd->longMemory || 1) && amd->delayCounter <= 0 && ((amd->targetX == 0. && amd->targetY == 0. && amd->targetZ == 0.) || entity_distsq_block(entity->attacking, amd->targetX, amd->targetY, amd->targetZ) >= 1. || game_rand_float() < .05)) {
        amd->targetX = entity->attacking->x;
        amd->targetY = entity->attacking->y;
        amd->targetZ = entity->attacking->z;
        if (dist > 1024.) amd->delayCounter += 10;
        else if (dist > 256.) amd->delayCounter += 5;
        //TODO: set path
    }
    if (--amd->attackTick <= 0) amd->attackTick = 0;
    struct entity_info* ei = entity_get_info(entity->type);
    struct entity_info* ei2 = entity_get_info(entity->attacking->type);
    float reach = ei->width * 2. * ei->width * 2. + ei2->width;
    if (dist <= reach && amd->attackTick <= 0) {
        amd->attackTick = 20;
        entity_animation(entity, 1);
        damageEntityWithItem(entity->attacking, entity, -1, NULL);
    }
    return 0;
}

int32_t ai_attackranged(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_attackrangedbow(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_avoidentity(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_beg(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_breakdoor(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_creeperswell(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_defendvillage(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_doorinteract(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_eatgrass(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_findentitynearest(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_findentitynearestplayer(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_fleesun(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_followgolem(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_followowner(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_followparent(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_harvestfarmland(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_hurtbytarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_leapattarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_llamafollowcaravan(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_lookattradeplayer(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_lookatvillager(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_lookidle(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_mate(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_moveindoors(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_movethroughvillage(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_movetoblock(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_movetowardsrestriction(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_movetowardstarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_nearestattackabletarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_ocelotattack(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_ocelotsit(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_opendoor(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_ownerhurtbytarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_ownerhurttarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_panic(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_play(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_restructopendoor(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_restrictsun(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_runaroundlikecrazy(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_sit(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_skeletonriders(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_swimming(struct world* world, struct entity* entity, struct aitask* ai) {
//if (game_rand_float() < .8) {
    entity_jump(entity);
//}
    return 0;
}

int32_t ai_target(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_targetnontamed(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_tempt(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_tradeplayer(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_villagerinteract(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_villagermate(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_wander(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_wanderaint32_twater(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_watchclosest(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int32_t ai_zombieattack(struct world* world, struct entity* entity, struct aitask* ai) {
    return ai_attackmelee(world, entity, ai);
}

//shoulds

int ai_shouldattackmelee(struct world* world, struct entity* entity, struct aitask* ai) {
    if (entity->attacking == NULL || entity->attacking->health <= 0. || (entity->type == ENT_PLAYER && (entity->data.player.player->gamemode == 1 || entity->data.player.player->gamemode == 3))) return 0;
    struct entity_info* info1 = entity_get_info(entity->type);
    struct entity_info* info2 = entity_get_info(entity->attacking->type);
    if (info1 == NULL || info2 == NULL || !hashset_has(info2->flags, "livingbase")) return 0;
    return entity->attacking != NULL;
    //float reach = ei->width * 2. * ei->width * 2. + ei2->width;
    //return reach >= entity_distsq(entity->attacking, entity);
}

int ai_shouldattackranged(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldattackrangedbow(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldavoidentity(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldbeg(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldbreakdoor(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldcreeperswell(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shoulddefendvillage(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shoulddoorinteract(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldeatgrass(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldfindentitynearest(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldfindentitynearestplayer(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldfleesun(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldfollowgolem(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldfollowowner(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldfollowparent(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldharvestfarmland(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldhurtbytarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldleapattarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldllamafollowcaravan(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldlookattradeplayer(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldlookatvillager(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldlookidle(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldmate(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldmoveindoors(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldmovethroughvillage(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldmovetoblock(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldmovetowardsrestriction(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldmovetowardstarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldnearestattackabletarget(struct world* world, struct entity* entity, struct aitask* ai) {
    struct ai_nearestattackabletarget_data* data = ai->data;
    if (data->chance > 0 && rand() % data->chance != 0) return 0;
    double current_distance = data->targetDist * data->targetDist;
    struct entity* current_entity = NULL;
    pthread_rwlock_rdlock(&world->entities->rwlock);
    ITER_MAP(world->entities) {
        struct entity* iter_entity = value;
        if (!hashset_has(entity_get_info(iter_entity->type)->flags, "livingbase") || iter_entity == entity) continue;
        double distance_squared = entity_distsq(entity, value);
        if (iter_entity->type == ENT_PLAYER) {
            struct player* pl = iter_entity->data.player.player;
            if (pl->gamemode == 1 || pl->gamemode == 3 || pl->invulnerable) continue;
            struct hashset* flags = entity_get_info(entity->type)->flags;
            int is_skeleton = hashset_has(flags, "skeleton");
            int is_zombie = hashset_has(flags, "zombie");
            int is_creeper = hashset_has(flags, "creeper");
            if (is_skeleton || is_zombie || is_creeper) {
                struct slot* holding_slot = inventory_get(pl, pl->inventory, 5);
                if (holding_slot != NULL) {
                    if (is_skeleton && holding_slot->damage == 0) distance_squared *= 2.;
                    else if (is_zombie && holding_slot->damage == 2) distance_squared *= 2.;
                    else if (is_creeper && holding_slot->damage == 4) distance_squared *= 2.;
                }
            }
        }
        if (distance_squared < current_distance) {
            //TODO check teams, sight
            current_distance = distance_squared;
            current_entity = value;
        }
        ITER_MAP_END()
    }
    pthread_rwlock_unlock(&world->entities->rwlock);
    if (entity->attacking != NULL && current_entity != entity->attacking) {
        hashmap_putint(entity->attacking->attackers, entity->id, NULL);
    }
    if (current_entity != NULL && entity->attacking != current_entity) {
        hashmap_putint(current_entity->attackers, entity->id, entity);
    }
    entity->attacking = current_entity;
    return 0;
}

int ai_shouldocelotattack(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldocelotsit(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldopendoor(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldownerhurtbytarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldownerhurttarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldpanic(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldplay(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldrestructopendoor(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldrestrictsun(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldrunaroundlikecrazy(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldsit(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldskeletonriders(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldswimming(struct world* world, struct entity* entity, struct aitask* ai) {
    return entity->inWater || entity->inLava;
}

int ai_shouldtarget(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldtargetnontamed(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldtempt(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldtradeplayer(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldvillagerinteract(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldvillagermate(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldwander(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldwanderaintwater(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldwatchclosest(struct world* world, struct entity* entity, struct aitask* ai) {
    return 0;
}

int ai_shouldzombieattack(struct world* world, struct entity* entity, struct aitask* ai) {
    return ai_shouldattackmelee(world, entity, ai);
}
