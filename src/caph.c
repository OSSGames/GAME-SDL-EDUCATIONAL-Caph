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

#include <SDL/SDL.h>
#undef	main

#include "caph.h"
#include "draw.h"
#include "phys.h"
#include "maps.h"

#if defined(_WIN32)
# include <windows.h>
#else
# include <unistd.h>
# include <sys/types.h>
#endif

#define SYS_PRINT	1
#define SYS_DATA_DIR	"../share/caph/"

#define DATA_CONFIG		"caph.conf"
#define HOME_CONFIG		".caph.conf"
#define DATA_PAPER		"paper.png"
#define DATA_BRUSH		"brush.png"
#define DATA_MAPS_LIST		"maps/maps.list"

static const char *
sys_get_data_dir()
{
	static char dir[256];

#if defined(_WIN32)
	strcpy(dir, SYS_DATA_DIR);
	return dir;
#else
	int ret = readlink("/proc/self/exe", dir, sizeof(dir));
	if (ret < 0) {
		fprintf(stderr, "%s:%i [ERROR] readlink failed: %s \n",
					__FILE__, __LINE__, strerror(ret));
		strcpy(dir, SYS_DATA_DIR);
		return dir;
	}

	char	*p = dir;

	while (*p != '\0') ++p;
	while (*p != '/') --p;
	*++p = '\0';

	strcat(dir, SYS_DATA_DIR);
	return dir;
#endif
}

static void
sys_chdir(const char *name)
{
#if defined(_WIN32)
	SetCurrentDirectory(name);
#else
	int	ret;

	ret = chdir(name);
#endif
}

static const char *
sys_get_config()
{
	static char dir[256];
	const char *home;

#if defined(_WIN32)
	strcpy(dir, DATA_CONFIG);
	return dir;
#else
	home = getenv("HOME");

	if (home)
		strcpy(dir, home);
	else
		return "";

	char	*p = dir;

	while (*p != '\0') ++p;

	if (*(p - 1) != '/') {
		*p++ = '/';
		*p++ = '\0';
	}

	strcat(dir, HOME_CONFIG);
	return dir;
#endif
}

int		screen_w;
int		screen_h;
uint8_t		*screen_pixels;
SDL_Surface 	*screen;

#define	STRIP_SIZE		(512)

static point_t  *p_strip;
static int	p_it;
static int	p_state;
static int	p_flags;
static float	p_dist_lim1;
static float	p_dist_lim2;

static int	time;
static int	mode;

static int	play_cond_time;
static int	play_fail_time;

static void
phys_paint_click(float x, float y, int state)
{
	concave_t	*cx;

	x *= (float) map_w / (float) screen_w;
	y *= (float) map_h / (float) screen_h;

	if (p_state && !state) {

		if (p_it > 2) {
			cx = phys_alloc(p_it + 1);
			cx->flags = p_flags;
			memcpy(cx->pts, p_strip, sizeof(point_t) * cx->pt);
			phys_insert(cx);
		}

		p_it = 0;
	}

	if (!p_state && state) {
		p_strip[p_it].x = x;
		p_strip[p_it].y = y;
	}

	p_state = state;
}

static void
phys_paint_move(float x, float y)
{
	float		dist;
	float		dx, dy;

	x *= (float) map_w / (float) screen_w;
	y *= (float) map_h / (float) screen_h;

	if (!p_state)
		return;

	dx = x - p_strip[p_it].x;
	dy = y - p_strip[p_it].y;

	dist = sqrt(dx * dx + dy * dy);

	if (dist < p_dist_lim1)
		return;

	if (p_it > 1 && dist < p_dist_lim2) {

		float		pdx, pdy;

		pdx = p_strip[p_it].x - p_strip[p_it - 1].x;
		pdy = p_strip[p_it].y - p_strip[p_it - 1].y;

		dist = sqrt(fabsf(pdx * dy - pdy * dx));

		if (dist < p_dist_lim1)
			return;
	}

	if (p_it < (STRIP_SIZE - 1)) {
		x += cos((float) p_it * 0.3f) * 0.1f;
		x += sin((float) p_it * 0.3f) * 0.1f;
		p_strip[++p_it].x = x;
		p_strip[p_it].y = y;
	}
}

static void
phys_paint_draw()
{
	concave_t	cx;

	if (p_it > 1) {

		cx.pts = p_strip;
		cx.pt = p_it + 1;
		cx.flags = p_flags;

		phys_draw_concave(&cx);
	}
}

static void
phys_flags_draw()
{
#if SYS_PRINT
	if (!mode) return;
	printf("type: %s%s%s%s%s  \t color: %s%s%s \t flags: %s%s%s%s%s \t mode: %s%s \n",
			(p_flags & CONCAVE_CONST) ? "const" : "",
			(p_flags & CONCAVE_SOFT) ? "soft" : "",
			(p_flags & CONCAVE_BEND) ? "bend" : "",
			(p_flags & CONCAVE_PHANTOM) ? "phantom" : "",
			(p_flags & CONCAVE_FLY) ? "fly" : "",
			(p_flags & CONCAVE_PHANTOM_RED) ? "r" : "_",
			(p_flags & CONCAVE_PHANTOM_GREEN) ? "g" : "_",
			(p_flags & CONCAVE_PHANTOM_BLUE) ? "b" : "_",
			(p_flags & CONCAVE_FLAG_PLAYER) ? "P" : "_",
			(p_flags & CONCAVE_FLAG_TARGET) ? "T" : "_",
			(p_flags & CONCAVE_FLAG_LOOP) ? "L" : "_",
			(p_flags & CONCAVE_FLAG_BKGROUND) ? "G" : "_",
			(p_flags & CONCAVE_FLAG_NOCROSS) ? "H" : "_",
			(time) ? "T" : "_",
			(mode) ? "E" : "P");
#endif
}

int main(int argc, char *argv[])
{
	SDL_Event event;
	point_t mpos;
	point_t mpos_l;
	int rpen = 1;
	int fade_g, fail_g, time_g;
	float fade_a, fail_a, time_a;
	int fs = 0;
	int run = 1;
	int ret;
	FILE *conf;
	const char *conf_name;

	sys_chdir(sys_get_data_dir());
	conf_name = sys_get_config();

	conf = fopen(conf_name, "r");

	if (conf == NULL) {
		fprintf(stderr, "%s:%i [ERROR] fopen(\"%s\") failed: %s \n",
				__FILE__, __LINE__, conf_name, strerror(errno));

		conf = fopen(DATA_CONFIG, "r");

		if (conf == NULL) {
			fprintf(stderr, "%s:%i [ERROR] fopen(\"%s\") failed: %s \n",
					__FILE__, __LINE__, DATA_CONFIG, strerror(errno));
		}
	}

	if (conf) {
		ret = fscanf(conf, "%i %i %i",
				&screen_w,
				&screen_h,
				&fs);

		fclose(conf);
	}
	else {
		screen_w = 1024;
		screen_h = 768;
	}

	if (screen_w < 320 || screen_w > (1024*4) ||
			screen_h < 240 || screen_h > (1024*4))
	{
		fprintf(stderr, "%s:%i [PANIC] Ivalid config values\n",
				__FILE__, __LINE__);
	}

	p_dist_lim1 = 8.0f;
	p_dist_lim2 = 36.0f;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0 ) {
		fprintf(stderr, "%s:%i [PANIC] SDL_Init failed: %s \n",
				__FILE__, __LINE__, SDL_GetError());
		exit(EXIT_FAILURE);
	}

	int sdl_flags = (fs ? SDL_FULLSCREEN : 0) |
			SDL_DOUBLEBUF |
			SDL_HWSURFACE;

#ifdef _OPENGL
	SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	sdl_flags |= SDL_OPENGL;
#endif

	screen = SDL_SetVideoMode(screen_w, screen_h, 32, sdl_flags);

	if (screen == NULL) {
		fprintf(stderr, "%s:%i [PANIC] SDL_SetVideoMode failed: %s \n",
				__FILE__, __LINE__, SDL_GetError());
		SDL_Quit();
		exit(EXIT_FAILURE);
	}

	SDL_WM_SetCaption("Caph", "Caph");
	SDL_ShowCursor(0);

	screen_pixels = screen->pixels;

	draw_init();
	draw_load_bg(DATA_PAPER);
	draw_load_bs(DATA_BRUSH);

	p_strip = malloc(sizeof(point_t) * STRIP_SIZE);
	p_it = 0;
	p_state = 0;
	p_flags = 0;

	phys_open();

	maps_list_load(DATA_MAPS_LIST);
	maps_load_this();

	time = 1;
	mode = 0;

	play_cond_time = 0;
	play_fail_time = 0;

	fade_g = 0;
	fail_g = 0;
	time_g = 0;

	fade_a = 0.0f;
	fail_a = 0.0f;
	time_a = 0.0f;

        int dt, t = 0;
        int fr_n = 0;
        int fr_tval = 0;
        int fps = 0;
        int dt_min = (1000/60);
	uint32_t t0, t1;

	t0 = t1 = SDL_GetTicks();

	srand(t0);

	do {
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT:
					run = 0;
					break;

				case SDL_KEYDOWN:
					switch ((int) event.key.keysym.sym) {
						case SDLK_q:
							run = 0;
							break;

						case SDLK_a:
							p_flags &= ~CONCAVE_TYPE_MASK;
							break;

						case SDLK_c:
							if (!mode) break;
							p_flags &= ~CONCAVE_TYPE_MASK;
							p_flags |= CONCAVE_CONST;
							break;

						case SDLK_s:
							p_flags &= ~CONCAVE_TYPE_MASK;
							p_flags |= CONCAVE_SOFT;
							break;

						case SDLK_b:
							p_flags &= ~CONCAVE_TYPE_MASK;
							p_flags |= CONCAVE_BEND;
							break;

						case SDLK_x:
							if (!mode) break;
							p_flags &= ~CONCAVE_TYPE_MASK;
							p_flags |= CONCAVE_PHANTOM;
							break;

						case SDLK_f:
							if (!mode) break;
							p_flags &= ~CONCAVE_TYPE_MASK;
							p_flags |= CONCAVE_FLY;
							break;

						case SDLK_1:
							if (!mode) break;
							p_flags ^= CONCAVE_PHANTOM_RED;
							break;

						case SDLK_2:
							if (!mode) break;
							p_flags ^= CONCAVE_PHANTOM_GREEN;
							break;

						case SDLK_3:
							if (!mode) break;
							p_flags ^= CONCAVE_PHANTOM_BLUE;
							break;

						case SDLK_4:
							if (!mode) break;
							p_flags ^= CONCAVE_FLAG_PLAYER;
							break;

						case SDLK_5:
							if (!mode) break;
							p_flags ^= CONCAVE_FLAG_TARGET;
							break;

						case SDLK_d:
						case SDLK_ESCAPE:
							phys_remove_back();
							break;

						case SDLK_t:
							time ^= 1;
							break;

						case SDLK_l:
							if (!mode) break;
							p_flags ^= CONCAVE_FLAG_LOOP;
							break;

						case SDLK_p:
							mode ^= 1;
							if (!mode) break;
							phys_remove_unhold();
							break;

						case SDLK_r:
							p_flags = 0;
							mode = 0;
							play_cond_time = 0;
							play_fail_time = 0;
							maps_load_this();
							break;

						case SDLK_n:
							p_flags = 0;
							mode = 0;
							play_cond_time = 0;
							play_fail_time = 0;
							maps_load_next();
							break;

						case SDLK_m:
							p_flags = 0;
							mode = 0;
							play_cond_time = 0;
							play_fail_time = 0;
							maps_load_prev();
							break;

						case SDLK_k:
							if (!mode) break;
							maps_save();
							break;

						case SDLK_e:
							if (!mode) break;
							phys_free_all();
							map_w = screen_w;
							map_h = screen_h;
							break;

						case SDLK_g:
							if (!mode) break;
							p_flags &= ~(CONCAVE_PHANTOM | CONCAVE_FLY);
							p_flags ^= CONCAVE_FLAG_BKGROUND;
							break;

						case SDLK_h:
							if (!mode) break;
							p_flags ^= CONCAVE_FLAG_NOCROSS;
							break;

						case SDLK_u:
							rpen ^= 1;
							break;
					}
					phys_flags_draw();
					break;

				case SDL_MOUSEBUTTONDOWN:
				case SDL_MOUSEBUTTONUP:
					if (event.button.button == SDL_BUTTON_LEFT) {
						phys_paint_click((float) event.button.x,
								(float) event.button.y,
								event.type == SDL_MOUSEBUTTONDOWN);
					}
					if (event.button.button == SDL_BUTTON_RIGHT) {
						if (mode) {
							if (event.type == SDL_MOUSEBUTTONDOWN) {
								phys_pick((float) event.button.x,
										(float) event.button.y);
							}
							else
								phys_pick_detach();
						}
						else {
							if (event.type != SDL_MOUSEBUTTONDOWN) break;

							if (p_flags & CONCAVE_SOFT) {
								p_flags &= ~CONCAVE_TYPE_MASK;
								p_flags |= CONCAVE_BEND;
							}
							else if (p_flags & CONCAVE_BEND) {
								p_flags &= ~CONCAVE_TYPE_MASK;
							}
							else {
								p_flags |= CONCAVE_SOFT;
							}
						}
					}
					phys_draw_paint_flush();
					break;

				case SDL_MOUSEMOTION:
					mpos.x = (float) event.motion.x
						* (float) map_w
						/ (float) screen_w - 0.0f;
					mpos.y = (float) event.motion.y
						* (float) map_h
						/ (float) screen_h - 32.0f;
					phys_paint_move((float) event.motion.x,
							(float) event.motion.y);
					if (!mode) break;
					phys_pick_move((float) event.button.x,
							(float) event.button.y);
					break;
			}
		}

		t0 = SDL_GetTicks();
		dt = t0 - t1;

		while (dt < dt_min) {
			SDL_Delay(1);
			t0 = SDL_GetTicks();
			dt = t0 - t1;
		}

		t1 = t0;

		++fr_n;
		t += dt;
		fr_tval += dt;

		if (fr_tval > 1000) {
			fps = (fr_n * 1000) / fr_tval;
#if SYS_PRINT
			if (mode) {
				printf("fps: %i \n", fps);
				fflush(stdout);
			}
#endif
			fr_n = 0;
			fr_tval = 0;
		}

		if (time) {
			phys_update((float) dt * 0.001f);

			if (phys_play_cond) {
				play_cond_time += dt;

				if (play_cond_time > (2 * 1000)) {
					p_flags = 0;
					mode = 0;
					play_cond_time = 0;
					play_fail_time = 0;
					maps_load_next();
				}
			}

			if (phys_fail_cond) {
				play_fail_time += dt;

				if (play_fail_time > (2 * 1000)) {
					p_flags = 0;
					mode = 0;
					play_cond_time = 0;
					play_fail_time = 0;
					maps_load_this();
				}
			}
		}

		if (rpen) {
			mpos_l.x = mpos_l.x + (mpos.x - mpos_l.x) * 0.05f;
			mpos_l.y = mpos_l.y + (mpos.y - mpos_l.y) * 0.05f;
		}
		else {
			mpos_l.x = mpos.x;
			mpos_l.y = mpos.y;
		}

		if (SDL_LockSurface(screen) == 0) {

			draw_clear();
			draw_scale((float) screen_w / (float) map_w,
					(float) screen_h / (float) map_h);

			phys_draw();
			phys_paint_draw();

			draw_brush(&mpos, &mpos_l, phys_get_concave_color(p_flags));

			if (phys_play_cond) {
				if (fade_g) {
					fade_a = fade_a + 0.7f * (float) dt * 0.001f;
					fade_a = (fade_a > 1.0f) ? 1.0f : fade_a;
					draw_fade(fade_a);
				}
				else {
					fade_g = 1;
					fade_a = 0.0f;
				}
			}
			else {
				if (fade_g) fade_g = 0;
			}

			if (phys_fail_cond) {
				if (fail_g) {
					fail_a = fail_a + 0.7f * (float) dt * 0.001f;
					fail_a = (fail_a > 1.0f) ? 1.0f : fail_a;
					draw_fail(fail_a);
				}
				else {
					fail_g = 1;
					fail_a = 0.0f;
				}
			}
			else {
				if (fail_g) fail_g = 0;
			}

			if (!time) {
				if (time_g) {
					time_a = time_a + 2.0f * (float) dt * 0.001f;
					time_a = (time_a > 0.5f) ? 0.5f : time_a;
					draw_time(time_a);
				}
				else {
					time_g = 1;
					time_a = 0.0f;
				}
			}
			else {
				if (time_g) {
					time_a = time_a - 2.0f * (float) dt * 0.001f;
					if (time_a < 0.0f) time_g = 0;
					draw_time(time_a);
				}
			}

			SDL_UnlockSurface(screen);
#ifdef _OPENGL
			SDL_GL_SwapBuffers();
#else
			SDL_Flip(screen);
#endif
		}

	} while (run);

	maps_list_free();
	phys_close();

	SDL_Quit();
}

