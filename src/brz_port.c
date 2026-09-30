#include "brz_port.h"
#include "brz_world.h"
#include "brz_agent.h"
#include "brz_settlement.h"
#include <stdlib.h>
#include <string.h>

typedef struct { BrzWorld* world; size_t resource_count; } LocalWorld;
typedef struct { BrzSettlement* settlements; int count; const ParsedConfig* config; } LocalSettlements;

static uint16_t local_tags_at(void* context, BrzPos position)
{ return brz_world_tags_at(((LocalWorld*)context)->world,position); }
static double local_available(void* context, BrzPos position, int resource)
{ LocalWorld* world=(LocalWorld*)context; return brz_world_peek(world->world,position,world->resource_count,resource); }
/* The local engine is single-threaded, so reservation is an advisory capped
   amount.  A concurrent adapter may replace this with a durable reservation. */
static double local_reserve(void* context, BrzPos position, int resource, double amount)
{ double available=local_available(context,position,resource); return available<amount ? available : amount; }
static double local_take(void* context, BrzPos position, int resource, double amount)
{ LocalWorld* world=(LocalWorld*)context; return brz_world_take(world->world,position,world->resource_count,resource,amount); }
static BrzPos local_nearest(void* context, BrzPos from, uint16_t tag, int radius)
{ return brz_world_find_nearest_tag(((LocalWorld*)context)->world,from,tag,radius); }
static BrzPos local_clamp_position(void* context, BrzPos position)
{
    BrzWorld* world=((LocalWorld*)context)->world;
    position.x=brz_clamp_i(position.x,0,world->w-1);
    position.y=brz_clamp_i(position.y,0,world->h-1);
    return position;
}
static void local_regen(void* context)
{ LocalWorld* world=(LocalWorld*)context; brz_world_step_regen(world->world,world->resource_count); }

void bronze_world_port_init(BronzeWorldPort* port, void* world, size_t resource_count)
{
    LocalWorld* local;
    if(!port) return;
    local=(LocalWorld*)malloc(sizeof(*local));
    if(!local){ memset(port,0,sizeof(*port)); return; }
    local->world=(BrzWorld*)world; local->resource_count=resource_count;
    port->context=local; port->tags_at=local_tags_at; port->available=local_available;
    port->reserve=local_reserve; port->take=local_take;
    port->nearest_tag=local_nearest; port->clamp_position=local_clamp_position;
    port->step_regen=local_regen;
}

void bronze_world_port_destroy(BronzeWorldPort* port)
{
    if(!port) return;
    free(port->context); memset(port,0,sizeof(*port));
}

static uint32_t local_actor_id(void* context) { return ((BrzAgent*)context)->id; }
static BrzPos local_actor_position(void* context) { return ((BrzAgent*)context)->pos; }
static void local_actor_set_position(void* context, BrzPos pos) { ((BrzAgent*)context)->pos=pos; }
static double local_actor_need(void* context, const char* name)
{ BrzAgent* actor=(BrzAgent*)context; return strcmp(name,"hunger")==0 ? actor->hunger : (strcmp(name,"fatigue")==0 ? actor->fatigue : 0.0); }
static void local_actor_need_add(void* context, const char* name, double amount)
{
    BrzAgent* actor=(BrzAgent*)context;
    double* need=strcmp(name,"hunger")==0 ? &actor->hunger : (strcmp(name,"fatigue")==0 ? &actor->fatigue : NULL);
    if(!need) return;
    *need+=amount;
    if(*need<0) *need=0;
    if(*need>1) *need=1;
}
static double local_actor_resource_get(void* context, int id)
{ BrzAgent* actor=(BrzAgent*)context; return id>=0 && (size_t)id<actor->res_n ? actor->res_inv[id] : 0; }
static void local_actor_resource_add(void* context, int id, double amount)
{ BrzAgent* actor=(BrzAgent*)context; if(id>=0 && (size_t)id<actor->res_n){ actor->res_inv[id]+=amount; if(actor->res_inv[id]<0) actor->res_inv[id]=0; } }
static double local_actor_item_get(void* context, int id)
{ BrzAgent* actor=(BrzAgent*)context; return id>=0 && (size_t)id<actor->item_n ? actor->item_inv[id] : 0; }
static void local_actor_item_add(void* context, int id, double amount)
{ BrzAgent* actor=(BrzAgent*)context; if(id>=0 && (size_t)id<actor->item_n){ actor->item_inv[id]+=amount; if(actor->item_inv[id]<0) actor->item_inv[id]=0; } }
static int local_actor_home_settlement(void* context) { return ((BrzAgent*)context)->home_settlement; }
static int local_actor_has_target(void* context) { return ((BrzAgent*)context)->has_target; }
static BrzPos local_actor_target(void* context) { return ((BrzAgent*)context)->target; }
static void local_actor_set_target(void* context, BrzPos target)
{ BrzAgent* actor=(BrzAgent*)context; actor->target=target; actor->has_target=1; }
static void local_actor_clear_target(void* context) { ((BrzAgent*)context)->has_target=0; }

void bronze_actor_port_init(BronzeActorPort* port, void* agent)
{
    if(!port) return;
    port->context=agent; port->id=local_actor_id; port->position=local_actor_position;
    port->set_position=local_actor_set_position; port->need=local_actor_need;
    port->need_add=local_actor_need_add;
    port->resource_get=local_actor_resource_get; port->resource_add=local_actor_resource_add;
    port->item_get=local_actor_item_get; port->item_add=local_actor_item_add;
    port->home_settlement=local_actor_home_settlement;
    port->has_target=local_actor_has_target; port->target=local_actor_target;
    port->set_target=local_actor_set_target; port->clear_target=local_actor_clear_target;
}

static int local_settlement_nearest(void* context, BrzPos position)
{ LocalSettlements* settlements=(LocalSettlements*)context; return brz_find_nearest_settlement(settlements->settlements,settlements->count,position); }
static BrzPos local_settlement_position(void* context, int id)
{
    LocalSettlements* settlements=(LocalSettlements*)context;
    return (id>=0 && id<settlements->count) ? settlements->settlements[id].pos : (BrzPos){0,0};
}
static double local_resource_get(void* context, int id, int resource)
{
    LocalSettlements* settlements=(LocalSettlements*)context;
    if(id<0 || id>=settlements->count || resource<0) return 0;
    return settlements->settlements[id].res_inv[resource];
}
static void local_resource_add(void* context, int id, int resource, double amount)
{
    LocalSettlements* settlements=(LocalSettlements*)context;
    if(id<0 || id>=settlements->count || resource<0) return;
    settlements->settlements[id].res_inv[resource]+=amount;
    if(settlements->settlements[id].res_inv[resource]<0) settlements->settlements[id].res_inv[resource]=0;
}
static double local_item_get(void* context, int id, int item)
{
    LocalSettlements* settlements=(LocalSettlements*)context;
    if(id<0 || id>=settlements->count || item<0) return 0;
    return settlements->settlements[id].item_inv[item];
}
static void local_item_add(void* context, int id, int item, double amount)
{
    LocalSettlements* settlements=(LocalSettlements*)context;
    if(id<0 || id>=settlements->count || item<0) return;
    settlements->settlements[id].item_inv[item]+=amount;
    if(settlements->settlements[id].item_inv[item]<0) settlements->settlements[id].item_inv[item]=0;
}
static double local_resource_price(void* context, int id, int resource)
{
    LocalSettlements* settlements=(LocalSettlements*)context;
    if(id<0 || id>=settlements->count) return 1.0;
    return brz_settlement_price_res(&settlements->settlements[id],settlements->config,resource);
}
static double local_item_price(void* context, int id, int item)
{
    LocalSettlements* settlements=(LocalSettlements*)context;
    if(id<0 || id>=settlements->count) return 1.0;
    return brz_settlement_price_item(&settlements->settlements[id],item);
}
void bronze_settlement_port_init(BronzeSettlementPort* port, void* settlements, int count,
                                 const ParsedConfig* config)
{
    LocalSettlements* local;
    if(!port) return;
    local=(LocalSettlements*)malloc(sizeof(*local));
    if(!local){ memset(port,0,sizeof(*port)); return; }
    local->settlements=(BrzSettlement*)settlements; local->count=count; local->config=config;
    port->context=local; port->nearest=local_settlement_nearest;
    port->position=local_settlement_position;
    port->resource_get=local_resource_get; port->resource_add=local_resource_add;
    port->item_get=local_item_get; port->item_add=local_item_add;
    port->resource_price=local_resource_price; port->item_price=local_item_price;
}

void bronze_settlement_port_destroy(BronzeSettlementPort* port)
{
    if(!port) return;
    free(port->context); memset(port,0,sizeof(*port));
}

void bronze_event_emit(const BronzeEventSink* sink, BronzeEventKind kind,
                       uint32_t actor_id, int settlement_id, const char* subject,
                       double amount, BronzeActionResult result, int day)
{
    if(!sink || !sink->emit) return;
    BronzeEvent event;
    event.kind=kind; event.actor_id=actor_id; event.settlement_id=settlement_id;
    event.subject=subject; event.amount=amount; event.result=result; event.day=day;
    sink->emit(sink->context,&event);
}
