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

#include <png.h>

#ifdef _OPENGL
# include <GL/gl.h>
# include <GL/glext.h>
#endif

#include "draw.h"
#include "caph.h"
#include "maps.h"

#define	FB_ADDR(x,y)		(((color_t *)screen_pixels) + screen_w * (y) + (x))
#define FB_DRAW(x,y,col)	(*FB_ADDR((x),(y)) = (col))

#ifndef _OPENGL

static float scale_x;
static float scale_y;

static int
clip_code(point_t *s)
{
	int		code = 0;
	float		lim_w = (float) (screen_w - 2);
	float		lim_h = (float) (screen_h - 2);

	if (s->x < 0.0f)
		code |= 0x01;
	else if (s->x > lim_w)
		code |= 0x02;
	if (s->y < 0.0f)
		code |= 0x04;
	else if (s->y > lim_h)
		code |= 0x08;

	return code;
}

static int
clip_line(point_t *s, point_t *e)
{
	int		s_cc = clip_code(s);
	int		e_cc = clip_code(e);
	float		lim_w = (float) (screen_w - 2);
	float		lim_h = (float) (screen_h - 2);
	float		dx, dy;

	do {
		if (s_cc & e_cc)
			return -1;

		if (!(s_cc | e_cc))
			return 0;

		if (!s_cc) {

			int tmp = s_cc;
			s_cc = e_cc;
			e_cc = tmp;

			point_t *ptmp = s;
			s = e;
			e = ptmp;
		}

		dx = e->x - s->x;
		dy = e->y - s->y;

		if ((fabsf(dx) < 1.0f) && (s_cc & 0x03))
			return -1;

		if ((fabsf(dy) < 1.0f) && (s_cc & 0x0c))
			return -1;

		if (s_cc & 0x01) {
			s->y = e->y + ((0.0f - e->x) * dy) / dx;
			s->x = 0.0f;
			goto __cl_recalc;
		}

		if (s_cc & 0x02) {
			s->y = s->y + ((lim_w - s->x) * dy) / dx;
			s->x = lim_w;
			goto __cl_recalc;
		}

		if (s_cc & 0x04) {
			s->x = e->x + ((0.0f - e->y) * dx) / dy;
			s->y = 0.0f;
			goto __cl_recalc;
		}

		if (s_cc & 0x08) {
			s->x = s->x + ((lim_h - s->y) * dx) / dy;
			s->y = lim_h;
			goto __cl_recalc;
		}

__cl_recalc:
		s_cc = clip_code(s);
	}
	while (1);
}

static void
draw_int_line(int xs, int ys, int xe, int ye, color_t col)
{
	int	dx, dy;
	int	vx, vy;
	int	i = 0;

	if (xs < xe) {
		dx = xe - xs;
		vx = 1;
	}
	else {
		dx = xs - xe;
		vx = -1;
	}

	if (ys < ye) {
		dy = ye - ys;
		vy = 1;
	}
	else {
		dy = ys - ye;
		vy = -1;
	}

	if (dx < dy) {
		while (ys != ye) {
			FB_DRAW(xs, ys, col);

			ys += vy;
			i += dx;

			if (i >= dy) {
				i -= dy;
				xs += vx;
			}
		}
	}
	else if (dx > dy) {
		while (xs != xe) {
			FB_DRAW(xs, ys, col);

			xs += vx;
			i += dy;

			if (i >= dx) {
				i -= dx;
				ys += vy;
			}
		}
	}
	else {
		while (ys != ye) {
			FB_DRAW(xs, ys, col);

			xs += vx;
			ys += vy;
		}
	}

	FB_DRAW(xe, ye, col);
}
#endif /* NOT _OPENGL */

void draw_init()
{
#ifdef _OPENGL
	float w = (float) screen_w;
	float h = (float) screen_h;

	float mx[16] = {
		2.0f / w, 0.0f, 0.0f, 0.0f,
		0.0f, - 2.0f / h, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		- 1.0f, 1.0f, - 1.0f, 1.0f };

	glViewport(0, 0, screen_w, screen_h);

	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(mx);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glEnable(GL_COLOR_MATERIAL);
	glEnable(GL_TEXTURE_2D);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
#endif
}

void draw_scale(float xs, float ys)
{
#ifdef _OPENGL
	glLoadIdentity();
	glScalef(xs, ys, 1.0f);
#else
	scale_x = xs;
	scale_y = ys;
#endif
}

typedef struct raw_img raw_img_t;

struct raw_img {
	int width;		/* image width */
	int height;		/* image height */
	void *raw;		/* raw color data */
	int alpha;		/* RGBA or RGB */
};

static raw_img_t bgimg;
static raw_img_t bsimg;

#ifdef _OPENGL
static GLuint bgimg_id;
static GLuint bsimg_id;
#else
static SDL_Surface *sbg;
static SDL_Surface *sbs;
#endif

int static
img_load_png(const char *png, raw_img_t *raw)
{
	unsigned char	sig[8];
	int 		ret;

	if (raw == NULL)
		return -1;

	FILE *fp = fopen(png, "rb");

	if (fp == NULL) {
		fprintf(stderr, "%s:%i [ERROR] fopen(\"%s\") failed: %s \n",
						__FILE__, __LINE__,
						png, strerror(errno));
		return -1;
	}

	ret = fread(sig, 8, 1, fp);

	if (png_sig_cmp(sig, 0, 8))
	{
		fprintf(stderr, "%s:%i [ERROR] \"%s\" is not png file \n",
						__FILE__, __LINE__, png);
		return -1;
	}

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
							NULL, NULL, NULL);
	if (png_ptr == NULL)
	{
		fprintf(stderr, "%s:%i [ERROR] libpng failed \n",
						 __FILE__, __LINE__);
		return -1;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		fprintf(stderr, "%s:%i [ERROR] libpng failed \n",
						 __FILE__, __LINE__);
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return -1;
	}

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);
	png_read_info(png_ptr, info_ptr);

	png_uint_32 w, h;
	int bits, ltype;
	png_get_IHDR(png_ptr, info_ptr, &w, &h, &bits, &ltype, 0, 0, 0);

	if (bits > 8)
		png_set_strip_16(png_ptr);

	if (ltype == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png_ptr);

	if ((ltype == PNG_COLOR_TYPE_GRAY) ||
		(ltype == PNG_COLOR_TYPE_GRAY_ALPHA))
	{
		if (bits < 8)
			png_set_expand_gray_1_2_4_to_8(png_ptr);
		png_set_gray_to_rgb(png_ptr);
	}

	png_read_update_info(png_ptr, info_ptr);
	png_get_IHDR(png_ptr, info_ptr, &w, &h, &bits, &ltype, 0, 0, 0);

	if (((ltype != PNG_COLOR_TYPE_RGB) &&
		(ltype != PNG_COLOR_TYPE_RGB_ALPHA)) ||
		(bits > 8))
	{
		fprintf(stderr, "%s:%i [ERROR] FIXME: Invalid png reading \n",
						__FILE__, __LINE__);
		return -1;
	}

	int alpha = (ltype == PNG_COLOR_TYPE_RGB_ALPHA);
	int bytes = alpha ? 4 : 3 ;

	png_byte *data = malloc(w * h * bytes);
	png_byte **row_pointers = malloc(h * sizeof(png_byte *));

	int i ;
	for (i = 0; i < h; i++)
		row_pointers[i] = data + i * w * bytes;

	png_read_image(png_ptr, row_pointers);

	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	free(row_pointers);
	fclose(fp);

	raw->width = w;
	raw->height = h;
	raw->raw = data;
	raw->alpha = alpha;

	return 0;
}

void draw_load_bg(const char *img)
{
	if (img_load_png(img, &bgimg) < 0) {
		fprintf(stderr, "%s:%i [ERROR] Background image is not loaded\n",
				__FILE__, __LINE__);
		return ;
	}

#ifdef _OPENGL
	GLuint id ;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	if (bgimg.alpha)
		glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA,
				bgimg.width, bgimg.height,
				0, GL_RGBA, GL_UNSIGNED_BYTE, bgimg.raw);
	else
		glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB,
				bgimg.width, bgimg.height,
				0, GL_RGB, GL_UNSIGNED_BYTE, bgimg.raw);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);

	free(bgimg.raw);
	bgimg_id = id;
#else
	SDL_Surface *temp;
	temp = SDL_CreateRGBSurfaceFrom(bgimg.raw, bgimg.width, bgimg.height,
			24, bgimg.width * 3, 0, 0, 0, 0);
	sbg = SDL_CreateRGBSurface(0, screen_w, screen_h, 24, 0, 0, 0, 0);
	SDL_SoftStretch(temp, NULL, sbg, NULL);
	SDL_FreeSurface(temp);
	free(bgimg.raw);
#endif
}

void draw_load_bs(const char *img)
{
	if (img_load_png(img, &bsimg) < 0) {
		fprintf(stderr, "%s:%i [ERROR] Brush image is not loaded\n",
				__FILE__, __LINE__);
		return ;
	}

#ifdef _OPENGL
	GLuint id ;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	if (bsimg.alpha)
		glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA,
				bsimg.width, bsimg.height,
				0, GL_RGBA, GL_UNSIGNED_BYTE, bsimg.raw);
	else
		glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB,
				bsimg.width, bsimg.height,
				0, GL_RGB, GL_UNSIGNED_BYTE, bsimg.raw);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	free(bsimg.raw);
	bsimg_id = id;
#else
	int		i;
	uint8_t 	*col = bsimg.raw;

	for (i = 0; i < (bsimg.width * bsimg.height); ++i, col += 4)
		if (col[3] < 16) *(uint32_t*) col = 0;

	sbs = SDL_CreateRGBSurfaceFrom(bsimg.raw, bsimg.width, bsimg.height,
			32, bsimg.width * 4, 0, 0, 0, 0);
	SDL_SetColorKey(sbs, SDL_SRCCOLORKEY, 0);
#endif
}

#ifdef _OPENGL
static void
draw_setup_color(color_t col, int brush)
{
	if (brush && col.a == 0) {
		col.r = (col.r < 155) ? col.r + 100 : 255;
		col.g = (col.g < 155) ? col.g + 100 : 255;
		col.b = (col.b < 155) ? col.b + 100 : 255;
	}

	if (col.a == 1) {
		glColor4ub(col.r, col.g, col.b, 100);
		glLineWidth(4.0f);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else if (col.a == 2) {
		if (col.r < 150 || col.g < 150)
			glColor4ub(col.r, col.g, col.b, 255);
		else
			glColor4ub(255, 255, 0, 255);
		glLineWidth(3.0f);
		glDisable(GL_BLEND);
	}
	else if (col.a == 3) {
		glColor4ub(col.r, col.g, col.b, 100);
		glLineWidth(2.0f);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else if (col.a == 4) {
		if (col.r < 150 || col.g < 150 || col.b < 150)
			glColor4ub(col.r, col.g, col.b, 60);
		else
			glColor4ub(255, 255, 255, 200);
		glLineWidth(4.0f);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else {
		glColor4ub(col.r, col.g, col.b, 255);
		glLineWidth(2.0f);
		glDisable(GL_BLEND);
	}
}
#else
static color_t
draw_setup_color(color_t col, int brush)
{
	if (brush && col.a == 0) {
		col.r = (col.r < 155) ? col.r + 100 : 255;
		col.g = (col.g < 155) ? col.g + 100 : 255;
		col.b = (col.b < 155) ? col.b + 100 : 255;
	}

	if (col.a == 2) {
		if (col.r < 150 || col.g < 150)
			;
		else {
			col.r = 255;
			col.g = 255;
		}
	}
	else if (col.a == 4) {
		if (col.r < 150 || col.g < 150 || col.b < 150)
			;
		else {
			col.r = 255;
			col.g = 255;
			col.b = 255;
		}
	}

	return col;
}
#endif

void draw_brush(const point_t *s, const point_t *p, color_t col)
{
#ifdef _OPENGL
	draw_setup_color(col, 1);
	glBindTexture(GL_TEXTURE_2D, bsimg_id);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glPushMatrix();

	glTranslatef(s->x, s->y, 0.0f);
	glTranslatef(0.0f, 32.0f, 0.0f);

	point_t dir;
	float len;

	dir.x = s->x - p->x;
	dir.y = s->y - p->y;

	len = sqrtf(dir.x * dir.x + dir.y * dir.y);

	if (len > 0.001f) {

		dir.x /= len;
		dir.y /= len;

		float mx[16] = {
			dir.x, dir.y, 0.0f, 0.0f,
			dir.y, -dir.x, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f };

		glMultMatrixf(mx);
	}

	glTranslatef(0.0f, -32.0f, 0.0f);

	glBegin(GL_QUADS);

		glVertex2f(0.0f, 0.0f);
		glTexCoord2f(1.0f, 0.0f);

		glVertex2f(32.0f, 0.0f);
		glTexCoord2f(1.0f, 1.0f);

		glVertex2f(32.0f, 32.0f);
		glTexCoord2f(0.0f, 1.0f);

		glVertex2f(0.0f, 32.0f);
		glTexCoord2f(0.0f, 0.0f);

	glEnd();

	glPopMatrix();
#else
	SDL_Rect tgt;

	tgt.x = (int) (s->x * scale_x);
	tgt.y = (int) ((s->y + 32.0f) * scale_y) - 32;
	tgt.w = 32;
	tgt.h = 32;

	SDL_UnlockSurface(screen);
	SDL_BlitSurface(sbs, NULL, screen, &tgt);
	SDL_LockSurface(screen);
#endif
}

void draw_clear()
{
#ifdef _OPENGL
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glBindTexture(GL_TEXTURE_2D, bgimg_id);

	glDisable(GL_BLEND);

	glBegin(GL_QUADS);

		glVertex2f(0.0f, 0.0f);
		glTexCoord2f(1.0f, 0.0f);

		glVertex2f((float) map_w, 0.0f);
		glTexCoord2f(1.0f, 1.0f);

		glVertex2f((float) map_w,
				(float) map_h);
		glTexCoord2f(0.0f, 1.0f);

		glVertex2f(0.0f, (float) map_h);
		glTexCoord2f(0.0f, 0.0f);

	glEnd();
#else
	SDL_UnlockSurface(screen);
	SDL_BlitSurface(sbg, NULL, screen, NULL);
	SDL_LockSurface(screen);
#endif
}

void draw_line(const point_t *s, const point_t *e, color_t col)
{
#ifdef _OPENGL
	glBindTexture(GL_TEXTURE_2D, 0);

	draw_setup_color(col, 0);

	glBegin(GL_LINES);

		glVertex2fv((void *) s);
		glVertex2fv((void *) e);

	glEnd();
#else
	point_t		ls, le;
	uint8_t		tmp;

	ls = *s;
	le = *e;

	ls.x *= scale_x;
	ls.y *= scale_y;

	le.x *= scale_x;
	le.y *= scale_y;

	if (isnan(ls.x) || isnan(ls.y))
		return;

	if (isnan(le.x) || isnan(le.y))
		return;

	if (clip_line(&ls, &le) < 0)
		return;

	col = draw_setup_color(col, 0);

	tmp = col.r;
	col.r = col.b;
	col.b = tmp;

	int 	sx, sy;
	int 	ex, ey;

	sx = (int) ls.x;
	sy = (int) ls.y;
	ex = (int) le.x;
	ey = (int) le.y;

	draw_int_line(sx, sy, ex, ey, col);
	draw_int_line(sx + 1, sy, ex + 1, ey, col);
	draw_int_line(sx, sy + 1, ex, ey + 1, col);
#endif
}

void draw_fade(float alpha)
{
#ifdef _OPENGL
	glColor4f(0.0f, 1.0f, 0.0f, alpha);
	glBindTexture(GL_TEXTURE_2D, 0);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glBegin(GL_QUADS);

		glVertex2f(0.0f, 0.0f);

		glVertex2f((float) map_w, 0.0f);

		glVertex2f((float) map_w,
				(float) map_h);

		glVertex2f(0.0f, (float) map_h);

	glEnd();
#else
	SDL_UnlockSurface(screen);
	/*SDL_BlendRects(screen, NULL, 0,
			0, 255, 0, (int) (alpha * 255.0f));*/
	SDL_LockSurface(screen);

#endif
}

void draw_fail(float alpha)
{
#ifdef _OPENGL
	glColor4f(1.0f, 0.0f, 0.0f, alpha);
	glBindTexture(GL_TEXTURE_2D, 0);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glBegin(GL_QUADS);

		glVertex2f(0.0f, 0.0f);

		glVertex2f((float) map_w, 0.0f);

		glVertex2f((float) map_w,
				(float) map_h);

		glVertex2f(0.0f, (float) map_h);

	glEnd();
#endif
}

void draw_time(float alpha)
{
#ifdef _OPENGL
	glColor4f(0.5f, 0.5f, 0.5f, alpha);
	glBindTexture(GL_TEXTURE_2D, 0);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glBegin(GL_QUADS);

		glVertex2f(0.0f, 0.0f);

		glVertex2f((float) map_w, 0.0f);

		glVertex2f((float) map_w,
				(float) map_h);

		glVertex2f(0.0f, (float) map_h);

	glEnd();
#endif
}

