/*
   Copyright (C) 2009 Roman Belov

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _H_DRAW_
#define _H_DRAW_

#include <stdint.h>

typedef struct color color_t;
typedef struct point point_t;

struct color {
	uint8_t		r;
	uint8_t		g;
	uint8_t		b;
	uint8_t		a;
};

struct point {
	float		x;
	float		y;
};

void draw_init();
void draw_scale(float xs, float ys);
void draw_load_bg(const char *img);
void draw_load_bs(const char *img);
void draw_brush(const point_t *s, const point_t *p, color_t col);
void draw_clear();
void draw_line(const point_t *s, const point_t *e, color_t col);
void draw_fade(float alpha);
void draw_fail(float alpha);
void draw_time(float alpha);

#endif /* _H_DRAW_ */

