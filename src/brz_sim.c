#include "brz_sim.h"
#include "brz_util.h"
#include "brz_kinds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    const VocationDef* voc;
    double* res;   /* resource amounts */
    double* items; /* item amounts */
    size_t res_n;
    size_t item_n;
    double hunger;  /* 0..1 */
    double fatigue; /* 0..1 */
} Agent;

/* ---------------- expression evaluator (very small) ----------------
   Supports:
     - <ident> <op> <number>  where op in > < >= <= == !=
     - prob <p>     where p in [0..1] (random chance)
     - and / or (and binds tighter than or)
   Identifiers supported: hunger, fatigue.
*/
typedef struct {
    const char* s;
    size_t i;
    BrzRng* rng;
    const Agent* a;
} ExprLex;

static void ex_skip(ExprLex* x)
{
    while(x->s[x->i]==' '||x->s[x->i]=='\t') x->i++;
}

static bool ex_peek_word(ExprLex* x, const char* w)
{
    ex_skip(x);
    size_t j = x->i;
    size_t wl = strlen(w);
    if(strncmp(x->s + j, w, wl)!=0) return false;
    char c = x->s[j+wl];
    if(c==0 || c==' '||c=='\t') return true;
    return false;
}

static bool ex_consume_word(ExprLex* x, const char* w)
{
    if(!ex_peek_word(x,w)) return false;
    x->i += strlen(w);
    return true;
}

static bool ex_read_ident(ExprLex* x, char* out, size_t out_cap)
{
    ex_skip(x);
    size_t j=x->i;
    if(!( (x->s[j]>='A'&&x->s[j]<='Z') || (x->s[j]>='a'&&x->s[j]<='z') || x->s[j]=='_' )) return false;
    size_t k=0;
    while( (x->s[x->i]>='A'&&x->s[x->i]<='Z') || (x->s[x->i]>='a'&&x->s[x->i]<='z') ||
           (x->s[x->i]>='0'&&x->s[x->i]<='9') || x->s[x->i]=='_' )
    {
        if(k+1<out_cap) out[k++] = x->s[x->i];
        x->i++;
    }
    out[k]=0;
    return true;
}

static bool ex_read_num(ExprLex* x, double* out)
{
    ex_skip(x);
    char* endp=NULL;
    double v = strtod(x->s + x->i, &endp);
    if(endp == x->s + x->i) return false;
    x->i = (size_t)(endp - x->s);
    *out = v;
    return true;
}

static bool ex_read_op(ExprLex* x, char* out2)
{
    ex_skip(x);
    const char* p = x->s + x->i;
    if(p[0]=='>' && p[1]=='='){ out2[0]='>'; out2[1]='='; out2[2]=0; x->i+=2; return true; }
    if(p[0]=='<' && p[1]=='='){ out2[0]='<'; out2[1]='='; out2[2]=0; x->i+=2; return true; }
    if(p[0]=='=' && p[1]=='='){ out2[0]='='; out2[1]='='; out2[2]=0; x->i+=2; return true; }
    if(p[0]=='!' && p[1]=='='){ out2[0]='!'; out2[1]='='; out2[2]=0; x->i+=2; return true; }
    if(p[0]=='>' || p[0]=='<'){ out2[0]=p[0]; out2[1]=0; x->i+=1; return true; }
    return false;
}

static double agent_var(const Agent* a, const char* ident)
{
    if(brz_streq(ident,"hunger")) return a->hunger;
    if(brz_streq(ident,"fatigue")) return a->fatigue;
    return 0.0;
}

static bool eval_atom(ExprLex* x);

static bool eval_cmp_or_prob(ExprLex* x)
{
    if(ex_consume_word(x, "prob"))
    {
        double p=0.0;
        if(!ex_read_num(x, &p)) return false;
        uint32_t r = brz_rng_u32(x->rng);
        double u = (double)r / (double)UINT32_MAX;
        return u < p;
    }

    char ident[64];
    if(!ex_read_ident(x, ident, sizeof(ident))) return false;
    char op[3];
    if(!ex_read_op(x, op)) return false;
    double rhs=0.0;
    if(!ex_read_num(x, &rhs)) return false;

    double lhs = agent_var(x->a, ident);

    if(brz_streq(op,">"))  return lhs > rhs;
    if(brz_streq(op,"<"))  return lhs < rhs;
    if(brz_streq(op,">=")) return lhs >= rhs;
    if(brz_streq(op,"<=")) return lhs <= rhs;
    if(brz_streq(op,"==")) return fabs(lhs - rhs) < 1e-9;
    if(brz_streq(op,"!=")) return fabs(lhs - rhs) >= 1e-9;
    return false;
}

static bool eval_atom(ExprLex* x)
{
    ex_skip(x);
    /* allow literal "true" / "false" */
    if(ex_consume_word(x, "true")) return true;
    if(ex_consume_word(x, "false")) return false;
    return eval_cmp_or_prob(x);
}

/* and binds tighter than or */
static bool eval_and(ExprLex* x)
{
    bool v = eval_atom(x);
    while(1)
    {
        if(ex_consume_word(x, "and"))
        {
            bool rhs = eval_atom(x);
            v = v && rhs;
            continue;
        }
        break;
    }
    return v;
}

static bool eval_or(ExprLex* x)
{
    bool v = eval_and(x);
    while(1)
    {
        if(ex_consume_word(x, "or"))
        {
            bool rhs = eval_and(x);
            v = v || rhs;
            continue;
        }
        break;
    }
    return v;
}

static bool eval_when(const char* expr, const Agent* a, BrzRng* rng)
{
    if(!expr || !expr[0]) return true;
    ExprLex x; x.s=expr; x.i=0; x.rng=rng; x.a=a;
    bool v = eval_or(&x);
    return v;
}

/* ---------------- task execution ---------------- */

static void clamp01(double* v)
{
    if(*v < 0.0) *v = 0.0;
    if(*v > 1.0) *v = 1.0;
}

static void agent_add_resource(Agent* a, const KindTable* kt, const char* name, double amt)
{
    int id = kind_table_find(kt, name);
    if(id < 0) return;
    if((size_t)id >= a->res_n) return;
    a->res[id] += amt;
    if(a->res[id] < 0) a->res[id] = 0;
}

static void agent_add_item(Agent* a, const KindTable* kt, const char* name, double amt)
{
    int id = kind_table_find(kt, name);
    if(id < 0) return;
    if((size_t)id >= a->item_n) return;
    a->items[id] += amt;
    if(a->items[id] < 0) a->items[id] = 0;
}

static void exec_op(Agent* a, const ParsedConfig* cfg, const OpDef* op, BrzRng* rng)
{
    (void)rng;
    const char* opname = op->op ? op->op : "";
    const char* arg0 = op->a0 ? op->a0 : "";
    double n = (op->has_n0 ? op->n0 : 1.0);

    if(brz_streq(opname, "gather"))
    {
        agent_add_resource(a, &cfg->resource_kinds, arg0, n);
        a->fatigue += 0.04 + 0.005 * n;
        a->hunger  += 0.02;
    }
    else if(brz_streq(opname, "craft"))
    {
        agent_add_item(a, &cfg->item_kinds, arg0, n);
        a->fatigue += 0.05 + 0.01 * n;
        a->hunger  += 0.02;
    }
    else if(brz_streq(opname, "trade"))
    {
        /* very simple: convert 1 unit of arg0 resource into 1 unit of arg1 resource if present */
        const char* give = arg0;
        const char* get  = (op->a1 ? op->a1 : "");
        int gid = kind_table_find(&cfg->resource_kinds, give);
        int rid = kind_table_find(&cfg->resource_kinds, get);
        if(gid>=0 && rid>=0 && (size_t)gid<a->res_n && (size_t)rid<a->res_n && a->res[gid] >= 1.0)
        {
            a->res[gid] -= 1.0;
            a->res[rid] += 1.0;
        }
        a->fatigue += 0.02;
        a->hunger  += 0.01;
    }
    else if(brz_streq(opname, "rest"))
    {
        a->fatigue -= 0.25;
        a->hunger  += 0.01;
    }
    else if(brz_streq(opname, "roam") || brz_streq(opname, "wander") || brz_streq(opname, "move_to"))
    {
        a->fatigue += 0.03;
        a->hunger  += 0.01;
    }
    else
    {
        /* unknown op: no-op but still costs a bit */
        a->fatigue += 0.01;
        a->hunger  += 0.005;
    }

    clamp01(&a->fatigue);
    clamp01(&a->hunger);
}

static void exec_stmts(Agent* a, const ParsedConfig* cfg, const BrzVec* stmts, BrzRng* rng);

static void exec_stmt(Agent* a, const ParsedConfig* cfg, const StmtDef* st, BrzRng* rng)
{
    if(!st) return;
    switch(st->kind)
    {
        case ST_OP:
            exec_op(a, cfg, &st->as.op, rng);
            break;
        case ST_CHANCE:
        {
            double pct = st->as.chance.chance_pct;
            uint32_t r = brz_rng_u32(rng);
            double u = (double)r / (double)UINT32_MAX;
            if(u * 100.0 < pct)
            {
                exec_stmts(a, cfg, &st->as.chance.body, rng);
            }
        } break;
        case ST_WHEN:
            if(eval_when(st->as.when_stmt.when_expr, a, rng))
            {
                exec_stmts(a, cfg, &st->as.when_stmt.body, rng);
            }
            break;
        default:
            break;
    }
}

static void exec_stmts(Agent* a, const ParsedConfig* cfg, const BrzVec* stmts, BrzRng* rng)
{
    for(size_t i=0;i<stmts->len;i++)
    {
        const StmtDef* st = (const StmtDef*)brz_vec_at((BrzVec*)stmts, i);
        exec_stmt(a, cfg, st, rng);
    }
}

static void agent_auto_eat(Agent* a, const ParsedConfig* cfg)
{
    /* If hunger is high and we have food, consume grain or fish to reduce hunger. */
    if(a->hunger < 0.60) return;

    int grain = kind_table_find(&cfg->resource_kinds, "grain");
    int fish  = kind_table_find(&cfg->resource_kinds, "fish");

    if(grain>=0 && (size_t)grain<a->res_n && a->res[grain] >= 1.0)
    {
        a->res[grain] -= 1.0;
        a->hunger -= 0.25;
    }
    else if(fish>=0 && (size_t)fish<a->res_n && a->res[fish] >= 1.0)
    {
        a->res[fish] -= 1.0;
        a->hunger -= 0.20;
    }
    clamp01(&a->hunger);
}

/* ---------------- rule selection ---------------- */

static const RuleDef* pick_rule(const Agent* a, const VocationDef* voc, const ParsedConfig* cfg, BrzRng* rng)
{
    (void)cfg;
    int total = 0;
    /* first pass: sum weights for eligible rules */
    for(size_t i=0;i<voc->rules.len;i++)
    {
        const RuleDef* r = (const RuleDef*)brz_vec_at((BrzVec*)&voc->rules, i);
        if(r->weight <= 0) continue;
        if(eval_when(r->when_expr, a, rng))
            total += r->weight;
    }

    if(total <= 0) return NULL;

    int roll = brz_rng_range(rng, 1, total);
    int acc = 0;
    for(size_t i=0;i<voc->rules.len;i++)
    {
        const RuleDef* r = (const RuleDef*)brz_vec_at((BrzVec*)&voc->rules, i);
        if(r->weight <= 0) continue;
        if(!eval_when(r->when_expr, a, rng)) continue;
        acc += r->weight;
        if(roll <= acc) return r;
    }
    return NULL;
}

static const TaskDef* pick_fallback_task(const VocationDef* voc)
{
    if(voc->tasks.len==0) return NULL;
    return (const TaskDef*)brz_vec_at((BrzVec*)&voc->tasks, 0);
}

static void print_kinds(const char* label, const KindTable* kt)
{
    size_t n = kind_table_count(kt);
    printf("%s (%zu):", label, n);
    for(size_t i=0;i<n && i<12;i++)
    {
        const char* nm = kind_table_name(kt, (int)i);
        printf(" %s", nm && nm[0] ? nm : "(null)");
    }
    if(n > 12) printf(" ...");
    printf("\n");
}

static void print_day_summary(int day, const ParsedConfig* cfg, Agent* agents, size_t agent_n)
{
    /* aggregate key resources if they exist */
    const char* keys[] = { "grain","fish","wood","clay","copper","tin","charcoal","cattle","sheep","pig","religion","nationalism" };
    double sums[sizeof(keys)/sizeof(keys[0])];
    memset(sums, 0, sizeof(sums));
    double hsum=0.0, fsum=0.0;

    for(size_t ai=0; ai<agent_n; ai++)
    {
        Agent* a = &agents[ai];
        hsum += a->hunger;
        fsum += a->fatigue;
        for(size_t k=0;k<sizeof(keys)/sizeof(keys[0]);k++)
        {
            int id = kind_table_find(&cfg->resource_kinds, keys[k]);
            if(id>=0 && (size_t)id<a->res_n) sums[k] += a->res[id];
        }
    }

    printf("Day %d | avg hunger %.3f avg fatigue %.3f |", day, hsum/agent_n, fsum/agent_n);
    for(size_t k=0;k<sizeof(keys)/sizeof(keys[0]);k++)
    {
        int id = kind_table_find(&cfg->resource_kinds, keys[k]);
        if(id>=0)
            printf(" %s=%.1f", keys[k], sums[k]);
    }
    printf("\n");
}

static int find_param_int(const ParsedConfig* cfg, const char* key, int defv)
{
    for(size_t i=0;i<cfg->params.len;i++)
    {
        const ParamDef* p = (const ParamDef*)brz_vec_at((BrzVec*)&cfg->params, i);
        if(p->key && brz_streq(p->key, key))
        {
            return (int)floor(p->value + 0.5);
        }
    }
    return defv;
}

int brz_run(const ParsedConfig* cfg)
{
    if(!cfg) return 1;

    print_kinds("resource kinds", &cfg->resource_kinds);
    print_kinds("item kinds", &cfg->item_kinds);
    printf("vocations (%zu)\n", cfg->vocations.len);

    size_t agent_n = cfg->vocations.len;
    if(agent_n == 0){ fprintf(stderr, "No vocations\n"); return 1; }

    size_t res_n = kind_table_count(&cfg->resource_kinds);
    size_t item_n = kind_table_count(&cfg->item_kinds);

    Agent* agents = (Agent*)calloc(agent_n, sizeof(Agent));
    if(!agents){ fprintf(stderr, "OOM agents\n"); return 1; }

    for(size_t i=0;i<agent_n;i++)
    {
        agents[i].voc = (const VocationDef*)brz_vec_at((BrzVec*)&cfg->vocations, i);
        agents[i].res_n = res_n;
        agents[i].item_n = item_n;
        agents[i].res = (double*)calloc(res_n ? res_n : 1, sizeof(double));
        agents[i].items = (double*)calloc(item_n ? item_n : 1, sizeof(double));
        agents[i].hunger = 0.25;
        agents[i].fatigue = 0.25;
    }

    int days = find_param_int(cfg, "cycles", 60);
    if(days < 1) days = 60;

    BrzRng rng;
    brz_rng_seed(&rng, 0xC0FFEEu);

    for(int day=1; day<=days; day++)
    {
        for(size_t ai=0; ai<agent_n; ai++)
        {
            Agent* a = &agents[ai];
            const VocationDef* voc = a->voc;

            /* baseline drift */
            a->hunger  += 0.035;
            a->fatigue += 0.020;
            clamp01(&a->hunger);
            clamp01(&a->fatigue);

            const RuleDef* r = pick_rule(a, voc, cfg, &rng);
            const TaskDef* task = NULL;
            if(r && r->do_task && r->do_task[0])
                task = brz_voc_find_task((VocationDef*)voc, r->do_task);
            if(!task) task = pick_fallback_task(voc);

            if(task)
                exec_stmts(a, cfg, &task->stmts, &rng);

            agent_auto_eat(a, cfg);
        }

        if(day==1 || day%5==0 || day==days)
            print_day_summary(day, cfg, agents, agent_n);
    }

    /* final per-vocation snapshot (top 10) */
    printf("\nFinal snapshot (first 10 vocations):\n");
    for(size_t i=0;i<agent_n && i<10;i++)
    {
        Agent* a = &agents[i];
        printf("  %s: hunger=%.3f fatigue=%.3f\n", a->voc->name ? a->voc->name : "(null)", a->hunger, a->fatigue);
    }

    for(size_t i=0;i<agent_n;i++)
    {
        free(agents[i].res);
        free(agents[i].items);
    }
    free(agents);
    return 0;
}
