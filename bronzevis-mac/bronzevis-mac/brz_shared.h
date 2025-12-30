/****************************************************************

 brz_shared.h

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

#ifndef SIMULATEDAPE_SHARED_H
#define SIMULATEDAPE_SHARED_H

void brz_shared_init(unsigned long random);




/* Enable grayscale height debug rendering (1 = on, 0 = off). */
void brz_shared_set_show_height(int enabled);

/* Load a BRONZESIM scenario (.bronze) and build a realtime world.
   Returns 0 on success, non-zero on failure (errors printed to stderr). */
int brz_shared_load_config(const char* path);
unsigned char * brz_shared_draw(long dim_x, long dim_y);
void brz_shared_cycle(unsigned long ticks);

void brz_shared_close(void);

#endif /* SIMULATEDAPE_SHARED_H */
