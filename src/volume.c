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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <math.h>

#include "common.h"
#include "file_decoder.h"
#include "i18n.h"
#include "volume.h"


#define RMSSIZE 100


typedef struct {
        float buffer[RMSSIZE];
        unsigned int pos;
        float sum;
} rms_env;


GtkWidget * vol_window = NULL;
GtkWidget * vol_label;
GtkWidget * progress;
GtkWidget * cancel_button;
int vol_x = -1000;
int vol_y = -1000;

extern GtkTreeStore * music_store;

vol_queue_t * vol_queue = NULL;
vol_queue_t * vol_queue_save = NULL;
file_decoder_t * fdec_vol;
unsigned long vol_chunk_size;
unsigned long vol_n_chunks;
unsigned long vol_chunks_read;
float vol_result;
int vol_running;
int vol_cancelled;

rms_env * rms_vol;


vol_queue_t *
vol_queue_push(vol_queue_t * q, char * file, GtkTreeIter iter) {

	vol_queue_t * q_item = NULL;
	vol_queue_t * q_prev;
	
	if ((q_item = (vol_queue_t *)calloc(1, sizeof(vol_queue_t))) == NULL) {
		fprintf(stderr, "vol_queue_push(): calloc error\n");
		return NULL;
	}
	
	q_item->file = strdup(file);
	q_item->iter = iter;
	q_item->next = NULL;

	if (q != NULL) {
		q_prev = q;
		while (q_prev->next != NULL) {
			q_prev = q_prev->next;
		}
		q_prev->next = q_item;
	}

	return q_item;
}


void
vol_queue_cleanup(vol_queue_t * q) {

	vol_queue_t * p;

	p = q;
	while (p != NULL) {
		q = p->next;
		free(p);
		p = q;
	}
}


inline static float
rms_env_process(rms_env * r, const float x) {

        r->sum -= r->buffer[r->pos];
        r->sum += x;
        r->buffer[r->pos] = x;

	r->pos++;
        if (r->pos == RMSSIZE) {
		r->pos = 0;
	}

        return sqrt(r->sum / (float)RMSSIZE);
}


int
vol_window_close(GtkWidget * widget, gpointer * data) {

        vol_cancelled = 1;
	vol_window = NULL;
        return 0;
}


gint
cancel_event(GtkWidget * widget, GdkEvent * event, gpointer data) {

        vol_cancelled = 1;
        return TRUE;
}


void
volume_window(void) {

	GtkWidget * vbox;


	vol_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(vol_window), _("Processing..."));
        gtk_window_set_position(GTK_WINDOW(vol_window), GTK_WIN_POS_CENTER);
        g_signal_connect(G_OBJECT(vol_window), "delete_event",
                         G_CALLBACK(vol_window_close), NULL);
        gtk_container_set_border_width(GTK_CONTAINER(vol_window), 10);

        vbox = gtk_vbox_new(FALSE, 0);
        gtk_container_add(GTK_CONTAINER(vol_window), vbox);

        vol_label = gtk_label_new("");
        gtk_box_pack_start(GTK_BOX(vbox), vol_label, FALSE, FALSE, 5);

	progress = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), 0.0f);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress), "0%");
        gtk_box_pack_start(GTK_BOX(vbox), progress, FALSE, FALSE, 5);

	cancel_button = gtk_button_new_with_label(_("Abort"));
        g_signal_connect(cancel_button, "clicked", G_CALLBACK(cancel_event), NULL);
        gtk_box_pack_start(GTK_BOX(vbox), cancel_button, FALSE, FALSE, 5);


	if ((vol_x > -1000) && (vol_y > -1000)) {
		gtk_window_move(GTK_WINDOW(vol_window), vol_x, vol_y);
	}
        gtk_widget_show_all(vol_window);
}


/* if returns 1, file will be skipped */
int
process_volume_setup(vol_queue_t * q) {

	char msg[MAXLEN];


	if (q == NULL) {
		fprintf(stderr, "programmer error: process_volume_setup() called on NULL\n");
		return 1;
	}
	
        if ((fdec_vol = file_decoder_new()) == NULL) {
                fprintf(stderr, "calculate_volume: error: file_decoder_new() returned NULL\n");
                return 1;
        }

        if (file_decoder_open(fdec_vol, g_locale_from_utf8(q->file, -1, NULL, NULL, NULL), 44100)) {
                fprintf(stderr, "file_decoder_open() failed on %s\n",
                        g_locale_from_utf8(q->file, -1, NULL, NULL, NULL));
                return 1;
        }

	if ((rms_vol = (rms_env *)calloc(1, sizeof(rms_env))) == NULL) {
		fprintf(stderr, "calculate_volume(): calloc error\n");
		return 1;
	}

	vol_chunk_size = fdec_vol->fileinfo.sample_rate / 100;
	vol_n_chunks = fdec_vol->fileinfo.total_samples / vol_chunk_size + 1;
	vol_chunks_read = 0;
	
	snprintf(msg, MAXLEN-1, _("Calculating volume level of %s"), q->file);
	gtk_label_set_text(GTK_LABEL(vol_label), msg);

	vol_running = 1;
	vol_cancelled = 0;
	vol_result = 0.0f;

	return 0;
}



gboolean
process_volume(gpointer data) {

	float * samples = NULL;
	unsigned long numread;
	char str_progress[10];
	float chunk_power = 0.0f;
	float rms;
	unsigned long i;
	int runs;

	if (!vol_running) {

		if (vol_queue->next != NULL) {
			vol_queue = vol_queue->next;
			if (process_volume_setup(vol_queue)) {
				vol_running = 0;
				return TRUE;
			}
		} else {
			vol_queue_cleanup(vol_queue_save);
			vol_queue_save = vol_queue = NULL;
			gtk_window_get_position(GTK_WINDOW(vol_window), &vol_x, &vol_y);
			gtk_widget_destroy(vol_window);
			vol_window = NULL;
			return FALSE;
		}
	}


	if (vol_cancelled) {
		vol_queue_cleanup(vol_queue_save);
		vol_queue_save = vol_queue = NULL;
		file_decoder_close(fdec_vol);
		file_decoder_delete(fdec_vol);
		if (vol_window != NULL) {
			gtk_window_get_position(GTK_WINDOW(vol_window), &vol_x, &vol_y);
			gtk_widget_destroy(vol_window);
			vol_window = NULL;
			free(rms_vol);
		}
		return FALSE;
	}

	if ((samples = (float *)malloc(vol_chunk_size * fdec_vol->channels * sizeof(float))) == NULL) {

		fprintf(stderr, "process_volume(): error: malloc() returned NULL\n");
		vol_queue_cleanup(vol_queue_save);
		vol_queue_save = vol_queue = NULL;
		file_decoder_close(fdec_vol);
		file_decoder_delete(fdec_vol);
		gtk_window_get_position(GTK_WINDOW(vol_window), &vol_x, &vol_y);
		gtk_widget_destroy(vol_window);
		vol_window = NULL;
		free(rms_vol);
		return FALSE;
	}


	/* just to make things faster */
	for (runs = 0; runs < 50; runs++) {

		numread = file_decoder_read(fdec_vol, samples, vol_chunk_size);
		++vol_chunks_read;

		if (runs == 0) {
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress),
						      (double)vol_chunks_read / vol_n_chunks);
			sprintf(str_progress, "%.1f%%", (double)vol_chunks_read / vol_n_chunks * 100.0f);
			gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress), str_progress);
		}
		
		
		/* calculate signal power of chunk and feed it in the rms envelope */
		if (numread > 0) {
			for (i = 0; i < numread * fdec_vol->channels; i++) {
				chunk_power += samples[i] * samples[i];
			}
			chunk_power /= numread * fdec_vol->channels;
			
			rms = rms_env_process(rms_vol, chunk_power);

			if (rms > vol_result)
				vol_result = rms;
		}
		
		if ((numread < vol_chunk_size) || (vol_cancelled)) {

			if (!vol_cancelled) {
				
				vol_result = 20.0f * log10f(vol_result);
				gtk_tree_store_set(music_store, &(vol_queue->iter), 5, vol_result, -1);
			}
			file_decoder_close(fdec_vol);
			file_decoder_delete(fdec_vol);
			free(rms_vol);
			free(samples);
			vol_running = 0;
			return TRUE;
		}
	}
	free(samples);
	return TRUE;
}


void
calculate_volume(vol_queue_t * q) {

	if (q == NULL)
		return;
	
	vol_queue_save = vol_queue = q;
	volume_window();
	process_volume_setup(vol_queue);
	g_idle_add(process_volume, NULL);
}


float
rva_from_volume(float volume, float rva_refvol, float rva_steepness) {

	return ((volume - rva_refvol) * (rva_steepness - 1.0f));
}
