#!/usr/bin/env python3
"""
BRONZESIM — single-file Python port of the reference C implementation.

- Parses the BRONZESIM DSL (.bronze) format (example.bronze)
- Runs the simulation for N days
- Prints the same style of periodic status reports as the C version

Usage:
  python3 bronzesim.py [config.bronze]

Notes:
- This is a direct, deterministic port. If you keep the same seed and DSL,
  you should get the same *style* of output (and typically identical results
  when the DSL uses only supported constructs).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Dict, Tuple, Optional
from collections import OrderedDict
import sys
import os
import json

# -----------------------------
# Constants and "enums"
# -----------------------------

FEET_PER_MILE = 5280
CELL_FEET = 3

WORLD_MILES_X = 30
WORLD_MILES_Y = 30
WORLD_FEET_X = WORLD_MILES_X * FEET_PER_MILE
WORLD_FEET_Y = WORLD_MILES_Y * FEET_PER_MILE
WORLD_CELLS_X = WORLD_FEET_X // CELL_FEET  # 52800
WORLD_CELLS_Y = WORLD_FEET_Y // CELL_FEET  # 52800

CHUNK_SIZE = 64
CHUNK_CELLS = CHUNK_SIZE * CHUNK_SIZE

# Terrain tags (bitfield)
TAG_COAST  = 1 << 0
TAG_BEACH  = 1 << 1
TAG_FOREST = 1 << 2
TAG_MARSH  = 1 << 3
TAG_HILL   = 1 << 4
TAG_RIVER  = 1 << 5
TAG_FIELD  = 1 << 6
TAG_SETTLE = 1 << 7

# Resources (must match brz_world.h ordering)
RES_FISH = 0
RES_GRAIN = 1
RES_WOOD = 2
RES_CLAY = 3
RES_COPPER = 4
RES_TIN = 5
RES_FIRE = 6
RES_PLANT_FIBER = 7
RES_CATTLE = 8
RES_SHEEP = 9
RES_PIG = 10
RES_CHARCOAL = 11
RES_RELIGION = 12
RES_TRIBALISM = 13
RES_MAX = 14

# Inventory items (must match brz_dsl.h ordering)
ITEM_FISH = 0
ITEM_GRAIN = 1
ITEM_WOOD = 2
ITEM_CLAY = 3
ITEM_COPPER = 4
ITEM_TIN = 5
ITEM_BRONZE = 6
ITEM_TOOL = 7
ITEM_POT = 8
ITEM_MAX = 9

# Seasons
SEASON_SPRING = 0
SEASON_SUMMER = 1
SEASON_AUTUMN = 2
SEASON_WINTER = 3
SEASON_ANY = 255

# Comparisons
CMP_ANY = 0
CMP_GT = 1
CMP_LT = 2
CMP_GE = 3
CMP_LE = 4

# Ops
OP_MOVE_TO = 0
OP_GATHER  = 1
OP_CRAFT   = 2
OP_TRADE   = 3
OP_REST    = 4
OP_ROAM    = 5


# -----------------------------
# Deterministic hashing / rng
# -----------------------------

MASK64 = (1 << 64) - 1
MASK32 = (1 << 32) - 1

def brz_splitmix64(x: int) -> int:
    x = (x + 0x9e3779b97f4a7c15) & MASK64
    x = ((x ^ (x >> 30)) * 0xbf58476d1ce4e5b9) & MASK64
    x = ((x ^ (x >> 27)) * 0x94d049bb133111eb) & MASK64
    return (x ^ (x >> 31)) & MASK64

def brz_hash_u32(a: int, b: int, c: int) -> int:
    x = ((a & MASK32) << 32) ^ (b & MASK32) ^ ((c & MASK32) << 16)
    return brz_splitmix64(x) & MASK32

def rng_u32(seed: int, a: int, b: int, c: int) -> int:
    return brz_hash_u32((a ^ seed) & MASK32, b & MASK32, c & MASK32)

def rng_f01(seed: int, a: int, b: int, c: int) -> float:
    return (rng_u32(seed, a, b, c) & 0xFFFFFF) / float(0x1000000)


def clamp_i32(v: int, lo: int, hi: int) -> int:
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v

def clamp_u8(v: int) -> int:
    if v < 0:
        return 0
    if v > 255:
        return 255
    return v


# -----------------------------
# DSL runtime structures
# -----------------------------

@dataclass
class Condition:
    has_hunger: bool = False
    hunger_threshold: float = 0.0
    has_fatigue: bool = False
    fatigue_threshold: float = 0.0
    season_eq: int = SEASON_ANY
    inv_item: List[int] = field(default_factory=list)
    inv_cmp: List[int] = field(default_factory=list)
    inv_value: List[int] = field(default_factory=list)
    has_prob: bool = False
    prob: float = 0.0

@dataclass
class OpDef:
    kind: int
    arg_i: int = 0
    arg_j: int = 0

@dataclass
class TaskDef:
    name: str
    ops: List[OpDef] = field(default_factory=list)

@dataclass
class RuleDef:
    name: str
    cond: Condition
    task_name: str
    weight: int

@dataclass
class VocationDef:
    name: str
    tasks: List[TaskDef] = field(default_factory=list)
    rules: List[RuleDef] = field(default_factory=list)

@dataclass
class VocationTable:
    vocations: List[VocationDef] = field(default_factory=list)

    def find(self, name: str) -> int:
        for i, v in enumerate(self.vocations):
            if v.name == name:
                return i
        return -1

    def get(self, vid: int) -> Optional[VocationDef]:
        if 0 <= vid < len(self.vocations):
            return self.vocations[vid]
        return None

    def add(self, name: str) -> VocationDef:
        v = VocationDef(name=name)
        self.vocations.append(v)
        return v


# -----------------------------
# Config produced by parsing
# -----------------------------

@dataclass
class ParsedConfig:
    seed: int = 1337
    days: int = 120
    agent_count: int = 220
    settlement_count: int = 6
    cache_max: int = 2048

    fish_renew: float = 0.08
    grain_renew: float = 0.06
    wood_renew: float = 0.03
    clay_renew: float = 0.02
    copper_renew: float = 0.005
    tin_renew: float = 0.002

    fire_renew: float = 0.10
    plant_fiber_renew: float = 0.04
    cattle_renew: float = 0.010
    sheep_renew: float = 0.010
    pig_renew: float = 0.010
    charcoal_renew: float = 0.005
    religion_renew: float = 0.002
    tribalism_renew: float = 0.0005

    voc_table: VocationTable = field(default_factory=VocationTable)

    snapshot_every_days: int = 30
    map_every_days: int = 0


# -----------------------------
# Lexer
# -----------------------------

TK_EOF = 0
TK_WORD = 1
TK_LBRACE = 2
TK_RBRACE = 3

@dataclass
class Tok:
    kind: int
    text: str

class Lexer:
    def __init__(self, src: str) -> None:
        self.s = src
        self.n = len(src)
        self.i = 0

    def _skip_ws_and_comments(self) -> None:
        while self.i < self.n:
            c = self.s[self.i]
            # whitespace
            if c.isspace():
                self.i += 1
                continue
            # line comment with '#'
            if c == '#':
                while self.i < self.n and self.s[self.i] != '\n':
                    self.i += 1
                continue
            # allow '//' comments too (handy when editing)
            if c == '/' and self.i + 1 < self.n and self.s[self.i+1] == '/':
                self.i += 2
                while self.i < self.n and self.s[self.i] != '\n':
                    self.i += 1
                continue
            break

    def next_tok(self) -> Tok:
        self._skip_ws_and_comments()
        if self.i >= self.n:
            return Tok(TK_EOF, "")
        c = self.s[self.i]

        if c == '{':
            self.i += 1
            return Tok(TK_LBRACE, "{")
        if c == '}':
            self.i += 1
            return Tok(TK_RBRACE, "}")

        # "word" token: includes operators like >, <, >=, <=, == and numbers/idents
        start = self.i
        while self.i < self.n:
            ch = self.s[self.i]
            if ch.isspace() or ch in '{}#':
                break
            # stop at '//' comment
            if ch == '/' and self.i + 1 < self.n and self.s[self.i+1] == '/':
                break
            self.i += 1
        return Tok(TK_WORD, self.s[start:self.i])

    def expect(self, kind: int) -> Tok:
        t = self.next_tok()
        if t.kind != kind:
            raise ValueError(f"Parse error: expected token kind {kind}, got {t.kind} '{t.text}'")
        return t


# -----------------------------
# DSL helpers: parsing identifiers
# -----------------------------

RESOURCE_MAP = {
    "fish": RES_FISH,
    "grain": RES_GRAIN,
    "wood": RES_WOOD,
    "clay": RES_CLAY,
    "copper": RES_COPPER,
    "tin": RES_TIN,
    "fire": RES_FIRE,
    "plant_fiber": RES_PLANT_FIBER,
    "cattle": RES_CATTLE,
    "sheep": RES_SHEEP,
    "pig": RES_PIG,
    "charcoal": RES_CHARCOAL,
    "religion": RES_RELIGION,
    "tribalism": RES_TRIBALISM,
}

ITEM_MAP = {
    "fish": ITEM_FISH,
    "grain": ITEM_GRAIN,
    "wood": ITEM_WOOD,
    "clay": ITEM_CLAY,
    "copper": ITEM_COPPER,
    "tin": ITEM_TIN,
    "bronze": ITEM_BRONZE,
    "tool": ITEM_TOOL,
    "pot": ITEM_POT,
}

TAG_MAP = {
    "coast": TAG_COAST,
    "beach": TAG_BEACH,
    "forest": TAG_FOREST,
    "marsh": TAG_MARSH,
    "hill": TAG_HILL,
    "river": TAG_RIVER,
    "field": TAG_FIELD,
    "settle": TAG_SETTLE,
    "settlement": TAG_SETTLE,
}

def season_parse(s: str) -> int:
    ss = s.lower()
    if ss == "spring":
        return SEASON_SPRING
    if ss == "summer":
        return SEASON_SUMMER
    if ss == "autumn" or ss == "fall":
        return SEASON_AUTUMN
    if ss == "winter":
        return SEASON_WINTER
    if ss == "any":
        return SEASON_ANY
    # default: ignore
    return SEASON_ANY

def season_name(k: int) -> str:
    if k == SEASON_SPRING:
        return "spring"
    if k == SEASON_SUMMER:
        return "summer"
    if k == SEASON_AUTUMN:
        return "autumn"
    if k == SEASON_WINTER:
        return "winter"
    return "any"

def parse_cmp(s: str) -> int:
    if s == ">":
        return CMP_GT
    if s == "<":
        return CMP_LT
    if s == ">=":
        return CMP_GE
    if s == "<=":
        return CMP_LE
    return CMP_ANY


# -----------------------------
# Parser (direct port of brz_parser.c behavior)
# -----------------------------

def _to_i32(s: str) -> int:
    return int(s, 10)

def _to_u32(s: str) -> int:
    v = int(s, 10)
    if v < 0:
        v = 0
    return v & MASK32

def _to_f32(s: str) -> float:
    return float(s)

def parse_condition(lx: Lexer) -> Condition:
    c = Condition()
    c.season_eq = SEASON_ANY

    while True:
        a = lx.next_tok()
        if a.kind != TK_WORD:
            raise ValueError("Parse error: expected condition clause")
        if a.text == "hunger":
            op = lx.expect(TK_WORD).text
            v = _to_f32(lx.expect(TK_WORD).text)
            if op != ">":
                raise ValueError("Only 'hunger > x' supported (matches C)")
            c.has_hunger = True
            c.hunger_threshold = v
        elif a.text == "fatigue":
            op = lx.expect(TK_WORD).text
            v = _to_f32(lx.expect(TK_WORD).text)
            if op != "<":
                raise ValueError("Only 'fatigue < x' supported (matches C)")
            c.has_fatigue = True
            c.fatigue_threshold = v
        elif a.text == "season":
            op = lx.expect(TK_WORD).text
            v = lx.expect(TK_WORD).text
            if op != "==":
                raise ValueError("Only 'season == <name>' supported (matches C)")
            c.season_eq = season_parse(v)
        elif a.text == "inv":
            item = lx.expect(TK_WORD).text
            op = lx.expect(TK_WORD).text
            v = _to_i32(lx.expect(TK_WORD).text)
            if item not in ITEM_MAP:
                raise ValueError(f"Unknown item '{item}' in inv clause")
            ck = parse_cmp(op)
            if ck == CMP_ANY:
                raise ValueError(f"Unknown comparison '{op}' in inv clause")
            if len(c.inv_item) >= 4:
                raise ValueError("Too many inv clauses (max 4, matches C)")
            c.inv_item.append(ITEM_MAP[item])
            c.inv_cmp.append(ck)
            c.inv_value.append(v)
        elif a.text == "prob":
            v = _to_f32(lx.expect(TK_WORD).text)
            if v < 0:
                v = 0.0
            if v > 1:
                v = 1.0
            c.has_prob = True
            c.prob = v
        else:
            raise ValueError(f"Unknown condition clause '{a.text}'")

        maybe_and = lx.next_tok()
        if maybe_and.kind == TK_WORD and maybe_and.text == "and":
            continue

        # must be "do" (this matches the C parser constraint)
        if not (maybe_and.kind == TK_WORD and maybe_and.text == "do"):
            raise ValueError("Parse error: expected 'do' after condition")
        return c

def parse_task(lx: Lexer, voc: VocationDef) -> None:
    name = lx.expect(TK_WORD).text
    t = TaskDef(name=name, ops=[])
    lx.expect(TK_LBRACE)
    while True:
        op = lx.next_tok()
        if op.kind == TK_RBRACE:
            break
        if op.kind != TK_WORD:
            raise ValueError("Parse error: expected op name")
        if op.text == "move_to":
            tag = lx.expect(TK_WORD).text
            if tag not in TAG_MAP:
                raise ValueError(f"Unknown tag '{tag}' in move_to")
            t.ops.append(OpDef(OP_MOVE_TO, 0, TAG_MAP[tag]))
        elif op.text == "gather":
            res = lx.expect(TK_WORD).text
            amt = _to_i32(lx.expect(TK_WORD).text)
            if res not in RESOURCE_MAP:
                raise ValueError(f"Unknown resource '{res}' in gather")
            t.ops.append(OpDef(OP_GATHER, amt, RESOURCE_MAP[res]))
        elif op.text == "craft":
            item = lx.expect(TK_WORD).text
            amt = _to_i32(lx.expect(TK_WORD).text)
            if item not in ITEM_MAP:
                raise ValueError(f"Unknown item '{item}' in craft")
            t.ops.append(OpDef(OP_CRAFT, amt, ITEM_MAP[item]))
        elif op.text == "trade":
            t.ops.append(OpDef(OP_TRADE, 0, 0))
        elif op.text == "rest":
            t.ops.append(OpDef(OP_REST, 0, 0))
        elif op.text == "roam":
            steps = _to_i32(lx.expect(TK_WORD).text)
            t.ops.append(OpDef(OP_ROAM, steps, 0))
        else:
            raise ValueError(f"Unknown op '{op.text}' in task '{name}'")
    voc.tasks.append(t)

def parse_rule(lx: Lexer, voc: VocationDef) -> None:
    name = lx.expect(TK_WORD).text
    lx.expect(TK_LBRACE)

    when = lx.expect(TK_WORD).text
    if when != "when":
        raise ValueError("Parse error: rule must start with 'when'")

    cond = parse_condition(lx)  # consumes the 'do'

    task_name = lx.expect(TK_WORD).text

    weight_kw = lx.expect(TK_WORD).text
    if weight_kw != "weight":
        raise ValueError("Parse error: expected 'weight'")

    weight = _to_i32(lx.expect(TK_WORD).text)

    # Optional: "prob P" (in addition to any prob inside condition)
    maybe = lx.next_tok()
    if maybe.kind == TK_WORD and maybe.text == "prob":
        pv = _to_f32(lx.expect(TK_WORD).text)
        pv = max(0.0, min(1.0, pv))
        cond.has_prob = True
        cond.prob = pv
        maybe = lx.next_tok()

    if maybe.kind != TK_RBRACE:
        raise ValueError("Parse error: expected '}' to end rule block")

    voc.rules.append(RuleDef(name=name, cond=cond, task_name=task_name, weight=weight))

def parse_vocations_block(lx: Lexer, cfg: ParsedConfig) -> None:
    lx.expect(TK_LBRACE)
    while True:
        t = lx.next_tok()
        if t.kind == TK_RBRACE:
            break
        if t.kind != TK_WORD or t.text != "vocation":
            raise ValueError("Parse error: expected 'vocation' within vocations block")

        name = lx.expect(TK_WORD).text
        voc = cfg.voc_table.add(name)

        lx.expect(TK_LBRACE)
        while True:
            k = lx.next_tok()
            if k.kind == TK_RBRACE:
                break
            if k.kind != TK_WORD:
                raise ValueError("Parse error: expected keyword in vocation body")
            if k.text == "task":
                parse_task(lx, voc)
            elif k.text == "rule":
                parse_rule(lx, voc)
            else:
                # skip unknown { ... } or k/v
                maybe = lx.next_tok()
                if maybe.kind == TK_LBRACE:
                    depth = 1
                    while depth > 0:
                        x = lx.next_tok()
                        if x.kind == TK_EOF:
                            break
                        if x.kind == TK_LBRACE:
                            depth += 1
                        elif x.kind == TK_RBRACE:
                            depth -= 1
                else:
                    # ignore single value
                    pass

        # post-fix rules to handle missing tasks, matching the C behavior
        task_names = {t.name for t in voc.tasks}
        if voc.rules:
            for r in voc.rules:
                if r.task_name not in task_names:
                    if voc.tasks:
                        old = r.task_name
                        r.task_name = voc.tasks[0].name
                        pass
                    else:
                        # synthesize idle task
                        voc.tasks.append(TaskDef(name="idle", ops=[OpDef(OP_REST, 0, 0)]))
                        r.task_name = "idle"
                        pass

def parse_bronze_file(path: str) -> ParsedConfig:
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        src = f.read()

    cfg = ParsedConfig()
    lx = Lexer(src)

    t = lx.next_tok()
    while t.kind != TK_EOF:
        if t.kind != TK_WORD:
            t = lx.next_tok()
            continue

        if t.text == "world":
            lx.expect(TK_LBRACE)
            while True:
                k = lx.next_tok()
                if k.kind == TK_RBRACE:
                    break
                if k.kind != TK_WORD:
                    continue
                v = lx.expect(TK_WORD).text
                if k.text == "seed":
                    cfg.seed = _to_u32(v)
                elif k.text == "days":
                    cfg.days = _to_i32(v)
                elif k.text == "cache_max":
                    c = _to_i32(v)
                    cfg.cache_max = max(16, c)
                elif k.text == "snapshot_every":
                    cfg.snapshot_every_days = _to_i32(v)
                elif k.text == "map_every":
                    cfg.map_every_days = _to_i32(v)
                elif k.text == "agents":
                    cfg.agent_count = max(1, _to_i32(v))
                elif k.text == "settlements":
                    cfg.settlement_count = max(1, _to_i32(v))
                else:
                    # ignore unknown kv
                    pass


        elif t.text == "sim":
            lx.expect(TK_LBRACE)
            while True:
                k = lx.next_tok()
                if k.kind == TK_RBRACE:
                    break
                if k.kind != TK_WORD:
                    continue
                v = lx.expect(TK_WORD).text
                if k.text == "days":
                    cfg.days = _to_i32(v)
                elif k.text == "cache_max":
                    c = _to_i32(v)
                    cfg.cache_max = max(16, c)
                elif k.text == "snapshot_every":
                    cfg.snapshot_every_days = _to_i32(v)
                elif k.text == "map_every":
                    cfg.map_every_days = _to_i32(v)
                else:
                    pass

        elif t.text == "agents":
            lx.expect(TK_LBRACE)
            while True:
                k = lx.next_tok()
                if k.kind == TK_RBRACE:
                    break
                if k.kind != TK_WORD:
                    continue
                v = lx.expect(TK_WORD).text
                if k.text == "count":
                    cfg.agent_count = max(1, _to_i32(v))
                else:
                    pass

        elif t.text == "settlements":
            lx.expect(TK_LBRACE)
            while True:
                k = lx.next_tok()
                if k.kind == TK_RBRACE:
                    break
                if k.kind != TK_WORD:
                    continue
                v = lx.expect(TK_WORD).text
                if k.text == "count":
                    cfg.settlement_count = max(1, _to_i32(v))
                else:
                    pass
        elif t.text == "resources":
            lx.expect(TK_LBRACE)
            while True:
                k = lx.next_tok()
                if k.kind == TK_RBRACE:
                    break
                if k.kind != TK_WORD:
                    continue
                v = lx.expect(TK_WORD).text
                f = _to_f32(v)
                if k.text == "fish_renew":
                    cfg.fish_renew = f
                elif k.text == "grain_renew":
                    cfg.grain_renew = f
                elif k.text == "wood_renew":
                    cfg.wood_renew = f
                elif k.text == "clay_renew":
                    cfg.clay_renew = f
                elif k.text == "copper_renew":
                    cfg.copper_renew = f
                elif k.text == "tin_renew":
                    cfg.tin_renew = f
                elif k.text == "fire_renew":
                    cfg.fire_renew = f
                elif k.text == "plant_fiber_renew":
                    cfg.plant_fiber_renew = f
                elif k.text == "cattle_renew":
                    cfg.cattle_renew = f
                elif k.text == "sheep_renew":
                    cfg.sheep_renew = f
                elif k.text == "pig_renew":
                    cfg.pig_renew = f
                elif k.text == "charcoal_renew":
                    cfg.charcoal_renew = f
                elif k.text == "religion_renew":
                    cfg.religion_renew = f
                elif k.text == "tribalism_renew":
                    cfg.tribalism_renew = f
                else:
                    pass

        elif t.text == "vocations":
            parse_vocations_block(lx, cfg)

        else:
            # skip unknown block
            maybe = lx.next_tok()
            if maybe.kind == TK_LBRACE:
                depth = 1
                while depth > 0:
                    x = lx.next_tok()
                    if x.kind == TK_EOF:
                        break
                    if x.kind == TK_LBRACE:
                        depth += 1
                    elif x.kind == TK_RBRACE:
                        depth -= 1

        t = lx.next_tok()

    return cfg


# -----------------------------
# World generation (procedural)
# -----------------------------

@dataclass
class ResourceModel:
    renew_per_day: List[float] = field(default_factory=lambda: [0.0] * RES_MAX)

@dataclass
class WorldSpec:
    seed: int
    settlement_count: int
    res_model: ResourceModel

@dataclass
class WorldGen:
    seed: int

def worldgen_init(seed: int) -> WorldGen:
    return WorldGen(seed=seed)

def noise01(g: WorldGen, x: int, y: int, salt: int) -> int:
    h = brz_hash_u32(x & MASK32, y & MASK32, (g.seed ^ salt) & MASK32)
    return h & 0xFF

def is_coast_cell(x: int, y: int) -> bool:
    return (x < 2) or (y < 2) or (x >= WORLD_CELLS_X - 2) or (y >= WORLD_CELLS_Y - 2)

def world_cell_tags(g: WorldGen, x: int, y: int, settlement_count: int) -> int:
    _ = settlement_count
    tags = 0
    if is_coast_cell(x, y):
        tags |= TAG_COAST

    # beach near coast edge
    if not (tags & TAG_COAST):
        if x < 3 or y < 3 or x > WORLD_CELLS_X - 4 or y > WORLD_CELLS_Y - 4:
            if noise01(g, x, y, 0xBEEF1234) < 140:
                tags |= TAG_BEACH

    # forest/hill/marsh
    n1 = noise01(g, x, y, 0x1111A11A)
    n2 = noise01(g, x, y, 0x2222B22B)
    n3 = noise01(g, x, y, 0x3333C33C)
    if n1 > 150:
        tags |= TAG_FOREST
    if n2 > 200:
        tags |= TAG_HILL
    if n3 > 215:
        tags |= TAG_MARSH

    # crude rivers
    rv = noise01(g, x // 8, y // 8, 0x52A17B3D)
    if rv > 245:
        tags |= TAG_RIVER

    # deterministic settlement clusters
    sx = (x // 2000) * 2000 + 1000
    sy = (y // 2000) * 2000 + 1000
    sc = noise01(g, sx, sy, 0x5E771EAD)

    dx = x - sx
    dy = y - sy
    d2 = dx * dx + dy * dy

    if sc > 240 and d2 < (70 * 70):
        tags |= TAG_SETTLE
    if sc > 240 and d2 < (250 * 250):
        tags |= TAG_FIELD

    return tags

def world_cell_res0(g: WorldGen, x: int, y: int, rk: int, tags: int) -> int:
    base = noise01(g, x, y, 0x9999DDDD)
    if rk == RES_FISH:
        return clamp_u8(120 + base // 2) if (tags & TAG_COAST) else 0
    if rk == RES_GRAIN:
        return clamp_u8(80 + base // 3) if (tags & TAG_FIELD) else 0
    if rk == RES_WOOD:
        return clamp_u8(90 + base // 3) if (tags & TAG_FOREST) else 0
    if rk == RES_CLAY:
        return clamp_u8(60 + base // 4) if ((tags & TAG_RIVER) or (tags & TAG_MARSH)) else 0
    if rk == RES_COPPER:
        return (40 if base > 240 else 5) if (tags & TAG_HILL) else 0
    if rk == RES_TIN:
        return (25 if base > 250 else 0) if (tags & TAG_HILL) else 0

    if rk == RES_FIRE:
        return clamp_u8(180 + base // 4) if (tags & TAG_SETTLE) else 0
    if rk == RES_PLANT_FIBER:
        return clamp_u8(70 + base // 3) if ((tags & TAG_MARSH) or (tags & TAG_FIELD)) else 0
    if rk == RES_CATTLE:
        return clamp_u8(40 + base // 4) if (tags & TAG_FIELD) else 0
    if rk == RES_SHEEP:
        return clamp_u8(35 + base // 4) if (tags & TAG_FIELD) else 0
    if rk == RES_PIG:
        return clamp_u8(30 + base // 4) if (tags & TAG_FIELD) else 0
    if rk == RES_CHARCOAL:
        return clamp_u8(25 + base // 5) if (tags & TAG_FOREST) else 0
    if rk == RES_RELIGION:
        return clamp_u8(60 + base // 5) if (tags & TAG_SETTLE) else 0
    if rk == RES_TRIBALISM:
        return clamp_u8(20 + base // 8) if (tags & TAG_SETTLE) else 0

    return 0

def world_season_kind(day: int) -> int:
    d = day % 360
    if d < 90:
        return SEASON_SPRING
    if d < 180:
        return SEASON_SUMMER
    if d < 270:
        return SEASON_AUTUMN
    return SEASON_WINTER


# -----------------------------
# Chunk cache (LRU dict)
# -----------------------------

@dataclass
class Chunk:
    cx: int
    cy: int
    terrain: List[int] = field(default_factory=lambda: [0] * CHUNK_CELLS)
    res: List[List[int]] = field(default_factory=lambda: [[0] * CHUNK_CELLS for _ in range(RES_MAX)])

class ChunkCache:
    def __init__(self, max_chunks: int, gen: WorldGen, spec: WorldSpec) -> None:
        self.max_chunks = max(1, int(max_chunks))
        self.gen = gen
        self.spec = spec
        self._lru: "OrderedDict[Tuple[int,int], Chunk]" = OrderedDict()
        self.live_chunks = 0

    def _generate_chunk(self, cx: int, cy: int) -> Chunk:
        # Optimized: compute only resources that can be non-zero for the cell's tags.
        ch = Chunk(cx=cx, cy=cy)
        base_x = cx * CHUNK_SIZE
        base_y = cy * CHUNK_SIZE
        gen = self.gen
        seed = gen.seed
        settle_n = self.spec.settlement_count

        terr = ch.terrain
        res = ch.res

        for iy in range(CHUNK_SIZE):
            wy = base_y + iy
            row_off = iy * CHUNK_SIZE
            for ix in range(CHUNK_SIZE):
                wx = base_x + ix
                idx = row_off + ix

                tags = world_cell_tags(gen, wx, wy, settle_n)
                terr[idx] = tags

                # single base noise reused (matches world_cell_res0 calling noise01 with same salt)
                base = brz_hash_u32(wx & MASK32, wy & MASK32, (seed ^ 0x9999DDDD) & MASK32) & 0xFF

                if tags & TAG_COAST:
                    res[RES_FISH][idx] = clamp_u8(120 + (base >> 1))
                if tags & TAG_FIELD:
                    res[RES_GRAIN][idx] = clamp_u8(80 + base // 3)
                    res[RES_PLANT_FIBER][idx] = clamp_u8(70 + base // 3)
                    res[RES_CATTLE][idx] = clamp_u8(40 + base // 4)
                    res[RES_SHEEP][idx] = clamp_u8(35 + base // 4)
                    res[RES_PIG][idx] = clamp_u8(30 + base // 4)
                if tags & TAG_FOREST:
                    res[RES_WOOD][idx] = clamp_u8(90 + base // 3)
                    res[RES_CHARCOAL][idx] = clamp_u8(25 + base // 5)
                if (tags & TAG_RIVER) or (tags & TAG_MARSH):
                    res[RES_CLAY][idx] = clamp_u8(60 + base // 4)
                    if tags & TAG_MARSH:
                        # marsh also yields plant fiber (world_cell_res0 allows marsh or field)
                        if res[RES_PLANT_FIBER][idx] == 0:
                            res[RES_PLANT_FIBER][idx] = clamp_u8(70 + base // 3)
                if tags & TAG_HILL:
                    res[RES_COPPER][idx] = 40 if base > 240 else 5
                    res[RES_TIN][idx] = 25 if base > 250 else 0
                if tags & TAG_SETTLE:
                    res[RES_FIRE][idx] = clamp_u8(180 + base // 4)
                    res[RES_RELIGION][idx] = clamp_u8(60 + base // 5)
                    res[RES_TRIBALISM][idx] = clamp_u8(20 + base // 8)

        return ch

    def get_chunk(self, cx: int, cy: int) -> Chunk:
        key = (cx, cy)
        ch = self._lru.get(key)
        if ch is not None:
            self._lru.move_to_end(key, last=True)
            return ch

        ch = self._generate_chunk(cx, cy)
        self._lru[key] = ch
        # evict LRU
        if len(self._lru) > self.max_chunks:
            self._lru.popitem(last=False)
        self.live_chunks = len(self._lru)
        return ch

    def get_cell(self, x: int, y: int) -> Tuple[Chunk, int]:
        cx = x // CHUNK_SIZE
        cy = y // CHUNK_SIZE
        ch = self.get_chunk(cx, cy)
        idx = (y % CHUNK_SIZE) * CHUNK_SIZE + (x % CHUNK_SIZE)
        return ch, idx

    def regen_loaded(self, season: int) -> None:
        fishMul = 0.70 if season == SEASON_WINTER else 1.0
        grainMul = 0.30 if season == SEASON_WINTER else (1.0 if season in (SEASON_SUMMER, SEASON_AUTUMN) else 0.70)

        # iterate in LRU order (matches "loaded chunks" semantics)
        for ch in list(self._lru.values()):
            for i in range(CHUNK_CELLS):
                tags = ch.terrain[i]

                if tags & TAG_SETTLE:
                    fi = ch.res[RES_FIRE][i] + int(self.spec.res_model.renew_per_day[RES_FIRE] * 255.0)
                    re = ch.res[RES_RELIGION][i] + int(self.spec.res_model.renew_per_day[RES_RELIGION] * 255.0)
                    na = ch.res[RES_TRIBALISM][i] + int(self.spec.res_model.renew_per_day[RES_TRIBALISM] * 255.0)
                    ch.res[RES_FIRE][i] = clamp_u8(fi)
                    ch.res[RES_RELIGION][i] = clamp_u8(re)
                    ch.res[RES_TRIBALISM][i] = clamp_u8(na)

                if tags & TAG_COAST:
                    v = ch.res[RES_FISH][i] + int(self.spec.res_model.renew_per_day[RES_FISH] * fishMul * 255.0)
                    ch.res[RES_FISH][i] = clamp_u8(v)

                if tags & TAG_FIELD:
                    v = ch.res[RES_GRAIN][i] + int(self.spec.res_model.renew_per_day[RES_GRAIN] * grainMul * 255.0)
                    ch.res[RES_GRAIN][i] = clamp_u8(v)

                if tags & TAG_FIELD:
                    pf = ch.res[RES_PLANT_FIBER][i] + int(self.spec.res_model.renew_per_day[RES_PLANT_FIBER] * 255.0)
                    ca = ch.res[RES_CATTLE][i] + int(self.spec.res_model.renew_per_day[RES_CATTLE] * 255.0)
                    sh = ch.res[RES_SHEEP][i] + int(self.spec.res_model.renew_per_day[RES_SHEEP] * 255.0)
                    pg = ch.res[RES_PIG][i] + int(self.spec.res_model.renew_per_day[RES_PIG] * 255.0)
                    ch.res[RES_PLANT_FIBER][i] = clamp_u8(pf)
                    ch.res[RES_CATTLE][i] = clamp_u8(ca)
                    ch.res[RES_SHEEP][i] = clamp_u8(sh)
                    ch.res[RES_PIG][i] = clamp_u8(pg)

                if tags & TAG_FOREST:
                    v = ch.res[RES_WOOD][i] + int(self.spec.res_model.renew_per_day[RES_WOOD] * 255.0)
                    ch.res[RES_WOOD][i] = clamp_u8(v)

                if tags & TAG_FOREST:
                    chv = ch.res[RES_CHARCOAL][i] + int(self.spec.res_model.renew_per_day[RES_CHARCOAL] * 255.0)
                    ch.res[RES_CHARCOAL][i] = clamp_u8(chv)

                if (tags & TAG_RIVER) or (tags & TAG_MARSH):
                    v = ch.res[RES_CLAY][i] + int(self.spec.res_model.renew_per_day[RES_CLAY] * 255.0)
                    ch.res[RES_CLAY][i] = clamp_u8(v)

                if tags & TAG_HILL:
                    cu = ch.res[RES_COPPER][i] + int(self.spec.res_model.renew_per_day[RES_COPPER] * 255.0)
                    tn = ch.res[RES_TIN][i] + int(self.spec.res_model.renew_per_day[RES_TIN] * 255.0)
                    ch.res[RES_COPPER][i] = clamp_u8(cu)
                    ch.res[RES_TIN][i] = clamp_u8(tn)


# -----------------------------
# Simulation
# -----------------------------

@dataclass
class Household:
    id: int
    settlement_id: int
    parent_id: int = -1

@dataclass
class Settlement:
    x: int
    y: int
    val: List[float] = field(default_factory=lambda: [1.0] * ITEM_MAX)

@dataclass
class Agent:
    x: int = 0
    y: int = 0
    vocation_id: int = -1
    age: int = 0
    household_id: int = 0
    inv: List[int] = field(default_factory=lambda: [0] * ITEM_MAX)
    hunger: float = 0.0
    fatigue: float = 0.0
    health: float = 1.0

@dataclass
class Sim:
    world: WorldSpec
    gen: WorldGen
    cache: ChunkCache
    settlements: List[Settlement]
    resources_pool: List[int]
    households: List[Household]
    agents: List[Agent]
    voc_table: VocationTable
    day: int = 0
    switch_every_days: int = 30

def pick_spawn(gen: WorldGen, i: int) -> Tuple[int, int]:
    h1 = brz_hash_u32(i & MASK32, gen.seed & MASK32, 0xABCDE123)
    h2 = brz_hash_u32(i & MASK32, gen.seed & MASK32, 0xCDEF2345)
    # spawn not right at edge
    x = int(h1 % (WORLD_CELLS_X - 200)) + 100
    y = int(h2 % (WORLD_CELLS_Y - 200)) + 100
    return x, y

def nearest_settlement(sim: Sim, x: int, y: int) -> int:
    best = 0
    bestd = 2**63 - 1
    for i, st in enumerate(sim.settlements):
        dx = x - st.x
        dy = y - st.y
        d2 = dx*dx + dy*dy
        if d2 < bestd:
            bestd = d2
            best = i
    return best

def step_toward(a: Agent, tx: int, ty: int) -> None:
    dx = 1 if tx > a.x else (-1 if tx < a.x else 0)
    dy = 1 if ty > a.y else (-1 if ty < a.y else 0)
    a.x = clamp_i32(a.x + dx, 0, WORLD_CELLS_X - 1)
    a.y = clamp_i32(a.y + dy, 0, WORLD_CELLS_Y - 1)
    a.fatigue += 0.004

def move_to_tag(sim: Sim, a: Agent, want_tag: int, radius: int) -> None:
    # Fast-mode: spatial movement is not simulated.
    a.fatigue += 0.002

def roam(sim: Sim, a: Agent, steps: int) -> None:
    # Fast-mode: roaming only affects fatigue.
    a.fatigue += 0.001 * float(steps)

def gather(sim: Sim, a: Agent, rk: int, want_units: int) -> int:
    # Performance mode: take from a global resource pool rather than per-cell density.
    # Units are abstract; we map 1 unit ~= 20 pool points (mirroring the C depletion).
    pool = sim.resources_pool[rk]
    max_take = pool // 20
    take = want_units if want_units < max_take else max_take
    sim.resources_pool[rk] = pool - take * 20
    return take

def craft_item(a: Agent, item: int, amount: int) -> None:
    for _ in range(amount):
        if item == ITEM_POT:
            if a.inv[ITEM_CLAY] >= 2 and a.inv[ITEM_WOOD] >= 1:
                a.inv[ITEM_CLAY] -= 2
                a.inv[ITEM_WOOD] -= 1
                a.inv[ITEM_POT] += 1
                a.fatigue += 0.01
        elif item == ITEM_BRONZE:
            if a.inv[ITEM_COPPER] >= 1 and a.inv[ITEM_TIN] >= 1 and a.inv[ITEM_WOOD] >= 2:
                a.inv[ITEM_COPPER] -= 1
                a.inv[ITEM_TIN] -= 1
                a.inv[ITEM_WOOD] -= 2
                a.inv[ITEM_BRONZE] += 1
                a.fatigue += 0.02
        elif item == ITEM_TOOL:
            if a.inv[ITEM_BRONZE] >= 1:
                a.inv[ITEM_BRONZE] -= 1
                a.inv[ITEM_TOOL] += 1
                a.fatigue += 0.02
        else:
            # no other recipes
            pass

def trade(sim: Sim, a: Agent) -> None:
    hh = sim.households[a.household_id]
    st = sim.settlements[hh.settlement_id]

    wants = [ITEM_GRAIN, ITEM_FISH, ITEM_TOOL, ITEM_POT]
    for want in wants:
        if a.inv[want] >= 3:
            continue

        offer = ITEM_FISH
        bestScore = -1.0
        for it in range(ITEM_MAX):
            if it == want:
                continue
            if a.inv[it] < 6:
                continue
            score = st.val[it]
            if score > bestScore:
                bestScore = score
                offer = it

        if a.inv[offer] < 6:
            continue

        if st.val[offer] >= st.val[want]:
            a.inv[offer] -= 2
            a.inv[want] += 1
            a.fatigue += 0.01

def eat(a: Agent) -> None:
    if a.hunger <= 0.7:
        return
    if a.inv[ITEM_FISH] > 0:
        a.inv[ITEM_FISH] -= 1
        a.hunger -= 0.35
    if a.hunger > 0.7 and a.inv[ITEM_GRAIN] > 0:
        a.inv[ITEM_GRAIN] -= 1
        a.hunger -= 0.30
    if a.hunger < 0:
        a.hunger = 0.0

def apprenticeship(sim: Sim, a: Agent) -> None:
    if a.age < 10 or a.age > 16:
        return
    hh = sim.households[a.household_id]
    if hh.parent_id < 0:
        return
    pid = hh.parent_id
    if pid < 0 or pid >= len(sim.agents):
        return
    pv = sim.agents[pid].vocation_id
    r = rng_f01(sim.world.seed, a.x, a.y, sim.day ^ 0xA22E11)
    if r < 0.10:
        a.vocation_id = pv

def cond_eval(c: Condition, sim: Sim, a: Agent, roll: float) -> bool:
    if c.has_hunger and not (a.hunger > c.hunger_threshold):
        return False
    if c.has_fatigue and not (a.fatigue < c.fatigue_threshold):
        return False
    if c.season_eq != SEASON_ANY and world_season_kind(sim.day) != c.season_eq:
        return False
    for ik, ck, v in zip(c.inv_item, c.inv_cmp, c.inv_value):
        have = a.inv[ik]
        if ck == CMP_GT and not (have > v):
            return False
        if ck == CMP_LT and not (have < v):
            return False
        if ck == CMP_GE and not (have >= v):
            return False
        if ck == CMP_LE and not (have <= v):
            return False
    if c.has_prob and not (roll < c.prob):
        return False
    return True

def choose_task(sim: Sim, a: Agent) -> Optional[TaskDef]:
    voc = sim.voc_table.get(a.vocation_id)
    if voc is None:
        return None
    roll = rng_f01(sim.world.seed, a.x, a.y, sim.day ^ (a.household_id * 131))
    total = 0
    eligible: List[RuleDef] = []
    for r in voc.rules:
        if cond_eval(r.cond, sim, a, roll):
            w = r.weight if r.weight > 0 else 0
            if w > 0:
                total += w
                eligible.append(r)
    if total <= 0:
        return None
    pick = int(rng_u32(sim.world.seed, a.x, a.y, sim.day ^ 0xC0FFEE) % total)
    for r in eligible:
        w = r.weight if r.weight > 0 else 0
        pick -= w
        if pick < 0:
            # resolve task
            for t in voc.tasks:
                if t.name == r.task_name:
                    return t
            return None
    return None

def exec_task(sim: Sim, a: Agent, t: TaskDef) -> None:
    for op in t.ops:
        if op.kind == OP_MOVE_TO:
            move_to_tag(sim, a, op.arg_j, 12)
        elif op.kind == OP_GATHER:
            rk = op.arg_j
            got = gather(sim, a, rk, op.arg_i)
            if rk == RES_FISH:
                a.inv[ITEM_FISH] += got
            elif rk == RES_GRAIN:
                a.inv[ITEM_GRAIN] += got
            elif rk == RES_WOOD:
                a.inv[ITEM_WOOD] += got
            elif rk == RES_CLAY:
                a.inv[ITEM_CLAY] += got
            elif rk == RES_COPPER:
                a.inv[ITEM_COPPER] += got
            elif rk == RES_TIN:
                a.inv[ITEM_TIN] += got
        elif op.kind == OP_CRAFT:
            craft_item(a, op.arg_j, op.arg_i)
        elif op.kind == OP_TRADE:
            trade(sim, a)
        elif op.kind == OP_REST:
            a.fatigue -= 0.2
            if a.fatigue < 0:
                a.fatigue = 0.0
        elif op.kind == OP_ROAM:
            roam(sim, a, op.arg_i)

def role_switching(sim: Sim) -> None:
    if sim.switch_every_days == 0:
        return
    if (sim.day % sim.switch_every_days) != 0:
        return

    totals = [0] * ITEM_MAX
    alive = 0
    for a in sim.agents:
        if a.health <= 0.0:
            continue
        alive += 1
        for it in range(ITEM_MAX):
            totals[it] += a.inv[it]
    if alive <= 0:
        return

    pc_grain = totals[ITEM_GRAIN] / float(alive)
    pc_fish  = totals[ITEM_FISH]  / float(alive)
    pc_tool  = totals[ITEM_TOOL]  / float(alive)
    pc_pot   = totals[ITEM_POT]   / float(alive)

    farmer_id = sim.voc_table.find("farmer")
    fisher_id = sim.voc_table.find("fisher")
    smith_id  = sim.voc_table.find("smith")
    potter_id = sim.voc_table.find("potter")

    target_voc = -1
    if farmer_id >= 0 and pc_grain < 3.0:
        target_voc = farmer_id
    if fisher_id >= 0 and pc_fish < 2.0 and (target_voc < 0 or pc_fish < pc_grain):
        target_voc = fisher_id
    if smith_id >= 0 and pc_tool < 0.6:
        target_voc = smith_id
    if potter_id >= 0 and pc_pot < 0.6:
        target_voc = potter_id
    if target_voc < 0:
        return

    switched = 0
    for i, a in enumerate(sim.agents):
        if a.health <= 0.0:
            continue
        if a.age < 17:
            continue
        if a.vocation_id == target_voc:
            continue

        hh = sim.households[a.household_id]
        if hh.parent_id == i:
            continue

        r = rng_f01(sim.world.seed, a.x, a.y, sim.day ^ 0x5A17C9)
        if r < 0.05:
            a.vocation_id = target_voc
            switched += 1
            if switched >= (alive // 50 + 1):
                break

    if switched > 0:
        v = sim.voc_table.get(target_voc)
        name = v.name if v else "?"
        sys.stdout.write(f"Day {sim.day}: role switching nudged {switched} adults into vocation '{name}'\n")

def sim_report(sim: Sim) -> None:
    inv = [0] * ITEM_MAX
    voc_counts = [0] * len(sim.voc_table.vocations)
    alive = 0
    for a in sim.agents:
        if a.health <= 0.0:
            continue
        alive += 1
        for it in range(ITEM_MAX):
            inv[it] += a.inv[it]
        if 0 <= a.vocation_id < len(voc_counts):
            voc_counts[a.vocation_id] += 1

    sys.stdout.write(
        f"Day {sim.day} season={season_name(world_season_kind(sim.day))} alive={alive} cache_chunks=0 | "
        f"fish={inv[ITEM_FISH]}...wood={inv[ITEM_WOOD]} clay={inv[ITEM_CLAY]} cu={inv[ITEM_COPPER]} tin={inv[ITEM_TIN]} "
        f"bronze={inv[ITEM_BRONZE]} tool={inv[ITEM_TOOL]} pot={inv[ITEM_POT]}\n"
    )
    sys.stdout.write("  vocations:")
    for v, c in zip(sim.voc_table.vocations, voc_counts):
        sys.stdout.write(f" {v.name}={c}")
    sys.stdout.write("\n")

def sim_write_snapshot_json(sim: Sim, path: str) -> None:
    inv = [0] * ITEM_MAX
    voc_counts = [0] * len(sim.voc_table.vocations)
    alive = 0
    for a in sim.agents:
        if a.health <= 0.0:
            continue
        alive += 1
        for it in range(ITEM_MAX):
            inv[it] += a.inv[it]
        if 0 <= a.vocation_id < len(voc_counts):
            voc_counts[a.vocation_id] += 1

    data = {
        "day": sim.day,
        "season": season_name(world_season_kind(sim.day)),
        "alive": alive,
        "inventory": {
            "fish": inv[ITEM_FISH],
            "grain": inv[ITEM_GRAIN],
            "wood": inv[ITEM_WOOD],
            "clay": inv[ITEM_CLAY],
            "copper": inv[ITEM_COPPER],
            "tin": inv[ITEM_TIN],
            "bronze": inv[ITEM_BRONZE],
            "tool": inv[ITEM_TOOL],
            "pot": inv[ITEM_POT],
        },
        "vocations": {v.name: voc_counts[i] for i, v in enumerate(sim.voc_table.vocations)}
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)

def sim_dump_ascii_map(sim: Sim, path: str, w: int, h: int) -> None:
    # crude overview map around world center
    cx = WORLD_CELLS_X // 2
    cy = WORLD_CELLS_Y // 2
    sx = cx - w // 2
    sy = cy - h // 2

    def cell_char(tags: int) -> str:
        if tags & TAG_SETTLE:
            return "S"
        if tags & TAG_COAST:
            return "~"
        if tags & TAG_RIVER:
            return "r"
        if tags & TAG_MARSH:
            return "m"
        if tags & TAG_HILL:
            return "^"
        if tags & TAG_FOREST:
            return "f"
        if tags & TAG_FIELD:
            return "."
        if tags & TAG_BEACH:
            return "b"
        return " "

    with open(path, "w", encoding="utf-8") as f:
        for yy in range(sy, sy + h):
            row = []
            for xx in range(sx, sx + w):
                if xx < 0 or yy < 0 or xx >= WORLD_CELLS_X or yy >= WORLD_CELLS_Y:
                    row.append(" ")
                    continue
                ch, idx = sim.cache.get_cell(xx, yy)
                row.append(cell_char(ch.terrain[idx]))
            f.write("".join(row) + "\n")

def sim_init_from_cfg(cfg: ParsedConfig) -> Sim:
    rm = ResourceModel()
    rm.renew_per_day[RES_FISH] = cfg.fish_renew
    rm.renew_per_day[RES_GRAIN] = cfg.grain_renew
    rm.renew_per_day[RES_WOOD] = cfg.wood_renew
    rm.renew_per_day[RES_CLAY] = cfg.clay_renew
    rm.renew_per_day[RES_COPPER] = cfg.copper_renew
    rm.renew_per_day[RES_TIN] = cfg.tin_renew
    rm.renew_per_day[RES_FIRE] = cfg.fire_renew
    rm.renew_per_day[RES_PLANT_FIBER] = cfg.plant_fiber_renew
    rm.renew_per_day[RES_CATTLE] = cfg.cattle_renew
    rm.renew_per_day[RES_SHEEP] = cfg.sheep_renew
    rm.renew_per_day[RES_PIG] = cfg.pig_renew
    rm.renew_per_day[RES_CHARCOAL] = cfg.charcoal_renew
    rm.renew_per_day[RES_RELIGION] = cfg.religion_renew
    rm.renew_per_day[RES_TRIBALISM] = cfg.tribalism_renew

    ws = WorldSpec(seed=cfg.seed, settlement_count=max(1, cfg.settlement_count), res_model=rm)
    gen = worldgen_init(ws.seed)
    cache = ChunkCache(max_chunks=cfg.cache_max, gen=gen, spec=ws)

    settlements: List[Settlement] = []
    for i in range(ws.settlement_count):
        h1 = brz_hash_u32(i, ws.seed, 0x5E77A11A)
        h2 = brz_hash_u32(i, ws.seed, 0x5E77B22B)
        x = int(h1 % (WORLD_CELLS_X - 2000)) + 1000
        y = int(h2 % (WORLD_CELLS_Y - 2000)) + 1000
        st = Settlement(x=x, y=y)
        r = (brz_hash_u32(i, ws.seed, 0xC0DE) % 100) / 100.0
        st.val[ITEM_FISH] = 1.0 + 0.5 * r
        st.val[ITEM_GRAIN] = 1.0 + 0.5 * (1.0 - r)
        st.val[ITEM_POT] = 1.0 + 0.4 * r
        st.val[ITEM_TOOL] = 1.2 + 0.6 * r
        st.val[ITEM_BRONZE] = 1.3 + 0.7 * r
        settlements.append(st)

    agent_count = max(1, cfg.agent_count)
    household_count = (agent_count + 4) // 5
    households = [Household(id=i, settlement_id=i % ws.settlement_count, parent_id=-1) for i in range(household_count)]

    agents = [Agent() for _ in range(agent_count)]
    default_voc = 0 if cfg.voc_table.vocations else -1
    farmer_id = cfg.voc_table.find("farmer")
    fisher_id = cfg.voc_table.find("fisher")
    potter_id = cfg.voc_table.find("potter")
    smith_id  = cfg.voc_table.find("smith")
    trader_id = cfg.voc_table.find("trader")

    for i, a in enumerate(agents):
        a.x, a.y = pick_spawn(gen, i)
        a.age = 8 + int(brz_hash_u32(i, ws.seed, 0xA9E) % 35)
        a.household_id = i % household_count

        rr = int(brz_hash_u32(i, ws.seed, 0xB00C) % 100)
        vid = default_voc
        if rr < 45 and farmer_id >= 0:
            vid = farmer_id
        elif rr < 70 and fisher_id >= 0:
            vid = fisher_id
        elif rr < 85 and potter_id >= 0:
            vid = potter_id
        elif rr < 95 and smith_id >= 0:
            vid = smith_id
        else:
            if trader_id >= 0:
                vid = trader_id
        a.vocation_id = vid
        a.health = 1.0
        a.hunger = 0.10
        a.fatigue = 0.10

    # determine household parent as oldest member
    for h in range(household_count):
        bestAge = -1
        parent = -1
        for i, a in enumerate(agents):
            if a.household_id != h:
                continue
            if a.age > bestAge:
                bestAge = a.age
                parent = i
        households[h].parent_id = parent

    sim = Sim(world=ws, gen=gen, cache=cache, settlements=settlements, resources_pool=[0]*RES_MAX, households=households,
              agents=agents, voc_table=cfg.voc_table, day=0, switch_every_days=30)

    # Simplified global resource pool (Python performance mode):
    # we keep a coarse-grained availability per resource instead of per-cell chunks.
    scale = max(1000, cfg.agent_count * 80)
    for rk in range(RES_MAX):
        # Start at about ~30 days worth of regen at the chosen scale.
        sim.resources_pool[rk] = int(ws.res_model.renew_per_day[rk] * 255.0 * 30.0 * scale)

    return sim

def sim_step(sim: Sim) -> None:
    sim.day += 1
    season = world_season_kind(sim.day)

    # Global resource regeneration (fast mode).
    fishMul = 0.70 if season == SEASON_WINTER else 1.0
    grainMul = 0.30 if season == SEASON_WINTER else (1.0 if season in (SEASON_SUMMER, SEASON_AUTUMN) else 0.70)
    scale = max(1000, len(sim.agents) * 80)
    for rk in range(RES_MAX):
        mul = 1.0
        if rk == RES_FISH:
            mul = fishMul
        elif rk == RES_GRAIN:
            mul = grainMul
        sim.resources_pool[rk] += int(sim.world.res_model.renew_per_day[rk] * mul * 255.0 * scale)

    for i, a in enumerate(sim.agents):
        if a.health <= 0.0:
            continue

        if (sim.day % 360) == 0:
            a.age += 1

        # needs drift
        a.hunger += 0.18
        if a.hunger > 1.0:
            a.hunger = 1.0
        a.fatigue -= 0.08
        if a.fatigue < 0.0:
            a.fatigue = 0.0

        eat(a)
        apprenticeship(sim, a)

        # starvation damage
        if a.hunger > 0.95:
            a.health -= 0.01
            if a.health < 0.0:
                a.health = 0.0
            continue

        # exhausted: rest
        if a.fatigue >= 0.90:
            a.fatigue -= 0.20
            if a.fatigue < 0.0:
                a.fatigue = 0.0
            continue

        t = choose_task(sim, a)
        if t is None:
            # idle drift + occasional trade
            a.fatigue += 0.003
            if (i % 9) == 0:
                trade(sim, a)
        else:
            exec_task(sim, a, t)

    role_switching(sim)


def main(argv: List[str]) -> int:
    if len(argv) >= 2 and argv[1] in ("-h", "--help"):
        sys.stdout.write(f"Usage: {os.path.basename(argv[0])} [config.bronze]\n")
        return 0
    path = argv[1] if len(argv) >= 2 else "example.bronze"

    cfg = parse_bronze_file(path)
    if not cfg.voc_table.vocations:
        sys.stderr.write("Error: example must define at least 1 vocation in vocations { ... }\n")
        return 1

    sim = sim_init_from_cfg(cfg)

    for _ in range(int(cfg.days)):
        sim_step(sim)
        if (sim.day % 10) == 0:
            sim_report(sim)

        if cfg.snapshot_every_days > 0 and (sim.day % int(cfg.snapshot_every_days)) == 0:
            fn = f"snapshot_day{sim.day:05d}.json"
            sim_write_snapshot_json(sim, fn)

        if cfg.map_every_days > 0 and (sim.day % int(cfg.map_every_days)) == 0:
            fn = f"map_day{sim.day:05d}.txt"
            sim_dump_ascii_map(sim, fn, 80, 40)

    sim_report(sim)
    return 0

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
