/****************************************************************
 
	board.c - Noble Warfare Skirmish
 
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

#include "battle.h"

static n_byte	*board = NOTHING;

#define		XY_BOARD(pt)		board[(pt->x) | ((pt->y) * BATTLE_BOARD_WIDTH)]

void board_init(n_byte * value)
{
	board = value;
}

static n_int board_location_check(n_vect2 * pt)
{
    if (board == NOTHING)
    {
        return SHOW_ERROR("board not initialized");
    }
    
    if (OUTSIDE_HEIGHT(pt->y) || OUTSIDE_WIDTH(pt->x))
    {
        return SHOW_ERROR("point out of bounds");
    }
    
    return 0;
}

static void board_fill(n_vect2 * pt, n_byte number)
{
    if (board_location_check(pt) == -1)
    {
        return;
    }
    XY_BOARD(pt) = number;
}

n_byte board_clear(n_vect2 * pt)
{
    n_byte value;
    if (board_location_check(pt) == -1)
    {
        return 0;
    }
    value = XY_BOARD(pt);
    
    XY_BOARD(pt) = 0;
    
    return value;
}

static n_int board_occupied(n_vect2 * pt)
{
    if (board_location_check(pt) == -1)
    {
        return 1;
    }
    return (XY_BOARD(pt) > 127);
}

static	n_byte	board_find(n_vect2 * pt) {
	n_uint	best_dsqu = BIG_INTEGER;
	n_int	best_x = 0, best_y = 0;
	n_int ly = -1;
    
	pt->x = (pt->x + BATTLE_BOARD_WIDTH) % BATTLE_BOARD_WIDTH;
	pt->y = (pt->y + BATTLE_BOARD_HEIGHT) % BATTLE_BOARD_HEIGHT;
    
	if (board_occupied(pt)==0) {
		return 1;
	}
	while (ly < 2)
    {
		n_int	lx = -1;
		n_int y_val = (pt->y + ly + BATTLE_BOARD_HEIGHT) % BATTLE_BOARD_HEIGHT;
		while (lx < 2)
        {
			n_int x_val = (pt->x + lx + BATTLE_BOARD_WIDTH) % BATTLE_BOARD_WIDTH;
            n_vect2 value;
            value.x = x_val;
            value.y = y_val;
			if (board_occupied(&value)==0) {
				n_int dx = (pt->x - lx);
				n_int dy = (pt->y - ly);
				n_uint	dsqu = (dx*dx) + (dy*dy);
				if (dsqu < best_dsqu) {
					best_dsqu = dsqu;
					best_x = x_val;
					best_y = y_val;
				}
			}
			lx++;
		}
		ly++;
	}
	if (best_dsqu != BIG_INTEGER) {
		pt->x = best_x;
		pt->y = best_y;
		return 1;
	}
	return 0;
}

n_byte	board_add(n_vect2 * pt, n_byte color) {
	if (board_find(pt))
    {
		board_fill(pt, color);
		return 1;
	}
	return 0;
}

n_byte	board_move(n_vect2 * fr, n_vect2 * pt) {
    if (board_location_check(pt) == -1)
    {
        return 0;
    }
	if (board_find(pt))
    {
		n_byte color = board_clear(fr);
		board_fill(pt, color);
		return 1;
	}
	return 0;
}
