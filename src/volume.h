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

#ifndef _VOLUME_H
#define _VOLUME_H

#include <gtk/gtk.h>


typedef struct _vol_queue_t {
        char * file;
        GtkTreeIter iter;
        struct _vol_queue_t * next;
} vol_queue_t;


vol_queue_t * vol_queue_push(vol_queue_t * q, char * file, GtkTreeIter iter);
void calculate_volume(vol_queue_t * q);


float rva_from_volume(float volume, float rva_refvol, float rva_steepness);


#endif /* _VOLUME_H */
