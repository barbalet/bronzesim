/****************************************************************

 shared.c

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


#include <stdio.h>
#include "battle.h"

static n_int   simulation_started = 0;

static n_byte * outputBuffer = 0L;
static n_byte * outputBufferOld = 0L;
static n_int    outputBufferMax = -1;

static n_string battle_string = 0L;

extern void draw_color(n_int value, n_byte* rgb);

void shared_color_8_bit_to_48_bit(n_byte2* values)
{
#ifdef _WIN32
	n_byte rgb[3];
	n_int loop = 0;
	n_int loop3 = 0;
	while (loop < 256)
	{
		draw_color(loop, rgb);
		values[loop3++] = rgb[0] << 8;
		values[loop3++] = rgb[1] << 8;
		values[loop3++] = rgb[2] << 8;
		loop++;
	}
#endif
}

void shared_dimensions(n_int * dimensions)
{
    dimensions[0] = 1;   /* number windows */
    dimensions[1] = (256 * 4); /* window dimension x */
    dimensions[2] = (256 * 3); /* window dimension y */
    dimensions[3] = 0;   /* has menus */
}

void brz_shared_cycle(n_uint ticks)
{
    if (simulation_started)
        engine_update();
}

void shared_battle_string(n_string string)
{
    battle_string = string;
}

void brz_shared_init(n_uint random)
{
    if (engine_init(random, battle_string))
    {
        simulation_started = 1;
    }
}

void brz_shared_close(void)
{
    engine_exit();
}


static n_byte * brz_shared_output_buffer(n_int width, n_int height)
{
    if (outputBufferOld)
    {
        memory_free((void **)&outputBufferOld);
    }
    
    if ((outputBuffer == 0L) || ((width * height * 4) > outputBufferMax))
    {
        outputBufferMax = width * height * 4;
        outputBufferOld = outputBuffer;
        outputBuffer = memory_new(outputBufferMax);
    }
    return outputBuffer;
}

n_byte * brz_shared_draw(n_int dim_x, n_int dim_y)
{
    n_byte * outputBuffer = brz_shared_output_buffer(dim_x, dim_y);
        
    if (simulation_started)
    {
        draw_engine(outputBuffer);
    }
    return outputBuffer;
}
