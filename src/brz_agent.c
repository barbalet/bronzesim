
#include "brz_agent.h"
#include "brz_kinds.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

/* ---- Small expression evaluator and DSL executor ported from old brz_sim.c ---- */

static double clamp01(double v){ if(v<0) return 0; if(v>1) return 1; return v; }

static void ex_skip(const char** s){ while(**s && isspace((unsigned char)**s)) (*s)++; }
static int ex_peek_word(const char* s, const char* w){
    ex_skip(&s);
    size_t n=strlen(w);
    return strncmp(s,w,n)==0 && (s[n]==0 || isspace((unsigned char)s[n]) || s[n]==')' || s[n]=='(');
}
static int ex_consume_word(const char** s, const char* w){
    ex_skip(s);
    size_t n=strlen(w);
    if(strncmp(*s,w,n)==0){ *s += n; return 1; }
    return 0;
}
static int ex_read_ident(const char** s, char* out, int out_n){
    ex_skip(s);
    int i=0;
    if(!(isalpha((unsigned char)**s) || **s=='_')) return 0;
    while(**s && (isalnum((unsigned char)**s) || **s=='_' || **s=='.')){
        if(i<out_n-1) out[i++] = **s;
        (*s)++;
    }
    out[i]=0;
    return 1;
}
static int ex_read_num(const char** s, double* out){
    ex_skip(s);
    char* end=NULL;
    double v = strtod(*s,&end);
    if(end==*s) return 0;
    *out=v; *s=end; return 1;
}
static int ex_read_op(const char** s, char* op2){
    ex_skip(s);
    if(strncmp(*s,">=",2)==0 || strncmp(*s,"<=",2)==0 || strncmp(*s,"==",2)==0 || strncmp(*s,"!=",2)==0){
        op2[0]=(*s)[0]; op2[1]=(*s)[1]; op2[2]=0; *s += 2; return 1;
    }
    if(**s=='>'||**s=='<'){
        op2[0]=**s; op2[1]=0; (*s)++; return 1;
    }
    return 0;
}

static double agent_var(const BronzeActorPort* actor, const ParsedConfig* cfg, const char* name){
    (void)cfg;
    return actor->need(actor->context,name);
}

static int eval_cmp_or_prob(const char** s, const BronzeActorPort* actor, const ParsedConfig* cfg, BrzRng* rng){
    /* Supports:
       - hunger < 0.5
       - fatigue >= 0.2
       - chance(0.3)
    */
    ex_skip(s);
    if(ex_consume_word(s,"chance")){
        ex_skip(s);
        if(**s=='('){ (*s)++; }
        double p=0;
        if(!ex_read_num(s,&p)) return 0;
        ex_skip(s);
        if(**s==')') (*s)++;
        int roll = (int)(brz_rng_u32(rng)%10000u);
        int thresh = (int)(clamp01(p)*10000.0);
        return roll < thresh;
    }

    char ident[128];
    if(!ex_read_ident(s,ident,sizeof(ident))) return 0;

    if(brz_streq(ident,"true")) return 1;
    if(brz_streq(ident,"false")) return 0;
    /* Legacy scenarios use `prob 0.25`; retain it as the concise form of
       chance(0.25) while compiled scenario conditions remain deterministic. */
    if(brz_streq(ident,"prob")){
        double p=0; int roll;
        if(!ex_read_num(s,&p)) return 0;
        roll=(int)(brz_rng_u32(rng)%10000u);
        return roll < (int)(clamp01(p)*10000.0);
    }

    double lhs = agent_var(actor,cfg,ident);
    char op2[3]={0};
    if(!ex_read_op(s,op2)) return lhs!=0.0; /* truthy */
    double rhs=0;
    if(!ex_read_num(s,&rhs)) return 0;

    if(strcmp(op2,">")==0) return lhs>rhs;
    if(strcmp(op2,"<")==0) return lhs<rhs;
    if(strcmp(op2,">=")==0) return lhs>=rhs;
    if(strcmp(op2,"<=")==0) return lhs<=rhs;
    if(strcmp(op2,"==")==0) return lhs==rhs;
    if(strcmp(op2,"!=")==0) return lhs!=rhs;
    return 0;
}

static int eval_atom(const char** s, const BronzeActorPort* actor, const ParsedConfig* cfg, BrzRng* rng){
    ex_skip(s);
    if(**s=='('){ (*s)++; int v = eval_cmp_or_prob(s,actor,cfg,rng); ex_skip(s); if(**s==')') (*s)++; return v; }
    return eval_cmp_or_prob(s,actor,cfg,rng);
}
static int eval_and(const char** s, const BronzeActorPort* actor, const ParsedConfig* cfg, BrzRng* rng){
    int v = eval_atom(s,actor,cfg,rng);
    for(;;){
        if(ex_peek_word(*s,"and")){ ex_consume_word(s,"and"); v = v && eval_atom(s,actor,cfg,rng); }
        else break;
    }
    return v;
}
static int eval_or(const char** s, const BronzeActorPort* actor, const ParsedConfig* cfg, BrzRng* rng){
    int v = eval_and(s,actor,cfg,rng);
    for(;;){
        if(ex_peek_word(*s,"or")){ ex_consume_word(s,"or"); v = v || eval_and(s,actor,cfg,rng); }
        else break;
    }
    return v;
}
static int eval_when_expr(const char* expr, const BronzeActorPort* actor, const ParsedConfig* cfg, BrzRng* rng){
    if(!expr || !expr[0]) return 1;
    const char* s = expr;
    int v = eval_or(&s,actor,cfg,rng);
    return v ? 1 : 0;
}

/* ---- Recipes (compiled from scenario content) ---- */
static int craft_with_recipe(BronzeActorPort* actor, const RecipeDef* recipe, double n){
    if(!recipe || recipe->output_amount<=0) return 0;
    double batches=n;
    for(size_t i=0;i<recipe->inputs.len;i++){
        const RecipeInputDef* in=(const RecipeInputDef*)brz_vec_cat(&recipe->inputs,i);
        int rid=in->resource_id, iid=in->item_id;
        double available=(rid>=0)?actor->resource_get(actor->context,rid):(iid>=0?actor->item_get(actor->context,iid):0.0);
        if(in->amount>0 && available/in->amount<batches) batches=available/in->amount;
    }
    if(batches<=0) return 1; /* valid recipe but insufficient inputs */
    for(size_t i=0;i<recipe->inputs.len;i++){
        const RecipeInputDef* in=(const RecipeInputDef*)brz_vec_cat(&recipe->inputs,i);
        int rid=in->resource_id, iid=in->item_id;
        if(rid>=0) actor->resource_add(actor->context,rid,-batches*in->amount);
        else if(iid>=0) actor->item_add(actor->context,iid,-batches*in->amount);
    }
    int out_r=recipe->output_resource_id, out_i=recipe->output_item_id;
    if(out_r>=0) actor->resource_add(actor->context,out_r,batches*recipe->output_amount);
    else if(out_i>=0) actor->item_add(actor->context,out_i,batches*recipe->output_amount);
    else return 0;
    return 1;
}

/* ---- Action execution against world/settlements ---- */

static int actor_at_settlement(const BronzeActorPort* actor, BrzPos settlement_position){
    return brz_dist_manhattan(actor->position(actor->context), settlement_position) <= 1;
}

typedef void (*CompiledActionHandler)(BronzeActorPort*, const ParsedConfig*, BronzeWorldPort*,
                                      BronzeSettlementPort*, const OpDef*, BrzRng*,
                                      const BronzeEventSink*, int);

typedef struct { int code; CompiledActionHandler execute; } CompiledActionRegistration;

static void execute_compiled_action(BronzeActorPort* actor, const ParsedConfig* cfg, BronzeWorldPort* world,
                                    BronzeSettlementPort* settlements, const OpDef* op, BrzRng* rng,
                                    const BronzeEventSink* events, int day)
{
    (void)rng;
    const char* arg0 = op->a0 ? op->a0 : "";
    double n = (op->has_n0 ? op->n0 : 1.0);

    if(op->action_code==BRZ_ACTION_GATHER)
    {
        int rid = op->arg0_resource_id;
        double gathered=0;
        if(rid >= 0){
            const ResourceDef* resource=brz_resource_find(cfg,arg0);
            uint16_t need = resource ? resource->terrain_query : 0;
            if(need){
                if(!(world->tags_at(world->context, actor->position(actor->context)) & need)){
                    /* set target toward nearest suitable tile */
                    actor->set_target(actor->context,world->nearest_tag(world->context, actor->position(actor->context), need, 32));
                }else{
                    double reserved=world->reserve(world->context,actor->position(actor->context),rid,n);
                    gathered = world->take(world->context, actor->position(actor->context), rid, reserved);
                    actor->resource_add(actor->context,rid,gathered);
                }
            }else{
                double reserved=world->reserve(world->context,actor->position(actor->context),rid,n);
                gathered = world->take(world->context, actor->position(actor->context), rid, reserved);
                actor->resource_add(actor->context,rid,gathered);
            }
        }
        actor->need_add(actor->context,"fatigue",0.04 + 0.005*n);
        actor->need_add(actor->context,"hunger",0.02);
        if(gathered>0) bronze_event_emit(events,BRZ_EVENT_GATHERED,actor->id(actor->context),actor->home_settlement(actor->context),arg0,gathered,BRZ_RESULT_COMPLETED,day);
    }
    else if(op->action_code==BRZ_ACTION_CRAFT)
    {
        /* crafting mostly at settlement, but allow anywhere */
        const RecipeDef* recipe=(op->recipe_index>=0 && (size_t)op->recipe_index<cfg->recipes.len)
                                ? (const RecipeDef*)brz_vec_cat(&cfg->recipes,(size_t)op->recipe_index) : NULL;
        if(!craft_with_recipe(actor, recipe, n)){
            int iid = op->arg0_item_id;
            if(iid >= 0) actor->item_add(actor->context,iid,n);
        }
        actor->need_add(actor->context,"fatigue",0.05 + 0.01*n);
        actor->need_add(actor->context,"hunger",0.02);
        bronze_event_emit(events,BRZ_EVENT_CRAFTED,actor->id(actor->context),actor->home_settlement(actor->context),arg0,n,BRZ_RESULT_COMPLETED,day);
    }
    else if(op->action_code==BRZ_ACTION_TRADE)
    {
        /* trade give(arg0) for want(arg1) through settlement market */
        int si = settlements->nearest(settlements->context, actor->position(actor->context));
        double completed=0;
        if(si >= 0 && actor_at_settlement(actor, settlements->position(settlements->context,si))){
            int give_r = op->arg0_resource_id;
            int want_r = op->arg1_resource_id;
            int give_i = op->arg0_item_id;
            int want_i = op->arg1_item_id;

            double give_amt = 1.0;
            if(give_r>=0 && actor->resource_get(actor->context,give_r) >= give_amt){
                double pg = settlements->resource_price(settlements->context,si,give_r);
                double pw = (want_r>=0) ? settlements->resource_price(settlements->context,si,want_r)
                                        : (want_i>=0 ? settlements->item_price(settlements->context,si,want_i) : 1.0);
                double want_amt = (pw>0? (give_amt*pg/pw) : 0.0);
                if(want_amt <= 0) want_amt = 0;
                /* settlement pays out want if stock */
                if(want_r>=0){
                    double pay = want_amt;
                    if(settlements->resource_get(settlements->context,si,want_r) < pay) pay = settlements->resource_get(settlements->context,si,want_r);
                    settlements->resource_add(settlements->context,si,want_r,-pay);
                    actor->resource_add(actor->context,want_r,pay);
                    completed=pay;
                }else if(want_i>=0){
                    double pay = want_amt;
                    if(settlements->item_get(settlements->context,si,want_i) < pay) pay = settlements->item_get(settlements->context,si,want_i);
                    settlements->item_add(settlements->context,si,want_i,-pay);
                    actor->item_add(actor->context,want_i,pay);
                    completed=pay;
                }
                if(completed>0){
                    actor->resource_add(actor->context,give_r,-give_amt);
                    settlements->resource_add(settlements->context,si,give_r,give_amt);
                }
            }else if(give_i>=0 && actor->item_get(actor->context,give_i) >= give_amt){
                double pg = settlements->item_price(settlements->context,si,give_i);
                double pw = (want_r>=0) ? settlements->resource_price(settlements->context,si,want_r)
                                        : (want_i>=0 ? settlements->item_price(settlements->context,si,want_i) : 1.0);
                double want_amt = (pw>0? (give_amt*pg/pw) : 0.0);
                if(want_r>=0){
                    double pay = want_amt;
                    if(settlements->resource_get(settlements->context,si,want_r) < pay) pay = settlements->resource_get(settlements->context,si,want_r);
                    settlements->resource_add(settlements->context,si,want_r,-pay);
                    actor->resource_add(actor->context,want_r,pay);
                    completed=pay;
                }else if(want_i>=0){
                    double pay = want_amt;
                    if(settlements->item_get(settlements->context,si,want_i) < pay) pay = settlements->item_get(settlements->context,si,want_i);
                    settlements->item_add(settlements->context,si,want_i,-pay);
                    actor->item_add(actor->context,want_i,pay);
                    completed=pay;
                }
                if(completed>0){
                    actor->item_add(actor->context,give_i,-give_amt);
                    settlements->item_add(settlements->context,si,give_i,give_amt);
                }
            }
        }else{
            /* move toward nearest settlement */
            if(si >= 0){
                actor->set_target(actor->context,settlements->position(settlements->context,si));
            }
        }
        actor->need_add(actor->context,"fatigue",0.02);
        actor->need_add(actor->context,"hunger",0.01);
        bronze_event_emit(events,BRZ_EVENT_TRADED,actor->id(actor->context),si,arg0,completed,
                          completed>0 ? BRZ_RESULT_COMPLETED : BRZ_RESULT_UNAVAILABLE,day);
    }
    else if(op->action_code==BRZ_ACTION_REST)
    {
        actor->need_add(actor->context,"fatigue",-0.1);
        actor->need_add(actor->context,"hunger",0.01);
        bronze_event_emit(events,BRZ_EVENT_RESTED,actor->id(actor->context),actor->home_settlement(actor->context),"rest",1.0,BRZ_RESULT_COMPLETED,day);
    }
    else if(op->action_code==BRZ_ACTION_MOVE_TO || op->action_code==BRZ_ACTION_ROAM || op->action_code==BRZ_ACTION_WANDER)
    {
        uint16_t t = op->terrain_query;
        BrzPos position=actor->position(actor->context);
        BrzPos target=actor->target(actor->context);
        if(!actor->has_target(actor->context) || brz_dist_manhattan(position,target) == 0){
            target=world->nearest_tag(world->context, position, t, 32);
            actor->set_target(actor->context,target);
        }
        position=brz_step_toward(position,target);
        actor->set_position(actor->context,position);
        if(brz_dist_manhattan(position,target)==0) actor->clear_target(actor->context);
        actor->need_add(actor->context,"fatigue",0.04);
        actor->need_add(actor->context,"hunger",0.01);
    }
}

/* Scenario content resolves names to BrzActionCode during parsing.  This table
   is the only execution dispatch point; adapters can register a different
   implementation for a supported code without reintroducing string branches. */
static const CompiledActionRegistration compiled_actions[] = {
    {BRZ_ACTION_GATHER, execute_compiled_action}, {BRZ_ACTION_CRAFT, execute_compiled_action},
    {BRZ_ACTION_TRADE, execute_compiled_action}, {BRZ_ACTION_REST, execute_compiled_action},
    {BRZ_ACTION_MOVE_TO, execute_compiled_action}, {BRZ_ACTION_ROAM, execute_compiled_action},
    {BRZ_ACTION_WANDER, execute_compiled_action}
};

static void exec_op(BronzeActorPort* actor, const ParsedConfig* cfg, BronzeWorldPort* world,
                    BronzeSettlementPort* settlements, const OpDef* op, BrzRng* rng,
                    const BronzeEventSink* events, int day)
{
    for(size_t i=0;i<sizeof(compiled_actions)/sizeof(compiled_actions[0]);i++){
        if(compiled_actions[i].code==op->action_code){
            compiled_actions[i].execute(actor,cfg,world,settlements,op,rng,events,day);
            return;
        }
    }
}


/* statement execution */

static void exec_stmt(BronzeActorPort* actor, const ParsedConfig* cfg, BronzeWorldPort* world, BronzeSettlementPort* settlements,
                      const StmtDef* st, BrzRng* rng, const BronzeEventSink* events, int day);

static void exec_stmts_vec(BronzeActorPort* actor, const ParsedConfig* cfg, BronzeWorldPort* world, BronzeSettlementPort* settlements,
                           const BrzVec* stmts, BrzRng* rng, const BronzeEventSink* events, int day)
{
    for(size_t i=0;i<stmts->len;i++){
        const StmtDef* st = (const StmtDef*)brz_vec_cat(stmts, i);
        exec_stmt(actor, cfg, world, settlements, st, rng, events, day);
    }
}

static void exec_stmt(BronzeActorPort* actor, const ParsedConfig* cfg, BronzeWorldPort* world, BronzeSettlementPort* settlements,
                      const StmtDef* st, BrzRng* rng, const BronzeEventSink* events, int day)
{
    if(st->kind == ST_OP){
        exec_op(actor, cfg, world, settlements, &st->as.op, rng, events, day);
    }else if(st->kind == ST_CHANCE){
        /* percent 0..100 */
        double pct = st->as.chance.chance_pct;
        if(pct < 0) { pct = 0; }
        if(pct > 100) { pct = 100; }
        int roll = (int)(brz_rng_u32(rng)%10000u);
        int thr = (int)((pct/100.0)*10000.0);
        if(roll < thr){
            exec_stmts_vec(actor, cfg, world, settlements, &st->as.chance.body, rng, events, day);
        }
    }else if(st->kind == ST_WHEN){
        if(eval_when_expr(st->as.when_stmt.when_expr, actor, cfg, rng)){
            exec_stmts_vec(actor, cfg, world, settlements, &st->as.when_stmt.body, rng, events, day);
        }
    }
}

/* auto-eat from own resources and settlement */
static void agent_auto_eat(BronzeActorPort* actor, const ParsedConfig* cfg, BronzeSettlementPort* settlements)
{
    int si = actor->home_settlement(actor->context);

    if(actor->need(actor->context,"hunger") > 0.7)
    {
        double eat = 0.0; int selected=-1;
        int preferred=kind_table_find(&cfg->resource_kinds,cfg->settlement_policy.food_fallback);
        if(preferred>=0){
            const ResourceDef* def=brz_resource_find(cfg,kind_table_name(&cfg->resource_kinds,preferred));
            if(def && def->nutrition>0 && actor->resource_get(actor->context,preferred)>0){ selected=preferred; eat=def->nutrition; }
        }
        for(size_t rid=0;rid<kind_table_count(&cfg->resource_kinds);rid++){
            const char* name=kind_table_name(&cfg->resource_kinds,(int)rid);
            const ResourceDef* def=brz_resource_find(cfg,name);
            if(selected<0 && def && def->nutrition>0 && actor->resource_get(actor->context,(int)rid)>0){ selected=(int)rid; eat=def->nutrition; break; }
        }
        if(selected>=0) actor->resource_add(actor->context,selected,-1);
        if(eat<=0.0 && si>=0 && actor_at_settlement(actor,settlements->position(settlements->context,si))){
            for(size_t rid=0;rid<kind_table_count(&cfg->resource_kinds);rid++){
                const char* name=kind_table_name(&cfg->resource_kinds,(int)rid);
                const ResourceDef* def=brz_resource_find(cfg,name);
                if(def && def->nutrition>0 && settlements->resource_get(settlements->context,si,(int)rid)>0){
                    settlements->resource_add(settlements->context,si,(int)rid,-1);
                    eat=def->nutrition; break;
                }
            }
        }
        actor->need_add(actor->context,"hunger",-eat);
    }
}

/* auto-rest: when at home settlement, reduce fatigue (keeps agents active long-term) */
static void agent_auto_rest(BronzeActorPort* actor, const ParsedConfig* cfg, BronzeSettlementPort* settlements)
{
    int si = actor->home_settlement(actor->context);
    if(si<0) return;

    if(actor_at_settlement(actor, settlements->position(settlements->context,si))){
        /* a little recovery every day at home */
        double recovery=cfg->settlement_policy.rest_recovery;
        if(recovery<=0) recovery=0.04;
        actor->need_add(actor->context,"fatigue",-recovery);
        /* if exhausted, recover more aggressively */
        if(actor->need(actor->context,"fatigue") > 0.85) actor->need_add(actor->context,"fatigue",-0.10);
    }
}


/* rule selection: first matching 'when' or fallback first */

/* weighted rule selection among matching whens (or all if no when) */
static const RuleDef* pick_rule(const BrzAgent* a, const BronzeActorPort* actor, const ParsedConfig* cfg, BrzRng* rng)
{
    (void)cfg;
    if(!a->voc) return NULL;

    double total_w = 0.0;
    /* first pass: compute total weight of matching rules */
    for(size_t i=0;i<a->voc->rules.len;i++){
        const RuleDef* r = (const RuleDef*)brz_vec_cat(&a->voc->rules, i);
        int ok = 1;
        if(r->when_expr && r->when_expr[0]){
            ok = eval_when_expr(r->when_expr, actor, cfg, rng);
        }
        if(ok){
            int w = (r->weight > 0) ? r->weight : 1;
            total_w += (double)w;
        }
    }
    if(total_w <= 0.0) return NULL;

    double pick = (double)(brz_rng_u32(rng)%100000u) / 100000.0 * total_w;
    double cur = 0.0;
    for(size_t i=0;i<a->voc->rules.len;i++){
        const RuleDef* r = (const RuleDef*)brz_vec_cat(&a->voc->rules, i);
        int ok = 1;
        if(r->when_expr && r->when_expr[0]){
            ok = eval_when_expr(r->when_expr, actor, cfg, rng);
        }
        if(ok){
            int w = (r->weight > 0) ? r->weight : 1;
            cur += (double)w;
            if(cur >= pick) return r;
        }
    }
    /* fallback */
    return (const RuleDef*)brz_vec_cat(&a->voc->rules, 0);
}



int brz_agents_alloc_and_spawn(BrzAgent** out, int agent_n, const ParsedConfig* cfg,
                               const BrzSettlement* setts, int sett_n,
                               size_t res_n, size_t item_n, unsigned seed)
{
    *out = (BrzAgent*)calloc((size_t)agent_n, sizeof(BrzAgent));
    if(!*out) return 1;

    BrzRng rng; brz_rng_seed(&rng, seed?seed:0xC0FFEEu);

    for(int i=0;i<agent_n;i++){
        BrzAgent* a = &(*out)[i];
        a->id = (uint32_t)i;
        a->voc = (const VocationDef*)brz_vec_cat(&cfg->vocations, (size_t)i % cfg->vocations.len);
        a->home_settlement = (sett_n>0) ? (i % sett_n) : 0;
        a->pos = (sett_n>0) ? setts[a->home_settlement].pos : (BrzPos){ brz_rng_range(&rng,0,50), brz_rng_range(&rng,0,50) };
        a->hunger = 0.3 + 0.4*(double)(brz_rng_u32(&rng)%1000u)/1000.0;
        a->fatigue = 0.2;
        a->res_n = res_n; a->item_n = item_n;
        a->res_inv = (double*)calloc(res_n, sizeof(double));
        a->item_inv = (double*)calloc(item_n, sizeof(double));
        if(!a->res_inv || !a->item_inv) return 1;
    }
    return 0;
}

void brz_agents_free(BrzAgent* agents, int agent_n){
    if(!agents) return;
    for(int i=0;i<agent_n;i++){
        free(agents[i].res_inv);
        free(agents[i].item_inv);
    }
    free(agents);
}

void brz_agent_step(BrzAgent* a, BronzeActorPort* actor, const ParsedConfig* cfg, BronzeWorldPort* world,
                    BronzeSettlementPort* settlements, BrzRng* rng,
                    const BronzeEventSink* events, int day)
{
    /* baseline drift (daily metabolism + rest)
       NOTE: fatigue naturally recovers a bit each day; hard work re-adds fatigue. */
    actor->need_add(actor->context,"hunger",0.02);
    actor->need_add(actor->context,"fatigue",-0.005);

    /* execute one rule per day */
    const RuleDef* r = pick_rule(a, actor, cfg, rng);
    if(r && r->do_task){
        bronze_event_emit(events,BRZ_EVENT_OCCUPATION_SELECTED,actor->id(actor->context),actor->home_settlement(actor->context),
                          r->do_task,1.0,BRZ_RESULT_COMPLETED,day);
        TaskDef* t = brz_voc_find_task((VocationDef*)a->voc, r->do_task);
        if(t){
            exec_stmts_vec(actor, cfg, world, settlements, &t->stmts, rng, events, day);
        }else if(brz_action_find(cfg,r->do_task)){
            OpDef action; memset(&action,0,sizeof(action));
            action.op=r->do_task; action.line=r->line; action.action_code=brz_action_code(r->do_task);
            exec_op(actor,cfg,world,settlements,&action,rng,events,day);
        }
    }

    /* movement toward target if set */
    if(actor->has_target(actor->context)){
        BrzPos position=brz_step_toward(actor->position(actor->context),actor->target(actor->context));
        actor->set_position(actor->context,position);
        if(brz_dist_manhattan(position,actor->target(actor->context))==0) actor->clear_target(actor->context);
    }

    /* clamp positions */
    actor->set_position(actor->context,world->clamp_position(world->context,actor->position(actor->context)));

    agent_auto_rest(actor, cfg, settlements);
    agent_auto_eat(actor, cfg, settlements);

    /* Deposit policy is scenario data: food resources above its threshold are
       communal stock, rather than a special case for named resources. */
    int si = actor->home_settlement(actor->context);
    if(si>=0 && actor_at_settlement(actor,settlements->position(settlements->context,si))){
        double threshold=cfg->settlement_policy.deposit_threshold;
        if(threshold<=0) threshold=2;
        for(size_t rid=0;rid<kind_table_count(&cfg->resource_kinds);rid++){
            const ResourceDef* def=brz_resource_find(cfg,kind_table_name(&cfg->resource_kinds,(int)rid));
            if(def && def->nutrition>0 && actor->resource_get(actor->context,(int)rid)>threshold){
                double move=floor(actor->resource_get(actor->context,(int)rid)-threshold);
                actor->resource_add(actor->context,(int)rid,-move);
                settlements->resource_add(settlements->context,si,(int)rid,move);
                if(move>0) bronze_event_emit(events,BRZ_EVENT_DEPOSITED,actor->id(actor->context),si,
                                              kind_table_name(&cfg->resource_kinds,(int)rid),move,BRZ_RESULT_COMPLETED,day);
            }
        }
    }
}
