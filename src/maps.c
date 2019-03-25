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
#include <errno.h>
#include <string.h>
#include <math.h>

#include "maps.h"
#include "caph.h"
#include "phys.h"

#define MAPS_MAX		1024
#define MAPS_NAME_LEN_MAX	32
#define MAPS_NAME_SAVE		"maps/save.map"

int			map_w;
int			map_h;

static char		**maps;
static int		this;

void maps_list_load(const char *list)
{
	char line[80], *c;

	FILE *ls = fopen(list, "r");

	if (ls == NULL) {
		fprintf(stderr, "%s:%i [PANIC] fopen(\"%s\") failed: %s \n",
				__FILE__, __LINE__, list, strerror(errno));
		exit(EXIT_FAILURE);
	}

	maps = malloc(sizeof(char *) * MAPS_MAX);
	this = 0;

	while (fgets(line, 79, ls)) {

		c = line;
		while (*c != '\n' && *c != ' ') ++c;
		*c = '\0';

		maps[this] = malloc(strlen(line) + 8);
		strcpy(maps[this], "maps/");
		strcat(maps[this++], line);
	}

	fclose(ls);

	maps[this] = NULL;
	this = 0;

	map_w = 1024;
	map_h = 768;
}

void maps_list_free()
{
	this = 0;

	while (maps[this]) free(maps[this++]);

	free(maps);
}

static concave_t *
get_object_by_number(int n)
{
	concave_t	*curr;
	int		i;

	curr = phys_get_all();

	if (!curr) return NULL;

	curr = curr->next;

	for (i = 0; i < n; ++i)
		curr = curr->next;

	return curr;
}

static void
search_for_point(point_t *p, int *obj, int *ptn)
{
	concave_t	*curr, *end;
	point_t		*s, *e;
	int		n;

	curr = phys_get_all();

	if (!curr) {
		*obj = -1;
		*ptn = -1;
		return;
	}

	curr = end = curr->next;
	n = 0;

	do {
		s = curr->pts;
		e = s + curr->pt;

		if (p < e && p >= s) {

			*obj = n;
			*ptn = p - s;

			return;
		}

		curr = curr->next;
		++n;
	}
	while (curr != end);

	*obj = -1;
	*ptn = -1;
}

static void
load_map(const char *map)
{
	concave_t		*curr, *end;
	concave_t		*cx;
	int			c;
	char			id[80];
	int 			flags;
	int			i, n;
	int			obj, ptn;
	int			ret;

	FILE *fd = fopen(map, "r");

	if (fd == NULL) {
		fprintf(stderr, "%s:%i [ERROR] fopen(\"%s\") failed: %s \n",
				__FILE__, __LINE__, map, strerror(errno));
		return;
	}

	phys_free_all();

	do {
		c = fgetc(fd);

		if (c == EOF) {
			fprintf(stderr, "%s:%i [PANIC] unexpected EOF: %s \n",
				__FILE__, __LINE__, map);
			exit(EXIT_FAILURE);
		}

		if (c == '\n') break;
	}
	while (1);

	ret = fscanf(fd, "%s", id);

	if (strcmp(id, "display")) {
		fprintf(stderr, "%s:%i [PANIC] Invalid map file: %s \n",
				__FILE__, __LINE__, map);
		exit(EXIT_FAILURE);
	}

	ret = fscanf(fd, "%i %i", &map_w, &map_h);

	if (map_w < 320 || map_h < 240) {
		fprintf(stderr, "%s:%i [PANIC] Invalid map file: %s \n",
				__FILE__, __LINE__, map);
		exit(EXIT_FAILURE);
	}

	do {
		ret = fscanf(fd, "%s", id);

		if (strcmp(id, "object")) break;

		ret = fscanf(fd, "%x", &flags);
		ret = fscanf(fd, "%i", &n);

		curr = phys_alloc(n);
		curr->flags = flags;

		for (i = 0; i < n; ++i) {
			ret = fscanf(fd, "%f %f",
					&curr->pts[i].x,
					&curr->pts[i].y);
		}

		for (i = 0; i < n; ++i) {
			ret = fscanf(fd, "%f %f",
					&curr->last[i].x,
					&curr->last[i].y);
		}

		phys_attach(curr);
	}
	while (1);

	if (strcmp(id, "links")) {
		fprintf(stderr, "%s:%i [PANIC] Invalid map file: %s \n",
				__FILE__, __LINE__, map);
		exit(EXIT_FAILURE);
	}

	curr = end = phys_get_all();

	do {
		ret = fscanf(fd, "%i", &n);

		curr->ln = n;

		if (n > 0) {

			curr->lns = malloc(sizeof(link_t) * n);

			for (i = 0; i < n; ++i) {
				ret = fscanf(fd, "%i %i", &obj, &ptn);
				cx = get_object_by_number(obj);
				curr->lns[i].beg = cx->pts + ptn;
				ret = fscanf(fd, "%i %i", &obj, &ptn);
				cx = get_object_by_number(obj);
				curr->lns[i].end = cx->pts + ptn;
				ret = fscanf(fd, "%f %f",
						&curr->lns[i].dist,
						&curr->lns[i].wght);
			}
		}

		curr = curr->prev;

		if (curr == end) break;

		ret = fscanf(fd, "%s", id);

		if (strcmp(id, "links")) {
			fprintf(stderr, "%s:%i [PANIC] Invalid map file: %s \n",
					__FILE__, __LINE__, map);
			exit(EXIT_FAILURE);
		}
	}
	while (1);

	fclose(fd);

	phys_pick_detach();
	phys_remove_hold();
	phys_play_cond = 0;
	phys_fail_cond = 0;

	phys_draw_paint_all();

	printf("Loaded from \"%s\"\n", map);
}

void maps_load_this()
{
	load_map(maps[this]);
}

void maps_load_next()
{
	if (maps[this + 1] != NULL) {
		++this;
		maps_load_this();
	}
}

void maps_load_prev()
{
	if (this > 0) {
		--this;
		maps_load_this();
	}
}

static void
maps_save_object(FILE *fd, concave_t *cx)
{
	int		i;

	fprintf(fd, "object ");
	fprintf(fd, "%08x ", cx->flags);
	fprintf(fd, "%i ", cx->pt);

	for (i = 0; i < cx->pt; ++i) {
		fprintf(fd, "%4.8f %4.8f ",
				cx->pts[i].x,
				cx->pts[i].y);
	}

	for (i = 0; i < cx->pt; ++i) {
		fprintf(fd, "%4.8f %4.8f ",
				cx->last[i].x,
				cx->last[i].y);
	}

	fprintf(fd, "\n");
}

static void
maps_save_links(FILE *fd, concave_t *cx)
{
	int		i;
	int		obj, ptn;

	fprintf(fd, "links ");
	fprintf(fd, "%i ", cx->ln);

	for (i = 0; i < cx->ln; ++i) {
		search_for_point(cx->lns[i].beg, &obj, &ptn);
		fprintf(fd, "%i %i ", obj, ptn);
		search_for_point(cx->lns[i].end, &obj, &ptn);
		fprintf(fd, "%i %i ", obj, ptn);
		fprintf(fd, "%4.8f %1.2f ",
				cx->lns[i].dist,
				cx->lns[i].wght);
	}

	fprintf(fd, "\n");
}

void maps_save()
{
	concave_t		*curr, *all;

	curr = all = phys_get_all();

	if (!curr) return;

	FILE *fd = fopen(MAPS_NAME_SAVE, "w");

	if (fd == NULL) {
		fprintf(stderr, "%s:%i [PANIC] fopen(\"%s\") failed: %s \n",
				__FILE__, __LINE__, MAPS_NAME_SAVE, strerror(errno));
		exit(EXIT_FAILURE);
	}

	fprintf(fd, "# caph map \n");
	fprintf(fd, "display %i %i \n",
			map_w,
			map_h);

	do {
		maps_save_object(fd, curr);
		curr = curr->prev;
	}
	while (curr != all);

	curr = all;

	do {
		maps_save_links(fd, curr);
		curr = curr->prev;
	}
	while (curr != all);

	fprintf(fd, "end \n");

	fclose(fd);

	printf("Saved to \"%s\"\n", MAPS_NAME_SAVE);
}

