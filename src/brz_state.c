#include "brz_state.h"
#include <string.h>
#include <stdio.h>

typedef struct { char magic[8]; uint32_t version, seed, rng; int32_t day,w,h,settlements,agents; uint32_t resources,items; } BrzCheckpointHeader;
typedef struct { char name[64]; BrzPos pos; int population; } SavedSettlement;
typedef struct { uint32_t id,vocation; BrzPos pos,target; int has_target,home; double hunger,fatigue; } SavedAgent;

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

int brz_state_save(const BrzSimulationState* s, const char* path)
{ BrzCheckpointHeader h={{'B','R','Z','S','T','A','T','E'},1,s->config->seed,s->rng.state,s->day,s->world.w,s->world.h,s->settlement_count,s->agent_count,(uint32_t)s->resource_count,(uint32_t)s->item_count}; FILE* f=fopen(path,"wb"); if(!f) return 1; if(fwrite(&h,sizeof h,1,f)!=1||fwrite(s->world.res,sizeof(double),(size_t)h.w*h.h*h.resources,f)!=(size_t)h.w*h.h*h.resources){fclose(f);return 1;} for(int i=0;i<s->settlement_count;i++){SavedSettlement x;memcpy(x.name,s->settlements[i].name,64);x.pos=s->settlements[i].pos;x.population=s->settlements[i].population;if(fwrite(&x,sizeof x,1,f)!=1||fwrite(s->settlements[i].res_inv,sizeof(double),h.resources,f)!=h.resources||fwrite(s->settlements[i].item_inv,sizeof(double),h.items,f)!=h.items){fclose(f);return 1;}} for(int i=0;i<s->agent_count;i++){SavedAgent x={s->agents[i].id,0,s->agents[i].pos,s->agents[i].target,s->agents[i].has_target,s->agents[i].home_settlement,s->agents[i].hunger,s->agents[i].fatigue}; for(size_t j=0;j<s->config->vocations.len;j++)if(s->agents[i].voc==brz_vec_cat(&s->config->vocations,j))x.vocation=j; if(fwrite(&x,sizeof x,1,f)!=1||fwrite(s->agents[i].res_inv,sizeof(double),h.resources,f)!=h.resources||fwrite(s->agents[i].item_inv,sizeof(double),h.items,f)!=h.items){fclose(f);return 1;}} return fclose(f)!=0; }

int brz_state_load(BrzSimulationState* s, const ParsedConfig* c, const char* path)
{ BrzCheckpointHeader h; FILE* f=fopen(path,"rb"); if(!f)return 1; if(fread(&h,sizeof h,1,f)!=1||memcmp(h.magic,"BRZSTATE",8)||h.version!=1||h.seed!=c->seed){fclose(f);return 1;} if(brz_state_init(s,c)||h.w!=s->world.w||h.h!=s->world.h||h.settlements!=s->settlement_count||h.agents!=s->agent_count||h.resources!=s->resource_count||h.items!=s->item_count)goto bad; if(fread(s->world.res,sizeof(double),(size_t)h.w*h.h*h.resources,f)!=(size_t)h.w*h.h*h.resources)goto bad; for(int i=0;i<h.settlements;i++){SavedSettlement x;if(fread(&x,sizeof x,1,f)!=1||fread(s->settlements[i].res_inv,sizeof(double),h.resources,f)!=h.resources||fread(s->settlements[i].item_inv,sizeof(double),h.items,f)!=h.items)goto bad;memcpy(s->settlements[i].name,x.name,64);s->settlements[i].pos=x.pos;s->settlements[i].population=x.population;} for(int i=0;i<h.agents;i++){SavedAgent x;if(fread(&x,sizeof x,1,f)!=1||x.vocation>=c->vocations.len||fread(s->agents[i].res_inv,sizeof(double),h.resources,f)!=h.resources||fread(s->agents[i].item_inv,sizeof(double),h.items,f)!=h.items)goto bad;s->agents[i].id=x.id;s->agents[i].voc=brz_vec_cat(&c->vocations,x.vocation);s->agents[i].pos=x.pos;s->agents[i].target=x.target;s->agents[i].has_target=x.has_target;s->agents[i].home_settlement=x.home;s->agents[i].hunger=x.hunger;s->agents[i].fatigue=x.fatigue;} s->day=h.day;s->rng.state=h.rng;fclose(f);return 0; bad:fclose(f);brz_state_destroy(s);return 1; }
