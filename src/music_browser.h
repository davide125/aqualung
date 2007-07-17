/*                                                     -*- linux-c -*-
    Copyright (C) 2004 Tom Szilagyi

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    $Id$
*/


#ifndef _MUSIC_BROWSER_H
#define _MUSIC_BROWSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <gtk/gtk.h>

void create_music_browser(void);
void show_music_browser(void);
void hide_music_browser(void);

int path_get_store_type(GtkTreePath * p);
int iter_get_store_type(GtkTreeIter * i);

void music_store_progress_bar_hide(void);
void music_store_set_status_bar_info(void);
void music_tree_expand_stores(void);

void search_cb(gpointer data);

struct keybinds {
	void (*callback)(gpointer);
	int keyval1;
	int keyval2;
};

#define MS_COL_NAME    0
#define MS_COL_SORT    1
#define MS_COL_FONT    8
#define MS_COL_ICON    9
#define MS_COL_DATA   10

enum {
	STORE_TYPE_FILE,
	STORE_TYPE_CDDA
};

typedef union {
	int type;
} store_t;


#ifdef __cplusplus
} /* extern "C" */
#endif


#endif /* _MUSIC_BROWSER_H */

// vim: shiftwidth=8:tabstop=8:softtabstop=8 :  
