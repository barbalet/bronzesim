/****************************************************************

draw.c

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

static n_int px = 0;
static n_int py = 0;
static n_int pz = 0;

static n_byte  render[ (256*4) * (256*3)*4 ]={0};
static n_byte4 points[ (256*4) * (256*3) ]={0};

static n_int   pointCount = 0;
static n_byte  color = 0;
static n_byte  unit_selected = 0;

void draw_init(void)
{
}

void draw_dpx(n_double dpx)
{
    n_int int_dpx = (n_int)(dpx * 100);
    px += int_dpx;
}

void draw_dpy(n_double dpy)
{
    n_int int_dpy = (n_int)(dpy * 100);
    py += int_dpy;
}

void draw_dpz(n_double dpz)
{
    n_int int_dpz = (n_int)(dpz * 100);
    n_int total_zoom = pz + int_dpz;
    if ((total_zoom > -100) && (total_zoom < 128))
    {
        pz = total_zoom;
    }
}

#ifdef _WIN32

static n_byte windows_screen[ (256*4) * (256*3) ] = {0};

#define   COLOR_WHITE      (255)

#define   COLOR_LIGHT_RED  (192)
#define   COLOR_DARK_RED   (128)
#define   COLOR_LIGHT_BLUE (64)
#define   COLOR_DARK_BLUE  (32)

#define   COLOR_BLACK      (0)

void draw_color(n_int value, n_byte * rgb)
{
    switch (value)
    {
        case COLOR_LIGHT_RED:
            rgb[0] = 255;
            rgb[1] = 77;
            rgb[2] = 77;
            break;
        case COLOR_DARK_RED:
            rgb[0] = 3*255/5;
            rgb[1] = 3*77/5;
            rgb[2] = 3*77/5;
            break;
        case COLOR_LIGHT_BLUE:
            rgb[0] = 128;
            rgb[1] = 128;
            rgb[2] = 255;
            break;
        case COLOR_DARK_BLUE:
            rgb[0] = 3*128/5;
            rgb[1] = 3*128/5;
            rgb[2] = 3*255/5;
            break;
        case COLOR_WHITE:
            rgb[0] = 255;
            rgb[1] = 255;
            rgb[2] = 255;
            break;
        case COLOR_BLACK:
        default:
            rgb[0] = 0;
            rgb[1] = 0;
            rgb[2] = 0;
            break;
    }
}

static void draw_point_platform(n_int px, n_int py)
{
    n_byte4 loop  = (n_byte4)(px + (py* (256*4)));
    points[pointCount++] = loop;
    if (color)
    {
        if (unit_selected)
        {
            render[loop] = COLOR_LIGHT_RED;
        }else{
            render[loop] = COLOR_DARK_RED;
        }
    }
    else
    {
        if (unit_selected)
        {
            render[loop] = COLOR_LIGHT_BLUE;
        }else{
            render[loop] = COLOR_DARK_BLUE;
        }
    }
}

static void draw_point_white_platform(n_int px, n_int py)
{
    n_byte4 loop  = (n_byte4)(px + (py*(256*4)));
    points[pointCount++] = loop;
    render[loop] = COLOR_WHITE;

}

static void draw_render_platform(n_byte * value)
{
    n_uint loop = 0;
    memory_copy(windows_screen, value, (256*4)*(256*3));
    while (loop < pointCount)
    {
        n_uint  loop4 = points[loop];
        windows_screen[loop4] = COLOR_BLACK;
        loop++;
    }
    pointCount = 0;
}

#else

static void draw_point_platform(n_int px, n_int py)
{
    n_byte4 loop  = (n_byte4)(px + (py*(256*4)));
    n_uint  loop4 = loop * 4;
    
    points[pointCount++] = loop;
    render[loop4 + 0] = 0;
    
    if (color)
    {
//        if (unit_selected)
//        {
            render[loop4 + 1] = 255;
            render[loop4 + 2] = 128;
            render[loop4 + 3] = 128;
//        }else{
//            render[loop4 + 1] = 3*255/5;
//            render[loop4 + 2] = 3*128/5;
//            render[loop4 + 3] = 3*128/5;
//        }
    }
    else
    {
//        if (unit_selected)
//        {
            render[loop4 + 1] = 128;
            render[loop4 + 2] = 128;
            render[loop4 + 3] = 255;
//        }else{
//            render[loop4 + 1] = 3*128/5;
//            render[loop4 + 2] = 3*128/5;
//            render[loop4 + 3] = 3*255/5;
//        }
    }
}

static void draw_point_white_platform(n_int px, n_int py)
{
    n_byte4 loop = (n_byte4)(px + (py * (256*4)));
    n_uint  loop4 = loop * 4;
    
    points[pointCount++] = loop;
    
    render[loop4 + 0] = 255;
    render[loop4 + 1] = 255;
    render[loop4 + 2] = 255;
    render[loop4 + 3] = 255;
}


static void draw_render_platform(n_byte * value)
{
    n_uint loop = 0;
    memory_copy(render, value, (256*4)*(256*3)*4);
    while (loop < pointCount)
    {
        n_uint  loop4 = points[loop] * 4;
        render[loop4 + 0] = 0;
        render[loop4 + 1] = 0;
        render[loop4 + 2] = 0;
        render[loop4 + 3] = 0;
        loop++;
    }
    pointCount = 0;
}

#endif

void draw_render(n_byte * value)
{
    draw_render_platform(value);
}

void draw_combatant(n_combatant * comb, n_general_variables *gvar, void * values)
{
    if (comb->wounds != NUNIT_DEAD)
    {
        n_byte2 x = ((comb->location.x * (256*4)) >> 10);
        n_byte2 y = (comb->location.y * (256*4)) >> 10;
        draw_point_platform(x, y);
    }
}

void draw_cycle(n_unit *un, n_general_variables * gvar)
{
    color = un->alignment;
    unit_selected = un->selected;
    combatant_loop(&draw_combatant, un, gvar, NOTHING);
}


void draw_point(n_int px, n_int py)
{
    if ((px > -1) && (px < (256*4)) && (py > -1) && (py < (256*3)))
    {
        draw_point_white_platform(px, py);
    }
}

void draw_line(n_int px1, n_int py1, n_int px2, n_int py2)
{
    n_int incrementer = 1;
    if (px1 == px2)
    {
        n_int loop = py1;

        if (py1 > py2)
        {
            incrementer = -1;
        }
        
        while (loop != py2)
        {
            draw_point(px1, loop);
            loop += incrementer;
        }
    }
    if (py1 == py2)
    {
        n_int loop = px1;

        if (px1 > px2)
        {
            incrementer = -1;
        }
        while (loop != px2)
        {
            draw_point(loop, py1);
            loop += incrementer;
        }
    }
}

void draw_rectangle(n_int px1, n_int py1, n_int px2, n_int py2)
{
    draw_line(px1, py1, px1, py2);
    draw_line(px2, py2, px1, py2);
    draw_line(px2, py2, px2, py1);
    draw_line(px1, py1, px2, py1);
}


void draw_engine(n_byte * value)
{
    n_vect2 start, end;
    n_byte2 number_units;
    n_unit * units = engine_units(&number_units);

    battle_loop(&draw_cycle, units, number_units, NOTHING);
    
    engine_square_dimensions(&start, &end);

    if ((end.x > -1) && (end.y > -1))
    {
        draw_rectangle(start.x, start.y, end.x, end.y);
    }
    draw_render(value);
}
