/****************************************************************
 
	battle.h - Noble Warfare Skirmish
 
 =============================================================
 
 Copyright 1996-2024 Tom Barbalet. All rights reserved.
 
 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or
 sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:
 
 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
 
 ****************************************************************/

#include "toolkit.h"
#include "shared.h"

#define MELEE_ATTACK_DISTANCE_SQUARED	5
#define LARGEST_DISTANCE_SQUARED 	    0xffff

#define BATTLE_BOARD_WIDTH              (1024)
#define BATTLE_BOARD_HEIGHT             (768)

#define OUTSIDE_WIDTH(num)   (((num) < 0) || ((num) >= BATTLE_BOARD_WIDTH))
#define OUTSIDE_HEIGHT(num)  (((num) < 0) || ((num) >= BATTLE_BOARD_HEIGHT))

#define BATTLE_BOARD_SIZE               (BATTLE_BOARD_WIDTH*BATTLE_BOARD_HEIGHT)

#define NUNIT_NO_ATTACK                 0xffff

#define NUNIT_DEAD			            (255)


typedef struct n_combatant
{
    n_vect2 location;
    
	n_byte2 attacking;
	n_byte2 distance_squ;
    
    n_byte speed_current;
	n_byte direction_facing;
	n_byte wounds;
	n_byte combatant_state;
}
n_combatant;

#define	GET_TYPE(un)		  ((n_type *)((un)->unit_type))

#define	UNIT_SIZE(un)		  ((GET_TYPE(un)->stature) >> 1)
#define	UNIT_ORDER(un)		  ((GET_TYPE(un)->stature) &  1)

typedef struct n_type {
	n_byte  defence;
	n_byte  melee_attack;
	n_byte  melee_damage;
	n_byte  melee_armpie;

	n_byte  missile_attack;
	n_byte  missile_damage;
	n_byte  missile_armpie;
	n_byte  missile_rate;

	n_byte2 missile_range;
	n_byte  speed_maximum;
	n_byte  stature;
	n_byte  leadership;

	n_byte  wounds_per_combatant;
	n_byte  points_per_combatant;
}
n_type;

typedef struct n_unit {
	n_byte  morale;
	n_byte  angle;

	n_byte2 average[2];

	n_byte2 width;
	n_byte2 number_combatants;

	n_byte  alignment;
	n_byte  missile_number;

	n_byte  missile_timer;
	n_byte2 number_living;
    
    n_area2 area;
    n_byte  selected;
    
	void *unit_type;
	void *combatants;
	void *unit_attacking;
}
n_unit;


typedef struct n_general_variables {
    n_byte2 random0;
    n_byte2 random1;
    n_byte2 attack_melee_dsq;
    n_byte2 declare_group_facing_dsq;
    n_byte2 declare_max_start_dsq;
    n_byte2 declare_one_to_one_dsq;
    n_byte2 declare_close_enough_dsq;
}
n_general_variables;

typedef struct n_additional_variables{
    n_int probability_melee;
    n_int probability_missile;
    n_int damage_melee;
    n_int damage_missile;
    n_int speed_max;
    n_int range_missile;
} n_additional_variables;

typedef void (*battle_function)(n_unit * un, n_general_variables * gvar);
typedef void (*combatant_function)(n_combatant * comb, n_general_variables * gvar, void * values);

#define NUMBER_COMBATANTS_A     1024
#define NUMBER_COMBATANTS_B     1024
#define NUMBER_COMBATANTS       (NUMBER_COMBATANTS_A + NUMBER_COMBATANTS_B)

#define	COMB_MEMORY	  (sizeof(n_combatant) * NUMBER_COMBATANTS)
#define	UNIT_MEMORY	  (sizeof(n_unit) * 2)
#define	TYPE_MEMORY	  (sizeof(n_type) * 2)

#define SIZEOF_BUFFER (COMB_MEMORY + engine_MEMORY + UNIT_MEMORY + TYPE_MEMORY)

typedef enum{
    BC_NO_COMMAND = 0,
    BC_ATTACK,
    BC_SLOW_DOWN,
    BC_HALT,
    BC_REGROUP
}battle_command;

n_byte board_clear(n_vect2 * pt);

void * engine_init(n_uint random_init, n_string battle_string);

void engine_square_dimensions(n_vect2 * start, n_vect2 * end);


unsigned char engine_mouse(short px, short py);
void engine_mouse_up(void);
void engine_key_received(n_byte2 key);

n_int engine_update(void);

n_int engine_new(void);
n_int engine_new_with_string(n_string input_string);

n_byte engine_over(void);

n_int engine_conditions(n_file *file_json);
n_file * engine_conditions_file(n_constant_string file_name);

n_unit * engine_units(n_byte2 * num_units);

void engine_cycle(void);
void engine_scorecard(void);
void engine_exit(void);

void battle_fill(n_unit * un, n_general_variables * gvar);
void battle_move(n_unit *un, n_general_variables * gvar);
void battle_declare(n_unit *un, n_general_variables * gvar);
void battle_attack(n_unit *un, n_general_variables * gvar);
void battle_remove_dead(n_unit *un, n_general_variables * gvar);

void draw_init(void);
void draw_cycle(n_unit *un, n_general_variables * gvar);
void draw_rectangle(n_int px1, n_int py1, n_int px2, n_int py2);
void draw_render(n_byte * value);

void draw_engine(n_byte * value);


void draw_dpx(n_double dpx);
void draw_dpy(n_double dpy);
void draw_dpz(n_double dpz);

void  combatant_loop(combatant_function func, n_unit * un, n_general_variables * gvar, void * values);
void  battle_loop(battle_function func, n_unit * un, const n_uint count, n_general_variables * gvar);
n_byte battle_opponent(n_unit * un, n_uint num, n_uint * no_movement);

void board_init(n_byte * value);

n_byte	board_add(n_vect2 * pt, n_byte color);
n_byte	board_move(n_vect2 * fr, n_vect2 * pt);






