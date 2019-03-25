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

#ifndef _H_MAPS_
#define _H_MAPS_

extern int		map_w;
extern int		map_h;

void maps_list_load(const char *list);
void maps_list_free();

void maps_load_this();
void maps_load_next();
void maps_load_prev();
void maps_save();

#endif /* _H_MAPS_ */

