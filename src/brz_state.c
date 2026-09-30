#include "brz_state.h"
#include <string.h>

int brz_state_init(BrzSimulationState* s, const ParsedConfig* cfg)
{
    int w,h; if(!s || !cfg || !cfg->vocations.len) return 1; memset(s,0,sizeof(*s)); s->config=cfg;
    s->resource_count=kind_table_count(&cfg->resource_kinds); s->item_count=kind_table_count(&cfg->item_kinds);
    s->agent_count=cfg->agent_count>0?cfg->agent_count:(int)cfg->vocations.len; s->settlement_count=cfg->settlement_count>0?cfg->settlement_count:1;
    w=80; h=40; for(size_t i=0;i<cfg->params.len;i++){ const ParamDef* p=brz_vec_cat(&cfg->params,i); if(!p->has_svalue && brz_streq(p->key,"sim_map_w")) w=(int)p->value; if(!p->has_svalue && brz_streq(p->key,"sim_map_h")) h=(int)p->value; }
    if(brz_world_init(&s->world,cfg,w,h,s->resource_count) || brz_settlements_alloc(&s->settlements,s->settlement_count,s->resource_count,s->item_count)) goto fail;
    brz_settlements_place(s->settlements,s->settlement_count,w,h,cfg->seed); brz_world_stamp_fields_around_settlements(&s->world,s->settlements,s->settlement_count,8);
    bronze_world_port_init(&s->world_port,&s->world,s->resource_count); bronze_settlement_port_init(&s->settlement_port,s->settlements,s->settlement_count,cfg);
    if(!s->world_port.context || !s->settlement_port.context || brz_agents_alloc_and_spawn(&s->agents,s->agent_count,cfg,s->settlements,s->settlement_count,s->resource_count,s->item_count,cfg->seed)) goto fail;
    brz_rng_seed(&s->rng,cfg->seed); return 0; fail: brz_state_destroy(s); return 1;
}
int brz_state_step(BrzSimulationState* s, const BronzeEventSink* events)
{ if(!s||!s->config) return 1; s->day++; s->world_port.step_regen(s->world_port.context); brz_settlements_begin_day(s->settlements,s->settlement_count); for(int i=0;i<s->agent_count;i++){ BronzeActorPort a; bronze_actor_port_init(&a,&s->agents[i]); brz_agent_step(&s->agents[i],&a,s->config,&s->world_port,&s->settlement_port,&s->rng,events,s->day); } return 0; }
void brz_state_destroy(BrzSimulationState* s)
{ if(!s) return; brz_agents_free(s->agents,s->agent_count); bronze_settlement_port_destroy(&s->settlement_port); brz_settlements_free(s->settlements,s->settlement_count); bronze_world_port_destroy(&s->world_port); brz_world_free(&s->world); memset(s,0,sizeof(*s)); }
