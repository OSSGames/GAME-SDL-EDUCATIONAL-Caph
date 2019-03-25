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

#ifndef _H_PHYS_
#define _H_PHYS_

#include <stdint.h>

#include "draw.h"

typedef struct link link_t;
typedef struct concave concave_t;

enum {
	CONCAVE_CONST		= 0x00000010,
	CONCAVE_SOFT		= 0x00000020,
	CONCAVE_BEND		= 0x00000040,
	CONCAVE_PHANTOM		= 0x00000080,
	CONCAVE_FLY		= 0x00010000,
	CONCAVE_TYPE_MASK	= 0x000f00f0,
	CONCAVE_PHANTOM_RED	= 0x00000100,
	CONCAVE_PHANTOM_GREEN	= 0x00000200,
	CONCAVE_PHANTOM_BLUE	= 0x00000400,
	CONCAVE_FLAG_BKGROUND	= 0x00000001,
	CONCAVE_FLAG_LOOP	= 0x00000002,
	CONCAVE_FLAG_TEARABLE	= 0x00000004,
	CONCAVE_FLAG_NOCROSS	= 0x00000008,
	CONCAVE_FLAG_PLAYER	= 0x00001000,
	CONCAVE_FLAG_TARGET	= 0x00002000
};

struct link {
	point_t		*beg;		/* Begin point */
	point_t		*end;		/* End point */
	float		dist;		/* Target distance */
	float		wght;		/* Weight of <beg> point */
};

struct concave {
	point_t		*last;		/* Last points */
	point_t		*pts;		/* Points */
	int		pt;		/* Points number */
	link_t		*lns;		/* Links */
	int		ln;		/* Links number */
	int		flags;		/* Various flags */
	point_t		bbox_min;	/* Bounding box minimal point */
	point_t		bbox_max;	/* Bounding box maximal point */
	uint8_t		*trts;		/* Test results */
	concave_t	*next;
	concave_t	*prev;
};

extern int phys_play_cond;
extern int phys_fail_cond;

concave_t *phys_alloc(int len);
void phys_free(concave_t *cx);
void phys_free_all();

void phys_insert(concave_t *cx);
void phys_attach(concave_t *cx);
void phys_remove(concave_t *cx);
void phys_remove_hold();
void phys_remove_unhold();
void phys_remove_back();

concave_t *phys_get_all();

void phys_pick(float px, float py);
void phys_pick_move(float px, float py);
void phys_pick_detach();

void phys_open();
void phys_close();
void phys_update(float dt);
void phys_draw_paint_all();
void phys_draw_paint_flush();
color_t phys_get_concave_color(int flags);
void phys_draw_concave(concave_t *cx);
void phys_draw();

#endif /* _H_PHYS_ */

