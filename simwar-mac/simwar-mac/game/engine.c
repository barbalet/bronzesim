/****************************************************************
 
	engine.c - Noble Warfare Skirmish
 
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

#ifdef _WIN32
#include <windows.h>
#endif

#include "stdio.h"

#include "battle.h"

#define BATTLE_JSON_LOCATION1 "./Simulated War.app/Contents/Resources/battle.json"
#define BATTLE_JSON_LOCATION2 "battle.json"
#define BATTLE_JSON_LOCATION3 "./war/game/battle.json"

n_byte          *local_board;

static n_general_variables  game_vars;

static n_unit	*units;
static n_byte2	number_units;
static n_type   *types;
static n_byte2  number_types;

static n_uint  no_movement = 0;

#define	SIZEOF_MEMORY	 (64*1024*1024)

static n_byte	*memory_buffer;
static n_uint   memory_allocated;
static n_uint	memory_used;

static n_byte engine_paused = 0;
static n_byte engine_new_required = 0;
static n_byte engine_debug = 0;

static n_file * open_file_json = 0L;

static n_int engine_count = 0;

static n_object * obj_unit_type(n_type * values)
{
    n_object * return_object = object_number(0L, "defence", values->defence);
    object_number(return_object, "melee_attack", values->melee_attack);
    object_number(return_object, "melee_damage", values->melee_damage);
    object_number(return_object, "melee_armpie", values->melee_armpie);
    
    object_number(return_object, "missile_attack", values->missile_attack);
    object_number(return_object, "missile_damage", values->missile_damage);
    object_number(return_object, "missile_armpie", values->missile_armpie);
    object_number(return_object, "missile_rate", values->missile_rate);
    
    object_number(return_object, "missile_range", values->missile_range);
    object_number(return_object, "speed_maximum", values->speed_maximum);
    object_number(return_object, "stature", values->stature);
    object_number(return_object, "leadership", values->leadership);
    
    object_number(return_object, "wounds_per_combatant", values->wounds_per_combatant);
    object_number(return_object, "type_id", values->points_per_combatant);
    
    return return_object;
}

static n_object * obj_unit(n_unit * values)
{
    n_object * return_object = object_number(0L, "type_id", values->morale);
    n_array  * average_array = array_number(values->average[0]);
    array_add(average_array, array_number(values->average[1]));
    object_number(return_object, "width", values->width);
    object_array(return_object, "average", average_array);
    object_number(return_object, "angle", values->angle);
    object_number(return_object, "number_combatants", values->number_combatants);
    object_number(return_object, "alignment", values->alignment);
    object_number(return_object, "missile_number", values->missile_number);
    
    return return_object;
}

static n_object * obj_general_variables(n_general_variables * values)
{
    n_object * return_object = object_number(0L, "random0", values->random0);
    
    object_number(return_object, "random1", values->random1);
    object_number(return_object, "attack_melee_dsq", values->attack_melee_dsq);
    object_number(return_object, "declare_group_facing_dsq", values->declare_group_facing_dsq);
    object_number(return_object, "declare_max_start_dsq", values->declare_max_start_dsq);
    object_number(return_object, "declare_one_to_one_dsq", values->declare_one_to_one_dsq);
    object_number(return_object, "declare_close_enough_dsq", values->declare_close_enough_dsq);
    
    return return_object;
}

static n_object * obj_additional_variables(n_additional_variables * values)
{
    n_object * return_object = object_number(0L, "probability_melee", values->probability_melee);
    
    object_number(return_object, "probability_missile", values->probability_missile);
    object_number(return_object, "damage_melee", values->damage_melee);
    object_number(return_object, "damage_missile", values->damage_missile);
    object_number(return_object, "speed_max", values->speed_max);
    object_number(return_object, "range_missile", values->range_missile);
    
    return return_object;
}

n_int draw_error(n_constant_string error_text, n_constant_string location, n_int line_number)
{
#ifdef _WIN32
	LPSTR lpBuff = error_text;
	DWORD dwSize = 0;
	WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), lpBuff, lstrlen(lpBuff), &dwSize, NULL);

	//char str[256];
	//sprintf_s(str, "ERROR: %s\n", error_text);
	OutputDebugString(error_text);
#else
    printf("ERROR: %s, %s line: %ld\n", error_text, location, line_number);
#endif
	return -1;
}


static void mem_init(n_byte start) {
	if (start) {
		memory_buffer = NOTHING;
		memory_allocated = SIZEOF_MEMORY;
		
		memory_buffer = memory_new_range((SIZEOF_MEMORY/4), &memory_allocated);
	}
	memory_used = 0;
}


static n_byte * mem_use(n_uint size) {
	n_byte * val = NOTHING;
	if (size > (memory_allocated - memory_used)) {
		engine_exit();
		/*plat_close();*/
	}
	val = &memory_buffer[memory_used];
	memory_used += size;
        
	return val;
}

const n_string_block json_file_string = "{\"general_variables\":{\"random0\":58668,\"random1\":8717,\"attack_melee_dsq\":5,\"declare_group_facing_dsq\":8000,\"declare_max_start_dsq\":65535,\"declare_one_to_one_dsq\":65535,\"declare_close_enough_dsq\":5},\"unit_types\":[{\"defence\":4,\"melee_attack\":6,\"melee_damage\":3,\"melee_armpie\":1,\"missile_attack\":3,\"missile_damage\":4,\"missile_armpie\":1,\"missile_rate\":100,\"missile_range\":40,\"speed_maximum\":3,\"stature\":4,\"leadership\":2,\"wounds_per_combatant\":3,\"type_id\":0},{\"defence\":2,\"melee_attack\":3,\"melee_damage\":1,\"melee_armpie\":1,\"missile_attack\":0,\"missile_damage\":0,\"missile_armpie\":0,\"missile_rate\":0,\"missile_range\":0,\"speed_maximum\":4,\"stature\":1,\"leadership\":2,\"wounds_per_combatant\":2,\"type_id\":1},{\"defence\":4,\"melee_attack\":6,\"melee_damage\":2,\"melee_armpie\":2,\"missile_attack\":0,\"missile_damage\":0,\"missile_armpie\":0,\"missile_rate\":0,\"missile_range\":0,\"speed_maximum\":6,\"stature\":2,\"leadership\":4,\"wounds_per_combatant\":3,\"type_id\":2},{\"defence\":2,\"melee_attack\":7,\"melee_damage\":1,\"melee_armpie\":3,\"missile_attack\":0,\"missile_damage\":0,\"missile_armpie\":0,\"missile_rate\":0,\"missile_range\":0,\"speed_maximum\":7,\"stature\":2,\"leadership\":3,\"wounds_per_combatant\":1,\"type_id\":3}],\"units\":[{\"type_id\":0,\"width\":28,\"average\":[100,200],\"angle\":120,\"number_combatants\":480,\"alignment\":0,\"missile_number\":20},{\"type_id\":0,\"width\":28,\"average\":[100,400],\"angle\":128,\"number_combatants\":480,\"alignment\":0,\"missile_number\":20},{\"type_id\":0,\"width\":28,\"average\":[100,600],\"angle\":136,\"number_combatants\":480,\"alignment\":0,\"missile_number\":20},{\"type_id\":2,\"width\":10,\"average\":[250,400],\"angle\":128,\"number_combatants\":100,\"alignment\":0,\"missile_number\":0},{\"type_id\":2,\"width\":10,\"average\":[265,200],\"angle\":128,\"number_combatants\":100,\"alignment\":0,\"missile_number\":0},{\"type_id\":0,\"width\":50,\"average\":[500,200],\"angle\":0,\"number_combatants\":600,\"alignment\":1,\"missile_number\":20},{\"type_id\":2,\"width\":35,\"average\":[500,600],\"angle\":0,\"number_combatants\":100,\"alignment\":1,\"missile_number\":0},{\"type_id\":0,\"width\":40,\"average\":[700,600],\"angle\":0,\"number_combatants\":600,\"alignment\":1,\"missile_number\":20},{\"type_id\":3,\"width\":40,\"average\":[600,600],\"angle\":0,\"number_combatants\":200,\"alignment\":1,\"missile_number\":0}]}";


n_unit * engine_units(n_byte2 * num_units)
{
    *num_units = number_units;
    return units;
}

n_file * engine_conditions_file(n_constant_string file_name)
{
    n_file         *file_json = io_file_new();

    if (io_disk_read_no_error(file_json, (n_string)file_name) != 0)
    {
        io_file_free(&file_json);
        return 0L;
    }
    else
    {
        printf("%s loaded\n", file_name);
    }
    
    if (open_file_json)
    {
        io_file_free(&open_file_json);
    }
    
    open_file_json = io_file_duplicate(file_json);
    
    return file_json;
}

n_int engine_conditions(n_file *file_json)
{
    if (file_json == 0L)
    {
        return SHOW_ERROR("Read file failed");
    }
    
    number_units = 0;
    number_types = 0;
    
    mem_init(0);

    local_board = (n_byte *)mem_use(BATTLE_BOARD_SIZE);
    
    if (local_board == NOTHING)
    {
        return SHOW_ERROR("Local board not allocated");
    }
    
    memory_erase(local_board, BATTLE_BOARD_SIZE);
    board_init(local_board);

    io_whitespace_json(file_json);
    
    {
        n_object_type type_of;
        void * returned_blob = unknown_file_to_tree(file_json, &type_of);
        n_object * returned_object = 0L;
        
        if (returned_blob == 0L)
        {
            return SHOW_ERROR("Failed to parse JSON file");
        }
        
        if (type_of == OBJECT_OBJECT)
        {
            returned_object = (n_object *)returned_blob;
        }
        
        if (returned_object == 0L)
        {
            unknown_free(&returned_blob, type_of);
            return SHOW_ERROR("JSON root is not an object");
        }
        
        {
            n_string str_general_variables = obj_contains(returned_object, "general_variables", OBJECT_OBJECT);
            n_string str_unit_types = obj_contains(returned_object, "unit_types", OBJECT_ARRAY);
            n_string str_units = obj_contains(returned_object, "units", OBJECT_ARRAY);
            n_object * obj_general_variables = 0L;
            
            if (str_general_variables)
            {
                obj_general_variables = obj_get_object(str_general_variables);
            }
            
            if (str_unit_types == 0L)
            {
                unknown_free(&returned_blob, type_of);
                return SHOW_ERROR("Missing unit_types array in JSON");
            }

            if (str_units == 0L)
            {
                unknown_free(&returned_blob, type_of);
                return SHOW_ERROR("Missing units array in JSON");
            }
            
            {
                n_array * arr_unit_types = obj_get_array(str_unit_types);
                n_array * arr_follow = 0L;
                n_int value;
                
                if (arr_unit_types == 0L)
                {
                    unknown_free(&returned_blob, type_of);
                    return SHOW_ERROR("Failed to get unit_types array");
                }
                
                types = (n_type *) mem_use(0);
                if (types == 0L)
                {
                    unknown_free(&returned_blob, type_of);
                    return SHOW_ERROR("Failed to allocate memory for unit types");
                }
                
                while ((arr_follow = obj_array_next(arr_unit_types, arr_follow)))
                {
                    n_object * obj_follow = obj_get_object(arr_follow->data);
                    n_type * current_type = &types[number_types];
                    
                    if (obj_follow == 0L)
                    {
                        unknown_free(&returned_blob, type_of);
                        return SHOW_ERROR("Invalid unit type object in array");
                    }

                    if (obj_contains_number(obj_follow, "defence", &value))
                    {
                        current_type->defence = value;
                    }
                    if (obj_contains_number(obj_follow, "melee_attack", &value))
                    {
                        current_type->melee_attack = value;
                    }
                    if (obj_contains_number(obj_follow, "melee_damage", &value))
                    {
                        current_type->melee_damage = value;
                    }
                    if (obj_contains_number(obj_follow, "melee_armpie", &value))
                    {
                        current_type->melee_armpie = value;
                    }
                    if (obj_contains_number(obj_follow, "missile_rate", &value))
                    {
                        current_type->missile_rate = value;
                    }
                    if (obj_contains_number(obj_follow, "missile_range", &value))
                    {
                        current_type->missile_range = value;
                    }
                    if (obj_contains_number(obj_follow, "speed_maximum", &value))
                    {
                        current_type->speed_maximum = value;
                    }
                    if (obj_contains_number(obj_follow, "stature", &value))
                    {
                        current_type->stature = value;
                    }
                    if (obj_contains_number(obj_follow, "leadership", &value))
                    {
                        current_type->leadership = value;
                    }
                    if (obj_contains_number(obj_follow, "wounds_per_combatant", &value))
                    {
                        current_type->wounds_per_combatant = value;
                    }
                    if (obj_contains_number(obj_follow, "type_id", &value))
                    {
                        current_type->points_per_combatant = value;
                    }
                    
                    if (mem_use(sizeof(n_type)) == 0L)
                    {
                        unknown_free(&returned_blob, type_of);
                        return SHOW_ERROR("Failed to allocate memory for unit type");
                    }
                    number_types++;
                    
                    if (number_types > 255)
                    {
                        unknown_free(&returned_blob, type_of);
                        return SHOW_ERROR("Too many unit types (maximum 255)");
                    }
                }
            
                {
                    n_array * arr_units = obj_get_array(str_units);
                    n_array * arr_follow = 0L;
                    n_int value;
                    
                    if (arr_units == 0L)
                    {
                        unknown_free(&returned_blob, type_of);
                        return SHOW_ERROR("Failed to get units array");
                    }
                    
                    units = (n_unit *) mem_use(0);
                    if (units == 0L)
                    {
                        unknown_free(&returned_blob, type_of);
                        return SHOW_ERROR("Failed to allocate memory for units");
                    }
                    
                    while ((arr_follow = obj_array_next(arr_units, arr_follow)))
                    {
                        n_object * obj_follow = obj_get_object(arr_follow->data);
                        n_unit * current_unit = &units[number_units];
                                              
                        memory_erase((n_byte *)current_unit, sizeof(n_unit));
                        
                        if (obj_follow == 0L)
                        {
                            unknown_free(&returned_blob, type_of);
                            return SHOW_ERROR("Invalid unit object in array");
                        }
                        
                        if (obj_contains_number(obj_follow, "type_id", &value))
                        {
                            current_unit->morale = value;
                        }
                        if (obj_contains_number(obj_follow, "width", &value))
                        {
                            current_unit->width = value;
                        }
                        if (obj_contains_number(obj_follow, "angle", &value))
                        {
                            current_unit->angle = value;
                        }
                        if (obj_contains_number(obj_follow, "number_combatants", &value))
                        {
                            current_unit->number_combatants = value;
                        }
                        if (obj_contains_number(obj_follow, "alignment", &value))
                        {
                            current_unit->alignment = value;
                        }
                        if (obj_contains_number(obj_follow, "missile_number", &value))
                        {
                            current_unit->missile_number = value;
                        }
                        
                        if (obj_contains_array_nbyte2(obj_follow, "average", current_unit->average, 2) == 0)
                        {
                            unknown_free(&returned_blob, type_of);
                            return SHOW_ERROR("Failed to read unit average position");
                        }
                        
                        if (mem_use(sizeof(n_unit)) == 0L)
                        {
                            unknown_free(&returned_blob, type_of);
                            return SHOW_ERROR("Failed to allocate memory for unit");
                        }
                        if (current_unit->number_combatants < 1)
                        {
                            unknown_free(&returned_blob, type_of);
                            return SHOW_ERROR("Failed to allow for combatants");
                        }
                        number_units++;
                    }
                    
                    if (obj_general_variables)
                    {
                        n_general_variables * values = (n_general_variables*)&game_vars;
                        n_int value;
                        if (obj_contains_number(obj_general_variables, "random0", &value))
                        {
                            values->random0 = value;
                        }
                        if (obj_contains_number(obj_general_variables, "random1", &value))
                        {
                            values->random1 = value;
                        }
                        if (obj_contains_number(obj_general_variables, "attack_melee_dsq", &value))
                        {
                            values->attack_melee_dsq = value;
                        }
                        if (obj_contains_number(obj_general_variables, "declare_group_facing_dsq", &value))
                        {
                            values->declare_group_facing_dsq = value;
                        }
                        if (obj_contains_number(obj_general_variables, "declare_max_start_dsq", &value))
                        {
                            values->declare_max_start_dsq = value;
                        }
                        if (obj_contains_number(obj_general_variables, "declare_one_to_one_dsq", &value))
                        {
                            values->declare_one_to_one_dsq = value;
                        }
                        if (obj_contains_number(obj_general_variables, "declare_close_enough_dsq", &value))
                        {
                            values->declare_close_enough_dsq = value;
                        }
                    }
                }
            }
            unknown_free(&returned_blob, type_of);
        }
    }
    
    if (number_types == 0)
    {
        return SHOW_ERROR("No unit types loaded");
    }
    
    if (number_units == 0)
    {
        return SHOW_ERROR("No units loaded");
    }
    
    if (number_types > 255)
    {
        return SHOW_ERROR("Too many unit types (maximum 255)");
    }
        
    /* resolve the units with types and check the alignments */
    {
        // At the start of the resolution section:
        n_byte resolve[256] = {[0 ... 255] = 0xFF};  // Initialize all to invalid
        n_uint check_alignment[2] = {0};
        n_byte loop = 0;

        while (loop < number_types) {
            if (types[loop].points_per_combatant > 255) {
                return SHOW_ERROR("Unit type ID exceeds maximum value");
            }
            resolve[types[loop].points_per_combatant] = loop;
            loop++;
        }
        
        loop = 0;

        while (loop < number_units) {
            n_byte2 local_combatants = units[loop].number_combatants;
            n_byte type_id = units[loop].morale;
            
            // First validate the type_id exists in our resolve map
            if (type_id >= 256) {  // type_id is n_byte (0-255)
                return SHOW_ERROR("Unit type ID exceeds maximum value");
            }
            
            if (resolve[type_id] == 0xFF) {  // 0xFF means not found
                return SHOW_ERROR("Unit references non-existent type ID");
            }
            
            // Only proceed if we have a valid type
            units[loop].unit_type = &types[resolve[type_id]];
            units[loop].morale = 255;
            units[loop].number_living = local_combatants;
            
            if (local_combatants == 0)
            {
                return SHOW_ERROR("Unit has zero combatants");
            }
            
            units[loop].combatants = (n_combatant *)mem_use(sizeof(n_combatant)*local_combatants);
            if (units[loop].combatants == 0L)
            {
                return SHOW_ERROR("Failed to allocate memory for combatants");
            }
            
            n_byte alignment = units[loop].alignment;
            if (alignment > 1)
            {
                return SHOW_ERROR("Invalid unit alignment (must be 0 or 1)");
            }
            
            check_alignment[alignment]++;
            loop++;
        }

        /* if there are none of one of the alignments, there can be no battle */
        if (check_alignment[0] == 0)
        {
            return SHOW_ERROR("No units with alignment 0 found");
        }
        
        if (check_alignment[1] == 0)
        {
            return SHOW_ERROR("No units with alignment 1 found");
        }
    }
    
    /* get the drawing ready, fill the units with spaced combatants and draw it all */
    battle_loop(&battle_fill, units, number_units, NOTHING);
    return 0;
}

static n_byte sm_last = 0;
static n_int startx = -1, starty = -1, endx = -1, endy = -1;

void engine_unit(n_unit * unit, n_int startx, n_int starty, n_int endx, n_int endy)
{
    n_int loop = 0;
    n_combatant * combatants = (n_combatant *)unit->combatants;

    while (loop < unit->number_combatants)
    {
        n_int px = combatants[loop].location.x;
        n_int py = combatants[loop].location.y;

        if ((startx <= px) && (px <= endx) && (starty <= py) && (py <= endy))
        {
            unit->selected = 1;
            return;
        }
        loop++;
    }
    unit->selected = 0;
}

void engine_mouse_up(void)
{
    n_int loop = 0;
    printf("start ( %ld , %ld ) end ( %ld , %ld )\n", startx, starty, endx, endy);
    
    if ((startx != endx) && (starty != endy))
    {
        if (startx > endx)
        {
            n_int temp = endx;
            endx = startx;
            startx = temp;
        }
        if (starty > endy)
        {
            n_int temp = endy;
            endy = starty;
            starty = temp;
        }
    }
    
    startx = (startx << 10) / 800;
    starty = (starty << 10) / 800;
    
    endx = (endx << 10) / 800;
    endy = (endy << 10) / 800;
    
    while (loop < number_units)
    {
        engine_unit(&units[loop], startx, starty, endx, endy);
        loop++;
    }
    
    sm_last = 0;
    startx = -1;
    starty = -1;
    endx = -1;
    endy = -1;
}

unsigned char engine_mouse(short px, short py)
{
	if (sm_last)
    {
		endx = px;
		endy = py;
	} else {
        startx = px;
        starty = py;
		endx = px;
		endy = py;
	}
    sm_last = 1;
	return 1;
}

void engine_square_dimensions(n_vect2 * start, n_vect2 * end)
{
    start->x = startx;
    start->y = starty;

    end->x = endx;
    end->y = endy;
}

n_int engine_new_with_string(n_string input_string) {
    no_movement = 0;
    
    // Input validation
    if (input_string == NULL) return SHOW_ERROR("String into engine is NULL");
    if (input_string[0] == '\0') return SHOW_ERROR("String into engine has no contents");
    
    // Clean up existing state
    if (open_file_json) io_file_free(&open_file_json);
    
    // Create and validate new file
    open_file_json = io_file_new_from_string_block(input_string);
    if (!open_file_json) return SHOW_ERROR("Failed to create file from JSON string");
    
    // This does all the actual validation including type IDs
    n_int result = engine_conditions(open_file_json);
    if (result != 0) {
        io_file_free(&open_file_json);
        return result;
    }
    
    // Final validation checks
    if (number_units == 0) return SHOW_ERROR("No units loaded from JSON");
    
    // Check alignments
    n_byte has_alignment[2] = {0};
    for (n_byte2 i = 0; i < number_units; i++) {
        if (units[i].alignment > 1) return SHOW_ERROR("Invalid unit alignment");
        has_alignment[units[i].alignment] = 1;
    }
    if (!has_alignment[0] || !has_alignment[1]) {
        return SHOW_ERROR("Need units from both alignments");
    }
    
    return 0;
}
n_int engine_new(void)
{
    return engine_new_with_string((n_string)json_file_string);
}

void * engine_init(n_uint random_init, n_string battle_string)
{
    engine_count = 0;
    
    game_vars.random0 = (n_byte2) (random_init & 0xFFFF);
    game_vars.random1 = (n_byte2) (random_init >> 16);

    printf("random (%hu , %hu)\n",game_vars.random0 ,  game_vars.random1);
    
    game_vars.attack_melee_dsq = 5;
    game_vars.declare_group_facing_dsq = 8000;
    game_vars.declare_max_start_dsq = 0xffff;
    game_vars.declare_one_to_one_dsq = 0xffff;
    game_vars.declare_close_enough_dsq = 5;
    
    mem_init(1);

    if (battle_string) {
        if (engine_new_with_string(battle_string) != 0)
        {
            return 0L;
        }
    } else {
        if (engine_new_with_string((n_string)json_file_string) != 0)
        {
            return 0L;
        }
    }
    
    return ((void *) local_board);
}

void engine_key_received(n_byte2 key)
{
    if ((key == 'p') || (key == 'P'))
    {
        engine_paused = ! engine_paused;
    }
    if ((key == 'n') || (key == 'N'))
    {
        engine_new_required = 1;
    }
    if ((key == 'd') || (key == 'D'))
    {
        engine_debug = ! engine_debug;
    }
}

void engine_scorecard(void)
{
    n_uint count[2] = {0};
    n_int  loop = 0;
    while (loop < number_units)
    {
        count[units[loop].alignment] += units[loop].number_living;
        loop++;
    }
    printf("%ld , %ld\n", count[0], count[1]);
    printf("random (%hu , %hu), %ld\n",game_vars.random0 ,  game_vars.random1, engine_count);
}

void engine_cycle(void)
{
    battle_loop(&battle_move, units, number_units, &game_vars);
    battle_loop(&battle_declare, units, number_units, &game_vars);
    battle_loop(&battle_attack, units, number_units, &game_vars);
    battle_loop(&battle_remove_dead, units, number_units, NOTHING);
    
    engine_count++;
}

n_byte engine_over(void)
{
    n_byte result = battle_opponent(units, number_units, &no_movement);
    
    if  (engine_debug)
    {
        engine_scorecard();
    }
    if ((result != 0) || (no_movement > 6))
    {
        printf("result %d no movement %ld\n", result, no_movement);
        return 1;
    }
    return 0;
}

n_int engine_update(void)
{
    if (engine_new_required)
    {
        engine_new();
        engine_new_required = 0;
    }
    else if (engine_paused == 0)
    {
        if (engine_over())
        {
            printf("engine_over\n");
            engine_scorecard();
            return engine_new();
        }
        engine_cycle();
    }
	return 0;
}


void engine_exit(void)
{
    if (open_file_json)
    {
        io_file_free(&open_file_json);
    }
	memory_free((void **)&memory_buffer);
}

// TODO: Remove this or integrate it better
void  io_command_line_execution_set( void )
{
}
