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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "phys.h"
#include "caph.h"
#include "maps.h"

#define	PHYS_DELTA		0.0078125f
#define PHYS_PUSH_BIAS		0.2f
#define	PHYS_LINK_PASSES	8
#define	PHYS_CCD_PASSES_MAX	8
#define	GLOB_LINKS_MAX		64
#define	GLOB_PICK_AREA		16.0f
#define GLOB_PAINT_DELTA	0.01f

int phys_play_cond;
int phys_fail_cond;

static float		phys_g = 0.1f;
static float		phys_f = 0.25f;

static concave_t	*all;
static float		adt;
static float		time;

static int		glob_cd;

static link_t		*glob_lns;
static int		glob_ln;

static concave_t	*glob_pick_obj;
static point_t		*glob_pick_point;
static point_t		glob_pick_dst;
static int		glob_pick;

static concave_t	*glob_rm_hold;

static float		glob_paint_adt;
static int		glob_paint_lim;
static int		glob_paint_it;

static void
all_list_insert(concave_t *cx)
{
	if (all) {
		cx->next = all->next;
		all->next->prev = cx;
		cx->prev = all;
		all->next = cx;
	}
	else {
		all = cx;
		all->prev = all;
		all->next = all;
	}
}

static void
all_list_remove(concave_t *cx)
{
	if (all == all->next)
		all = NULL;
	else {
		if (all == cx)
			all = all->prev;
		cx->prev->next = cx->next;
		cx->next->prev = cx->prev;
	}
}

concave_t *phys_alloc(int len)
{
	concave_t	*cx;

	cx = malloc(sizeof(concave_t));
	cx->pt = len;
	cx->lns = NULL;

	cx->last = malloc(sizeof(point_t) * len);
	cx->pts = malloc(sizeof(point_t) * len);
	cx->trts = malloc(sizeof(uint8_t) * len);

	return cx;
}

void phys_free(concave_t *cx)
{
	free(cx->pts);
	free(cx->last);
	free(cx->trts);
	if (cx->lns) free(cx->lns);
	free(cx);
}

void phys_free_all()
{
	concave_t	*curr = all;
	concave_t	*next;

	if (!curr) return;

	do {
		next = curr->next;
		phys_free(curr);
		curr = next;
	}
	while (curr != all);

	all = NULL;
}

static int
phys_intersect_lines(
		const point_t *l1a,
		const point_t *l1b,
		const point_t *l2a,
		const point_t *l2b)
{
	point_t		l1_max, l1_min;
	point_t		l2_max, l2_min;
	point_t		n;
	float		d, sgn;

	/* AABB test */

	if (l1a->x < l1b->x) {
		l1_min.x = l1a->x;
		l1_max.x = l1b->x;
	}
	else {
		l1_min.x = l1b->x;
		l1_max.x = l1a->x;
	}

	if (l1a->y < l1b->y) {
		l1_min.y = l1a->y;
		l1_max.y = l1b->y;
	}
	else {
		l1_min.y = l1b->y;
		l1_max.y = l1a->y;
	}

	if (l2a->x < l2b->x) {
		l2_min.x = l2a->x;
		l2_max.x = l2b->x;
	}
	else {
		l2_min.x = l2b->x;
		l2_max.x = l2a->x;
	}

	if (l2a->y < l2b->y) {
		l2_min.y = l2a->y;
		l2_max.y = l2b->y;
	}
	else {
		l2_min.y = l2b->y;
		l2_max.y = l2a->y;
	}
	
	if (l1_max.x < l2_min.x) return 0;
	if (l1_max.y < l2_min.y) return 0;
	if (l1_min.x > l2_max.x) return 0;
	if (l1_min.y > l2_max.y) return 0;

	/* Full line x line test */

	n.y = (l1a->x - l1b->x);
	n.x = -(l1a->y - l1b->y);
	d = l1a->x * n.x + l1a->y * n.y;

	sgn = l2a->x * n.x + l2a->y * n.y - d;
	sgn *= l2b->x * n.x + l2b->y * n.y - d;

	if (sgn > 0.0f) return 0;

	n.y = (l2a->x - l2b->x);
	n.x = -(l2a->y - l2b->y);
	d = l2a->x * n.x + l2a->y * n.y;

	sgn = l1a->x * n.x + l1a->y * n.y - d;
	sgn *= l1b->x * n.x + l1b->y * n.y - d;

	if (sgn > 0.0f) return 0;

	return 1;
}

static void
phys_glob_link_put(point_t *p1, point_t *p2, int flags)
{
	float x, y;
	float dist;
	float w;

	if (glob_ln > (GLOB_LINKS_MAX - 1)) return;

	x = p1->x - p2->x;
	y = p1->y - p2->y;
	dist = sqrtf(x * x + y * y);

	glob_lns[glob_ln].beg = p1;
	glob_lns[glob_ln].end = p2;
	glob_lns[glob_ln].dist = dist;

	if (flags & 1)
		w = 0.0f;
	else if (flags & 2)
		w = 1.0f;
	else
		w = 0.5f;

	glob_lns[glob_ln++].wght = w;
}

static void
phys_intersection_links(concave_t *cx, concave_t *px)
{
	point_t		*pts = cx->pts;
	int		pt = cx->pt;
	point_t		*p_pts = px->pts;
	link_t		*p_lns = px->lns;
	int		p_pt = px->pt;
	link_t		*lnc;
	int		i, j;
	int		flags, ret;

	flags =
		((cx->flags & CONCAVE_CONST) ? 1 : 0) |
		((px->flags & CONCAVE_CONST) ? 2 : 0) ;

	for (i = 1; i < pt; ++i) {
		for (j = 1; j < p_pt; ++j) {

			if (px->flags & CONCAVE_FLAG_TEARABLE) {

				lnc = p_lns + j - 1;
				if (lnc->beg == lnc->end)
					continue;
			}

			ret = phys_intersect_lines(
					pts + i - 1,
					pts + i,
					p_pts + j - 1,
					p_pts + j);

			if (ret) {
				phys_glob_link_put(
						pts + i - 1,
						p_pts + j - 1,
						flags);

				phys_glob_link_put(
						pts + i,
						p_pts + j,
						flags);

				phys_glob_link_put(
						pts + i - 1,
						p_pts + j,
						flags);

				phys_glob_link_put(
						pts + i,
						p_pts + j - 1,
						flags);
			}
		}
	}
}

static void
phys_intersection_links_all(concave_t *cx)
{
	concave_t	*curr = all;

	if (!curr) return;

	glob_ln = 0;

	do {
		do {
			if (curr == cx) break;

			if ((curr->flags & CONCAVE_CONST) &&
					(cx->flags & CONCAVE_CONST))
				break;

			if (curr->flags & CONCAVE_PHANTOM)
				break;

			if (curr->flags & CONCAVE_FLY)
				break;

			phys_intersection_links(cx, curr);
		}
		while (0);

		curr = curr->next;
	}
	while (curr != all);
}

void phys_insert(concave_t *cx)
{
	float		x, y;
	float		dist;
	int		pt, ln;
	int		i, j;

	all_list_insert(cx);

	pt = cx->pt;
	ln = 0;

	/* Do not moving */

	for (i = 0; i < pt; ++i) {
		cx->last[i].x = cx->pts[i].x;
		cx->last[i].y = cx->pts[i].y;
	}

	/* Create joints */

	if (cx->flags & CONCAVE_FLAG_NOCROSS)
		glob_ln = 0;
	else
		phys_intersection_links_all(cx);

	if (cx->flags & CONCAVE_FLY) {
		/* Do not save joints links */
		glob_ln = 0;
	}

	if (cx->flags & CONCAVE_CONST) {
		/* Do not save joints links */
		glob_ln = 0;
	}
	else if (cx->flags & CONCAVE_PHANTOM) {
		/* Do not save joints links */
		glob_ln = 0;
	}
	else if (cx->flags & CONCAVE_SOFT) {

		/* Wire/Rope objects */

		int	ln_c = pt - 1;

		if (cx->flags & CONCAVE_FLAG_LOOP)
			ln_c += 1;

		cx->lns = malloc(sizeof(link_t) * (ln_c + glob_ln));

		for (i = 1; i < pt; ++i) {
			x = cx->pts[i - 1].x - cx->pts[i].x;
			y = cx->pts[i - 1].y - cx->pts[i].y;
			dist = sqrtf(x * x + y * y);

			cx->lns[ln].beg = cx->pts + i - 1;
			cx->lns[ln].end = cx->pts + i;
			cx->lns[ln].dist = dist;
			cx->lns[ln++].wght = 0.5f;
		}

		if (cx->flags & CONCAVE_FLAG_LOOP) {
			cx->lns[ln].beg = cx->pts + pt - 1;
			cx->lns[ln].end = cx->pts + 0;
			cx->lns[ln].dist = 16.0f;
			cx->lns[ln++].wght = 0.5f;
		}

		cx->flags |= CONCAVE_FLAG_TEARABLE;
	}
	else if (!(cx->flags & (CONCAVE_CONST | CONCAVE_PHANTOM))) {

		/* Regular and Bendable objects */

		int	ln_c = pt * (pt - 1) / 2;

		cx->lns = malloc(sizeof(link_t) * (ln_c + glob_ln));

		for (i = 0; i < pt; ++i) {
			for (j = i + 1; j < pt; ++j) {

				x = cx->pts[i].x - cx->pts[j].x;
				y = cx->pts[i].y - cx->pts[j].y;
				dist = sqrtf(x * x + y * y);

				cx->lns[ln].beg = cx->pts + i;
				cx->lns[ln].end = cx->pts + j;
				cx->lns[ln].dist = dist;
				cx->lns[ln++].wght = 0.5f;
			}
		}
	}

	/* Copy joints to end */

	if (glob_ln) {
		memcpy(cx->lns + ln, glob_lns, sizeof(link_t) * glob_ln);
		ln += glob_ln;
	}

	cx->ln = ln;
}

void phys_attach(concave_t *cx)
{
	all_list_insert(cx);

	if (cx->flags & CONCAVE_SOFT) {
		cx->flags |= CONCAVE_FLAG_TEARABLE;
	}
}

void phys_remove(concave_t *cx)
{
	all_list_remove(cx);
	phys_free(cx);
}

static void
phys_remove_links_for(concave_t *cx, concave_t *px)
{
	point_t		*p, *s, *e;
	link_t		*lns = px->lns;
	int 		ln;
	int		i;

	s = cx->pts;
	e = s + cx->pt;

	if (px->flags & CONCAVE_SOFT) {
		ln = px->ln - (px->pt - 1);
		if (px->flags & CONCAVE_FLAG_LOOP) --ln;
	}
	else {
		ln = px->ln - (px->pt * (px->pt - 1) / 2);
	}

	for (i = px->ln - ln; i < px->ln; ++i) {

		p = lns[i].end;

		if (p < e && p >= s) {
			lns[i].end = lns[i].beg;
		}
	}
}

static void
phys_remove_links(concave_t *cx)
{
	concave_t	*curr = all;

	if (!curr) return;

	do {
		do {
			if (curr == cx) break;

			if (curr->flags & CONCAVE_CONST)
				break;

			if (curr->flags & CONCAVE_PHANTOM)
				break;

			phys_remove_links_for(cx, curr);
		}
		while (0);

		curr = curr->next;
	}
	while (curr != all);
}

void phys_remove_hold()
{
	if (!all) return;

	glob_rm_hold = all->next;
}

void phys_remove_unhold()
{
	glob_rm_hold = NULL;
}

void phys_remove_back()
{
	concave_t	*target;

	if (glob_pick) {

		phys_remove(glob_pick_obj);
		glob_pick = 0;

		return;
	}

	if (!all) return;

	target = all->next;

	if (target != glob_rm_hold) {
		phys_remove(target);
	}
}

concave_t *phys_get_all()
{
	return all;
}

static void
phys_verlet(concave_t *cx)
{
	point_t		*lst = cx->last;
	point_t		*pts = cx->pts;
	int		i, pt = cx->pt;
	float		dx, dy;
	float		x, y;

	for (i = 0; i < pt; ++i) {
		x = pts[i].x;
		y = pts[i].y;
		dx = (x - lst[i].x);
		dy = (y - lst[i].y) + phys_g;
		pts[i].x = x + dx;
		pts[i].y = y + dy;
		lst[i].x = x;
		lst[i].y = y;
	}
}

static void
phys_fly(concave_t *cx)
{
	point_t		*lst = cx->last;
	point_t		*pts = cx->pts;
	int		i, pt = cx->pt;
	float		dx, dy;
	float		x, y;
	float		kv;

	kv = 1.0f + 0.05f * (float) (pt & 7);

	for (i = 0; i < pt; ++i) {
		x = pts[i].x;
		y = pts[i].y;
		dx = (x - lst[i].x);
		dy = (y - lst[i].y);
		if (dx > +phys_f) dx = +phys_f;
		if (dx < -phys_f) dx = -phys_f;
		if (dy > +(phys_f * kv)) dy = +(phys_f * kv);
		if (dy < -phys_f) dy = -phys_f;
		if ((rand() & 255) < 16) {
			dx += phys_f * (rand() / (float) RAND_MAX - 0.5f) * 0.25f;
			dy += phys_f * (rand() / (float) RAND_MAX - 0.2f) * 0.25f;
		}
		pts[i].x = x + dx;
		pts[i].y = y + dy;
		lst[i].x = x;
		lst[i].y = y;
	}
}

static void
phys_links(concave_t *cx)
{
	point_t		*beg, *end;
	link_t		*lns = cx->lns;
	int		ln = cx->ln;
	float		dx, dy;
	float		dist, diff, w;
	float		diff_beg;
	float		diff_end;
	int		i;

	if (cx->flags & CONCAVE_BEND) {
		for (i = 0; i < ln; ++i) {
			beg = lns[i].beg;
			end = lns[i].end;
			if (beg == end) continue;
			dx = beg->x - end->x;
			dy = beg->y - end->y;
			dist = sqrtf(dx * dx + dy * dy);
			dx /= dist;
			dy /= dist;
			diff = (dist - lns[i].dist);
			if (fabsf(diff) > 1.0f) {
				lns[i].dist += diff * 0.2f;
			}
			diff = (lns[i].dist - dist) * 0.5f;
			w = lns[i].wght;
			diff_beg = diff * w;
			diff_end = diff * (1.0f - w);
			beg->x += dx * diff_beg;
			beg->y += dy * diff_beg;
			end->x += -dx * diff_end;
			end->y += -dy * diff_end;
		}
	}
	else if (cx->flags & CONCAVE_SOFT) {
		for (i = 0; i < ln; ++i) {
			beg = lns[i].beg;
			end = lns[i].end;
			if (beg == end) continue;
			dx = beg->x - end->x;
			dy = beg->y - end->y;
			dist = sqrtf(dx * dx + dy * dy);
			dx /= dist;
			dy /= dist;
			diff = (lns[i].dist - dist);
			if (fabsf(diff) > 20.0f) {
				lns[i].end = beg;
				lns[i].dist = 0.0f;
			}
			w = lns[i].wght;
			diff_beg = diff * w;
			diff_end = diff * (1.0f - w);
			beg->x += dx * diff_beg;
			beg->y += dy * diff_beg;
			end->x += -dx * diff_end;
			end->y += -dy * diff_end;
		}
	}
	else {
		for (i = 0; i < ln; ++i) {
			beg = lns[i].beg;
			end = lns[i].end;
			if (beg == end) continue;
			dx = beg->x - end->x;
			dy = beg->y - end->y;
			dist = sqrtf(dx * dx + dy * dy);
			dx /= dist;
			dy /= dist;
			diff = (lns[i].dist - dist) * 0.5f;
			w = lns[i].wght;
			diff_beg = diff * w;
			diff_end = diff * (1.0f - w);
			beg->x += dx * diff_beg;
			beg->y += dy * diff_beg;
			end->x += -dx * diff_end;
			end->y += -dy * diff_end;
		}
	}
}

static void
phys_reverse(concave_t *cx)
{
	point_t		*lst = cx->last;
	point_t		*pts = cx->pts;
	int		i, pt = cx->pt;

	if (cx->bbox_min.x > (float) map_w) {
		for (i = 0; i < pt; ++i) {
			pts[i].x += - cx->bbox_max.x;
			lst[i].x += - cx->bbox_max.x;
		}
	}

	if (cx->bbox_max.x < 0.0f) {
		for (i = 0; i < pt; ++i) {
			pts[i].x += + ((float) map_w - cx->bbox_min.x);
			lst[i].x += + ((float) map_w - cx->bbox_min.x);
		}
	}

	if (cx->bbox_min.y > (float) map_h) {
		for (i = 0; i < pt; ++i) {
			pts[i].y += - cx->bbox_max.y;
			lst[i].y += - cx->bbox_max.y;
		}
	}

	if (cx->bbox_max.y < 0.0f) {
		for (i = 0; i < pt; ++i) {
			pts[i].y += + ((float) map_h - cx->bbox_min.y);
			lst[i].y += + ((float) map_h - cx->bbox_min.y);
		}
	}
}

static void
phys_bbox(concave_t *cx)
{
	point_t		*lst = cx->last;
	point_t		*pts = cx->pts;
	int		i, pt = cx->pt;
	point_t		min, max;

	min.x = + 4096.f;
	min.y = + 4096.f;

	max.x = - 4096.f;
	max.y = - 4096.f;

	for (i = 0; i < pt; ++i) {
		if (pts[i].x < min.x) min.x = pts[i].x;
		if (lst[i].x < min.x) min.x = lst[i].x;
		if (pts[i].y < min.y) min.y = pts[i].y;
		if (lst[i].y < min.y) min.y = lst[i].y;
		if (pts[i].x > max.x) max.x = pts[i].x;
		if (lst[i].x > max.x) max.x = lst[i].x;
		if (pts[i].y > max.y) max.y = pts[i].y;
		if (lst[i].y > max.y) max.y = lst[i].y;
	}

	cx->bbox_min = min;
	cx->bbox_max = max;
}

static int
phys_bbox_intersect(const concave_t *cx, const concave_t *px)
{
	if (cx->bbox_min.x > px->bbox_max.x) return 0;
	if (cx->bbox_max.x < px->bbox_min.x) return 0;
	if (cx->bbox_min.y > px->bbox_max.y) return 0;
	if (cx->bbox_max.y < px->bbox_min.y) return 0;

	return 1;
}

static int
phys_bbox_away(const concave_t *cx)
{
	if (cx->bbox_min.x > (float) map_w) return 1;
	if (cx->bbox_max.x < 0.0f) return 1;
	if (cx->bbox_min.y > (float) map_h) return 1;
	if (cx->bbox_max.y < 0.0f) return 1;

	return 0;
}

static void
phys_push_point_line(
		point_t *pt,
		point_t *pt_p,
		point_t *la,
		point_t *lb,
		point_t *la_p,
		point_t *lb_p,
		int flags)
{
	point_t		n, s;
	float		dist, frct;
	float		d;

	/* Distanse */

	n.y = (la->x - lb->x);
	n.x = -(la->y - lb->y);
	d = sqrtf(n.x * n.x + n.y * n.y);
	n.x /= d;
	n.y /= d;
	d = la->x * n.x + la->y * n.y;

	dist = pt->x * n.x + pt->y * n.y - d;

	/* Velocity */

	s.x = (pt->x - pt_p->x) - ((la->x - la_p->x) + (lb->x - lb_p->x)) * 0.5f;
	s.y = (pt->y - pt_p->y) - ((la->y - la_p->y) + (lb->y - lb_p->y)) * 0.5f;

	frct = s.x * n.y - s.y * n.x;

	/* If one of bodies is a wire (soft object) */

	if (flags & 4) {
		frct *= 0.1f;
	}

	/* If line is motionless body */

	if (flags & 2) {

		dist *= 1.0f;
		frct *= -0.4f;

		dist += (dist < 0.0f) ? - PHYS_PUSH_BIAS : + PHYS_PUSH_BIAS;

		pt->x +=	- n.x * dist	+ n.y * frct;
		pt->y +=	- n.y * dist	- n.x * frct;

		return;
	}

	/* If point is motionless body */

	if (flags & 1) {

		dist *= 1.0f;
		frct *= -0.4f;

		dist += (dist < 0.0f) ? - PHYS_PUSH_BIAS : + PHYS_PUSH_BIAS;

		la->x +=	+ n.x * dist	- n.y * frct;
		la->y +=	+ n.y * dist	+ n.x * frct;
		lb->x +=	+ n.x * dist	- n.y * frct;
		lb->y +=	+ n.y * dist	+ n.x * frct;

		return;
	}

	/* No motionless bodies */

	dist *= 0.5f;
	frct *= -0.2f;

	dist += (dist < 0.0f) ? - PHYS_PUSH_BIAS : + PHYS_PUSH_BIAS;

	pt->x +=	- n.x * dist	+ n.y * frct;
	pt->y +=	- n.y * dist	- n.x * frct;
	la->x +=	+ n.x * dist	- n.y * frct;
	la->y +=	+ n.y * dist	+ n.x * frct;
	lb->x +=	+ n.x * dist	- n.y * frct;
	lb->y +=	+ n.y * dist	+ n.x * frct;
}

#define	PHYS_EPS	0.00001f
#define NONZERO(x)	(fabsf(x) > PHYS_EPS)

static int
phys_ccd_point_line(
		point_t *pt,
		point_t *pt_p,
		point_t *la,
		point_t *lb,
		point_t *la_p,
		point_t *lb_p,
		int flags)
{
	point_t		pa, ba, ppaa, bbaa;
	point_t		n_pt, n_a, n_b;
	point_t		n, pb;
	float		a, b, c, d;
	float		t, t1, t2;

	/* Full CCD test */

	pa.x = pt_p->x - la_p->x;
	pa.y = pt_p->y - la_p->y;

	ba.y = lb_p->x - la_p->x;
	ba.x = -(lb_p->y - la_p->y);

	ppaa.x = (pt->x - pt_p->x) - (la->x - la_p->x);
	ppaa.y = (pt->y - pt_p->y) - (la->y - la_p->y);

	bbaa.y = (lb->x - lb_p->x) - (la->x - la_p->x);
	bbaa.x = -((lb->y - lb_p->y) - (la->y - la_p->y));

	a = ppaa.x * bbaa.x + ppaa.y * bbaa.y;
	b = pa.x * bbaa.x + pa.y * bbaa.y + ba.x * ppaa.x + ba.y * ppaa.y;
	c = pa.x * ba.x + pa.y * ba.y;

	if (NONZERO(a)) {
		d = b * b - 4.0f * a * c;
		if (d < 0.0f) return 0;
		d = sqrtf(d);
		t1 = (- b + d) / (2.0f * a);
		t2 = (- b - d) / (2.0f * a);
		if (t1 >= 0.0f && t1 <= 1.0f) {
			if (t2 >= 0.0f && t2 <= 1.0f)
				return 0;
			else
				t = t1;
		}
		else if (t2 >= 0.0f && t2 <= 1.0f)
			t = t2;
		else
			return 0;
	}
	else if (NONZERO(b)) {
		t = - c / b;
		if (t < 0.0f || t > 1.0f) return 0;
	}
	else return 0;

	if (isnan(t)) return 0;

	n_pt.x = pt_p->x + (pt->x - pt_p->x) * t;
	n_pt.y = pt_p->y + (pt->y - pt_p->y) * t;

	n_a.x = la_p->x + (la->x - la_p->x) * t;
	n_a.y = la_p->y + (la->y - la_p->y) * t;

	n_b.x = lb_p->x + (lb->x - lb_p->x) * t;
	n_b.y = lb_p->y + (lb->y - lb_p->y) * t;

	n.x = n_b.x - n_a.x;
	n.y = n_b.y - n_a.y;

	pa.x = n_pt.x - n_a.x;
	pa.y = n_pt.y - n_a.y;
	d = pa.x * n.x + pa.y * n.y;

	pb.x = n_pt.x - n_b.x;
	pb.y = n_pt.y - n_b.y;
	d *= pb.x * n.x + pb.y * n.y;

	if (d > 0.0f) return 0;

	/* Collision detected, go to pushing */

	phys_push_point_line(pt, pt_p, la, lb, la_p, lb_p, flags);

	return 1;
}

static inline int
phys_collise_point_test(
		const point_t *min,
		const point_t *max,
		const point_t *p)
{
	if (p->x > max->x) return 0;
	if (p->x < min->x) return 0;
	if (p->y > max->y) return 0;
	if (p->y < min->y) return 0;

	return 1;
}

static inline int
phys_collise_line_test(
		const point_t *min,
		const point_t *max,
		const point_t *la,
		const point_t *lb)
{
	point_t		lmin;
	point_t		lmax;

	if (la->x < lb->x) {
		lmin.x = la->x;
		lmax.x = lb->x;
	}
	else {
		lmin.x = lb->x;
		lmax.x = la->x;
	}

	if (la->y < lb->y) {
		lmin.y = la->y;
		lmax.y = lb->y;
	}
	else {
		lmin.y = lb->y;
		lmax.y = la->y;
	}

	if (lmin.x > max->x) return 0;
	if (lmax.x < min->x) return 0;
	if (lmin.y > max->y) return 0;
	if (lmax.y < min->y) return 0;

	return 2;
}

static void
phys_collise_fly(concave_t *cx, concave_t *fly)
{
	point_t		*pts = fly->pts;
	int		i, pt = fly->pt;
	point_t		*c_pts = cx->pts;
	point_t		*c_lst = cx->last;
	int		n, c_pt = cx->pt;
	point_t		carea_min;
	point_t		carea_max;
	point_t		force;

	/* Calculate bounding boxes crossing */

	carea_min.x = (cx->bbox_min.x < fly->bbox_min.x) ?
		fly->bbox_min.x : cx->bbox_min.x - 1.0f;
	carea_min.y = (cx->bbox_min.y < fly->bbox_min.y) ?
		fly->bbox_min.y : cx->bbox_min.y - 1.0f;
	carea_max.x = (cx->bbox_max.x > fly->bbox_max.x) ?
		fly->bbox_max.x : cx->bbox_max.x + 1.0f;
	carea_max.y = (cx->bbox_max.y > fly->bbox_max.y) ?
		fly->bbox_max.y : cx->bbox_max.y + 1.0f;

	/* Calculate force */

	force.x = 0.0f;
	force.y = 0.0f;
	n = 1;

	for (i = 0; i < c_pt; ++i) {
		if (phys_collise_point_test(
					&carea_min,
					&carea_max,
					c_pts + i)) {
			force.x += c_pts[i].x - c_lst[i].x;
			force.y += c_pts[i].y - c_lst[i].y;
			++n;
		}
	}

	force.x *= 0.25f / (float) n;
	force.y *= 0.25f / (float) n;

	/* Apply force */

	for (i = 0; i < pt; ++i) {
		if (phys_collise_point_test(
					&carea_min,
					&carea_max,
					pts + i)) {
			pts[i].x += force.x;
			pts[i].y += force.y;
		}
	}
}

static void
phys_collise(concave_t *cx, concave_t *px)
{
	point_t		*lst = cx->last;
	point_t		*pts = cx->pts;
	link_t		*lns = cx->lns;
	int		pt = cx->pt;
	point_t		*p_lst = px->last;
	point_t		*p_pts = px->pts;
	link_t		*p_lns = px->lns;
	int		p_pt = px->pt;
	link_t		*lnc;
	int		i, j;
	int		flags, flags_swp;
	point_t		carea_min;
	point_t		carea_max;
	int		ret;

	/* This flags have influence to colision response */

	flags =
		((cx->flags & CONCAVE_CONST) ? 1 : 0) |
		((px->flags & CONCAVE_CONST) ? 2 : 0) |
		(((cx->flags | px->flags) & CONCAVE_SOFT) ? 4 : 0) ;

	flags_swp = (flags & 4) |
		((flags & 1) << 1) |
		((flags & 2) >> 1) ;

	/* Calculate bounding boxes crossing */

	carea_min.x = (cx->bbox_min.x < px->bbox_min.x) ?
		px->bbox_min.x : cx->bbox_min.x - 1.0f;
	carea_min.y = (cx->bbox_min.y < px->bbox_min.y) ?
		px->bbox_min.y : cx->bbox_min.y - 1.0f;
	carea_max.x = (cx->bbox_max.x > px->bbox_max.x) ?
		px->bbox_max.x : cx->bbox_max.x + 1.0f;
	carea_max.y = (cx->bbox_max.y > px->bbox_max.y) ?
		px->bbox_max.y : cx->bbox_max.y + 1.0f;

	/* Test points */

	for (i = 0; i < pt; ++i) {
		ret = phys_collise_point_test(
				&carea_min,
				&carea_max,
				pts + i);
		ret |= phys_collise_point_test(
				&carea_min,
				&carea_max,
				lst + i);
		cx->trts[i] = ret;
	}

	for (i = 1; i < pt; ++i) {
		ret = phys_collise_line_test(
				&carea_min,
				&carea_max,
				pts + i - 1,
				pts + i);
		ret |= phys_collise_line_test(
				&carea_min,
				&carea_max,
				lst + i - 1,
				lst + i);
		cx->trts[i] |= ret;
	}

	for (i = 0; i < p_pt; ++i) {
		ret = phys_collise_point_test(
				&carea_min,
				&carea_max,
				p_pts + i);
		ret |= phys_collise_point_test(
				&carea_min,
				&carea_max,
				p_lst + i);
		px->trts[i] = ret;
	}

	for (i = 1; i < p_pt; ++i) {
		ret = phys_collise_line_test(
				&carea_min,
				&carea_max,
				p_pts + i - 1,
				p_pts + i);
		ret |= phys_collise_line_test(
				&carea_min,
				&carea_max,
				p_lst + i - 1,
				p_lst + i);
		px->trts[i] |= ret;
	}

	/* Collision detection */

	if (px->flags & CONCAVE_FLAG_TEARABLE) {

		for (i = 0; i < pt; ++i) {

			if (!(cx->trts[i] & 1)) continue;

			for (j = 1; j < p_pt; ++j) {

				lnc = p_lns + j - 1;
				if (lnc->beg == lnc->end) continue;

				if (!(px->trts[j] & 2))
					continue;

				glob_cd += phys_ccd_point_line(
						pts + i,
						lst + i,
						p_pts + j - 1,
						p_pts + j,
						p_lst + j - 1,
						p_lst + j,
						flags);
			}
		}

		/* For loops */

		do {
			if (!(px->flags & CONCAVE_FLAG_LOOP))
				break;

			lnc = p_lns + p_pt - 1;
			if (lnc->beg == lnc->end) break;

			for (i = 0; i < pt; ++i) {

				if (!(cx->trts[i] & 1)) continue;

				glob_cd += phys_ccd_point_line(
						pts + i,
						lst + i,
						p_pts + 0,
						p_pts + p_pt - 1,
						p_lst + 0,
						p_lst + p_pt - 1,
						flags);
			}
		}
		while (0);
	}
	else {

		for (i = 0; i < pt; ++i) {

			if (!(cx->trts[i] & 1)) continue;

			for (j = 1; j < p_pt; ++j) {

				if (!(px->trts[j] & 2))
					continue;

				glob_cd += phys_ccd_point_line(
						pts + i,
						lst + i,
						p_pts + j - 1,
						p_pts + j,
						p_lst + j - 1,
						p_lst + j,
						flags);
			}
		}
	}

	if (cx->flags & CONCAVE_FLAG_TEARABLE) {

		for (i = 0; i < p_pt; ++i) {

			if (!(px->trts[i] & 1)) continue;

			for (j = 1; j < pt; ++j) {

				lnc = lns + j - 1;
				if (lnc->beg == lnc->end) continue;

				if (!(cx->trts[j] & 2))
					continue;

				glob_cd += phys_ccd_point_line(
						p_pts + i,
						p_lst + i,
						pts + j - 1,
						pts + j,
						lst + j - 1,
						lst + j,
						flags_swp);
			}
		}

		/* For loops */

		do {
			if (!(cx->flags & CONCAVE_FLAG_LOOP))
				break;

			lnc = lns + pt - 1;
			if (lnc->beg == lnc->end) break;

			for (i = 0; i < p_pt; ++i) {

				if (!(px->trts[i] & 1)) continue;

				glob_cd += phys_ccd_point_line(
						p_pts + i,
						p_lst + i,
						pts + 0,
						pts + pt - 1,
						lst + 0,
						lst + pt - 1,
						flags_swp);
			}
		}
		while (0);
	}
	else {

		for (i = 0; i < p_pt; ++i) {

			if (!(px->trts[i] & 1)) continue;

			for (j = 1; j < pt; ++j) {

				if (!(cx->trts[j] & 2))
					continue;

				glob_cd += phys_ccd_point_line(
						p_pts + i,
						p_lst + i,
						pts + j - 1,
						pts + j,
						lst + j - 1,
						lst + j,
						flags_swp);
			}
		}
	}
}

static void
phys_collise_all(concave_t *cx)
{
	concave_t	*curr = cx;
	int		cdsv, flags, mask;

	curr = curr->next;
	mask = CONCAVE_FLAG_PLAYER | CONCAVE_FLAG_TARGET;

	while (curr != all) {

		do {
			if (curr->flags & CONCAVE_PHANTOM)
				break;

			if ((curr->flags & CONCAVE_CONST) &&
					(cx->flags & CONCAVE_CONST))
				break;

			if ((curr->flags & CONCAVE_SOFT) &&
					(cx->flags & CONCAVE_SOFT))
				break;

			if ((curr->flags & CONCAVE_FLAG_BKGROUND) ^
					(cx->flags & CONCAVE_FLAG_BKGROUND))
				break;

			if (!phys_bbox_intersect(cx, curr))
				break;

			if ((!(cx->flags & CONCAVE_FLY))
					&& (curr->flags & CONCAVE_FLY)) {
				phys_collise_fly(cx, curr);
				continue;
			}

			flags = cx->flags | curr->flags;

			if (flags & CONCAVE_FLY) {
				if ((flags & mask) != mask)
					break;
			}

			cdsv = glob_cd;
			phys_collise(cx, curr);

			/* If collision detected */

			if (glob_cd > cdsv) {

				/* Recalculate boxes */

				if (!(cx->flags & CONCAVE_CONST))
					phys_bbox(cx);

				if (!(curr->flags & CONCAVE_CONST))
					phys_bbox(curr);

				/* Play-end condition detection */

				if ((flags & mask) == mask) {
					phys_play_cond = 1;
					cx->flags &= ~CONCAVE_TYPE_MASK;
					cx->flags |= CONCAVE_FLY;
					cx->ln = 0;
					curr->flags &= ~CONCAVE_TYPE_MASK;
					curr->flags |= CONCAVE_FLY;
					curr->ln = 0;
				}
			}
		}
		while (0);

		curr = curr->next;
	}
}

void phys_open()
{
	all = NULL;
	adt = 0.0f;

	glob_lns = malloc(sizeof(link_t) * GLOB_LINKS_MAX);
	glob_ln = 0;

	glob_pick = 0;
}

void phys_close()
{
	phys_free_all();
	free(glob_lns);
}

static void
phys_fail_check(int flags)
{
	concave_t	*curr;
	int		mask;

	mask = CONCAVE_FLAG_PLAYER | CONCAVE_FLAG_TARGET;

	if (!(flags & mask))
		return;

	curr = all;

	if (!curr) {
		phys_fail_cond = 1;
		return;
	}

	flags = 0;

	do {
		do {
			flags |= curr->flags;

			if ((flags & mask) == mask)
				return;
		}
		while (0);

		curr = curr->next;
	}
	while (curr != all);

	phys_fail_cond = 1;
}

static void
phys_update_all()
{
	concave_t	*next, *curr = all;
	int		it, flags;

	if (!curr) return;

	/* Verlet integration */

	do {
		do {
			if (curr->flags & CONCAVE_CONST)
				break;

			if (curr->flags & CONCAVE_PHANTOM)
				break;

			if (curr->flags & CONCAVE_FLY)
				phys_fly(curr);
			else
				phys_verlet(curr);
		}
		while (0);

		curr = curr->next;
	}
	while (curr != all);

	/* Links and joints */

	for (it = 0; it < PHYS_LINK_PASSES; ++it) {

		curr = all;

		do {
			do {
				if (curr->flags & CONCAVE_CONST)
					break;

				if (curr->flags & CONCAVE_PHANTOM)
					break;

				phys_links(curr);
			}
			while (0);

			curr = curr->next;
		}
		while (curr != all);
	}

	/* Joint for pick object */

	if (glob_pick) {
		*glob_pick_point = glob_pick_dst;
	}

	/* Calculate bounding boxes */

	curr = all;

	do {
		next = curr->next;

		do {
			if (curr->flags & CONCAVE_PHANTOM)
				break;

			phys_bbox(curr);

			/* If object fly away */

			if (phys_bbox_away(curr)) {

				if (curr->flags & CONCAVE_FLY) {
					/* Reverse it */

					phys_reverse(curr);
					phys_bbox(curr);
				}
				else {
					/* Remove it */

					if (curr == glob_rm_hold) {
						glob_rm_hold = curr->next;
					}

					flags = curr->flags;

					phys_remove_links(curr);
					phys_remove(curr);

					phys_fail_check(flags);

					if (!all) return;
				}
			}
		}
		while (0);

		curr = next;
	}
	while (curr != all);

	/* Collision detection */

	it = PHYS_CCD_PASSES_MAX;

	do {
		curr = all;
		glob_cd = 0;

		do {
			do {
				if (curr->flags & CONCAVE_PHANTOM)
					break;

				phys_collise_all(curr);
			}
			while (0);

			curr = curr->next;
		}
		while (curr != all);

		--it;
	}
	while (glob_cd && it);
}

void phys_update(float dt)
{
	adt += dt;
	time += dt;
	glob_paint_adt += dt;

	/* Boundary for prevent stalls */

	if (adt > (4.0f * PHYS_DELTA))
		adt = (4.0f * PHYS_DELTA);

	/* Simulate */

	while (adt > PHYS_DELTA) {
		phys_update_all();
		adt -= PHYS_DELTA;
	}

	/* Paint counter */

	while (glob_paint_adt > GLOB_PAINT_DELTA) {
		++glob_paint_lim;
		glob_paint_adt -= GLOB_PAINT_DELTA;
	}
}

void phys_draw_paint_all()
{
	glob_paint_adt = 0.0f;
	glob_paint_lim = 0;
}

void phys_draw_paint_flush()
{
	glob_paint_lim = 99999;
}

color_t phys_get_concave_color(int flags)
{
	color_t		col_def = { 25, 25, 25, 0 };
	color_t		col_const = { 155, 25, 25, 0 };
	color_t		col_soft = { 25, 25, 155, 0 };
	color_t		col_bend = { 25, 125, 25, 0 };
	color_t		col;

	if (flags & CONCAVE_CONST)
		col = col_const;
	else if (flags & CONCAVE_SOFT)
		col = col_soft;
	else if (flags & CONCAVE_BEND)
		col = col_bend;
	else if (flags & (CONCAVE_PHANTOM | CONCAVE_FLY)) {
		col.r = (flags & CONCAVE_PHANTOM_RED) ? 200 : 50 ;
		col.g = (flags & CONCAVE_PHANTOM_GREEN) ? 200 : 50 ;
		col.b = (flags & CONCAVE_PHANTOM_BLUE) ? 200 : 50 ;
		col.a = (flags & CONCAVE_PHANTOM) ? 1 : 4;
	}
	else
		col = col_def;

	if (flags & (CONCAVE_FLAG_PLAYER | CONCAVE_FLAG_TARGET)) {

		col.r = 0;
		col.g = 0;
		col.b = 0;
		col.a = 2;

		if (flags & CONCAVE_FLAG_PLAYER) {
			col.r += 200;
			col.g += 0;
			col.b += 0;
		}

		if (flags & CONCAVE_FLAG_TARGET) {
			col.r += 0;
			col.g += 200;
			col.b += 0;
		}
	}

	if (flags & CONCAVE_FLAG_BKGROUND) {
		col.a = 3;
	}

	return col;
}

void phys_draw_concave(concave_t *cx)
{
	color_t		col;
	point_t		*pts = cx->pts;
	int		i, pt = cx->pt;

	col = phys_get_concave_color(cx->flags);

	glob_paint_it += pt - 1;

	if (glob_paint_it > glob_paint_lim) {
		pt -= glob_paint_it - glob_paint_lim;
	}

	if (cx->flags & CONCAVE_FLAG_TEARABLE) {

		link_t	*lns = cx->lns;
		int	n = pt - 1;

		if (cx->flags & CONCAVE_FLAG_LOOP)
			n += 1;

		for (i = 0; i < n; ++i) {
			draw_line(lns[i].beg,
					lns[i].end, col);
		}
	}
	else {
		for (i = 1; i < pt; ++i) {
			draw_line(pts + i - 1,
					pts + i, col);
		}
	}
}

void phys_draw()
{
	concave_t	*curr = all;

	if (!curr)
		return;

	glob_paint_it = 0;

	do {
		phys_draw_concave(curr);
		curr = curr->prev;
	}
	while (curr != all);
}

static point_t *
phys_dist_point(concave_t *cx, float px, float py)
{
	point_t		*pts = cx->pts;
	int		pt = cx->pt;
	float		x, y;
	float		dist;
	int		i;

	for (i = 0; i < pt; ++i) {
		x = pts[i].x - px;
		y = pts[i].y - py;
		dist = sqrtf(x * x + y * y);

		if (dist < GLOB_PICK_AREA)
			return pts + i;
	}

	return NULL;
}

void phys_pick(float px, float py)
{
	concave_t	*curr = all;
	point_t		*ret;

	if (!curr)
		return;

	do {
		do {
			if (curr->flags & CONCAVE_CONST)
				break;

			ret = phys_dist_point(curr, px, py);

			if (ret) {
				glob_pick_obj = curr;
				glob_pick_point = ret;
				glob_pick = 1;
			}
		}
		while (0);

		curr = curr->next;
	}
	while (curr != all);
}

void phys_pick_move(float px, float py)
{
	glob_pick_dst.x = px;
	glob_pick_dst.y = py;
}

void phys_pick_detach()
{
	glob_pick = 0;
}

