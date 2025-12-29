/****************************************************************
 
	battle.c - Noble Warfare Skirmish
 
 =============================================================
 
 Copyright 1996-2025 Tom Barbalet. All rights reserved.
 
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

#include "battle.h"

void  combatant_loop(combatant_function func, n_unit * un, n_general_variables * gvar, void * values)
{
    n_combatant * combatant = (n_combatant *)(un->combatants);
    n_byte2 loop = 0;
    while (loop < un->number_combatants) {
        (*func)((&combatant[loop++]), gvar, values);
    }
}

static void battle_area(n_unit * unit)
{
    n_combatant * combatant = (n_combatant *)unit->combatants;
    n_int loop = 0;
    while (loop < unit->number_combatants)
    {
        area2_add(&(unit->area), &(combatant->location), loop == 0);
        loop++;
    }
}

void  battle_loop(battle_function func, n_unit * un, const n_uint count , n_general_variables * gvar)
{
	n_uint loop = 0;
	while (loop < count)
		(*func)((&un[loop++]), gvar);
}

n_byte battle_alignment_color(n_unit * un)
{
    if (un->alignment == 0)
    {
        return 128;
    }
    return 255;
}

typedef struct
{
    n_vect2 px;
    n_vect2 py;
    n_vect2 dpx;
    n_vect2 dpy;
    n_int   edgex;
    n_int   edgey;
    n_byte  color;
    n_int   loc_angle;
    n_byte  loc_wounds;
    n_int   line;
    n_int   loc_width;
} battle_fill_struct;


void combatant_fill(n_combatant * comb, n_general_variables * gvar, void * values)
{
    battle_fill_struct * local_bfs = (battle_fill_struct *)values;
 
    n_int	pos_x = ((((local_bfs->px.x + local_bfs->py.x) >> 9) + local_bfs->edgex) % BATTLE_BOARD_WIDTH);
    n_int   pos_y = ((((local_bfs->px.y - local_bfs->py.y) >> 9) + local_bfs->edgey) % BATTLE_BOARD_HEIGHT);
    
    n_vect2 pos;
    
    pos.x = pos_x;
    pos.y = pos_y;
    
    if (board_add(&pos, local_bfs->color))
    {
        comb->location.x = pos.x;
        comb->location.y = pos.y;
        comb->direction_facing = (n_byte)local_bfs->loc_angle;
        comb->attacking = NUNIT_NO_ATTACK;
        comb->wounds = local_bfs->loc_wounds;
        comb->speed_current = 0;
    }
    
    local_bfs->line ++;
    
    if (local_bfs->line == local_bfs->loc_width)
    {
        local_bfs->line = 0;
        vect2_populate(&local_bfs->px, 0, 0);
        vect2_d(&local_bfs->py, &local_bfs->dpy, 1, 1);
    }
    else
    {
        vect2_d(&local_bfs->px, &local_bfs->dpx, 1, 1);
    }
}


void battle_fill(n_unit * un, n_general_variables * gvar)
{
    battle_fill_struct local_bfs;
    n_int   dx = (UNIT_SIZE(un)+2)/2;
    n_int   dy = (UNIT_SIZE(un)+3)/2;
    n_int   loc_angle  = (un->angle);
	n_int   loc_order  = UNIT_ORDER(un);
    n_int   loc_height;
    n_vect2 facing;

	local_bfs.loc_wounds = GET_TYPE(un)->wounds_per_combatant;
    local_bfs.color = battle_alignment_color(un);
    
	local_bfs.loc_width  = (un->width);
    
    local_bfs.line = 0;
    
    local_bfs.px.x = 0;
    local_bfs.px.y = 0;
    
    local_bfs.py.x = 0;
    local_bfs.py.y = 0;
    
    vect2_direction(&facing, loc_angle, 16);
    
	if (local_bfs.loc_width > un->number_combatants)
    {
		local_bfs.loc_width = un->number_combatants;
	}
    
    NA_ASSERT(local_bfs.loc_width , "width is zero");
    
    loc_height = (un->number_combatants + local_bfs.loc_width - (un->number_combatants % local_bfs.loc_width)) / local_bfs.loc_width;
    
    if ((loc_order & 1) == 1)
    {
		if (dx == dy)
        {
			dx += 1;
			dy += 1;
		}
        else
        {
			dx = dy;
		}
	}
	
    vect2_populate(&local_bfs.dpx, (facing.y * dx), (facing.x * dx));
    vect2_populate(&local_bfs.dpy, (facing.x * dy), (facing.y * dy));
    
	dx = (local_bfs.loc_width  * dx);
	dy = (loc_height * dy);
	
	local_bfs.edgex = (un->average[0]) - (((facing.y * dx) + (facing.x * dy)) >> 10);
	local_bfs.edgey = (un->average[1]) - (((facing.x * dx) - (facing.y * dy)) >> 10);
    
    combatant_loop(&combatant_fill, un, gvar, (void*)&local_bfs);
    battle_area(un);
}


static n_int battle_calc_damage(n_int wounds, n_int damage)
{
	wounds -= damage;
    if (wounds < 1) {
		wounds = 0;
    }
	return wounds;
}

static void battle_combatant_attack(n_combatant * comb, n_combatant * comb_at,
                                    n_general_variables * gvar, void *additional_variables){
    	/* which opponent combatant are they attacking */
	const n_byte2	loc_attacking = comb->attacking;
	/* what is the squared distance between them */
	const n_int		distance_squared = comb->distance_squ;
	/* roll the 1024 sided dice */
	n_int			dice_roll = math_random(&gvar->random0) & 1023;
    
	/* if the attacking combatant isn't dead */
	if (comb->wounds == NUNIT_DEAD) {
		return;
	}
	/* are they attacking a unit? */
	if (loc_attacking == NUNIT_NO_ATTACK) {
		return;
	}
    
	comb_at = &comb_at[loc_attacking];
    {
        n_additional_variables * av = (n_additional_variables*)additional_variables;
    
        /* if the distance is within the melee range */
        if (distance_squared < gvar->attack_melee_dsq) {	/* val1*/
            /* stop if melee attacking */
            comb->speed_current = 0;
            /* melee hit */
            if (dice_roll < av->probability_melee) {
                comb_at->wounds = (n_byte) battle_calc_damage(comb_at->wounds, av->damage_melee);
            }
            /* else if the distance is within the missle range */
        } else if (distance_squared < av->range_missile) {
            /* missile hit */
            if (dice_roll < av->probability_missile) {
                comb_at->wounds = (n_byte) battle_calc_damage(comb_at->wounds,av->damage_missile);
            }
        } else {
            comb->speed_current = (n_byte)av->speed_max;
        }
    }
}


void battle_attack(n_unit *un, n_general_variables * gvar) {
    
	n_additional_variables		additional_variables;
	n_uint		loop = 0;
	/* the combatants doing the attacking */
	n_combatant  *comb = (n_combatant *)(un->combatants);
	n_combatant  *comb_at;
    
	/* only worthwhile if the unit is attacking something */
	if (un->unit_attacking == NOTHING) {
		return;
	}
    
	{
		/* the range of the missile weapons */
		n_int	  rang_missile = 0;
		n_type   *typ = un->unit_type;
		/* the unit that is being attacked */
		n_unit	 *un_at =  un->unit_attacking;
		n_type   *typ_at = un_at->unit_type;
		/* the combatants being attacked */
		
		comb_at = un_at->combatants;
        
		/* the probabilities of causing damage to the attacked unit */
        
		additional_variables.probability_melee = (typ->melee_attack) * (16 - (typ_at->defence) + (typ->melee_armpie));
		additional_variables.probability_missile = (typ->missile_attack) * (16 - (typ_at->defence) + (typ->missile_armpie));
        
		additional_variables.damage_melee = typ->melee_damage;
		additional_variables.damage_missile = typ->missile_damage;
        
		additional_variables.speed_max = typ->speed_maximum;
        
		if (un->missile_number != 0) {
			if (un->missile_timer == typ->missile_rate) {
				rang_missile = typ->missile_range;
				rang_missile *= rang_missile;
				un->missile_number--;
				un->missile_timer = 0;
			} else
				un->missile_timer++;
		}
        
		additional_variables.range_missile = rang_missile;
        
	}
	while (loop < un->number_combatants) {
		battle_combatant_attack(&comb[loop],comb_at,gvar,(void*)&additional_variables);
		loop++;
	}
}

static n_int combatant_random_facing(n_int local_facing, n_general_variables * gvar)
{
    switch (math_random(&gvar->random0) & 31) {
    
        case 1:
            return (local_facing + 1) & 255;
        case 2:
            return (local_facing + 255) & 255;
        case 3:
            return (local_facing + 2) & 255;
    }
    return (local_facing + 254) & 255;
}


static void battle_combatant_declare(n_combatant * comb, n_general_variables * gvar,
                                     n_unit * un_at, n_byte reverso, n_byte group_facing){
    
	/* if the attacking combatant isn't dead */
	/* cache the important values locally */
	n_int	loc_f = comb->direction_facing;
	/* the initial condition sets up "nothing to attack" and the maximum distance to attack squared */
	n_byte2 loc_attack = NUNIT_NO_ATTACK;
	n_byte2 max_distance_squared = gvar->declare_max_start_dsq;	/* val3 */
    n_vect2 *loc = &comb->location;
    
	n_combatant *comb_at = un_at->combatants;
    
	n_int	distance_centre_squ;
    n_vect2 average;
    n_vect2 delta;
    
    vect2_populate(&average, un_at->average[0], un_at->average[1]);
    
    vect2_subtract(&delta, loc, &average);
    
    distance_centre_squ = vect2_dot(&delta, &delta, 1, 1);
    
	if (comb->wounds == NUNIT_DEAD)
    {
		return;
	}
    
	if (distance_centre_squ < gvar->declare_one_to_one_dsq)
    {							               /* val4 */
		/* the direction facing vector */
        n_vect2 facing;

		/* if the combatant is more than half way through, switch the direction back on the closest-checking loop */
        
		n_byte2 loop2 = 0;
        
        vect2_direction(&facing, loc_f, 32);
        
		while (loop2 < un_at->number_combatants) {
			n_byte2	loc_test = loop2;
			
			if (reverso) {
				loc_test = (n_byte2) ((un_at->number_combatants) - 1 - loop2);
			}
            
			if (comb_at[ loc_test ].wounds != NUNIT_DEAD)
            {
                n_int    distance_squared;
                n_int    distance_facing;
                n_vect2  distance;
                
                vect2_subtract(&distance, &comb_at[ loc_test ].location, loc);
                
                distance_squared = vect2_dot(&distance, &distance, 1, 1);
                distance_facing = vect2_dot(&distance, &facing, 1 ,1);
                
				/* if the combatant is closer than the previous lot AND visible (ie in front) */
				if ((distance_squared < max_distance_squared) && (distance_facing > 0)) {
					/* it is the prefered attacked combatant and the distance and instance is
                     stored */
					max_distance_squared = (n_byte2)distance_squared;
					loc_attack = loc_test;
					/* if this combatant is within the melee attacking distance,
                     the search is over, end this loop(2) */
					if (max_distance_squared < gvar->declare_close_enough_dsq) /* val5 */
                    {
						loop2 += 0xffff;
                    }
				}
			}
			loop2++;
		}
	}
	
	comb->attacking    = loc_attack;
	comb->distance_squ = max_distance_squared;
    
	if (group_facing == 255)
    {
		if (loc_attack != NUNIT_NO_ATTACK)
        {
            n_vect2 delta;
            vect2_subtract(&delta, &comb_at[ loc_attack ].location, loc);
			group_facing = math_tan(&delta) ;
		}
        else
        {
            group_facing = combatant_random_facing( group_facing, gvar);
		}
	}
	comb->direction_facing = group_facing;
}


void battle_declare(n_unit *un, n_general_variables * gvar)
{
	n_uint		 loop = 0;
	n_byte		 group_facing = 255;
	n_combatant *comb      = un->combatants;
	/* the unit that is being attacked */
	n_unit	    *un_at      = un->unit_attacking;
	n_byte2      loc_number = un->number_combatants;
    
	/* only worthwhile if the unit is attacking something */
	if (un_at == NOTHING)
    {
		return;
	}
	{
		/* the combatants being attacked */
		n_int	delta_x = un_at->average[0] - un->average[0];
		n_int	delta_y = un_at->average[1] - un->average[1];
        n_vect2 delta;
        vect2_populate(&delta, delta_x, delta_y);
        
		if ((delta_x * delta_x) + (delta_y * delta_y) >= gvar->declare_group_facing_dsq)
        {		                   /* val2 */
			group_facing = math_tan(&delta);
		}
	}
	while (loop < loc_number) {
		n_byte  reverso = (n_byte) (loop > (n_uint)(loc_number >> 1));
		battle_combatant_declare(&comb[loop], gvar, un_at, reverso, group_facing);
		loop++;
	}
}

void combatant_dead(n_combatant * comb)
{
    comb->wounds = NUNIT_DEAD;
    comb->speed_current = 0;
    comb->attacking = NUNIT_NO_ATTACK;
}


static void combatant_move(n_combatant * comb, n_general_variables * gvar, void * values)
{
	n_int   local_speed = comb->speed_current;
	n_int   local_facing = comb->direction_facing;
    n_vect2 old_location;
    n_vect2 temp_location;
    n_vect2 facing;
    vect2_copy(&old_location, &comb->location);

    vect2_copy(&temp_location, &old_location);
    
	if (comb->wounds == NUNIT_DEAD)
    {
		return;
	}

    if (local_speed == 0)
    {
		return;
	}
    
    local_facing = combatant_random_facing(local_facing, gvar);
    
    vect2_direction(&facing, local_facing, 1);
    
    vect2_d(&temp_location, &facing, local_speed, 26880);

    if (OUTSIDE_HEIGHT(temp_location.y) || OUTSIDE_WIDTH(temp_location.x))
    {
        combatant_dead(comb);
        return;
    }

    if (old_location.x != temp_location.x || old_location.y != temp_location.y)
    {
        if (board_move(&old_location,&temp_location))
        {
            vect2_copy(&comb->location, &temp_location);
        }
    }

	comb->direction_facing = (n_byte) local_facing;
	comb->speed_current    = (n_byte) local_speed;
}

/* this is currently fudged for the skirmish testing... it will be fixed
 in the future... honest... */

void battle_move(n_unit * un, n_general_variables * gvar) {
    combatant_loop(&combatant_move, un, gvar, NOTHING);
    battle_area(un);
}


/* as combatants can continue to fight for the entire battle_cycle,
 the death condition only occurs when wounds == NUNIT_DEAD */

void battle_remove_dead(n_unit *un, n_general_variables * gvar) {
	n_combatant *comb = (n_combatant *)(un->combatants);
    n_vect2 sum = {0};
	n_int   count = 0;
	n_byte2 loop = 0;
	while (loop < un->number_combatants)
    {
		if (comb[ loop ].wounds != NUNIT_DEAD)
        {
			if (comb[ loop ].wounds == 0)
            {
                combatant_dead(&comb[loop]);
				(void)board_clear(&comb[ loop ].location);                
			} else {
                vect2_d(&sum, &comb[ loop ].location, 1, 1);
				count++;
			}
		}
		loop++;
	}
	if (count != 0)
    {
		un->average[0] = (n_byte2)(sum.x / count);
		un->average[1] = (n_byte2)(sum.y / count);
	}
	un->number_living = count;
}

/* returns 1 if the alignment 0 army is all dead,
 returns 2 if the alignment 1 army is all dead,
 returns 3 if both armies are all dead, and,
 returns 0 in all other cases */

n_byte battle_opponent(n_unit * un, n_uint	num, n_uint * no_movement) {
	n_uint	loop = 0;
	n_uint	unit_count[2] = {0};
    n_uint  unit_movement[2] = {0};
	while (loop < num)
    {
		if (un[loop].number_living > 0)
        {
			n_unit	*un_att = un[loop].unit_attacking;
			n_int	local_alignment = (un[loop].alignment) & 1;
            n_combatant *combatants = (n_combatant *)un[loop].combatants;
            n_uint       number_combatants = un[loop].number_combatants;
            n_uint       loop2 = 0;
            n_uint       movement = 0;
            while (loop2 < number_combatants)
            {
                if (combatants[loop2].speed_current != 0)
                {
                    movement = 1;
#ifdef DEBUG_MOVEMENT_TRANSITIONS
                    un[loop].selected = 1;
#endif
                }
                loop2++;
            }
            unit_count[local_alignment]++;

			unit_movement[local_alignment] += movement;
#ifdef DEBUG_MOVEMENT_TRANSITIONS
            if (movement == 0)
            {
                un[loop].selected = 0;
            }
#endif
			if (un_att != NOTHING)
            {
				if (un_att->number_living == 0)
                {
					un_att = NOTHING;
                }
			}
            
			if (un_att == NOTHING)
            {
				n_int	px = un[loop].average[0];
				n_int	py = un[loop].average[1];
				n_uint	min_dist_squ = BIG_INTEGER;
				n_uint	loop2 = 0;
				while (loop2 < num)
                {
					if (loop != loop2 && un[loop2].number_living)
                    {
						if (((un[loop2].alignment)&1) != local_alignment)
                        {
							n_int	tx = un[loop2].average[0];
							n_int	ty = un[loop2].average[1];
							n_uint   dist_squ;
							tx -= px;
							ty -= py;
                            
							dist_squ = (n_uint)((tx*tx) + (ty*ty));
							if (dist_squ < min_dist_squ)
                            {
								min_dist_squ = dist_squ;
								un_att = (n_unit *)&un[loop2];
							}
						}
					}
					loop2++;
				}
			}
			un[loop].unit_attacking = (void *)un_att;
		}
        else
        {
			un[loop].unit_attacking = NOTHING;
        }
		loop++;
	}
    
    if ((unit_movement[0] == 0) && (unit_movement[1] == 0))
    {
        *no_movement = *no_movement + 1;
    }
    else
    {
        *no_movement = 0;
    }
	return ((unit_count[0] == 0) | (unit_count[1] == 0));
}

