/*
 * BRONZESIM — brz_parser.c
 *
 * Implements a small, purpose-built tokenizer and parser
 * for the BRONZESIM Domain-Specific Language (DSL).
 *
 * This parser is intentionally simple:
 *  - No recursion-heavy grammar
 *  - No dynamic AST allocation
 *  - No expression trees beyond simple comparisons
 *
 * Responsibilities:
 *  - Tokenize example.bronze
 *  - Parse blocks such as:
 *      world { ... }
 *      agents { ... }
 *      vocations { vocation X { task ... rule ... } }
 *  - Populate runtime-friendly data structures
 *
 * Design philosophy:
 *  - Fail gracefully where possible
 *  - Warn instead of abort for content author errors
 *  - Never crash the simulation on missing tasks or rules
 *
 * This file is the bridge between text and behavior.
 */

#include "brz_parser.h"
#include "brz_util.h"
#include "brz_world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef enum { TK_EOF=0, TK_WORD, TK_LBRACE, TK_RBRACE } TokKind;

typedef struct
{
    TokKind kind;
    char text[256];
} Tok;

typedef struct
{
    const char* p;
} Lexer;

static void skip_ws_and_comments(Lexer* lx)
{
    for(;;)
    {
        while(*lx->p && isspace((unsigned char)*lx->p)) lx->p++;
        if(*lx->p == '#')
        {
            while(*lx->p && *lx->p != '\n') lx->p++;
            continue;
        }
        break;
    }
}

static void parse_simple_block_kv(Lexer* lx, const char* block, ParsedConfig* cfg);

static Tok next_tok(Lexer* lx)
{
    skip_ws_and_comments(lx);
    Tok t;
    memset(&t,0,sizeof(t));
    if(!*lx->p)
    {
        t.kind = TK_EOF;
        return t;
    }
    if(*lx->p == '{')
    {
        lx->p++;
        t.kind = TK_LBRACE;
        strcpy(t.text,"{");
        return t;
    }
    if(*lx->p == '}')
    {
        lx->p++;
        t.kind = TK_RBRACE;
        strcpy(t.text,"}");
        return t;
    }

    t.kind = TK_WORD;
    size_t n=0;
    while(*lx->p && !isspace((unsigned char)*lx->p) && *lx->p!='{' && *lx->p!='}' && *lx->p!='#')
    {
        if(n+1 < sizeof(t.text)) t.text[n++] = *lx->p;
        lx->p++;
    }
    t.text[n] = 0;
    return t;
}

static bool to_u32(const char* s, uint32_t* out)
{
    char* end=0;
    unsigned long v = strtoul(s,&end,10);
    if(end==s) return false;
    *out = (uint32_t)v;
    return true;
}
static bool to_i32(const char* s, int* out)
{
    char* end=0;
    long v = strtol(s,&end,10);
    if(end==s) return false;
    *out = (int)v;
    return true;
}
static bool to_f32(const char* s, float* out)
{
    char* end=0;
    float v = (float)strtod(s,&end);
    if(end==s) return false;
    *out = v;
    return true;
}

static void tok_expect(Lexer* lx, TokKind k)
{
    Tok t = next_tok(lx);
    BRZ_ASSERT(t.kind == k);
}


static void cfg_ensure_renew_capacity(ParsedConfig* cfg)
{
    if(cfg->renew_per_day && cfg->resources.count > 0)
        return;
    if(cfg->resources.count <= 0)
        return;
    cfg->renew_per_day = (float*)calloc((size_t)cfg->resources.count, sizeof(float));
    BRZ_ASSERT(cfg->renew_per_day != NULL);
}

static int cfg_add_resource_kind(ParsedConfig* cfg, const char* name)
{
    int before = cfg->resources.count;
    int id = kind_table_add(&cfg->resources, name);
    if(id < 0) return -1;
    if(cfg->resources.count != before)
    {
        // grew by one; grow renew array too
        float* nn = (float*)realloc(cfg->renew_per_day, (size_t)cfg->resources.count * sizeof(float));
        BRZ_ASSERT(nn != NULL);
        cfg->renew_per_day = nn;
        cfg->renew_per_day[cfg->resources.count-1] = 0.0f;
    }
    if(cfg->renew_per_day == NULL)
        cfg->renew_per_day = (float*)calloc((size_t)cfg->resources.count, sizeof(float));
    return id;
}

static int cfg_add_item_kind(ParsedConfig* cfg, const char* name)
{
    return kind_table_add(&cfg->items, name);
}


static CmpKind parse_cmp(const char* s)
{
    if(brz_streq(s,">")) return CMP_GT;
    if(brz_streq(s,"<")) return CMP_LT;
    if(brz_streq(s,">=")) return CMP_GE;
    if(brz_streq(s,"<=")) return CMP_LE;
    return CMP_ANY;
}

// when hunger > 0.3
// when fatigue < 0.8
// when season == winter
// when inv fish > 2
// optional: and ... (repeat)
static void parse_condition(Lexer* lx, ParsedConfig* cfg, Condition* c)
{
    memset(c,0,sizeof(*c));
    c->season_eq = SEASON_ANY;

    // consume tokens until we see "do" or "weight" or "}" (caller stops)
    // But our grammar is: rule NAME { when <cond...> do TASK weight N [prob P] }
    // We'll parse multiple clauses separated by "and"
    for(;;)
    {
        Tok a = next_tok(lx);
        if(a.kind==TK_WORD && brz_streq(a.text,"do"))
        {
            // caller will handle "do"; push back not supported, so we must store a sentinel:
            // We'll use a convention: lexer pointer can't go back; so caller must call parse_condition
            // only after consuming "when", and parse_condition must stop before "do".
            // To support this, we detect "do" and error.
            BRZ_ASSERT(!"parse_condition called too far: hit 'do'");
        }
        BRZ_ASSERT(a.kind == TK_WORD);

        if(brz_streq(a.text,"hunger"))
        {
            Tok op = next_tok(lx);
            BRZ_ASSERT(op.kind==TK_WORD);
            Tok v  = next_tok(lx);
            BRZ_ASSERT(v.kind==TK_WORD);
            float fv=0;
            BRZ_ASSERT(to_f32(v.text,&fv));
            // only support hunger > x
            BRZ_ASSERT(brz_streq(op.text,">"));
            c->has_hunger = true;
            c->hunger_threshold = fv;
        }
        else if(brz_streq(a.text,"fatigue"))
        {
            Tok op = next_tok(lx);
            BRZ_ASSERT(op.kind==TK_WORD);
            Tok v  = next_tok(lx);
            BRZ_ASSERT(v.kind==TK_WORD);
            float fv=0;
            BRZ_ASSERT(to_f32(v.text,&fv));
            // only support fatigue < x
            BRZ_ASSERT(brz_streq(op.text,"<"));
            c->has_fatigue = true;
            c->fatigue_threshold = fv;
        }
        else if(brz_streq(a.text,"season"))
        {
            Tok op = next_tok(lx);
            BRZ_ASSERT(op.kind==TK_WORD);
            Tok v  = next_tok(lx);
            BRZ_ASSERT(v.kind==TK_WORD);
            BRZ_ASSERT(brz_streq(op.text,"=="));
            c->season_eq = world_season_parse(v.text);
        }
        else if(brz_streq(a.text,"inv"))
        {
            Tok item = next_tok(lx);
            BRZ_ASSERT(item.kind==TK_WORD);
            Tok op   = next_tok(lx);
            BRZ_ASSERT(op.kind==TK_WORD);
            Tok v    = next_tok(lx);
            BRZ_ASSERT(v.kind==TK_WORD);
            int ik_id = -1;
            ik_id = dsl_parse_item_id(&cfg->items, item.text);
            if(ik_id < 0) ik_id = cfg_add_item_kind(cfg, item.text);
            BRZ_ASSERT(ik_id >= 0);
            int iv=0;
            BRZ_ASSERT(to_i32(v.text,&iv));
            CmpKind ck = parse_cmp(op.text);
            BRZ_ASSERT(ck != CMP_ANY);
            BRZ_ASSERT(c->inv_count < 4);
            c->inv_item[c->inv_count] = ik_id;
            c->inv_cmp[c->inv_count] = ck;
            c->inv_value[c->inv_count] = iv;
            c->inv_count++;
        }
        else if(brz_streq(a.text,"prob"))
        {
            Tok v = next_tok(lx);
            BRZ_ASSERT(v.kind==TK_WORD);
            float fv=0;
            BRZ_ASSERT(to_f32(v.text,&fv));
            if(fv < 0) fv = 0;
            if(fv > 1) fv = 1;
            c->has_prob = true;
            c->prob = fv;
        }
        else
        {
            BRZ_ASSERT(!"Unknown condition clause");
        }

        Tok maybe_and = next_tok(lx);
        if(maybe_and.kind==TK_WORD && brz_streq(maybe_and.text,"and"))
        {
            continue;
        }

        // We consumed one token too far (either "do" or something else).
        // Since we can't push back, we require that the next token is "do".
        BRZ_ASSERT(maybe_and.kind==TK_WORD && brz_streq(maybe_and.text,"do"));
        // Caller expects that we've consumed "do" already; we'll return leaving lexer after "do".
        return;
    }
}

static void parse_task(Lexer* lx, ParsedConfig* cfg, VocationDef* voc)
{
    Tok name = next_tok(lx);
    BRZ_ASSERT(name.kind==TK_WORD);
    BRZ_ASSERT(voc->task_count < MAX_TASKS_PER_VOC);
    TaskDef* t = &voc->tasks[voc->task_count++];
    memset(t,0,sizeof(*t));
    strncpy(t->name, name.text, sizeof(t->name)-1);

    tok_expect(lx, TK_LBRACE);
    for(;;)
    {
        Tok op = next_tok(lx);
        if(op.kind==TK_RBRACE) break;
        BRZ_ASSERT(op.kind==TK_WORD);
        BRZ_ASSERT(t->op_count < MAX_OPS_PER_TASK);

        if(brz_streq(op.text,"move_to"))
        {
            Tok tag = next_tok(lx);
            BRZ_ASSERT(tag.kind==TK_WORD);
            int tagbit=0;
            BRZ_ASSERT(dsl_parse_tagbit(tag.text,&tagbit));
            t->ops[t->op_count++] = (OpDef)
            {
                .kind=OP_MOVE_TO, .arg_i=0, .arg_j=tagbit
            };
        }
        else if(brz_streq(op.text,"gather"))
        {
            Tok res = next_tok(lx);
            BRZ_ASSERT(res.kind==TK_WORD);
            Tok amt = next_tok(lx);
            BRZ_ASSERT(amt.kind==TK_WORD);
            int rk_id = -1;
            BRZ_ASSERT((dsl_parse_resource_id(&cfg->resources, res.text) >= 0));
            int a=0;
            BRZ_ASSERT(to_i32(amt.text,&a));
            t->ops[t->op_count++] = (OpDef)
            {
                .kind=OP_GATHER, .arg_i=a, .arg_j=rk_id
            };
        }
        else if(brz_streq(op.text,"craft"))
        {
            Tok item = next_tok(lx);
            BRZ_ASSERT(item.kind==TK_WORD);
            Tok amt  = next_tok(lx);
            BRZ_ASSERT(amt.kind==TK_WORD);
            int ik_id = -1;
            BRZ_ASSERT((dsl_parse_item_id(&cfg->items, item.text) >= 0));
            int a=0;
            BRZ_ASSERT(to_i32(amt.text,&a));
            t->ops[t->op_count++] = (OpDef)
            {
                .kind=OP_CRAFT, .arg_i=a, .arg_j=ik_id
            };
        }
        else if(brz_streq(op.text,"trade"))
        {
            t->ops[t->op_count++] = (OpDef)
            {
                .kind=OP_TRADE, .arg_i=0, .arg_j=0
            };
        }
        else if(brz_streq(op.text,"rest"))
        {
            t->ops[t->op_count++] = (OpDef)
            {
                .kind=OP_REST, .arg_i=0, .arg_j=0
            };
        }
        else if(brz_streq(op.text,"roam"))
        {
            Tok steps = next_tok(lx);
            BRZ_ASSERT(steps.kind==TK_WORD);
            int s=0;
            BRZ_ASSERT(to_i32(steps.text,&s));
            t->ops[t->op_count++] = (OpDef)
            {
                .kind=OP_ROAM, .arg_i=s, .arg_j=0
            };
        }
        else
        {
            BRZ_ASSERT(!"Unknown op in task");
        }
    }
}

static void parse_rule(Lexer* lx, ParsedConfig* cfg, VocationDef* voc)
{
    Tok name = next_tok(lx);
    BRZ_ASSERT(name.kind==TK_WORD);
    BRZ_ASSERT(voc->rule_count < MAX_RULES_PER_VOC);
    RuleDef* r = &voc->rules[voc->rule_count++];
    memset(r,0,sizeof(*r));
    strncpy(r->name, name.text, sizeof(r->name)-1);

    tok_expect(lx, TK_LBRACE);

    Tok when = next_tok(lx);
    BRZ_ASSERT(when.kind==TK_WORD && brz_streq(when.text,"when"));
    // parse_condition consumes up to and including "do"
    parse_condition(lx, cfg, &r->cond);

    Tok task = next_tok(lx);
    BRZ_ASSERT(task.kind==TK_WORD);
    strncpy(r->task_name, task.text, sizeof(r->task_name)-1);

    Tok wt = next_tok(lx);
    BRZ_ASSERT(wt.kind==TK_WORD && brz_streq(wt.text,"weight"));
    Tok wv = next_tok(lx);
    BRZ_ASSERT(wv.kind==TK_WORD);
    int w=0;
    BRZ_ASSERT(to_i32(wv.text,&w));
    r->weight = (w<0)?0:w;

    // optional: prob P (alternative place, besides inside condition)
    Tok maybe = next_tok(lx);
    if(maybe.kind==TK_WORD && brz_streq(maybe.text,"prob"))
    {
        Tok pv = next_tok(lx);
        BRZ_ASSERT(pv.kind==TK_WORD);
        float f=0;
        BRZ_ASSERT(to_f32(pv.text,&f));
        if(f<0) f=0;
        if(f>1) f=1;
        r->cond.has_prob = true;
        r->cond.prob = f;
        maybe = next_tok(lx);
    }

    BRZ_ASSERT(maybe.kind==TK_RBRACE);
}

static void parse_vocations_block(Lexer* lx, ParsedConfig* cfg)
{
    tok_expect(lx, TK_LBRACE);
    for(;;)
    {
        Tok t = next_tok(lx);
        if(t.kind==TK_RBRACE) break;
        BRZ_ASSERT(t.kind==TK_WORD);
        BRZ_ASSERT(brz_streq(t.text,"vocation"));
        Tok name = next_tok(lx);
        BRZ_ASSERT(name.kind==TK_WORD);

        VocationDef* voc = voc_table_add(&cfg->voc_table, name.text);

        tok_expect(lx, TK_LBRACE);
        for(;;)
        {
            Tok k = next_tok(lx);
            if(k.kind==TK_RBRACE) break;
            BRZ_ASSERT(k.kind==TK_WORD);
            if(brz_streq(k.text,"task"))
            {
                parse_task(lx, cfg, voc);
            }
            else if(brz_streq(k.text,"rule"))
            {
                parse_rule(lx, cfg, voc);
            }
            else
            {
                BRZ_ASSERT(!"Unknown inside vocation");
            }
        }

        // validate rules point at tasks
        for(int ri=0; ri<voc->rule_count; ri++)
        {
            const char* tname = voc->rules[ri].task_name;
            if(voc_task(voc, tname) == NULL)
            {
                // Be forgiving: bind to first task if present, otherwise synthesize an "idle" task.
                if(voc->task_count > 0)
                {
                    strncpy(voc->rules[ri].task_name, voc->tasks[0].name, sizeof(voc->rules[ri].task_name)-1);
                    fprintf(stderr, "WARN: vocation %s rule %s refers to missing task %s; using %s\n",
                            voc->name, voc->rules[ri].name, tname, voc->tasks[0].name);
                }
                else
                {
                    // Create idle task
                    BRZ_ASSERT(voc->task_count < MAX_TASKS_PER_VOC);
                    TaskDef* idle = &voc->tasks[voc->task_count++];
                    memset(idle,0,sizeof(*idle));
                    strncpy(idle->name, "idle", sizeof(idle->name)-1);
                    idle->op_count = 1;
                    idle->ops[0].kind = OP_REST;
                    strncpy(voc->rules[ri].task_name, "idle", sizeof(voc->rules[ri].task_name)-1);
                    fprintf(stderr, "WARN: vocation %s had no tasks; synthesized idle task\n", voc->name);
                }
            }
        }
        for(int i=0; i<voc->rule_count; i++)
        {
        }
    }
}


static void parse_kind_list_block(Lexer* lx, ParsedConfig* cfg, bool is_resource)
{
    tok_expect(lx, TK_LBRACE);
    for(;;)
    {
        Tok t = next_tok(lx);
        if(t.kind == TK_RBRACE) break;
        if(t.kind != TK_WORD) continue;
        if(is_resource)
            cfg_add_resource_kind(cfg, t.text);
        else
            cfg_add_item_kind(cfg, t.text);
    }
}

static void parse_simple_block_kv(Lexer* lx, const char* block, ParsedConfig* cfg)
{
    (void)block;
    tok_expect(lx, TK_LBRACE);

    for(;;)
    {
        Tok k = next_tok(lx);
        if(k.kind == TK_RBRACE) break;
        if(k.kind == TK_EOF) break;
        if(k.kind != TK_WORD) continue;

        Tok v = next_tok(lx);
        if(v.kind == TK_RBRACE) break;
        if(v.kind == TK_EOF) break;

        if(brz_streq(k.text,"seed"))
        {
            int n=0;
            if(to_i32(v.text,&n)) cfg->seed = (uint32_t)n;
        }
        else if(brz_streq(k.text,"settlement_count"))
        {
            int n=0;
            if(to_i32(v.text,&n)) cfg->settlement_count = n;
        }
        else if(brz_streq(k.text,"agent_count"))
        {
            int n=0;
            if(to_i32(v.text,&n)) cfg->agent_count = n;
        }
        else if(brz_streq(k.text,"cache_max"))
        {
            int n=0;
            if(to_i32(v.text,&n)) cfg->cache_max = (uint32_t)n;
        }
        else if(brz_streq(k.text,"snapshot_every"))
        {
            int n=0;
            if(to_i32(v.text,&n)) cfg->snapshot_every_days = n;
        }
        else if(brz_streq(k.text,"map_every"))
        {
            int n=0;
            if(to_i32(v.text,&n)) cfg->map_every_days = n;
        }
        else
        {
            // Dynamic resource renew values: "<resource>_renew <float>"
            const char* kt = k.text;
            size_t n = strlen(kt);
            if(n > 6 && strcmp(kt + (n-6), "_renew")==0)
            {
                char name[256];
                size_t base_n = n - 6;
                if(base_n >= sizeof(name)) base_n = sizeof(name)-1;
                memcpy(name, kt, base_n);
                name[base_n] = 0;

                float f=0;
                if(to_f32(v.text,&f))
                {
                    int rid = kind_table_find(&cfg->resources, name);
                    if(rid < 0) rid = cfg_add_resource_kind(cfg, name);
                    if(rid >= 0)
                    {
                        if(cfg->renew_per_day == NULL) cfg_ensure_renew_capacity(cfg);
                        cfg->renew_per_day[rid] = f;
                    }
                }
            }
        }
    }
}


void parse_kinds_block(Lexer* lx, ParsedConfig* cfg)
{
    tok_expect(lx, TK_LBRACE);
    for(;;)
    {
        Tok t = next_tok(lx);
        if(t.kind == TK_RBRACE) break;
        if(t.kind != TK_WORD) continue;
        if(brz_streq(t.text,"resources"))
        {
            parse_kind_list_block(lx, cfg, true);
        }
        else if(brz_streq(t.text,"items"))
        {
            parse_kind_list_block(lx, cfg, false);
        }
        else
        {
            // unknown sub-block; skip
            Tok k = next_tok(lx);
            if(k.kind == TK_LBRACE)
            {
                int depth=1;
                while(depth>0)
                {
                    Tok tt = next_tok(lx);
                    if(tt.kind == TK_LBRACE) depth++;
                    else if(tt.kind == TK_RBRACE) depth--;
                    else if(tt.kind == TK_EOF) break;
                }
            }
        }
    }

    // ensure renew array exists if resources were declared
    if(cfg->resources.count > 0 && cfg->renew_per_day == NULL)
        cfg_ensure_renew_capacity(cfg);
}
bool brz_parse_file(const char* path, ParsedConfig* out_cfg)
{
    FILE* f = fopen(path,"rb");
    if(!f)
    {
        fprintf(stderr,"Cannot open %s\n", path);
        return false;
    }
    fseek(f,0,SEEK_END);
    long sz = ftell(f);
    fseek(f,0,SEEK_SET);

    char* buf = (char*)malloc((size_t)sz+1);
    if(!buf)
    {
        fclose(f);
        return false;
    }
    fread(buf,1,(size_t)sz,f);
    buf[sz]=0;
    fclose(f);

    ParsedConfig cfg;
    memset(&cfg,0,sizeof(cfg));
    voc_table_init(&cfg.voc_table);
    cfg.seed = 1337;
    cfg.days = 120;
    kind_table_init(&cfg.resources);
    kind_table_init(&cfg.items);
    cfg.renew_per_day = NULL;

    cfg.agent_count = 220;
    cfg.settlement_count = 6;
    cfg.cache_max = 2048;
    cfg.snapshot_every_days = 30;
    cfg.map_every_days = 0;

    cfg.voc_table.vocation_count = 0;

    Lexer lx = { .p = buf };

    Tok t = next_tok(&lx);
    while(t.kind != TK_EOF)
    {
        if(t.kind != TK_WORD)
        {
            t = next_tok(&lx);
            continue;
        }

        if(brz_streq(t.text,"world"))
        {
            tok_expect(&lx, TK_LBRACE);
            for(;;)
            {
                Tok k = next_tok(&lx);
                if(k.kind==TK_RBRACE) break;
                Tok v = next_tok(&lx);
                if(brz_streq(k.text,"seed"))
                {
                    uint32_t s=0;
                    if(to_u32(v.text,&s)) cfg.seed = s;
                }
            }
        }
        else if(brz_streq(t.text,"sim"))
        {
            tok_expect(&lx, TK_LBRACE);
            for(;;)
            {
                Tok k = next_tok(&lx);
                if(k.kind==TK_RBRACE) break;
                Tok v = next_tok(&lx);
                if(brz_streq(k.text,"days"))
                {
                    int d=0;
                    if(to_i32(v.text,&d)) cfg.days = d;
                }
                else if(brz_streq(k.text,"cache_max"))
                {
                    int c=0;
                    if(to_i32(v.text,&c)) cfg.cache_max = (uint32_t)(c>16?c:16);
                }
                else if(brz_streq(k.text,"snapshot_every"))
                {
                    int n=0;
                    if(to_i32(v.text,&n)) cfg.snapshot_every_days = n;
                }
                else if(brz_streq(k.text,"map_every"))
                {
                    int n=0;
                    if(to_i32(v.text,&n)) cfg.map_every_days = n;
                }
            }
        }
        else if(brz_streq(t.text,"agents"))
        {
            tok_expect(&lx, TK_LBRACE);
            for(;;)
            {
                Tok k = next_tok(&lx);
                if(k.kind==TK_RBRACE) break;
                Tok v = next_tok(&lx);
                if(brz_streq(k.text,"count"))
                {
                    int c=0;
                    if(to_i32(v.text,&c)) cfg.agent_count = c;
                }
            }
        }
        else if(brz_streq(t.text,"settlements"))
        {
            tok_expect(&lx, TK_LBRACE);
            for(;;)
            {
                Tok k = next_tok(&lx);
                if(k.kind==TK_RBRACE) break;
                Tok v = next_tok(&lx);
                if(brz_streq(k.text,"count"))
                {
                    int c=0;
                    if(to_i32(v.text,&c)) cfg.settlement_count = c;
                }
            }
        }
        else if(brz_streq(t.text,"kinds"))
        {
            parse_kinds_block(&lx, &cfg);
        }
        else if(brz_streq(t.text,"resources"))
        {
            parse_simple_block_kv(&lx, "resources", &cfg);
        }
        else if(brz_streq(t.text,"vocations"))
        {
            parse_vocations_block(&lx, &cfg);
        }
        else
        {
            // skip unknown { ... }
            Tok maybe = next_tok(&lx);
            if(maybe.kind==TK_LBRACE)
            {
                int depth=1;
                while(depth>0)
                {
                    Tok x = next_tok(&lx);
                    if(x.kind==TK_EOF) break;
                    if(x.kind==TK_LBRACE) depth++;
                    if(x.kind==TK_RBRACE) depth--;
                }
            }
        }

        t = next_tok(&lx);
    }

    free(buf);

    // minimal sanity: must have at least 1 vocation
    if(cfg.voc_table.vocation_count == 0)
    {
        fprintf(stderr, "Parse warning: no vocations{} block found; you must define at least 1 vocation.\n");
    }

    *out_cfg = cfg;
    return true;
}




void brz_parsed_config_destroy(ParsedConfig* cfg)
{
    if(!cfg) return;
    free(cfg->renew_per_day);
    cfg->renew_per_day = NULL;

    voc_table_destroy(&cfg->voc_table);
    kind_table_destroy(&cfg->resources);
    kind_table_destroy(&cfg->items);
    memset(cfg,0,sizeof(*cfg));
}
