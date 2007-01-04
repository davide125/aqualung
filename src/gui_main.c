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
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#ifdef _WIN32
#include <glib.h>
#else
#include <pthread.h>
#endif /* _WIN32 */

#ifdef HAVE_LADSPA
#include <lrdf.h>
#endif /* HAVE_LADSPA */

#ifdef HAVE_SRC
#include <samplerate.h>
#endif /* HAVE_SRC */

#include "common.h"
#include "core.h"
#include "rb.h"
#include "cover.h"
#include "transceiver.h"
#include "decoder/file_decoder.h"
#include "about.h"
#include "options.h"
#include "skin.h"
#include "search.h"
#include "ports.h"
#include "music_browser.h"
#include "playlist.h"
#include "plugin.h"
#include "file_info.h"
#include "i18n.h"
#include "cdda.h"
#include "loop_bar.h"
#include "gui_main.h"
#include "version.h"

/* receive at most this much remote messages in one run of timeout_callback() */
#define MAX_RCV_COUNT 32

/* period of main timeout callback [ms] */
#define TIMEOUT_PERIOD 100

extern options_t options;

char pl_color_active[14];
char pl_color_inactive[14];

PangoFontDescription *fd_playlist;
PangoFontDescription *fd_browser;
PangoFontDescription *fd_bigtimer;
PangoFontDescription *fd_smalltimer;
PangoFontDescription *fd_songtitle;
PangoFontDescription *fd_songinfo;
PangoFontDescription *fd_statusbar;

/* Communication between gui thread and disk thread */
extern AQUALUNG_MUTEX_DECLARE(disk_thread_lock)
extern AQUALUNG_COND_DECLARE(disk_thread_wake)
extern rb_t * rb_gui2disk;
extern rb_t * rb_disk2gui;

#ifdef HAVE_JACK
extern jack_client_t * jack_client;
extern char * client_name;
extern int jack_is_shutdown;
#endif /* HAVE_JACK */

extern volatile int vol_cancelled;

extern int aqualung_socket_fd;
extern int aqualung_session_id;

extern GtkListStore * ms_pathlist_store;
extern GtkTreeStore * play_store;
extern GtkListStore * running_store;
extern GtkWidget * play_list;

extern GtkWidget * plist_menu;

void init_plist_menu(GtkWidget *append_menu);

/* the physical name of the file that is playing, or a '\0'. */
char current_file[MAXLEN];

/* default window title */
char win_title[MAXLEN];

char send_cmd, recv_cmd;
char command[RB_CONTROL_SIZE];
fileinfo_t fileinfo;
status_t status;
unsigned long out_SR;
extern int output;
unsigned long rb_size;
unsigned long long total_samples;
unsigned long long sample_pos;

#ifdef HAVE_SRC
extern int src_type;
extern int src_type_parsed;
#else
int src_type = 0;
int src_type_parsed = 0;
#endif /* HAVE_SRC */

/* this flag set to 1 in core.c if --play
   for current instance is specified. */
int immediate_start = 0; 

int search_pl_flags = 0;
int search_ms_flags = SEARCH_F_AN | SEARCH_F_RT | SEARCH_F_TT | SEARCH_F_CO;


#ifdef HAVE_SNDFILE
extern char * valid_extensions_sndfile[];
#endif /* HAVE_SNDFILE */

#ifdef HAVE_MPEG
extern char * valid_extensions_mpeg[];
#endif /* HAVE_MPEG */

#ifdef HAVE_MOD
extern char * valid_extensions_mod[];
#endif /* HAVE_MOD */

/* volume & balance sliders */
double vol = 0.0f;
double vol_prev = 0.0f;
double vol_lin = 1.0f;
double bal = 0.0f;
double bal_prev = 0.0f;
extern double left_gain;
extern double right_gain;

/* label/display data */
fileinfo_t disp_info;
unsigned long disp_samples;
unsigned long disp_pos;

/* tooltips group */
GtkTooltips * aqualung_tooltips;

GtkWidget * main_window;
extern GtkWidget * browser_window;
extern GtkWidget * playlist_window;
extern GtkWidget * fxbuilder_window;
extern GtkWidget * ports_window;
extern GtkWidget * info_window;
extern GtkWidget * vol_window;
extern GtkWidget * build_prog_window;
extern GtkWidget * ripper_prog_window;
extern GtkWidget * browser_paned;

int main_pos_x;
int main_pos_y;
int main_size_x;
int main_size_y;

extern int music_store_changed;

extern int browser_pos_x;
extern int browser_pos_y;
extern int browser_size_x;
extern int browser_size_y;
extern int browser_on;
extern int browser_paned_pos;

extern int playlist_pos_x;
extern int playlist_pos_y;
extern int playlist_size_x;
extern int playlist_size_y;
extern int playlist_on;

extern int fxbuilder_on;

GtkObject * adj_pos;
GtkObject * adj_vol;
GtkObject * adj_bal;
GtkWidget * scale_pos;
GtkWidget * scale_vol;
GtkWidget * scale_bal;

#ifdef HAVE_LOOP

GtkWidget * loop_bar;
float loop_range_start = 0.0f;
float loop_range_end = 1.0f;

#endif /* HAVE_LOOP */

GtkWidget * time_labels[3];
int time_idx[3] = { 0, 1, 2 };
int refresh_time_label = 1;


gulong play_id;
gulong pause_id;

GtkWidget * play_button;
GtkWidget * pause_button;
GtkWidget * prev_button;
GtkWidget * stop_button;
GtkWidget * next_button;
GtkWidget * repeat_button;
GtkWidget * repeat_all_button;
GtkWidget * shuffle_button;

int repeat_on = 0;
int repeat_all_on = 0;
int shuffle_on = 0;

GtkWidget * label_title;
GtkWidget * label_format;
GtkWidget * label_samplerate;
GtkWidget * label_bps;
GtkWidget * label_mono;
GtkWidget * label_output;
GtkWidget * label_src_type;

int shift_state;
int x_scroll_start;
int x_scroll_pos;
int scroll_btn;

GtkWidget * plugin_toggle;
GtkWidget * musicstore_toggle;
GtkWidget * playlist_toggle;

guint timeout_tag;
guint vol_bal_timeout_tag = 0;

gint timeout_callback(gpointer data);

/* whether we are refreshing the scale on STATUS commands recv'd from disk thread */
int refresh_scale = 1;
/* suppress scale refreshing after seeking (discard this much STATUS packets).
   Prevents position slider to momentarily jump back to original position. */
int refresh_scale_suppress = 0;

/* whether we allow seeks (depending on if we are at the end of the track) */
int allow_seeks = 1;

/* controls when to load the new file's data on display */
int fresh_new_file = 1;
int fresh_new_file_prev = 1;

/* whether we have a file loaded, that is currently playing (or paused) */
int is_file_loaded = 0;

/* whether playback is paused */
int is_paused = 0;


/* popup menu for configuration */
GtkWidget * conf_menu;
GtkWidget * conf__options;
GtkWidget * conf__skin;
GtkWidget * conf__jack;
GtkWidget * conf__fileinfo;
GtkWidget * conf__about;
GtkWidget * conf__quit;

GtkWidget * bigtimer_label;
GtkWidget * smalltimer_label_1;
GtkWidget * smalltimer_label_2;

/* systray stuff */
#ifdef HAVE_SYSTRAY

GtkStatusIcon * systray_icon;

GtkWidget * systray_menu;
GtkWidget * systray__show;
GtkWidget * systray__hide;
GtkWidget * systray__play;
GtkWidget * systray__pause;
GtkWidget * systray__stop;
GtkWidget * systray__prev;
GtkWidget * systray__next;
GtkWidget * systray__quit;

int warn_wm_not_systray_capable = 0;
int systray_semaphore = 0;

void hide_all_windows(gpointer data);

#endif /* HAVE_SYSTRAY */

int systray_main_window_on = 1;

void create_main_window(char * skin_path);

gint prev_event(GtkWidget * widget, GdkEvent * event, gpointer data);
gint play_event(GtkWidget * widget, GdkEvent * event, gpointer data);
gint pause_event(GtkWidget * widget, GdkEvent * event, gpointer data);
gint stop_event(GtkWidget * widget, GdkEvent * event, gpointer data);
gint next_event(GtkWidget * widget, GdkEvent * event, gpointer data);

void load_config(void);

void playlist_toggled(GtkWidget * widget, gpointer data);

void assign_audio_fc_filters(GtkFileChooser *fc);
void assign_playlist_fc_filters(GtkFileChooser *fc);

GtkWidget * cover_align;
GtkWidget * c_event_box;
GtkWidget * cover_image_area;
gint cover_show_flag;

void set_buttons_relief(void);


/* externs form playlist.c */
extern void clear_playlist_selection(void);
extern void cut__sel_cb(gpointer data);
extern void plist__search_cb(gpointer data);
extern void direct_add(GtkWidget * widget, gpointer * data);

extern void start_playback_from_playlist(GtkTreePath * path);

extern void show_active_position_in_playlist(void);
extern void show_last_position_in_playlist(void);

extern char fileinfo_name[MAXLEN];
extern char fileinfo_file[MAXLEN];

extern GtkWidget * plist__fileinfo;
extern GtkWidget * plist__rva;

extern gint playlist_state;
extern gint browser_state;


gint
aqualung_dialog_run(GtkDialog * dialog) {

#ifdef HAVE_SYSTRAY
	int ret;
	systray_semaphore++;
	ret = gtk_dialog_run(dialog);
	systray_semaphore--;
	return ret;
#else
	return gtk_dialog_run(dialog);
#endif /* HAVE_SYSTRAY */
}

void
deflicker(void) {
	while (g_main_context_iteration(NULL, FALSE));
}


void
try_waking_disk_thread(void) {

	if (AQUALUNG_MUTEX_TRYLOCK(disk_thread_lock)) {
		AQUALUNG_COND_SIGNAL(disk_thread_wake)
		AQUALUNG_MUTEX_UNLOCK(disk_thread_lock)
	}
}


/* returns (hh:mm:ss) or (mm:ss) format time string from sample position */
void
sample2time(unsigned long SR, unsigned long long sample, char * str, int sign) {

	int h;
	char m, s;

	if (!SR)
		SR = 1;

	h = (sample / SR) / 3600;
	m = (sample / SR) / 60 - h * 60;
	s = (sample / SR) - h * 3600 - m * 60;

	if (h > 9)
		sprintf(str, (sign)?("-%02d:%02d:%02d"):("%02d:%02d:%02d"), h, m, s);
	else if (h > 0)
		sprintf(str, (sign)?("-%1d:%02d:%02d"):("%1d:%02d:%02d"), h, m, s);
	else
		sprintf(str, (sign)?("-%02d:%02d"):("%02d:%02d"), m, s);
}

/* converts a length measured in seconds to the appropriate string */
void
time2time(float seconds, char * str) {

	int d, h;
	char m, s;

        d = seconds / 86400;
	h = seconds / 3600;
	m = seconds / 60 - h * 60;
	s = seconds - h * 3600 - m * 60;
        h = h - d * 24;

        if (d > 0) {
                if (d == 1 && h > 9) {
                        sprintf(str, "%d %s, %2d:%02d:%02d", d, _("day"), h, m, s);
                } else if (d == 1 && h < 9) {
                        sprintf(str, "%d %s, %1d:%02d:%02d", d, _("day"), h, m, s);
                } else if (d != 1 && h > 9) {
                        sprintf(str, "%d %s, %2d:%02d:%02d", d, _("days"), h, m, s);
                } else {
                        sprintf(str, "%d %s, %1d:%02d:%02d", d, _("days"), h, m, s);
                }
        } else if (h > 0) {
		if (h > 9) {
			sprintf(str, "%02d:%02d:%02d", h, m, s);
		} else {
			sprintf(str, "%1d:%02d:%02d", h, m, s);
		}
	} else {
		sprintf(str, "%02d:%02d", m, s);
	}
}


void
time2time_na(float seconds, char * str) {

	if (seconds == 0.0) {
		strcpy(str, "N/A");
	} else {
		time2time(seconds, str);
	}
}


/* pack 2 strings into one
 * output format: 4 hex digits -> length of 1st string (N)
 *                4 hex digits -> length of 2nd string (M)
 *                N characters of 1st string (w/o trailing zero)
 *                M characters of 2nd string (w/o trailing zero)
 *                trailing zero
 * sum: length(str1) + length(str2) + 4 + 4 + 1
 * result should point to an area with sufficient space
 */
void
pack_strings(char * str1, char * str2, char * result) {

	sprintf(result, "%04X%04X%s%s", strlen(str1), strlen(str2), str1, str2);
}

/* inverse of pack_strings()
 * str1 and str2 should point to areas of sufficient space
 */
void
unpack_strings(char * packed, char * str1, char * str2) {

	int len1, len2;

	if (strlen(packed) < 8) {
		str1[0] = '\0';
		str2[0] = '\0';
		return;
	}

	sscanf(packed, "%04X%04X", &len1, &len2);
	strncpy(str1, packed + 8, len1);
	strncpy(str2, packed + 8 + len1, len2);
	str1[len1] = '\0';
	str2[len2] = '\0';
}


void
set_title_label(char * str) {

	gchar default_title[MAXLEN];
        char tmp[MAXLEN];

	if (is_file_loaded) {
		if (GTK_IS_LABEL(label_title)) {
			gtk_label_set_text(GTK_LABEL(label_title), str);
			if (options.show_sn_title) {
				strncpy(tmp, str, MAXLEN-1);
				strncat(tmp, " - ", MAXLEN-1);
				strncat(tmp, win_title, MAXLEN-1);
				gtk_window_set_title(GTK_WINDOW(main_window), tmp);
#ifdef HAVE_SYSTRAY
				gtk_status_icon_set_tooltip(systray_icon, tmp);
#endif /* HAVE_SYSTRAY */
			} else {
				gtk_window_set_title(GTK_WINDOW(main_window), win_title);
#ifdef HAVE_SYSTRAY
				gtk_status_icon_set_tooltip(systray_icon, win_title);
#endif /* HAVE_SYSTRAY */
			}
		}
	} else {
		if (GTK_IS_LABEL(label_title)) {
                        sprintf(default_title, "Aqualung %s", AQUALUNG_VERSION);
			gtk_label_set_text(GTK_LABEL(label_title), default_title);
			gtk_window_set_title(GTK_WINDOW(main_window), win_title);
#ifdef HAVE_SYSTRAY
			gtk_status_icon_set_tooltip(systray_icon, win_title);
#endif /* HAVE_SYSTRAY */
                }         
        }
}


void
set_format_label(char * format_str) {

	if (!is_file_loaded) {
		if (GTK_IS_LABEL(label_format))
			gtk_label_set_text(GTK_LABEL(label_format), "");
		return;
	}

	if (GTK_IS_LABEL(label_format))
		gtk_label_set_text(GTK_LABEL(label_format), format_str);
}


void
format_bps_label(int bps, int format_flags, char * str) {

	if (bps == 0) {
		strcpy(str, "N/A kbit/s");
		return;
	}

	if (format_flags & FORMAT_VBR) {
		sprintf(str, "%.1f kbit/s VBR", bps/1000.0);
	} else {
		if (format_flags & FORMAT_UBR) {
			sprintf(str, "%.1f kbit/s UBR", bps/1000.0);
		} else {
			sprintf(str, "%.1f kbit/s", bps/1000.0);
		}
	}
}

void
set_bps_label(int bps, int format_flags) {
	
	char str[MAXLEN];

	format_bps_label(bps, format_flags, str);

	if (is_file_loaded) {
		if (GTK_IS_LABEL(label_bps))
			gtk_label_set_text(GTK_LABEL(label_bps), str);
	} else {
		if (GTK_IS_LABEL(label_bps))
			gtk_label_set_text(GTK_LABEL(label_bps), "");
	}
}


void
set_samplerate_label(int sr) {
	
	char str[MAXLEN];

	sprintf(str, "%d Hz", sr);

	if (is_file_loaded) {
		if (GTK_IS_LABEL(label_samplerate))
			gtk_label_set_text(GTK_LABEL(label_samplerate), str);
	} else {
		if (GTK_IS_LABEL(label_samplerate))
			gtk_label_set_text(GTK_LABEL(label_samplerate), "");
	}
}


void
set_mono_label(int is_mono) {

	if (is_file_loaded) {
		if (is_mono) {
			if (GTK_IS_LABEL(label_mono))
				gtk_label_set_text(GTK_LABEL(label_mono), _("MONO"));
		} else {
			if (GTK_IS_LABEL(label_mono))
				gtk_label_set_text(GTK_LABEL(label_mono), _("STEREO"));
		}
	} else {
		if (GTK_IS_LABEL(label_mono))
			gtk_label_set_text(GTK_LABEL(label_mono), "");
	}
}



void
set_output_label(int output, int out_SR) {

	char str[MAXLEN];
	
	switch (output) {
#ifdef HAVE_OSS
	case OSS_DRIVER:
		sprintf(str, "%s OSS @ %d Hz", _("Output:"), out_SR);
		break;
#endif /* HAVE_OSS */
#ifdef HAVE_ALSA
	case ALSA_DRIVER:
		sprintf(str, "%s ALSA @ %d Hz", _("Output:"), out_SR);
		break;
#endif /* HAVE_ALSA */
#ifdef HAVE_JACK
	case JACK_DRIVER:
		sprintf(str, "%s JACK @ %d Hz", _("Output:"), out_SR);
		break;
#endif /* HAVE_JACK */
#ifdef _WIN32
	case WIN32_DRIVER:
		sprintf(str, "%s Win32 @ %d Hz", _("Output:"), out_SR);
		break;
#endif /* _WIN32 */
	default:
		strcpy(str, _("No output"));
		break;
	}
	
	if (GTK_IS_LABEL(label_output))
		gtk_label_set_text(GTK_LABEL(label_output), str);
}


void
set_src_type_label(int src_type) {
	
	char str[MAXLEN];
	
	strcpy(str, _("SRC Type: "));
#ifdef HAVE_SRC
	strcat(str, src_get_name(src_type));
#else
	strcat(str, _("None"));
#endif /* HAVE_SRC */

	if (GTK_IS_LABEL(label_src_type))
		gtk_label_set_text(GTK_LABEL(label_src_type), str);
}


void
refresh_time_displays(void) {

	char str[MAXLEN];

	if (is_file_loaded) {
		if (refresh_time_label || time_idx[0] != 0) {
			sample2time(disp_info.sample_rate, disp_pos, str, 0);
			if (GTK_IS_LABEL(time_labels[0])) 
				gtk_label_set_text(GTK_LABEL(time_labels[0]), str);
                        
		}

		if (refresh_time_label || time_idx[0] != 1) {
			if (disp_samples == 0) {
				strcpy(str, " N/A ");
			} else {
				sample2time(disp_info.sample_rate, disp_samples - disp_pos, str, 1);
			}
			if (GTK_IS_LABEL(time_labels[1])) 
				gtk_label_set_text(GTK_LABEL(time_labels[1]), str);
                        
		}
		
		if (refresh_time_label || time_idx[0] != 2) {
			if (disp_samples == 0) {
				strcpy(str, " N/A ");
			} else {
				sample2time(disp_info.sample_rate, disp_samples, str, 0);
			}
			if (GTK_IS_LABEL(time_labels[2])) 
				gtk_label_set_text(GTK_LABEL(time_labels[2]), str);
                        
		}
	} else {
		int i;
		for (i = 0; i < 3; i++) {
			if (GTK_IS_LABEL(time_labels[i]))
				gtk_label_set_text(GTK_LABEL(time_labels[i]), " 00:00 ");
		}
	}

}


void
refresh_displays(void) {

	GtkTreePath * p;
	GtkTreeIter iter;
	char * title_str;

	refresh_time_displays();

	if (play_store) {
		p = get_playing_path(play_store);
		if (p != NULL) {
			int n = gtk_tree_path_get_depth(p);
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
			gtk_tree_path_free(p);
			
			if (n > 1) { /* track under album node */
				GtkTreeIter iter_parent;
				char artist[MAXLEN];
				char record[MAXLEN];
				char list_str[MAXLEN];

				gtk_tree_model_iter_parent(GTK_TREE_MODEL(play_store), &iter_parent, &iter);
				gtk_tree_model_get(GTK_TREE_MODEL(play_store), &iter_parent, 1, &title_str, -1);
				unpack_strings(title_str, artist, record);
				g_free(title_str);
				gtk_tree_model_get(GTK_TREE_MODEL(play_store), &iter, 0, &title_str, -1);
				make_title_string(list_str, options.title_format, artist, record, title_str);
				g_free(title_str);
				set_title_label(list_str);				
			} else {
				gtk_tree_model_get(GTK_TREE_MODEL(play_store), &iter, 0, &title_str, -1);
				set_title_label(title_str);
				g_free(title_str);
			}
                        if (is_file_loaded) {
                                gtk_tree_model_get(GTK_TREE_MODEL(play_store), &iter, 1, &title_str, -1);
                                display_cover(cover_image_area, c_event_box, cover_align,
					      48, 48, title_str, TRUE, TRUE);
        			g_free(title_str);
                        }
		} else {
			set_title_label("");
                        cover_show_flag = 0;
                        gtk_widget_hide(cover_image_area);
                        gtk_widget_hide(c_event_box);
                        gtk_widget_hide(cover_align);
		}
	} else {
		set_title_label("");
                cover_show_flag = 0;
                gtk_widget_hide(cover_image_area);
                gtk_widget_hide(c_event_box);
		gtk_widget_hide(cover_align);
	}

	set_format_label(disp_info.format_str);
	set_samplerate_label(disp_info.sample_rate);
	set_bps_label(disp_info.bps, disp_info.format_flags);
	set_mono_label(disp_info.is_mono);
	set_output_label(output, out_SR);
	set_src_type_label(src_type);
}


void
zero_displays(void) {

	disp_info.total_samples = 0;
	disp_info.sample_rate = 0;
	disp_info.channels = 2;
	disp_info.is_mono = 0;
	disp_info.bps = 0;

	disp_samples = 0;
	disp_pos = 0;

	refresh_displays();
}


void
save_window_position(void) {

	gtk_window_get_position(GTK_WINDOW(main_window), &main_pos_x, &main_pos_y);

	if (!options.playlist_is_embedded && playlist_on) {
		gtk_window_get_position(GTK_WINDOW(playlist_window), &playlist_pos_x, &playlist_pos_y);
	}

	if (browser_on) {
		gtk_window_get_position(GTK_WINDOW(browser_window), &browser_pos_x, &browser_pos_y);
	}

	gtk_window_get_size(GTK_WINDOW(main_window), &main_size_x, &main_size_y);
	gtk_window_get_size(GTK_WINDOW(browser_window), &browser_size_x, &browser_size_y);

	if (!options.playlist_is_embedded) {
		gtk_window_get_size(GTK_WINDOW(playlist_window), &playlist_size_x, &playlist_size_y);
	} else {
		playlist_size_x = playlist_window->allocation.width;
		playlist_size_y = playlist_window->allocation.height;
	}

	if (!options.hide_comment_pane) {
		browser_paned_pos = gtk_paned_get_position(GTK_PANED(browser_paned));
	}
}


void
restore_window_position(void) {

	gtk_window_move(GTK_WINDOW(main_window), main_pos_x, main_pos_y);
	deflicker();
	gtk_window_move(GTK_WINDOW(browser_window), browser_pos_x, browser_pos_y);
	deflicker();
	if (!options.playlist_is_embedded) {
		gtk_window_move(GTK_WINDOW(playlist_window), playlist_pos_x, playlist_pos_y);
		deflicker();
	}
	
	gtk_window_resize(GTK_WINDOW(main_window), main_size_x, main_size_y);
	deflicker();
	gtk_window_resize(GTK_WINDOW(browser_window), browser_size_x, browser_size_y);
	deflicker();
	if (!options.playlist_is_embedded) {
		gtk_window_resize(GTK_WINDOW(playlist_window), playlist_size_x, playlist_size_y);
		deflicker();
	}

	if (!options.hide_comment_pane) {
		gtk_paned_set_position(GTK_PANED(browser_paned), browser_paned_pos);
		deflicker();
	}
}


void
change_skin(char * path) {

	int st_play = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(play_button));
	int st_pause = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pause_button));
	int st_r_track = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(repeat_button));
	int st_r_all = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(repeat_all_button));
	int st_shuffle = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shuffle_button));
	int st_fx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(plugin_toggle));
	int st_mstore = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(musicstore_toggle));
	int st_plist = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(playlist_toggle));


#ifdef HAVE_LADSPA
	GtkTreeIter iter;
	gpointer gp_instance;
	int i = 0;
#endif /* HAVE_LADSPA */

	char rcpath[MAXLEN];

        GdkColor color;

	g_source_remove(timeout_tag);
#ifdef HAVE_CDDA
	cdda_timeout_stop();
#endif /* HAVE_CDDA */

	save_window_position();

	gtk_widget_destroy(main_window);
	deflicker();
	if (!options.playlist_is_embedded) {
		gtk_widget_destroy(playlist_window);
		deflicker();
	}
	gtk_widget_destroy(browser_window);
	deflicker();
#ifdef HAVE_LADSPA
	gtk_widget_destroy(fxbuilder_window);
	deflicker();
#endif /* HAVE_LADSPA */
	
	sprintf(rcpath, "%s/rc", path);
	gtk_rc_parse(rcpath);

	create_main_window(path);
	deflicker();
	create_playlist();
	deflicker();
	create_music_browser();
	deflicker();
#ifdef HAVE_LADSPA
	create_fxbuilder();
	deflicker();
#endif /* HAVE_LADSPA */
	
	g_signal_handler_block(G_OBJECT(play_button), play_id);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(play_button), st_play);
	g_signal_handler_unblock(G_OBJECT(play_button), play_id);

	g_signal_handler_block(G_OBJECT(pause_button), pause_id);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pause_button), st_pause);
	g_signal_handler_unblock(G_OBJECT(pause_button), pause_id);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button), st_r_track);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_all_button), st_r_all);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button), st_shuffle);

	if (options.playlist_is_embedded) {
		g_signal_handlers_block_by_func(G_OBJECT(playlist_toggle), playlist_toggled, NULL);
	}
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(playlist_toggle), st_plist);
	if (options.playlist_is_embedded) {
		g_signal_handlers_unblock_by_func(G_OBJECT(playlist_toggle), playlist_toggled, NULL);
	}

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(musicstore_toggle), st_mstore);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(plugin_toggle), st_fx);

	gtk_widget_show_all(main_window);

#ifdef HAVE_LOOP
	if (!st_r_track) {
		gtk_widget_hide(loop_bar);
	}
#endif /* HAVE_LOOP */

	deflicker();

        cover_show_flag = 0;
        gtk_widget_hide(cover_image_area);
        gtk_widget_hide(c_event_box);
	gtk_widget_hide(cover_align);

	if (options.playlist_is_embedded) {
		if (!playlist_on) {
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(playlist_toggle), FALSE);
			gtk_widget_hide(playlist_window);
			deflicker();
		}
	}

#ifdef HAVE_LADSPA
        while (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(running_store), &iter, NULL, i) &&
		i < MAX_PLUGINS) {

		gtk_tree_model_get(GTK_TREE_MODEL(running_store), &iter, 1, &gp_instance, -1);
		gtk_widget_reset_rc_styles(((plugin_instance *)gp_instance)->window);
		gtk_widget_queue_draw(((plugin_instance *)gp_instance)->window);
		deflicker();
		++i;
	}
#endif /* HAVE_LADSPA */

	if (info_window) {
		gtk_widget_reset_rc_styles(info_window);
		gtk_widget_queue_draw(info_window);
		gtk_window_present(GTK_WINDOW(info_window));
		deflicker();
	}

	if (vol_window) {
		gtk_widget_reset_rc_styles(vol_window);
		gtk_widget_queue_draw(vol_window);
		deflicker();
	}

	if (build_prog_window) {
		gtk_widget_reset_rc_styles(build_prog_window);
		gtk_widget_queue_draw(build_prog_window);
		deflicker();
	}

	if (ripper_prog_window) {
		gtk_widget_reset_rc_styles(ripper_prog_window);
		gtk_widget_queue_draw(ripper_prog_window);
		deflicker();
	}

#ifdef HAVE_SYSTRAY
	gtk_widget_reset_rc_styles(systray_menu);
	gtk_widget_queue_draw(systray_menu);
	deflicker();
#endif /* HAVE_SYSTRAY */

	restore_window_position();
	deflicker();
	refresh_displays();

        if (options.override_skin_settings && (gdk_color_parse(options.activesong_color, &color) == TRUE)) {

                /* it's temporary workaround - see playlist.c FIXME tag for details */

                if (!color.red && !color.green && !color.blue)
                        color.red++;

                play_list->style->fg[SELECTED].red = color.red;
                play_list->style->fg[SELECTED].green = color.green;
                play_list->style->fg[SELECTED].blue = color.blue;
        }

	set_playlist_color();
	
	gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_vol), vol);
	gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_bal), bal);

	timeout_tag = g_timeout_add(TIMEOUT_PERIOD, timeout_callback, NULL);
#ifdef HAVE_CDDA
	cdda_timeout_start();
#endif /* HAVE_CDDA */

        show_active_position_in_playlist();
        gtk_widget_realize(play_list);

        if (options.playlist_is_embedded) {
                gtk_widget_grab_focus(GTK_WIDGET(play_list));
        }
}


void
main_window_close(GtkWidget * widget, gpointer data) {

	send_cmd = CMD_FINISH;
	rb_write(rb_gui2disk, &send_cmd, 1);
	try_waking_disk_thread();

	vol_cancelled = 1;

#ifdef HAVE_CDDA
	cdda_scanner_stop();
#endif /* HAVE_CDDA */

	if (music_store_changed) {
		GtkWidget * dialog;

                dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, 
                                                _("One or more stores in Music Store have been modified.\n"
                                                "Do you want to save them before exiting?"));
		gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
		gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
		gtk_container_set_border_width(GTK_CONTAINER(dialog), 5);
                gtk_window_set_title(GTK_WINDOW(dialog), _("Question"));
		
		if (aqualung_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
			save_all_music_store();
		}
		
		gtk_widget_destroy(dialog);
	}
	
	if (systray_main_window_on) {
		save_window_position();
	}

	save_config();

#ifdef HAVE_LADSPA
	save_plugin_data();
	lrdf_cleanup();
#endif /* HAVE_LADSPA */

	if (options.auto_save_playlist) {
		char playlist_name[MAXLEN];

		snprintf(playlist_name, MAXLEN-1, "%s/%s", options.confdir, "playlist.xml");
		save_playlist(playlist_name);
	}

        pango_font_description_free(fd_playlist);
        pango_font_description_free(fd_browser);

	gtk_main_quit();
}


/***********************************************************************************/


void
conf__options_cb(gpointer data) {

	create_options_window();
}


void
conf__skin_cb(gpointer data) {

	create_skin_window();
}


void
conf__jack_cb(gpointer data) {

#ifdef HAVE_JACK
	port_setup_dialog();
#endif /* HAVE_JACK */
}


void
conf__fileinfo_cb(gpointer data) {

	if (is_file_loaded) {

		GtkTreeIter dummy;
		const char * name = gtk_label_get_text(GTK_LABEL(label_title));
	
		show_file_info((char *)name, current_file, 0, NULL, dummy);
	}
}


void
conf__about_cb(gpointer data) {

	create_about_window();
}


void
conf__quit_cb(gpointer data) {

	main_window_close(NULL, NULL);
}


gint
vol_bal_timeout_callback(gpointer data) {

	refresh_time_label = 1;
	vol_bal_timeout_tag = 0;
	refresh_time_displays();

	return FALSE;
}


void
musicstore_toggled(GtkWidget * widget, gpointer data) {

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		show_music_browser();
	} else {
		hide_music_browser();
	}
}


void
playlist_toggled(GtkWidget * widget, gpointer data) {

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		show_playlist();
	} else {
		hide_playlist();
	}
}


void
plugin_toggled(GtkWidget * widget, gpointer data) {

#ifdef HAVE_LADSPA
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		show_fxbuilder();
	} else {
		hide_fxbuilder();
	}
#else
	/* XXX */
	printf("Aqualung compiled without LADSPA plugin support.\n");
#endif /* HAVE_LADSPA */
}


gint
main_window_key_pressed(GtkWidget * widget, GdkEventKey * event) {

        shift_state = event->state & GDK_SHIFT_MASK;

	switch (event->keyval) {	
	case GDK_KP_Divide:
		refresh_time_label = 0;
		if (vol_bal_timeout_tag) {
			g_source_remove(vol_bal_timeout_tag);
		}
			
		vol_bal_timeout_tag = g_timeout_add(1000, vol_bal_timeout_callback, NULL);

                if (event->state & GDK_MOD1_MASK) {  /* ALT + KP_Divide */
			g_signal_emit_by_name(G_OBJECT(scale_bal), "move-slider",
					      GTK_SCROLL_STEP_BACKWARD, NULL);
		} else {
			g_signal_emit_by_name(G_OBJECT(scale_vol), "move-slider",
					      GTK_SCROLL_STEP_BACKWARD, NULL);
		}
		return TRUE;
	case GDK_KP_Multiply:
		refresh_time_label = 0;
		if (vol_bal_timeout_tag) {
			g_source_remove(vol_bal_timeout_tag);
		}
		
		vol_bal_timeout_tag = g_timeout_add(1000, vol_bal_timeout_callback, NULL);

                if (event->state & GDK_MOD1_MASK) {  /* ALT + KP_Multiply */
			g_signal_emit_by_name(G_OBJECT(scale_bal),
					      "move-slider", GTK_SCROLL_STEP_FORWARD, NULL);
		} else {
			g_signal_emit_by_name(G_OBJECT(scale_vol),
					      "move-slider", GTK_SCROLL_STEP_FORWARD, NULL);
		}
		return TRUE;
	case GDK_Right:
		if (is_file_loaded && allow_seeks && total_samples != 0) {
			refresh_scale = 0;
			g_signal_emit_by_name(G_OBJECT(scale_pos), "move-slider",
					      GTK_SCROLL_STEP_FORWARD, NULL);
		}
		return TRUE;
	case GDK_Left:
		if (is_file_loaded && allow_seeks && total_samples != 0) {
			refresh_scale = 0;
			g_signal_emit_by_name(G_OBJECT(scale_pos), "move-slider",
					      GTK_SCROLL_STEP_BACKWARD, NULL);
		}
		return TRUE;
	case GDK_b:
	case GDK_B:
	case GDK_period:
		next_event(NULL, NULL, NULL);
		return TRUE;
	case GDK_z:
	case GDK_Z:
	case GDK_y:
	case GDK_Y:
	case GDK_comma:
		prev_event(NULL, NULL, NULL);
		return TRUE;
	case GDK_s:
	case GDK_S:
                if (event->state & GDK_MOD1_MASK) {  /* ALT + s */
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(musicstore_toggle),
						     !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(musicstore_toggle)));
		} else {
			stop_event(NULL, NULL, NULL);
		}
		return TRUE;
	case GDK_v:
	case GDK_V:
		stop_event(NULL, NULL, NULL);
		return TRUE;
	case GDK_c:
	case GDK_C:
	case GDK_space:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pause_button),
					     !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pause_button)));
		return TRUE;
	case GDK_p:
	case GDK_P:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(play_button),
					     !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(play_button)));
		return TRUE;
	case GDK_i:
	case GDK_I:
		if (!options.playlist_is_embedded) {
                        conf__fileinfo_cb(NULL);
        		return TRUE;
                }
                break;
	case GDK_BackSpace:
		if (allow_seeks && total_samples != 0) {
			seek_t seek;
			
			send_cmd = CMD_SEEKTO;
			seek.seek_to_pos = 0.0f;
			rb_write(rb_gui2disk, &send_cmd, 1);
			rb_write(rb_gui2disk, (char *)&seek, sizeof(seek_t));
			try_waking_disk_thread();
			refresh_scale_suppress = 2;
		}
		
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pause_button)))
			gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_pos), 0.0f);
		
		return TRUE;
	case GDK_l:
	case GDK_L:
                if (event->state & GDK_MOD1_MASK) {  /* ALT + l */
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(playlist_toggle),
						     !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(playlist_toggle)));
		}
		return TRUE;
	case GDK_x:
	case GDK_X:
                if (event->state & GDK_MOD1_MASK) {  /* ALT + x */
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(plugin_toggle),
						     !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(plugin_toggle)));
		} else {
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(play_button),
						     !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(play_button)));
		}
		return TRUE;
	case GDK_q:
	case GDK_Q:
                if (event->state & GDK_CONTROL_MASK) {  /* CTRL + q */
			main_window_close(NULL, NULL);
		}
		return TRUE;
	case GDK_k:
	case GDK_K:
        	create_skin_window();
                return TRUE;
	case GDK_o:
	case GDK_O:
                create_options_window();
                return TRUE;
	case GDK_1:
	        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(repeat_button))) {
        		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button), FALSE);
                } else {
        		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button), TRUE);
		}
                return TRUE;
	case GDK_2:
	        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(repeat_all_button))) {
        		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_all_button), FALSE);
		} else {
        		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_all_button), TRUE);
		}
                return TRUE;
	case GDK_3:
	        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shuffle_button))) {
        		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button), FALSE);
		} else {
        		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button), TRUE);
		}
                return TRUE;
#ifdef HAVE_SYSTRAY
	case GDK_Escape:
		hide_all_windows(NULL);
		return TRUE;
#endif /* HAVE_SYSTRAY */
	}
	

        if (options.playlist_is_embedded) {
		playlist_window_key_pressed(widget, event);
        }

	return FALSE;
}


gint
main_window_key_released(GtkWidget * widget, GdkEventKey * event) {

        shift_state = event->state & GDK_SHIFT_MASK;

	switch (event->keyval) {
        case GDK_Right:
        case GDK_Left:
                if (is_file_loaded && allow_seeks && refresh_scale == 0 && total_samples != 0) {
                        seek_t seek;

                        refresh_scale = 1;
                        send_cmd = CMD_SEEKTO;
                        seek.seek_to_pos = gtk_adjustment_get_value(GTK_ADJUSTMENT(adj_pos))
                                / 100.0f * total_samples;
                        rb_write(rb_gui2disk, &send_cmd, 1);
                        rb_write(rb_gui2disk, (char *)&seek, sizeof(seek_t));
			try_waking_disk_thread();
			refresh_scale_suppress = 2;
                }
                break;
	}

	return FALSE;
}


gint
main_window_focus_out(GtkWidget * widget, GdkEventFocus * event, gpointer data) {

        refresh_scale = 1;

        return FALSE;
}


gint
main_window_state_changed(GtkWidget * widget, GdkEventWindowState * event, gpointer data) {

        if (!options.united_minimization)
		return FALSE;
	
	if (event->new_window_state == GDK_WINDOW_STATE_ICONIFIED) {
		if (browser_on) {
			gtk_window_iconify(GTK_WINDOW(browser_window));
		}
		
		if (!options.playlist_is_embedded && playlist_on) {
			gtk_window_iconify(GTK_WINDOW(playlist_window));
		}
		
		if (vol_window) {
			gtk_window_iconify(GTK_WINDOW(vol_window));
		}
		
		if (info_window) {
			gtk_window_iconify(GTK_WINDOW(info_window));
		}
		
#ifdef HAVE_LADSPA
		if (fxbuilder_on) {
			GtkTreeIter iter;
			gpointer gp_instance;
			int i = 0;
			
			gtk_window_iconify(GTK_WINDOW(fxbuilder_window));
			
			while (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(running_store), &iter, NULL, i) &&
			       i < MAX_PLUGINS) {
				gtk_tree_model_get(GTK_TREE_MODEL(running_store), &iter, 1, &gp_instance, -1);
				gtk_widget_hide(((plugin_instance *)gp_instance)->window);
				++i;
			}
		}
#endif /* HAVE_LADSPA */
	}
	
	if (event->new_window_state == 0) {
		if (browser_on) {
			gtk_window_deiconify(GTK_WINDOW(browser_window));
		}
		
		if (!options.playlist_is_embedded && playlist_on) {
			gtk_window_deiconify(GTK_WINDOW(playlist_window));
		}
		
		if (vol_window) {
			gtk_window_deiconify(GTK_WINDOW(vol_window));
		}
		
		if (info_window) {
			gtk_window_deiconify(GTK_WINDOW(info_window));
		}
		
		if (fxbuilder_on) {
			gtk_window_deiconify(GTK_WINDOW(fxbuilder_window));
		}
	}
        return FALSE;
}


gint
main_window_button_pressed(GtkWidget * widget, GdkEventButton * event) {

	if (event->button == 3) {

		GtkWidget * fileinfo;

		if (options.playlist_is_embedded) {
			fileinfo = plist__fileinfo;
		} else {
			fileinfo = conf__fileinfo;
		}

		if (is_file_loaded && (current_file[0] != '\0')) {

			const char * name = gtk_label_get_text(GTK_LABEL(label_title));

			strncpy(fileinfo_name, name, MAXLEN-1);
			strncpy(fileinfo_file, current_file, MAXLEN-1);

			gtk_widget_set_sensitive(fileinfo, TRUE);
		} else {
			gtk_widget_set_sensitive(fileinfo, FALSE);
		}

		if (options.playlist_is_embedded) {
			gtk_widget_set_sensitive(plist__rva, (vol_window == NULL) ? TRUE : FALSE);
		}

                gtk_menu_popup(GTK_MENU(conf_menu), NULL, NULL, NULL, NULL,
			       event->button, event->time);
	}

	return TRUE;
}


static gint
scale_button_press_event(GtkWidget * widget, GdkEventButton * event) {

	if (event->button == 3)
		return FALSE;

	if (!is_file_loaded)
		return FALSE;

	if (!allow_seeks)
		return FALSE;

	if (total_samples == 0)
		return FALSE;

	refresh_scale = 0;
	return FALSE;
}


static gint
scale_button_release_event(GtkWidget * widget, GdkEventButton * event) {

	seek_t seek;

	if (is_file_loaded) {
		
		if (!allow_seeks)
			return FALSE;

		if (total_samples == 0)
			return FALSE;
		
		if (refresh_scale == 0) {
			refresh_scale = 1;

			send_cmd = CMD_SEEKTO;
			seek.seek_to_pos = gtk_adjustment_get_value(GTK_ADJUSTMENT(adj_pos))
				/ 100.0f * total_samples;
			rb_write(rb_gui2disk, &send_cmd, 1);
			rb_write(rb_gui2disk, (char *)&seek, sizeof(seek_t));
			try_waking_disk_thread();
			refresh_scale_suppress = 2;
		}
	}

	return FALSE;
}


#ifdef HAVE_LOOP

void
loop_range_changed_cb(AqualungLoopBar * bar, float start, float end, gpointer data) {

	loop_range_start = start;
	loop_range_end = end;
}

#endif /* HAVE_LOOP */

void
changed_pos(GtkAdjustment * adj, gpointer data) {

        char str[16];

	if (!is_file_loaded)
		gtk_adjustment_set_value(adj, 0.0f);

        if (options.enable_tooltips) {
                sprintf(str, _("Position: %d%%"), (gint)gtk_adjustment_get_value(GTK_ADJUSTMENT(adj))); 
                gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), scale_pos, str, NULL);
        }
}


gint
scale_vol_button_press_event(GtkWidget * widget, GdkEventButton * event) {

	char str[10];
	vol = gtk_adjustment_get_value(GTK_ADJUSTMENT(adj_vol));

        if (event->state & GDK_SHIFT_MASK) {  /* SHIFT */
		gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_vol), 0);
		return TRUE;
	}

	if (vol < -40.5f) {
		sprintf(str, _("Mute"));
	} else {
		sprintf(str, _("%d dB"), (int)vol);
	}

	gtk_label_set_text(GTK_LABEL(time_labels[time_idx[0]]), str);
	
	refresh_time_label = 0;

	if (event->button == 3) {
		return TRUE;
	} else {
		return FALSE;
	}
}


void
changed_vol(GtkAdjustment * adj, gpointer date) {

	char str[10], str2[32];

	vol = gtk_adjustment_get_value(GTK_ADJUSTMENT(adj_vol));
	vol = (int)vol;
	gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_vol), vol);

        if (vol < -40.5f) {
                sprintf(str, _("Mute"));
        } else {
                sprintf(str, _("%d dB"), (int)vol);
        }

        if (!shift_state && !refresh_time_label) {
		gtk_label_set_text(GTK_LABEL(time_labels[time_idx[0]]), str);
        }

        sprintf(str2, _("Volume: %s"), str);
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), scale_vol, str2, NULL);
}


gint
scale_vol_button_release_event(GtkWidget * widget, GdkEventButton * event) {

	refresh_time_label = 1;
	refresh_time_displays();

	return FALSE;
}


gint
scale_bal_button_press_event(GtkWidget * widget, GdkEventButton * event) {

	char str[10];
	bal = gtk_adjustment_get_value(GTK_ADJUSTMENT(adj_bal));

        if (event->state & GDK_SHIFT_MASK) {  /* SHIFT */
		gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_bal), 0);
		return TRUE;
	}
	
	if (bal != 0.0f) {
		if (bal > 0.0f) {
			sprintf(str, _("%d%% R"), (int)bal);
		} else {
			sprintf(str, _("%d%% L"), -1*(int)bal);
		}
	} else {
		sprintf(str, _("C"));
	}

	gtk_label_set_text(GTK_LABEL(time_labels[time_idx[0]]), str);
	
	refresh_time_label = 0;

	if (event->button == 3) {
		return TRUE;
	} else {
		return FALSE;
	}
}


void
changed_bal(GtkAdjustment * adj, gpointer date) {

	char str[10], str2[32];

	bal = gtk_adjustment_get_value(GTK_ADJUSTMENT(adj_bal));
	bal = (int)bal;
	gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_bal), bal);

        if (bal != 0.0f) {
                if (bal > 0.0f) {
                        sprintf(str, _("%d%% R"), (int)bal);
                } else {
                        sprintf(str, _("%d%% L"), -1*(int)bal);
                }
        } else {
                sprintf(str, _("C"));
        }
	
        if (!shift_state && !refresh_time_label) {  /* SHIFT */
		gtk_label_set_text(GTK_LABEL(time_labels[time_idx[0]]), str);
	}

        sprintf(str2, _("Balance: %s"), str);
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), scale_bal, str2, NULL);
}


gint
scale_bal_button_release_event(GtkWidget * widget, GdkEventButton * event) {

	refresh_time_label = 1;
	refresh_time_displays();

	return FALSE;
}


/******** Cue functions *********/

void
toggle_noeffect(int id, int state) {
	switch (id) {
	case PLAY:
		g_signal_handler_block(G_OBJECT(play_button), play_id);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(play_button), state);
		g_signal_handler_unblock(G_OBJECT(play_button), play_id);
		break;
	case PAUSE:
		g_signal_handler_block(G_OBJECT(pause_button), pause_id);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pause_button), state);
		g_signal_handler_unblock(G_OBJECT(pause_button), pause_id);
		break;
	default:
		printf("error in gui_main.c/toggle_without_effect(): unknown id value %d\n", id);
		break;
	}
}


void
mark_track(GtkTreeIter * piter) {

        int j, n;
        char * track_name;
	char *str;
        char counter[MAXLEN];
	char tmptrackname[MAXLEN];


	gtk_tree_model_get(GTK_TREE_MODEL(play_store), piter, 2, &str, -1);
	if (strcmp(str, pl_color_active) == 0) {
		g_free(str);
		return;
	}
	g_free(str);

        n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(play_store), piter);

        if (n) {
		GtkTreeIter iter_child;

                gtk_tree_model_get(GTK_TREE_MODEL(play_store), piter, 0, &track_name, -1);
                strncpy(tmptrackname, track_name, MAXLEN-1);

                j = 0;
		while (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(play_store), &iter_child, piter, j++)) {
			gtk_tree_model_get(GTK_TREE_MODEL(play_store), &iter_child, 2, &str, -1);
			if (strcmp(str, pl_color_active) == 0) {
				g_free(str);
                                break;
			}
			g_free(str);
		}

		if (j > n) {
			return;
		}

                sprintf(counter, _(" (%d/%d)"), j, n);
                strncat(tmptrackname, counter, MAXLEN-1);
		gtk_tree_store_set(play_store, piter, 0, tmptrackname, -1);
                g_free(track_name);
        }

	gtk_tree_store_set(play_store, piter, 2, pl_color_active, -1);
	if (options.show_active_track_name_in_bold) {
		gtk_tree_store_set(play_store, piter, 7, PANGO_WEIGHT_BOLD, -1);
	}

	if (gtk_tree_store_iter_depth(play_store, piter)) { /* track node of album */
		GtkTreeIter iter_parent;

		gtk_tree_model_iter_parent(GTK_TREE_MODEL(play_store), &iter_parent, piter);
		mark_track(&iter_parent);
	}
}


void
unmark_track(GtkTreeIter * piter) {

	int n;

	gtk_tree_store_set(play_store, piter, 2, pl_color_inactive, -1);
	gtk_tree_store_set(play_store, piter, 7, PANGO_WEIGHT_NORMAL, -1);

        n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(play_store), piter);

        if (n) {
		/* unmarking an album node: cut the counter string from the end */

		char * name;
		char * pack;
		int len1, len2;

		gtk_tree_model_get(GTK_TREE_MODEL(play_store), piter, 0, &name, 1, &pack, -1);

		sscanf(pack, "%04X%04X", &len1, &len2);

		/* the +2 in the index below is the length of the ": "
		   string which is put between the artist and album
		   names in music_browser.c: record_addlist_iter() */
		name[len1 + len2 + 2] = '\0';

		gtk_tree_store_set(play_store, piter, 0, name, -1);

		g_free(pack);
		g_free(name);
	}

	if (gtk_tree_store_iter_depth(play_store, piter)) { /* track node of album */
		GtkTreeIter iter_parent;

		gtk_tree_model_iter_parent(GTK_TREE_MODEL(play_store), &iter_parent, piter);
		unmark_track(&iter_parent);
	}
}


void
cue_track_for_playback(GtkTreeIter * piter, cue_t * cue) {

	char * str;

	gtk_tree_model_get(GTK_TREE_MODEL(play_store), piter, 1, &str, 3, &(cue->voladj), -1);
	cue->filename = strdup(str);
	strncpy(current_file, str, MAXLEN-1);
	g_free(str);
}


/* retcode for choose_X_track(): 1->success, 0->empty list */
int
choose_first_track(GtkTreeIter * piter) {

	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(play_store), piter)) {
		if (gtk_tree_model_iter_has_child(GTK_TREE_MODEL(play_store), piter)) {
			GtkTreeIter iter_parent = *piter;
			gtk_tree_model_iter_children(GTK_TREE_MODEL(play_store), piter, &iter_parent);
		}
		return 1;
	}
	return 0;
}


/* get first or last child iter */
void
get_child_iter(GtkTreeIter * piter, int first) {
	
	GtkTreeIter iter;
	if (first) {
		gtk_tree_model_iter_children(GTK_TREE_MODEL(play_store), &iter, piter);
	} else {
		int n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(play_store), piter);
		gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(play_store), &iter, piter, n-1);
	}
	*piter = iter;
}


int
choose_prev_track(GtkTreeIter * piter) {

	GtkTreePath * p = gtk_tree_model_get_path(GTK_TREE_MODEL(play_store), piter);

 try_again_prev:
	if (gtk_tree_path_prev(p)) {
		if (gtk_tree_path_get_depth(p) == 1) { /* toplevel */
			GtkTreeIter iter;
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
			if (gtk_tree_model_iter_has_child(GTK_TREE_MODEL(play_store), &iter)) {
				get_child_iter(&iter, 0/* last */);
			}
			*piter = iter;
		} else {
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), piter, p);
		}
		gtk_tree_path_free(p);
		return 1;
	} else {
		if (gtk_tree_path_get_depth(p) == 1) { /* toplevel */
			GtkTreeIter iter;
			int n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(play_store), NULL);
			if (n) {
				gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(play_store), &iter, NULL, n-1);
				if (gtk_tree_model_iter_has_child(GTK_TREE_MODEL(play_store), &iter)) {
					get_child_iter(&iter, 0/* last */);
				}
				*piter = iter;
				gtk_tree_path_free(p);
				return 1;
			} else {
				gtk_tree_path_free(p);
				return 0;
			}
		} else {
			gtk_tree_path_up(p);
			goto try_again_prev;
		}
	}
}


int
choose_next_track(GtkTreeIter * piter) {

 try_again_next:
	if (gtk_tree_model_iter_next(GTK_TREE_MODEL(play_store), piter)) {
		GtkTreePath * p = gtk_tree_model_get_path(GTK_TREE_MODEL(play_store), piter);
		if (gtk_tree_path_get_depth(p) == 1) { /* toplevel */
			if (gtk_tree_model_iter_has_child(GTK_TREE_MODEL(play_store), piter)) {
				get_child_iter(piter, 1/* first */);
			}
		}
		gtk_tree_path_free(p);
		return 1;
	} else {
		GtkTreePath * p = gtk_tree_model_get_path(GTK_TREE_MODEL(play_store), piter);
		if (gtk_tree_path_get_depth(p) == 1) { /* toplevel */
			int n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(play_store), NULL);
			if (n) {
				gtk_tree_model_get_iter_first(GTK_TREE_MODEL(play_store), piter);
				if (gtk_tree_model_iter_has_child(GTK_TREE_MODEL(play_store), piter)) {
					get_child_iter(piter, 1/* first */);
				}
				gtk_tree_path_free(p);
				return 1;
			} else {
				gtk_tree_path_free(p);
				return 0;
			}
		} else {
			gtk_tree_path_up(p);
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), piter, p);
			gtk_tree_path_free(p);
			goto try_again_next;
		}		
	}
}


/* simpler case than choose_next_track(); no support for wrap-around at end of list.
 * used by the timeout callback for track flow-through */
int
choose_adjacent_track(GtkTreeIter * piter) {

 try_again_adjacent:
	if (gtk_tree_model_iter_next(GTK_TREE_MODEL(play_store), piter)) {
		GtkTreePath * p = gtk_tree_model_get_path(GTK_TREE_MODEL(play_store), piter);
		if (gtk_tree_path_get_depth(p) == 1) { /* toplevel */
			if (gtk_tree_model_iter_has_child(GTK_TREE_MODEL(play_store), piter)) {
				get_child_iter(piter, 1/* first */);
			}
		}
		gtk_tree_path_free(p);
		return 1;
	} else {
		GtkTreePath * p = gtk_tree_model_get_path(GTK_TREE_MODEL(play_store), piter);
		if (gtk_tree_path_get_depth(p) == 1) { /* toplevel */
			gtk_tree_path_free(p);
			return 0;
		} else {
			gtk_tree_path_up(p);
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), piter, p);
			gtk_tree_path_free(p);
			goto try_again_adjacent;
		}		
	}
}


/* also used to pick the track numbered n_stop in the flattened tree */
long
count_playlist_tracks(GtkTreeIter * piter, long n_stop) {

	GtkTreeIter iter;
	long i = 0;
	long n = 0;

	while (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(play_store), &iter, NULL, i++)) {
		long c = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(play_store), &iter);
		long d = c;
		if (!c) ++c;
		if (n_stop > -1) {
			if (n_stop > c) {
				n_stop -= c;
			} else {
				if (d) {
					gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(play_store),
								      piter, &iter, n_stop-1);
					return 0;
				} else {
					*piter = iter;
					return 0;
				}
			}
		}
		n += c;
	}
	return n;
}


int
random_toplevel_item(GtkTreeIter * piter) {

	long n_items;
	long n;

	n_items = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(play_store), NULL);
	if (!n_items) {
		return 0;
	}

	n = (double)rand() * n_items / RAND_MAX;
	if (n == n_items)
		--n;
	
	gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(play_store), piter, NULL, n);
	return 1;
}


int
random_first_track(GtkTreeIter * piter) {

	if (random_toplevel_item(piter)) {
		if (gtk_tree_model_iter_has_child(GTK_TREE_MODEL(play_store), piter)) {
			get_child_iter(piter, 1/* first */);
		}
		return 1;
	} else {
		return 0;
	}
}


int
choose_random_track(GtkTreeIter * piter) {

	long n_items;
	long n;

	if (options.album_shuffle_mode) {
		if (gtk_tree_store_iter_is_valid(play_store, piter)) {
			int d = gtk_tree_store_iter_depth(play_store, piter);
			if (d) {
				if (gtk_tree_model_iter_next(GTK_TREE_MODEL(play_store), piter)) {
					return 1;
				}
			}
		}
		return random_first_track(piter);
	} else {
		n_items = count_playlist_tracks(NULL, -1);
		if (n_items) {
			n = (double)rand() * n_items / RAND_MAX;
			if (n == n_items) {
				--n;
			}
			count_playlist_tracks(piter, n+1);
			return 1;
		}
		return 0;
	}
}


void
prepare_playback(GtkTreeIter * piter, cue_t * pcue) {

	mark_track(piter);
	cue_track_for_playback(piter, pcue);
	is_file_loaded = 1;
	toggle_noeffect(PLAY, TRUE);
}


void
unprepare_playback(void) {
	
	is_file_loaded = 0;
	current_file[0] = '\0';
	zero_displays();
	toggle_noeffect(PLAY, FALSE);
}


gint
prev_event(GtkWidget * widget, GdkEvent * event, gpointer data) {

	GtkTreeIter iter;
	GtkTreePath * p;
	char cmd;
	cue_t cue;

	if (!allow_seeks)
		return FALSE;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shuffle_button))) {
		/* normal or repeat mode */
		p = get_playing_path(play_store);
		if (p != NULL) {
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
			gtk_tree_path_free(p);
			unmark_track(&iter);
			if (choose_prev_track(&iter)) {
				mark_track(&iter);
			}
		} else {
			if (choose_first_track(&iter)) {
				mark_track(&iter);
			}
		}
	} else {
		/* shuffle mode */
		p = get_playing_path(play_store);
		if (p != NULL) {
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
			gtk_tree_path_free(p);
			unmark_track(&iter);
		}
		if (choose_random_track(&iter)) {
			mark_track(&iter);
		}
	}

	if (is_file_loaded) {
		if ((p = get_playing_path(play_store)) == NULL) {
			if (is_paused) {
				is_paused = 0;
				toggle_noeffect(PAUSE, FALSE);
				stop_event(NULL, NULL, NULL);
			}
			return FALSE;
		}

		gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
		gtk_tree_path_free(p);
		cue_track_for_playback(&iter, &cue);

		if (is_paused) {
			is_paused = 0;
			toggle_noeffect(PAUSE, FALSE);
			toggle_noeffect(PLAY, TRUE);
		}

		cmd = CMD_CUE;
		rb_write(rb_gui2disk, &cmd, sizeof(char));
		rb_write(rb_gui2disk, (void *)&cue, sizeof(cue_t));
		try_waking_disk_thread();
	}
	return FALSE;
}


gint
next_event(GtkWidget * widget, GdkEvent * event, gpointer data) {

	GtkTreeIter iter;
	GtkTreePath * p;
	char cmd;
	cue_t cue;

	if (!allow_seeks)
		return FALSE;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shuffle_button))) {
		/* normal or repeat mode */
		p = get_playing_path(play_store);
		if (p != NULL) {
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
			gtk_tree_path_free(p);
			unmark_track(&iter);
			if (choose_next_track(&iter)) {
				mark_track(&iter);
			}
		} else {
			if (choose_first_track(&iter)) {
				mark_track(&iter);
			}
		}
	} else {
		/* shuffle mode */
		p = get_playing_path(play_store);
		if (p != NULL) {
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
			gtk_tree_path_free(p);
			unmark_track(&iter);
		}
		if (choose_random_track(&iter)) {
			mark_track(&iter);
		}
	}

	if (is_file_loaded) {
		if ((p = get_playing_path(play_store)) == NULL) {
			if (is_paused) {
				is_paused = 0;
				toggle_noeffect(PAUSE, FALSE);
				stop_event(NULL, NULL, NULL);
			}
			return FALSE;
		}

		gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
		gtk_tree_path_free(p);
		cue_track_for_playback(&iter, &cue);

		if (is_paused) {
			is_paused = 0;
			toggle_noeffect(PAUSE, FALSE);
			toggle_noeffect(PLAY, TRUE);
		}

		cmd = CMD_CUE;
		rb_write(rb_gui2disk, &cmd, sizeof(char));
		rb_write(rb_gui2disk, (void *)&cue, sizeof(cue_t));
		try_waking_disk_thread();
	}
	return FALSE;
}


gint
play_event(GtkWidget * widget, GdkEvent * event, gpointer data) {

	GtkTreeIter iter;
	GtkTreePath * p;
	char cmd;
	cue_t cue;

	if (is_paused) {
		is_paused = 0;
		toggle_noeffect(PAUSE, FALSE);
		send_cmd = CMD_RESUME;
		rb_write(rb_gui2disk, &send_cmd, 1);
		try_waking_disk_thread();
		return FALSE;
	}

	cmd = CMD_CUE;
	cue.filename = NULL;
	
	p = get_playing_path(play_store);
	if (p != NULL) {
		gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
		gtk_tree_path_free(p);
		prepare_playback(&iter, &cue);
	} else {
		if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shuffle_button))) {
			/* normal or repeat mode */
			if (choose_first_track(&iter)) {
				prepare_playback(&iter, &cue);
			} else {
				unprepare_playback();
			}			
		} else { /* shuffle mode */
			if (choose_random_track(&iter)) {
				prepare_playback(&iter, &cue);
			} else {
				unprepare_playback();
			}
		}
	}
	if (cue.filename == NULL) {
		stop_event(NULL, NULL, NULL);
	} else {
		rb_write(rb_gui2disk, &cmd, sizeof(char));
		rb_write(rb_gui2disk, (void *)&cue, sizeof(cue_t));
		try_waking_disk_thread();
	}
	return FALSE;
}


gint
pause_event(GtkWidget * widget, GdkEvent * event, gpointer data) {

	if ((!allow_seeks) || (!is_file_loaded)) {
		toggle_noeffect(PAUSE, FALSE);
		return FALSE;
	}

	if (!is_paused) {
		is_paused = 1;
		toggle_noeffect(PLAY, FALSE);
		send_cmd = CMD_PAUSE;
		rb_write(rb_gui2disk, &send_cmd, 1);

	} else {
		is_paused = 0;
		toggle_noeffect(PLAY, TRUE);
		send_cmd = CMD_RESUME;
		rb_write(rb_gui2disk, &send_cmd, 1);
	}

	try_waking_disk_thread();
	return FALSE;
}


gint
stop_event(GtkWidget * widget, GdkEvent * event, gpointer data) {

	char cmd;
	cue_t cue;

	is_file_loaded = 0;
	current_file[0] = '\0';
	gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_pos), 0.0f);
	zero_displays();
	toggle_noeffect(PLAY, FALSE);
	toggle_noeffect(PAUSE, FALSE);

        if (is_paused) {
                is_paused = 0;
        }

	cmd = CMD_CUE;
	cue.filename = NULL;
        rb_write(rb_gui2disk, &cmd, sizeof(char));
        rb_write(rb_gui2disk, (void *)&cue, sizeof(cue_t));
	try_waking_disk_thread();

        /* hide cover */
        cover_show_flag = 0;
        gtk_widget_hide(cover_image_area);
        gtk_widget_hide(c_event_box);
	gtk_widget_hide(cover_align);
			
	return FALSE;
}


/* called when a track ends without user intervention */
void
decide_next_track(cue_t * pcue) {

	GtkTreePath * p;
	GtkTreeIter iter;

	p = get_playing_path(play_store);
	if (p != NULL) { /* there is a marked track in playlist */
		if ((!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shuffle_button))) &&
		    (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(repeat_button)))) {
			/* normal or list repeat mode */
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
			gtk_tree_path_free(p);
			unmark_track(&iter);
			if (choose_adjacent_track(&iter)) {
				prepare_playback(&iter, pcue);
			} else {
				if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(repeat_all_button))) {
					/* normal mode */
					allow_seeks = 1;
					changed_pos(GTK_ADJUSTMENT(adj_pos), NULL);
					unprepare_playback();
				} else {
					/* list repeat mode */
					if (choose_first_track(&iter)) {
						prepare_playback(&iter, pcue);
					} else {
						allow_seeks = 1;
						changed_pos(GTK_ADJUSTMENT(adj_pos), NULL);
						unprepare_playback();
					}
				}
			}
		} else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(repeat_button))) {
			/* track repeat mode */
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
			gtk_tree_path_free(p);
			prepare_playback(&iter, pcue);
		} else {
			/* shuffle mode */
			gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
			gtk_tree_path_free(p);
			unmark_track(&iter);
			if (choose_random_track(&iter)) {
				prepare_playback(&iter, pcue);
			} else {
				unprepare_playback();
			}
		}
	} else { /* no marked track in playlist */
		if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shuffle_button))) {
			/* normal or repeat mode */
			if (choose_first_track(&iter)) {
				prepare_playback(&iter, pcue);
			} else {
				allow_seeks = 1;
				changed_pos(GTK_ADJUSTMENT(adj_pos), NULL);
				unprepare_playback();
			}
		} else { /* shuffle mode */
			if (choose_random_track(&iter)) {
				prepare_playback(&iter, pcue);
			} else {
				unprepare_playback();
			}
		}
	}
}


/********************************************/

void
swap_labels(int a, int b) {

	GtkWidget * tmp;
	int t;

	tmp = time_labels[time_idx[a]];
	time_labels[time_idx[a]] = time_labels[time_idx[b]] ;
	time_labels[time_idx[b]] = tmp;

	t = time_idx[b];
	time_idx[b] = time_idx[a];
	time_idx[a] = t;
}


gint
time_label0_clicked(GtkWidget * widget, GdkEventButton * event) {

	switch (event->button) {
	case 1:
		swap_labels(0, 1);
		refresh_time_displays();
		break;
	case 3:
		swap_labels(0, 2);
		refresh_time_displays();
		break;
	}

	return TRUE;
}


gint
time_label1_clicked(GtkWidget * widget, GdkEventButton * event) {

	switch (event->button) {
	case 1:
		swap_labels(0, 1);
		refresh_time_displays();
		break;
	case 3:
		swap_labels(1, 2);
		refresh_time_displays();
		break;
	}

	return TRUE;
}


gint
time_label2_clicked(GtkWidget * widget, GdkEventButton * event) {

	switch (event->button) {
	case 1:
		swap_labels(1, 2);
		refresh_time_displays();
		break;
	case 3:
		swap_labels(0, 2);
		refresh_time_displays();
		break;
	}

	return TRUE;
}


gint
scroll_btn_pressed(GtkWidget * widget, GdkEventButton * event) {

	if (event->button != 1)
		return FALSE;

	x_scroll_start = event->x;
	x_scroll_pos = event->x;
	scroll_btn = event->button;

	return TRUE;
}

gint
scroll_btn_released(GtkWidget * widget, GdkEventButton * event, gpointer * win) {

	scroll_btn = 0;
	gdk_window_set_cursor(gtk_widget_get_parent_window(GTK_WIDGET(win)), NULL);

	return TRUE;
}

gint
scroll_motion_notify(GtkWidget * widget, GdkEventMotion * event, gpointer * win) {

	int dx = event->x - x_scroll_start;

	if (scroll_btn != 1)
		return FALSE;

	if (!scroll_btn)
		return TRUE;

	if (dx < -10) {
		g_signal_emit_by_name(G_OBJECT(win), "scroll-child", GTK_SCROLL_STEP_FORWARD, TRUE, NULL);
		x_scroll_start = event->x;
		gdk_window_set_cursor(gtk_widget_get_parent_window(GTK_WIDGET(win)),
				      gdk_cursor_new(GDK_SB_H_DOUBLE_ARROW));
	}

	if (dx > 10) {
		g_signal_emit_by_name(G_OBJECT(win), "scroll-child", GTK_SCROLL_STEP_BACKWARD, TRUE, NULL);
		x_scroll_start = event->x;
		gdk_window_set_cursor(gtk_widget_get_parent_window(GTK_WIDGET(win)),
				      gdk_cursor_new(GDK_SB_H_DOUBLE_ARROW));
	}

	x_scroll_pos = event->x;

	return TRUE;
}

#ifdef HAVE_LOOP
void
hide_loop_bar() {
	gtk_widget_hide(loop_bar);

	if (options.playlist_is_embedded && !playlist_on) {
		gtk_window_resize(GTK_WINDOW(main_window), main_size_x,
				  main_size_y - 14 - 6 - 6 -
				  playlist_window->allocation.height);
	}

	if (!options.playlist_is_embedded) {
		gtk_window_resize(GTK_WINDOW(main_window),
				  main_size_x, main_size_y - 14 - 6);
	}
}
#endif /* HAVE_LOOP */

void
repeat_toggled(GtkWidget * widget, gpointer data) {

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(repeat_button))) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_all_button), FALSE);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button), FALSE);
		repeat_on = 1;
	} else {
		repeat_on = 0;
	}

#ifdef HAVE_LOOP
	if (repeat_on) {
		gtk_widget_show(loop_bar);
	} else {
		hide_loop_bar();
	}
#endif /* HAVE_LOOP */
}


void
repeat_all_toggled(GtkWidget * widget, gpointer data) {

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(repeat_all_button))) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button), FALSE);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button), FALSE);
		repeat_all_on = 1;
	} else {
		repeat_all_on = 0;
	}
}


void
shuffle_toggled(GtkWidget * widget, gpointer data) {

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shuffle_button))) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button), FALSE);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_all_button), FALSE);
		shuffle_on = 1;
	} else {
		shuffle_on = 0;
	}
}


GtkWidget *
create_button_with_image(char * path, int toggle, char * alt) {

        GtkWidget * button;
        GdkPixbuf * pixbuf;
        GtkWidget * image;

        gchar tmp_path[MAXLEN];

        sprintf(tmp_path, "%s.xpm", path);

        pixbuf = gdk_pixbuf_new_from_file(tmp_path, NULL);

        if (!pixbuf) {
                sprintf(tmp_path, "%s.png", path);
                pixbuf = gdk_pixbuf_new_from_file(tmp_path, NULL);
        }

	if (pixbuf) {
		image = gtk_image_new_from_pixbuf(pixbuf);
		
		if (toggle) {
			button = gtk_toggle_button_new();
		} else {
			button = gtk_button_new();
		}
		gtk_container_add(GTK_CONTAINER(button), image);
	} else {
		if (toggle) {
			button = gtk_toggle_button_new_with_label(alt);
		} else {
			button = gtk_button_new_with_label(alt);
		}
	}

        return button;
}


#ifdef HAVE_JACK
void
jack_shutdown_window(void) {

	GtkWidget * window;
	GtkWidget * label;

        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window), _("JACK connection lost"));
        gtk_container_set_border_width(GTK_CONTAINER(window), 10);

	label = gtk_label_new(_("JACK has either been shutdown or it\n\
disconnected Aqualung because it was\n\
not fast enough. All you can do now\n\
is restart both JACK and Aqualung.\n"));
        gtk_container_add(GTK_CONTAINER(window), label);
	gtk_widget_show_all(window);
}
#endif /* HAVE_JACK */


void
set_win_title(void) {

	char str_session_id[32];

	strcpy(win_title, "Aqualung");
	if (aqualung_session_id > 0) {
		sprintf(str_session_id, ".%d", aqualung_session_id);
		strcat(win_title, str_session_id);
	}
#ifdef HAVE_JACK
	if ((output == JACK_DRIVER) && (strcmp(client_name, "aqualung") != 0)) {
		strcat(win_title, " [");
		strcat(win_title, client_name);
		strcat(win_title, "]");
	}
#endif /* HAVE_JACK */
}


#ifdef HAVE_SYSTRAY
void
hide_all_windows(gpointer data) {
	/* Inverse operation of show_all_windows().
	 * note that hiding/showing multiple windows has to be done in a
	 * stack-like order, eg. hide main window last, show it first. */

	if (gtk_status_icon_is_embedded(systray_icon) == FALSE) {

		if (!warn_wm_not_systray_capable) {

			GtkWidget * dialog;

			dialog = gtk_message_dialog_new(options.playlist_is_embedded ? GTK_WINDOW(main_window) : GTK_WINDOW(playlist_window),
							GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
							GTK_MESSAGE_WARNING,
							GTK_BUTTONS_CLOSE,
							_("Aqualung is compiled with system tray support, but the status icon could not be embedded in the notification area. Your desktop may not have support for a system tray, or it has not been configured correctly."));

                        gtk_window_set_title(GTK_WINDOW(dialog), _("Warning"));
			gtk_widget_show(dialog);
			aqualung_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);

			warn_wm_not_systray_capable = 1;
		}

		return;
	}

	if (!systray_main_window_on) {
		return;
	}

	systray_main_window_on = 0;

	save_window_position();

	if (ripper_prog_window) {
		gtk_widget_hide(ripper_prog_window);
	}

	if (build_prog_window) {
		gtk_widget_hide(build_prog_window);
	}

	if (vol_window) {
		gtk_widget_hide(vol_window);
	}

	if (info_window) {
		gtk_widget_hide(info_window);
	}

	if (!options.playlist_is_embedded && playlist_on) {
		gtk_widget_hide(playlist_window);
	}

	if (browser_on) {
		gtk_widget_hide(browser_window);
	}

	if (fxbuilder_on) {
		gtk_widget_hide(fxbuilder_window);
	}

	gtk_widget_hide(main_window);

	gtk_widget_hide(systray__hide);
	gtk_widget_show(systray__show);
}

void
show_all_windows(gpointer data) {

	gtk_widget_show(main_window);
	systray_main_window_on = 1;
	deflicker();

	if (!options.playlist_is_embedded && playlist_on) {
		gtk_widget_show(playlist_window);
		deflicker();
	}

	if (browser_on) {
		gtk_widget_show(browser_window);
		deflicker();
	}

	if (fxbuilder_on) {
		gtk_widget_show(fxbuilder_window);
		deflicker();
	}

	if (info_window) {
		gtk_widget_show(info_window);
		deflicker();
	}

	if (vol_window) {
		gtk_widget_show(vol_window);
		deflicker();
	}

	if (build_prog_window) {
		gtk_widget_show(build_prog_window);
		deflicker();
	}

	if (ripper_prog_window) {
		gtk_widget_show(ripper_prog_window);
		deflicker();
	}

	restore_window_position();

	gtk_widget_hide(systray__show);
	gtk_widget_show(systray__hide);
}
#endif /* HAVE_SYSTRAY */

gboolean    
cover_press_button_cb (GtkWidget *widget, GdkEventButton *event, gpointer user_data) {

        GtkTreePath * p;
	GtkTreeIter iter;
	char * title_str;

	if (event->type == GDK_BUTTON_PRESS && event->button == 1) { /* LMB ? */
                if (play_store) {
                        p = get_playing_path(play_store);
                        if (p != NULL) {
                                gtk_tree_model_get_iter(GTK_TREE_MODEL(play_store), &iter, p);
                                gtk_tree_path_free(p);
                                if (is_file_loaded) {
                                        gtk_tree_model_get(GTK_TREE_MODEL(play_store), &iter, 1, &title_str, -1);
                                        display_zoomed_cover(main_window, c_event_box, title_str);
                                        g_free(title_str);
                                }
                        }
                }
        }
        return TRUE;
}    

void
create_main_window(char * skin_path) {

	GtkWidget * vbox;
	GtkWidget * disp_hbox;
	GtkWidget * btns_hbox;
	GtkWidget * title_hbox;
	GtkWidget * info_hbox;
	GtkWidget * vb_table;

	GtkWidget * conf__separator1;
	GtkWidget * conf__separator2;

	GtkWidget * time_table;
	GtkWidget * time0_viewp;
	GtkWidget * time1_viewp;
	GtkWidget * time2_viewp;
	GtkWidget * time_hbox1;
	GtkWidget * time_hbox2;

	GtkWidget * disp_vbox;
	GtkWidget * title_viewp;
	GtkWidget * title_scrolledwin;
	GtkWidget * info_viewp;
	GtkWidget * info_scrolledwin;
	GtkWidget * info_vsep;

	GtkWidget * sr_table;

	char path[MAXLEN];

	set_win_title();
	main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_name(main_window, "main_window");
	gtk_window_set_title(GTK_WINDOW(main_window), win_title);
#ifdef HAVE_SYSTRAY
	gtk_status_icon_set_tooltip(systray_icon, win_title);
#endif /* HAVE_SYSTRAY */

#ifdef HAVE_SYSTRAY
        g_signal_connect(G_OBJECT(main_window), "delete_event", G_CALLBACK(hide_all_windows), NULL);
#else
        g_signal_connect(G_OBJECT(main_window), "delete_event", G_CALLBACK(main_window_close), NULL);
#endif /* HAVE_SYSTRAY */

        g_signal_connect(G_OBJECT(main_window), "key_press_event", G_CALLBACK(main_window_key_pressed), NULL);
        g_signal_connect(G_OBJECT(main_window), "key_release_event",
			 G_CALLBACK(main_window_key_released), NULL);
        g_signal_connect(G_OBJECT(main_window), "button_press_event",
			 G_CALLBACK(main_window_button_pressed), NULL);
        g_signal_connect(G_OBJECT(main_window), "focus_out_event",
                         G_CALLBACK(main_window_focus_out), NULL);
        g_signal_connect(G_OBJECT(main_window), "window-state-event",
                         G_CALLBACK(main_window_state_changed), NULL);
	
	gtk_widget_set_events(main_window, GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
	gtk_container_set_border_width(GTK_CONTAINER(main_window), 5);

        /* always on top ? */

	if (options.main_window_always_on_top) {
                gtk_window_set_keep_above (GTK_WINDOW(main_window), TRUE);
        }

        /* initialize fonts */

	fd_playlist = pango_font_description_from_string(options.playlist_font);
 	fd_browser = pango_font_description_from_string(options.browser_font);
 	fd_bigtimer = pango_font_description_from_string(options.bigtimer_font);
 	fd_smalltimer = pango_font_description_from_string(options.smalltimer_font);
 	fd_songtitle = pango_font_description_from_string(options.songtitle_font);
 	fd_songinfo = pango_font_description_from_string(options.songinfo_font);
 	fd_statusbar = pango_font_description_from_string(options.statusbar_font);

        aqualung_tooltips = gtk_tooltips_new();

        conf_menu = gtk_menu_new();

        if (options.playlist_is_embedded) {
                init_plist_menu(conf_menu);
                plist_menu = conf_menu;
        }

        conf__options = gtk_menu_item_new_with_label(_("Settings"));
        conf__skin = gtk_menu_item_new_with_label(_("Skin chooser"));
        conf__jack = gtk_menu_item_new_with_label(_("JACK port setup"));
        if (!options.playlist_is_embedded) {
                conf__fileinfo = gtk_menu_item_new_with_label(_("File info"));
	}
        conf__separator1 = gtk_separator_menu_item_new();
        conf__about = gtk_menu_item_new_with_label(_("About"));
        conf__separator2 = gtk_separator_menu_item_new();
        conf__quit = gtk_menu_item_new_with_label(_("Quit"));

        if (options.playlist_is_embedded) {
                gtk_menu_shell_append(GTK_MENU_SHELL(conf_menu), conf__separator1);
	}
        gtk_menu_shell_append(GTK_MENU_SHELL(conf_menu), conf__options);
        gtk_menu_shell_append(GTK_MENU_SHELL(conf_menu), conf__skin);

#ifdef HAVE_JACK
        if (output == JACK_DRIVER) {
                gtk_menu_shell_append(GTK_MENU_SHELL(conf_menu), conf__jack);
        }
#endif /* HAVE_JACK */

        if (!options.playlist_is_embedded) {
                gtk_menu_shell_append(GTK_MENU_SHELL(conf_menu), conf__fileinfo);
                gtk_menu_shell_append(GTK_MENU_SHELL(conf_menu), conf__separator1);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(conf_menu), conf__about);
	gtk_menu_shell_append(GTK_MENU_SHELL(conf_menu), conf__separator2);
        gtk_menu_shell_append(GTK_MENU_SHELL(conf_menu), conf__quit);

        g_signal_connect_swapped(G_OBJECT(conf__options), "activate", G_CALLBACK(conf__options_cb), NULL);
        g_signal_connect_swapped(G_OBJECT(conf__skin), "activate", G_CALLBACK(conf__skin_cb), NULL);
        g_signal_connect_swapped(G_OBJECT(conf__jack), "activate", G_CALLBACK(conf__jack_cb), NULL);

        g_signal_connect_swapped(G_OBJECT(conf__about), "activate", G_CALLBACK(conf__about_cb), NULL);
        g_signal_connect_swapped(G_OBJECT(conf__quit), "activate", G_CALLBACK(conf__quit_cb), NULL);

        if (!options.playlist_is_embedded) {
                g_signal_connect_swapped(G_OBJECT(conf__fileinfo), "activate", G_CALLBACK(conf__fileinfo_cb), NULL);
                gtk_widget_set_sensitive(conf__fileinfo, FALSE);
                gtk_widget_show(conf__fileinfo);
        }

        gtk_widget_show(conf__options);
        gtk_widget_show(conf__skin);
        gtk_widget_show(conf__jack);
        gtk_widget_show(conf__separator1);
        gtk_widget_show(conf__about);
        gtk_widget_show(conf__separator2);
        gtk_widget_show(conf__quit);


	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(main_window), vbox);


	disp_hbox = gtk_hbox_new(FALSE, 3);
	gtk_box_pack_start(GTK_BOX(vbox), disp_hbox, FALSE, FALSE, 0);


	time_table = gtk_table_new(2, 2, FALSE);
	disp_vbox = gtk_vbox_new(FALSE, 0);

	gtk_box_pack_start(GTK_BOX(disp_hbox), time_table, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(disp_hbox), disp_vbox, TRUE, TRUE, 0);

	time0_viewp = gtk_viewport_new(NULL, NULL);
        time1_viewp = gtk_viewport_new(NULL, NULL);
	time2_viewp = gtk_viewport_new(NULL, NULL);

	gtk_widget_set_name(time0_viewp, "time_viewport");
	gtk_widget_set_name(time1_viewp, "time_viewport");
	gtk_widget_set_name(time2_viewp, "time_viewport");

	g_signal_connect(G_OBJECT(time0_viewp), "button_press_event", G_CALLBACK(time_label0_clicked), NULL);
	g_signal_connect(G_OBJECT(time1_viewp), "button_press_event", G_CALLBACK(time_label1_clicked), NULL);
	g_signal_connect(G_OBJECT(time2_viewp), "button_press_event", G_CALLBACK(time_label2_clicked), NULL);


	gtk_table_attach(GTK_TABLE(time_table), time0_viewp, 0, 2, 0, 1,
			 GTK_FILL | GTK_EXPAND, 0, 0, 0);
	gtk_table_attach(GTK_TABLE(time_table), time1_viewp, 0, 1, 1, 2,
			 GTK_FILL | GTK_EXPAND, 0, 0, 0);
	gtk_table_attach(GTK_TABLE(time_table), time2_viewp, 1, 2, 1, 2,
			 GTK_FILL | GTK_EXPAND, 0, 0, 0);


	info_scrolledwin = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_size_request(info_scrolledwin, 1, -1);           /* MAGIC */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(info_scrolledwin),
				       GTK_POLICY_NEVER, GTK_POLICY_NEVER);

	info_viewp = gtk_viewport_new(NULL, NULL);
	gtk_widget_set_name(info_viewp, "info_viewport");
	gtk_container_add(GTK_CONTAINER(info_scrolledwin), info_viewp);
	gtk_widget_set_events(info_viewp, GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

        g_signal_connect(G_OBJECT(info_viewp), "button_press_event",
			 G_CALLBACK(scroll_btn_pressed), NULL);
        g_signal_connect(G_OBJECT(info_viewp), "button_release_event",
			 G_CALLBACK(scroll_btn_released), (gpointer)info_scrolledwin);
        g_signal_connect(G_OBJECT(info_viewp), "motion_notify_event",
			 G_CALLBACK(scroll_motion_notify), (gpointer)info_scrolledwin);

	info_hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(info_hbox), 1);
	gtk_container_add(GTK_CONTAINER(info_viewp), info_hbox);


	title_scrolledwin = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_size_request(title_scrolledwin, 1, -1);          /* MAGIC */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(title_scrolledwin),
				       GTK_POLICY_NEVER, GTK_POLICY_NEVER);

	title_viewp = gtk_viewport_new(NULL, NULL);
	gtk_widget_set_name(title_viewp, "title_viewport");
	gtk_container_add(GTK_CONTAINER(title_scrolledwin), title_viewp);
	gtk_widget_set_events(title_viewp, GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
	g_signal_connect(G_OBJECT(title_viewp), "button_press_event", G_CALLBACK(scroll_btn_pressed), NULL);
	g_signal_connect(G_OBJECT(title_viewp), "button_release_event", G_CALLBACK(scroll_btn_released),
			 (gpointer)title_scrolledwin);
	g_signal_connect(G_OBJECT(title_viewp), "motion_notify_event", G_CALLBACK(scroll_motion_notify),
			 (gpointer)title_scrolledwin);

	title_hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(title_hbox), 1);
	gtk_container_add(GTK_CONTAINER(title_viewp), title_hbox);

	gtk_box_pack_start(GTK_BOX(disp_vbox), title_scrolledwin, TRUE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(disp_vbox), info_scrolledwin, TRUE, FALSE, 0);

	/* labels */
	bigtimer_label = time_labels[time_idx[0]] = gtk_label_new("");

        if (options.override_skin_settings) {
                gtk_widget_modify_font (bigtimer_label, fd_bigtimer);
        }

        gtk_widget_set_name(time_labels[time_idx[0]], "big_timer_label");
	gtk_container_add(GTK_CONTAINER(time0_viewp), time_labels[time_idx[0]]);

	time_hbox1 = gtk_hbox_new(FALSE, 3);
	gtk_container_set_border_width(GTK_CONTAINER(time_hbox1), 2);
	gtk_container_add(GTK_CONTAINER(time1_viewp), time_hbox1);

	smalltimer_label_1 = time_labels[time_idx[1]] = gtk_label_new("");

        if (options.override_skin_settings) {
                gtk_widget_modify_font (smalltimer_label_1, fd_smalltimer);
        }

        gtk_widget_set_name(time_labels[time_idx[1]], "small_timer_label");
	gtk_box_pack_start(GTK_BOX(time_hbox1), time_labels[time_idx[1]], TRUE, TRUE, 0);

	time_hbox2 = gtk_hbox_new(FALSE, 3);
	gtk_container_set_border_width(GTK_CONTAINER(time_hbox2), 2);
	gtk_container_add(GTK_CONTAINER(time2_viewp), time_hbox2);

	smalltimer_label_2 = time_labels[time_idx[2]] = gtk_label_new("");

        if (options.override_skin_settings) {
                gtk_widget_modify_font (smalltimer_label_2, fd_smalltimer);
        }

        gtk_widget_set_name(time_labels[time_idx[2]], "small_timer_label");
	gtk_box_pack_start(GTK_BOX(time_hbox2), time_labels[time_idx[2]], TRUE, TRUE, 0);

	label_title = gtk_label_new("");
        gtk_widget_set_name(label_title, "label_title");
	gtk_box_pack_start(GTK_BOX(title_hbox), label_title, FALSE, FALSE, 3);
	
	label_mono = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(info_hbox), label_mono, FALSE, FALSE, 3);
	gtk_widget_set_name(label_mono, "label_info");

	label_samplerate = gtk_label_new("0");
	gtk_widget_set_name(label_samplerate, "label_info");
	gtk_box_pack_start(GTK_BOX(info_hbox), label_samplerate, FALSE, FALSE, 3);

	label_bps = gtk_label_new("");
	gtk_widget_set_name(label_bps, "label_info");
	gtk_box_pack_start(GTK_BOX(info_hbox), label_bps, FALSE, FALSE, 3);

	label_format = gtk_label_new("");
	gtk_widget_set_name(label_format, "label_info");
	gtk_box_pack_start(GTK_BOX(info_hbox), label_format, FALSE, FALSE, 3);

	info_vsep = gtk_vseparator_new();
	gtk_box_pack_start(GTK_BOX(info_hbox), info_vsep, FALSE, FALSE, 3);

	label_output = gtk_label_new("");
	gtk_widget_set_name(label_output, "label_info");
	gtk_box_pack_start(GTK_BOX(info_hbox), label_output, FALSE, FALSE, 3);

	label_src_type = gtk_label_new("");
	gtk_widget_set_name(label_src_type, "label_info");
	gtk_box_pack_start(GTK_BOX(info_hbox), label_src_type, FALSE, FALSE, 3);

        if (options.override_skin_settings) {
                gtk_widget_modify_font (label_title, fd_songtitle);

                gtk_widget_modify_font (label_mono, fd_songinfo);
                gtk_widget_modify_font (label_samplerate, fd_songinfo);
                gtk_widget_modify_font (label_bps, fd_songinfo);
                gtk_widget_modify_font (label_format, fd_songinfo);
                gtk_widget_modify_font (label_output, fd_songinfo);
                gtk_widget_modify_font (label_src_type, fd_songinfo);
        }

	/* Volume and balance slider */
	vb_table = gtk_table_new(1, 3, FALSE);
        gtk_table_set_col_spacings(GTK_TABLE(vb_table), 3);
	gtk_box_pack_start(GTK_BOX(disp_vbox), vb_table, TRUE, FALSE, 0);

	adj_vol = gtk_adjustment_new(0.0f, -41.0f, 6.0f, 1.0f, 3.0f, 0.0f);
	g_signal_connect(G_OBJECT(adj_vol), "value_changed", G_CALLBACK(changed_vol), NULL);
	scale_vol = gtk_hscale_new(GTK_ADJUSTMENT(adj_vol));
	gtk_widget_set_name(scale_vol, "scale_vol");
	g_signal_connect(GTK_OBJECT(scale_vol), "button_press_event",
			   (GtkSignalFunc)scale_vol_button_press_event, NULL);
	g_signal_connect(GTK_OBJECT(scale_vol), "button_release_event",
			   (GtkSignalFunc)scale_vol_button_release_event, NULL);
	gtk_widget_set_size_request(scale_vol, -1, 8);
	gtk_scale_set_digits(GTK_SCALE(scale_vol), 0);
	gtk_scale_set_draw_value(GTK_SCALE(scale_vol), FALSE);
	gtk_table_attach(GTK_TABLE(vb_table), scale_vol, 0, 2, 0, 1,
			 GTK_FILL | GTK_EXPAND, 0, 0, 0);

        adj_bal = gtk_adjustment_new(0.0f, -100.0f, 100.0f, 1.0f, 10.0f, 0.0f);
        g_signal_connect(G_OBJECT(adj_bal), "value_changed", G_CALLBACK(changed_bal), NULL);
        scale_bal = gtk_hscale_new(GTK_ADJUSTMENT(adj_bal));
	gtk_scale_set_digits(GTK_SCALE(scale_bal), 0);
	gtk_widget_set_size_request(scale_bal, -1, 8);
	gtk_widget_set_name(scale_bal, "scale_bal");
	g_signal_connect(GTK_OBJECT(scale_bal), "button_press_event",
			   (GtkSignalFunc)scale_bal_button_press_event, NULL);
	g_signal_connect(GTK_OBJECT(scale_bal), "button_release_event",
			   (GtkSignalFunc)scale_bal_button_release_event, NULL);
        gtk_scale_set_draw_value(GTK_SCALE(scale_bal), FALSE);
	gtk_table_attach(GTK_TABLE(vb_table), scale_bal, 2, 3, 0, 1,
			 GTK_FILL | GTK_EXPAND, 0, 0, 0);

	/* Loop bar */

#ifdef HAVE_LOOP
	loop_bar = aqualung_loop_bar_new(loop_range_start, loop_range_end);
	g_signal_connect(loop_bar, "range-changed", G_CALLBACK(loop_range_changed_cb), NULL);
	gtk_widget_set_size_request(loop_bar, -1, 14);
	gtk_box_pack_start(GTK_BOX(vbox), loop_bar, FALSE, FALSE, 3);
#endif /* HAVE_LOOP */


	/* Position slider */
        adj_pos = gtk_adjustment_new(0, 0, 100, 1, 5, 0);
        g_signal_connect(G_OBJECT(adj_pos), "value_changed", G_CALLBACK(changed_pos), NULL);
        scale_pos = gtk_hscale_new(GTK_ADJUSTMENT(adj_pos));
	gtk_widget_set_name(scale_pos, "scale_pos");
	g_signal_connect(GTK_OBJECT(scale_pos), "button_press_event",
			   (GtkSignalFunc)scale_button_press_event, NULL);
	g_signal_connect(GTK_OBJECT(scale_pos), "button_release_event",
			   (GtkSignalFunc)scale_button_release_event, NULL);
	gtk_scale_set_digits(GTK_SCALE(scale_pos), 0);
        gtk_scale_set_draw_value(GTK_SCALE(scale_pos), FALSE);
	gtk_range_set_update_policy(GTK_RANGE(scale_pos), GTK_UPDATE_DISCONTINUOUS);
	gtk_box_pack_start(GTK_BOX(vbox), scale_pos, FALSE, FALSE, 3);

        GTK_WIDGET_UNSET_FLAGS(scale_vol, GTK_CAN_FOCUS);
        GTK_WIDGET_UNSET_FLAGS(scale_bal, GTK_CAN_FOCUS);
        GTK_WIDGET_UNSET_FLAGS(scale_pos, GTK_CAN_FOCUS);

        /* cover display widget */

	cover_align = gtk_alignment_new(0.5f, 0.5f, 0.0f, 0.0f);
	gtk_box_pack_start(GTK_BOX(disp_hbox), cover_align, FALSE, FALSE, 0);
        cover_image_area = gtk_image_new();
        c_event_box = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(cover_align), c_event_box);
        gtk_container_add(GTK_CONTAINER(c_event_box), cover_image_area);
        g_signal_connect(G_OBJECT(c_event_box), "button_press_event",
                         G_CALLBACK(cover_press_button_cb), cover_image_area);

        /* Embedded playlist */

        if (options.playlist_is_embedded && options.buttons_at_the_bottom) {
		playlist_window = gtk_vbox_new(FALSE, 0);
		gtk_widget_set_name(playlist_window, "playlist_window");
		gtk_box_pack_start(GTK_BOX(vbox), playlist_window, TRUE, TRUE, 3);
	}

        /* Button box with prev, play, pause, stop, next buttons */

	btns_hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), btns_hbox, FALSE, FALSE, 0);

	sprintf(path, "%s/%s", skin_path, "prev");
	prev_button = create_button_with_image(path, 0, "prev");
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), prev_button, _("Previous song"), NULL);

	sprintf(path, "%s/%s", skin_path, "stop");
	stop_button = create_button_with_image(path, 0, "stop");
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), stop_button, _("Stop"), NULL);

	sprintf(path, "%s/%s", skin_path, "next");
	next_button = create_button_with_image(path, 0, "next");
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), next_button, _("Next song"), NULL);

	sprintf(path, "%s/%s", skin_path, "play");
	play_button = create_button_with_image(path, 1, "play");
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), play_button, _("Play"), NULL);

	sprintf(path, "%s/%s", skin_path, "pause");
	pause_button = create_button_with_image(path, 1, "pause");
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), pause_button, _("Pause"), NULL);

	GTK_WIDGET_UNSET_FLAGS(prev_button, GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS(stop_button, GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS(next_button, GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS(play_button, GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS(pause_button, GTK_CAN_FOCUS);

	gtk_box_pack_start(GTK_BOX(btns_hbox), prev_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(btns_hbox), play_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(btns_hbox), pause_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(btns_hbox), stop_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(btns_hbox), next_button, FALSE, FALSE, 0);

	g_signal_connect(prev_button, "clicked", G_CALLBACK(prev_event), NULL);
	play_id = g_signal_connect(play_button, "toggled", G_CALLBACK(play_event), NULL);
	pause_id = g_signal_connect(pause_button, "toggled", G_CALLBACK(pause_event), NULL);
	g_signal_connect(stop_button, "clicked", G_CALLBACK(stop_event), NULL);
	g_signal_connect(next_button, "clicked", G_CALLBACK(next_event), NULL);


	/* toggle buttons for shuffle and repeat */
	sr_table = gtk_table_new(2, 2, FALSE);

	sprintf(path, "%s/%s", skin_path, "repeat");
	repeat_button = create_button_with_image(path, 1, "repeat");
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), repeat_button, _("Repeat current song"), NULL);
	gtk_widget_set_size_request(repeat_button, -1, 1);
	gtk_table_attach(GTK_TABLE(sr_table), repeat_button, 0, 1, 0, 1,
			 GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	g_signal_connect(repeat_button, "toggled", G_CALLBACK(repeat_toggled), NULL);

	sprintf(path, "%s/%s", skin_path, "repeat_all");
	repeat_all_button = create_button_with_image(path, 1, "rep_all");
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), repeat_all_button, _("Repeat all songs"), NULL);
	gtk_widget_set_size_request(repeat_all_button, -1, 1);
	gtk_table_attach(GTK_TABLE(sr_table), repeat_all_button, 0, 1, 1, 2,
			 GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	g_signal_connect(repeat_all_button, "toggled", G_CALLBACK(repeat_all_toggled), NULL);

	sprintf(path, "%s/%s", skin_path, "shuffle");
	shuffle_button = create_button_with_image(path, 1, "shuffle");
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), shuffle_button, _("Shuffle songs"), NULL);
	gtk_widget_set_size_request(shuffle_button, -1, 1);
	gtk_table_attach(GTK_TABLE(sr_table), shuffle_button, 1, 2, 0, 2,
			 GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	g_signal_connect(shuffle_button, "toggled", G_CALLBACK(shuffle_toggled), NULL);

	GTK_WIDGET_UNSET_FLAGS(repeat_button, GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS(repeat_all_button, GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS(shuffle_button, GTK_CAN_FOCUS);

        /* toggle buttons for sub-windows visibility */
	sprintf(path, "%s/%s", skin_path, "pl");
	playlist_toggle = create_button_with_image(path, 1, "PL");
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), playlist_toggle, _("Toggle playlist"), NULL);
	gtk_box_pack_end(GTK_BOX(btns_hbox), playlist_toggle, FALSE, FALSE, 0);
	
	sprintf(path, "%s/%s", skin_path, "ms");
	musicstore_toggle = create_button_with_image(path, 1, "MS");
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), musicstore_toggle, _("Toggle music store"), NULL);
	gtk_box_pack_end(GTK_BOX(btns_hbox), musicstore_toggle, FALSE, FALSE, 3);

	sprintf(path, "%s/%s", skin_path, "fx");
	plugin_toggle = create_button_with_image(path, 1, "FX");
        gtk_tooltips_set_tip (GTK_TOOLTIPS (aqualung_tooltips), plugin_toggle, _("Toggle LADSPA patch builder"), NULL);
#ifdef HAVE_LADSPA
	gtk_box_pack_end(GTK_BOX(btns_hbox), plugin_toggle, FALSE, FALSE, 0);
#endif /* HAVE_LADSPA */

	g_signal_connect(playlist_toggle, "toggled", G_CALLBACK(playlist_toggled), NULL);
	g_signal_connect(musicstore_toggle, "toggled", G_CALLBACK(musicstore_toggled), NULL);
	g_signal_connect(plugin_toggle, "toggled", G_CALLBACK(plugin_toggled), NULL);

	GTK_WIDGET_UNSET_FLAGS(playlist_toggle, GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS(musicstore_toggle, GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS(plugin_toggle, GTK_CAN_FOCUS);

	gtk_box_pack_end(GTK_BOX(btns_hbox), sr_table, FALSE, FALSE, 3);

        set_buttons_relief();

	/* Embedded playlist */
	if (options.playlist_is_embedded && !options.buttons_at_the_bottom) {
		playlist_window = gtk_vbox_new(FALSE, 0);
		gtk_widget_set_name(playlist_window, "playlist_window");
		gtk_box_pack_start(GTK_BOX(vbox), playlist_window, TRUE, TRUE, 3);
	}

        if (options.enable_tooltips) {
                gtk_tooltips_enable(aqualung_tooltips);
        } else {
                gtk_tooltips_disable(aqualung_tooltips);
        }
}


void
process_filenames(char ** argv, int optind, int enqueue) {
	
	int i;
	
	for (i = optind; argv[i] != NULL; i++) {
		if ((enqueue) || (i > optind)) {
			add_to_playlist(argv[i], 1);
		} else {
			add_to_playlist(argv[i], 0);
		}
	}
}	


/*** Systray support ***/
#ifdef HAVE_SYSTRAY
void
systray_popup_menu_cb(GtkStatusIcon * systray_icon, guint button, guint time, gpointer data) {

	if (systray_semaphore == 0) {
		gtk_menu_popup(GTK_MENU(systray_menu), NULL, NULL,
			       gtk_status_icon_position_menu, data,
			       button, time);
	}
}

void
systray__show_cb(gpointer data) {

	show_all_windows(NULL);
}

void
systray__hide_cb(gpointer data) {

	hide_all_windows(NULL);
}

void
systray__play_cb(gpointer data) {

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(play_button),
				     !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(play_button)));
}

void
systray__pause_cb(gpointer data) {

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pause_button),
				     !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pause_button)));
}

void
systray__stop_cb(gpointer data) {

	stop_event(NULL, NULL, NULL);
}

void
systray__prev_cb(gpointer data) {

	prev_event(NULL, NULL, NULL);
}

void
systray__next_cb(gpointer data) {

	next_event(NULL, NULL, NULL);
}

void
systray__quit_cb(gpointer data) {

	main_window_close(NULL, NULL);
}

void
systray_activate_cb(GtkStatusIcon * systray_icon, gpointer data) {

	if (systray_semaphore == 0) {
		if (!systray_main_window_on) {
			systray__show_cb(NULL);
		} else {
			systray__hide_cb(NULL);
		}
	}
}


/* returns a hbox with a stock image and label in it */
GtkWidget *
create_systray_menu_item(const gchar * stock_id, char * text) {

	GtkWidget * hbox;
	GtkWidget * widget;

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_widget_show(hbox);
	widget = gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_MENU);
	gtk_widget_show(widget);
	gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, TRUE, 2);
	widget = gtk_label_new(text);
	gtk_widget_show(widget);
	gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, TRUE, 2);

	return hbox;
}

void
setup_systray(void) {

	char path[MAXLEN];

	GtkWidget * systray__separator1;
	GtkWidget * systray__separator2;

	sprintf(path, "%s/icon_64.png", AQUALUNG_DATADIR);
	systray_icon = gtk_status_icon_new_from_file(path);

        g_signal_connect_swapped(G_OBJECT(systray_icon), "activate",
				 G_CALLBACK(systray_activate_cb), NULL);
        g_signal_connect_swapped(G_OBJECT(systray_icon), "popup-menu",
				 G_CALLBACK(systray_popup_menu_cb), (gpointer)systray_icon);

        systray_menu = gtk_menu_new();

        systray__show = gtk_menu_item_new_with_label(_("Show Aqualung"));
        systray__hide = gtk_menu_item_new_with_label(_("Hide Aqualung"));

        systray__separator1 = gtk_separator_menu_item_new();

        systray__play = gtk_menu_item_new();
	gtk_container_add(GTK_CONTAINER(systray__play),
			  create_systray_menu_item(GTK_STOCK_MEDIA_PLAY, _("Play")));

        systray__pause = gtk_menu_item_new();
	gtk_container_add(GTK_CONTAINER(systray__pause),
			  create_systray_menu_item(GTK_STOCK_MEDIA_PAUSE, _("Pause")));

        systray__stop = gtk_menu_item_new();
	gtk_container_add(GTK_CONTAINER(systray__stop),
			  create_systray_menu_item(GTK_STOCK_MEDIA_STOP, _("Stop")));

        systray__prev = gtk_menu_item_new();
	gtk_container_add(GTK_CONTAINER(systray__prev),
			  create_systray_menu_item(GTK_STOCK_MEDIA_PREVIOUS, _("Previous")));

        systray__next = gtk_menu_item_new();
	gtk_container_add(GTK_CONTAINER(systray__next),
			  create_systray_menu_item(GTK_STOCK_MEDIA_NEXT, _("Next")));

        systray__separator2 = gtk_separator_menu_item_new();

        systray__quit = gtk_menu_item_new();
	gtk_container_add(GTK_CONTAINER(systray__quit),
			  create_systray_menu_item(GTK_STOCK_STOP, _("Quit")));
	
        gtk_menu_shell_append(GTK_MENU_SHELL(systray_menu), systray__show);
        gtk_menu_shell_append(GTK_MENU_SHELL(systray_menu), systray__hide);
        gtk_menu_shell_append(GTK_MENU_SHELL(systray_menu), systray__separator1);
        gtk_menu_shell_append(GTK_MENU_SHELL(systray_menu), systray__play);
        gtk_menu_shell_append(GTK_MENU_SHELL(systray_menu), systray__pause);
        gtk_menu_shell_append(GTK_MENU_SHELL(systray_menu), systray__stop);
        gtk_menu_shell_append(GTK_MENU_SHELL(systray_menu), systray__prev);
        gtk_menu_shell_append(GTK_MENU_SHELL(systray_menu), systray__next);
        gtk_menu_shell_append(GTK_MENU_SHELL(systray_menu), systray__separator2);
        gtk_menu_shell_append(GTK_MENU_SHELL(systray_menu), systray__quit);

        g_signal_connect_swapped(G_OBJECT(systray__show), "activate", G_CALLBACK(systray__show_cb), NULL);
        g_signal_connect_swapped(G_OBJECT(systray__hide), "activate", G_CALLBACK(systray__hide_cb), NULL);
        g_signal_connect_swapped(G_OBJECT(systray__play), "activate", G_CALLBACK(systray__play_cb), NULL);
        g_signal_connect_swapped(G_OBJECT(systray__pause), "activate", G_CALLBACK(systray__pause_cb), NULL);
        g_signal_connect_swapped(G_OBJECT(systray__stop), "activate", G_CALLBACK(systray__stop_cb), NULL);
        g_signal_connect_swapped(G_OBJECT(systray__prev), "activate", G_CALLBACK(systray__prev_cb), NULL);
        g_signal_connect_swapped(G_OBJECT(systray__next), "activate", G_CALLBACK(systray__next_cb), NULL);
        g_signal_connect_swapped(G_OBJECT(systray__quit), "activate", G_CALLBACK(systray__quit_cb), NULL);

        gtk_widget_show(systray_menu);
        gtk_widget_show(systray__hide);
        gtk_widget_show(systray__separator1);
	gtk_widget_show(systray__play);
	gtk_widget_show(systray__pause);
	gtk_widget_show(systray__stop);
	gtk_widget_show(systray__prev);
	gtk_widget_show(systray__next);
	
        gtk_widget_show(systray__separator2);
        gtk_widget_show(systray__quit);
}
#endif /* HAVE_SYSTRAY */


void
create_gui(int argc, char ** argv, int optind, int enqueue,
	   unsigned long rate, unsigned long rb_audio_size) {

	char path[MAXLEN];
	GList * glist = NULL;
	GdkPixbuf * pixbuf = NULL;
        GdkColor color;

	srand(time(0));
	sample_pos = 0;
	out_SR = rate;
	rb_size = rb_audio_size;

	gtk_init(&argc, &argv);
#ifdef HAVE_LADSPA
	lrdf_init();
#endif /* HAVE_LADSPA */

	if (chdir(options.confdir) != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "Creating directory %s\n", options.confdir);
			mkdir(options.confdir, S_IRUSR | S_IWUSR | S_IXUSR);
			chdir(options.confdir);
		} else {
			fprintf(stderr, "An error occured while attempting chdir(\"%s\"). errno = %d\n",
				options.confdir, errno);
		}
	}

	load_config();

	if (options.title_format[0] == '\0')
		sprintf(options.title_format, "%%a: %%t [%%r]");
	if (options.skin[0] == '\0') {
		sprintf(options.skin, "%s/plain", AQUALUNG_SKINDIR);
		main_pos_x = 280;
		main_pos_y = 30;
		main_size_x = 380;
		main_size_y = 380;
		browser_pos_x = 30;
		browser_pos_y = 30;
		browser_size_x = 240;
		browser_size_y = 380;
		browser_on = 1;
		playlist_pos_x = 300;
		playlist_pos_y = 180;
		playlist_size_x = 400;
		playlist_size_y = 500;
		playlist_on = 1;
	}

	if (options.cddb_server[0] == '\0') {
		sprintf(options.cddb_server, "freedb.org");
	}

	if (src_type == -1) {
		src_type = 4;
	}

	sprintf(path, "%s/rc", options.skin);
	gtk_rc_parse(path);

#ifdef HAVE_SYSTRAY
	setup_systray();
#endif /* HAVE_SYSTRAY */

	create_main_window(options.skin);

	vol_prev = -101.0f;
	gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_vol), vol);
	bal_prev = -101.0f;
	gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_bal), bal);

	create_playlist();

        playlist_size_allocate(NULL, NULL);

	create_music_browser();

#ifdef HAVE_LADSPA
	create_fxbuilder();
	load_plugin_data();
#endif /* HAVE_LADSPA */

	load_all_music_store();
#ifdef HAVE_CDDA
	create_cdda_node();
	cdda_scanner_start();
#endif /* HAVE_CDDA */

	if (options.auto_save_playlist) {
		char playlist_name[MAXLEN];

		snprintf(playlist_name, MAXLEN-1, "%s/%s", options.confdir, "playlist.xml");
		load_playlist(playlist_name, 0);
	}

	sprintf(path, "%s/icon_16.png", AQUALUNG_DATADIR);
	if ((pixbuf = gdk_pixbuf_new_from_file(path, NULL)) != NULL) {
		glist = g_list_append(glist, gdk_pixbuf_new_from_file(path, NULL));
	}

	sprintf(path, "%s/icon_24.png", AQUALUNG_DATADIR);
	if ((pixbuf = gdk_pixbuf_new_from_file(path, NULL)) != NULL) {
		glist = g_list_append(glist, gdk_pixbuf_new_from_file(path, NULL));
	}

	sprintf(path, "%s/icon_32.png", AQUALUNG_DATADIR);
	if ((pixbuf = gdk_pixbuf_new_from_file(path, NULL)) != NULL) {
		glist = g_list_append(glist, gdk_pixbuf_new_from_file(path, NULL));
	}

	sprintf(path, "%s/icon_48.png", AQUALUNG_DATADIR);
	if ((pixbuf = gdk_pixbuf_new_from_file(path, NULL)) != NULL) {
		glist = g_list_append(glist, gdk_pixbuf_new_from_file(path, NULL));
	}

	sprintf(path, "%s/icon_64.png", AQUALUNG_DATADIR);
	if ((pixbuf = gdk_pixbuf_new_from_file(path, NULL)) != NULL) {
		glist = g_list_append(glist, gdk_pixbuf_new_from_file(path, NULL));
	}

	if (glist != NULL) {
		gtk_window_set_default_icon_list(glist);
	}

	if (repeat_on) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button), TRUE);
	}

	if (repeat_all_on) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_all_button), TRUE);
	}

	if (shuffle_on) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button), TRUE);
	}

        if (playlist_state != -1) {
                playlist_on = playlist_state;
	}

        if (browser_state != -1) {
                browser_on = browser_state;
	}

	if (browser_on) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(musicstore_toggle), TRUE);
		deflicker();
	}

	if (playlist_on) {
		if (options.playlist_is_embedded) {
			g_signal_handlers_block_by_func(G_OBJECT(playlist_toggle), playlist_toggled, NULL);
		}
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(playlist_toggle), TRUE);
		deflicker();
		if (options.playlist_is_embedded) {
			g_signal_handlers_unblock_by_func(G_OBJECT(playlist_toggle), playlist_toggled, NULL);
		}
	}

	restore_window_position();

	gtk_widget_show_all(main_window);

	deflicker();

        cover_show_flag = 0;
        gtk_widget_hide(cover_align);
        gtk_widget_hide(c_event_box);
        gtk_widget_hide(cover_image_area);

	if (options.playlist_is_embedded) {
		if (!playlist_on) {
                        hide_playlist();
			deflicker();
		}
	}

#ifdef HAVE_LOOP
	if (!repeat_on) {
		hide_loop_bar();
	}
#endif /* HAVE_LOOP */


        /* update sliders' tooltips */
        if (options.enable_tooltips) {
                changed_vol(GTK_ADJUSTMENT(adj_vol), NULL);
                changed_bal(GTK_ADJUSTMENT(adj_bal), NULL);
	        changed_pos(GTK_ADJUSTMENT(adj_pos), NULL);
        }

        /* change color of active song in playlist */
        if (options.override_skin_settings && (gdk_color_parse(options.activesong_color, &color) == TRUE)) {

                /* sorry for this, but it's temporary workaround */
                /* see playlist.c:1848 FIXME tag for details */

                if (!color.red && !color.green && !color.blue)
                        color.red++;

                play_list->style->fg[SELECTED].red = color.red;
                play_list->style->fg[SELECTED].green = color.green;
                play_list->style->fg[SELECTED].blue = color.blue;
        }

	zero_displays();
	set_playlist_color();

	/* read command line filenames */
	process_filenames(argv, optind, enqueue);

	/* activate jack client and connect ports */
#ifdef HAVE_JACK
	if (output == JACK_DRIVER) {
		jack_client_start();
	}
#endif /* HAVE_JACK */

	/* set timeout function */
	timeout_tag = g_timeout_add(TIMEOUT_PERIOD, timeout_callback, NULL);

        /* make active row with last played song */

        show_active_position_in_playlist();
        gtk_widget_realize(play_list);

        if (options.playlist_is_embedded) {
                gtk_widget_set_sensitive(plist__fileinfo, FALSE);
                gtk_widget_grab_focus(GTK_WIDGET(play_list));
        }
}


void
adjust_remote_volume(char * str) {

	char * endptr = NULL;
	int val = gtk_adjustment_get_value(GTK_ADJUSTMENT(adj_vol));

	switch (str[0]) {
	case 'm':
	case 'M':
		val = -41;
		break;
	case '=':
		val = strtol(str + 1, &endptr, 10);
		if (endptr[0] != '\0') {
			fprintf(stderr, "Cannot convert to integer value: %s\n", str + 1);
			return;
		}
		break;
	default:
		val += strtol(str, &endptr, 10);
		if (endptr[0] != '\0') {
			fprintf(stderr, "Cannot convert to integer value: %s\n", str);
			return;
		}
		break;
	}

	gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_vol), val);
}


gint
timeout_callback(gpointer data) {

	long pos;
	char cmd;
	cue_t cue;
	static double left_gain_shadow;
	static double right_gain_shadow;
	char rcmd;
	static char cmdbuf[MAXLEN];
	int rcv_count;
	static int last_rcmd_loadenq = 0;
#ifdef HAVE_JACK
	static int jack_popup_beenthere = 0;
#endif /* HAVE_JACK */


	while (rb_read_space(rb_disk2gui)) {
		rb_read(rb_disk2gui, &recv_cmd, 1);
		switch (recv_cmd) {

		case CMD_FILEREQ:
			cmd = CMD_CUE;
			cue.filename = NULL;
			cue.voladj = 0.0f;

			if (!is_file_loaded)
				break; /* ignore leftover filereq message */

			decide_next_track(&cue);

			if (cue.filename != NULL) {
				rb_write(rb_gui2disk, &cmd, sizeof(char));
				rb_write(rb_gui2disk, (void *)&cue, sizeof(cue_t));
			} else {
				send_cmd = CMD_STOPWOFL;
				rb_write(rb_gui2disk, &send_cmd, sizeof(char));
			}

			try_waking_disk_thread();
			break;

		case CMD_FILEINFO:
			while (rb_read_space(rb_disk2gui) < sizeof(fileinfo_t))
				;
			rb_read(rb_disk2gui, (char *)&fileinfo, sizeof(fileinfo_t));

			total_samples = fileinfo.total_samples;
			status.samples_left = fileinfo.total_samples;
			status.sample_pos = 0;
			status.sample_offset = 0;
			fresh_new_file = fresh_new_file_prev = 0;
			break;

		case CMD_STATUS:
			while (rb_read_space(rb_disk2gui) < sizeof(status_t))
				;
			rb_read(rb_disk2gui, (char *)&status, sizeof(status_t));

			pos = total_samples - status.samples_left;

#ifdef HAVE_LOOP
			if (repeat_on &&
			    (pos < total_samples * loop_range_start ||
			     pos > total_samples * loop_range_end)) {

				seek_t seek;

				send_cmd = CMD_SEEKTO;
				seek.seek_to_pos = loop_range_start * total_samples;
				rb_write(rb_gui2disk, &send_cmd, 1);
				rb_write(rb_gui2disk, (char *)&seek, sizeof(seek_t));
				try_waking_disk_thread();
				refresh_scale_suppress = 2;
			}
#endif /* HAVE_LOOP */

			if ((is_file_loaded) && (status.samples_left < 2*status.sample_offset)) {
				allow_seeks = 0;
			} else {
				allow_seeks = 1;
			}

			/* treat files with unknown length */
			if (total_samples == 0) {
				allow_seeks = 1;
				pos = status.sample_pos - status.sample_offset;
			}

			if ((!fresh_new_file) && (pos > status.sample_offset)) {
				fresh_new_file = 1;
			}

			if (fresh_new_file && !fresh_new_file_prev) {
				disp_info = fileinfo;
				disp_samples = total_samples;
				if (pos > status.sample_offset) {
					disp_pos = pos - status.sample_offset;
				} else {
					disp_pos = 0;
				}
				refresh_displays();
			} else {
				if (pos > status.sample_offset) {
					disp_pos = pos - status.sample_offset;
				} else {
					disp_pos = 0;
				}

				if (is_file_loaded) {
					refresh_time_displays();
				}
			}

			fresh_new_file_prev = fresh_new_file;

			if (refresh_scale && !refresh_scale_suppress && GTK_IS_ADJUSTMENT(adj_pos)) {
				if (total_samples == 0) {
					gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_pos), 0.0f);
				} else {
					gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_pos),
								 100.0f * (double)(pos) / total_samples);
				}
			}

			if (refresh_scale_suppress > 0) {
				--refresh_scale_suppress;
			}
			break;

		default:
			fprintf(stderr, "gui: unexpected command %d recv'd from disk\n", recv_cmd);
			break;
		}
	}
        /* update volume & balance if necessary */
	if ((vol != vol_prev) || (bal != bal_prev)) {
		vol_prev = vol;
		vol_lin = (vol < -40.5f) ? 0 : db2lin(vol);
		bal_prev = bal;
		if (bal >= 0.0f) {
			left_gain_shadow = vol_lin * db2lin(-0.4f * bal);
			right_gain_shadow = vol_lin;
		} else {
			left_gain_shadow = vol_lin;
			right_gain_shadow = vol_lin * db2lin(0.4f * bal);
		}
			left_gain = left_gain_shadow;
			right_gain = right_gain_shadow;
	}

	/* receive and execute remote commands, if any */
	rcv_count = 0;
	while (((rcmd = receive_message(aqualung_socket_fd, cmdbuf)) != 0) && (rcv_count < MAX_RCV_COUNT)) {
		switch (rcmd) {
		case RCMD_BACK:
			prev_event(NULL, NULL, NULL);
			last_rcmd_loadenq = 0;
			break;
		case RCMD_PLAY:
			if (last_rcmd_loadenq != 2) {
				if (is_paused) {
					stop_event(NULL, NULL, NULL);
				}
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(play_button),
					!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(play_button)));
			}
			last_rcmd_loadenq = 0;
			break;
		case RCMD_PAUSE:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pause_button),
			        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pause_button)));
			last_rcmd_loadenq = 0;
			break;
		case RCMD_STOP:
			stop_event(NULL, NULL, NULL);
			last_rcmd_loadenq = 0;
			break;
		case RCMD_FWD:
			next_event(NULL, NULL, NULL);
			last_rcmd_loadenq = 0;
			break;
		case RCMD_LOAD:
			add_to_playlist(cmdbuf, 0);
			last_rcmd_loadenq = 1;
			break;
		case RCMD_ENQUEUE:
			add_to_playlist(cmdbuf, 1);
                        show_last_position_in_playlist();  
			if (last_rcmd_loadenq != 1)
				last_rcmd_loadenq = 2;
			break;
		case RCMD_VOLADJ:
			adjust_remote_volume(cmdbuf);
			last_rcmd_loadenq = 0;
			break;
		case RCMD_QUIT:
			main_window_close(NULL, NULL);
			last_rcmd_loadenq = 0;
			break;
		}
		++rcv_count;
	}

	if (immediate_start) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(play_button),
		        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(play_button)));
		immediate_start = 0;
	}

	/* check for JACK shutdown condition */
#ifdef HAVE_JACK
	if (output == JACK_DRIVER) {
		if (jack_is_shutdown) {
			if (is_file_loaded) {
				stop_event(NULL, NULL, NULL);
			}
			if (!jack_popup_beenthere) {
				jack_shutdown_window();
				if (ports_window) {
					ports_clicked_close(NULL, NULL);
				}
				gtk_widget_set_sensitive(conf__jack, FALSE);
				jack_popup_beenthere = 1;
			}
		}
	}
#endif /* HAVE_JACK */

	return TRUE;
}



void
run_gui(void) {

	gtk_main();

	return;
}


#define SAVE_OPTION_STR(XmlStr, OptVar) \
        xmlNewTextChild(root, NULL, (const xmlChar *) XmlStr, (xmlChar *) options.OptVar);

#define SAVE_OPTION_STR_1(Var) SAVE_OPTION_STR(#Var, Var)

#define SAVE_FONT(Font) \
	snprintf(str, MAX_FONTNAME_LEN, "%s", options.Font); \
        xmlNewTextChild(root, NULL, (const xmlChar *) #Font, (xmlChar *) str);

#define SAVE_INT(XmlStr, Var) \
	snprintf(str, 31, "%d", Var); \
        xmlNewTextChild(root, NULL, (const xmlChar *) XmlStr, (xmlChar *) str);

#define SAVE_INT_1(Var) SAVE_INT(#Var, Var)

#define SAVE_OPTION_INT(XmlStr, OptVar) SAVE_INT(XmlStr, options.OptVar)

#define SAVE_OPTION_INT_1(Var) SAVE_OPTION_INT(#Var, Var)

#define SAVE_OPTION_INT_SH_1(Var) SAVE_OPTION_INT(#Var, Var##_shadow)

#define SAVE_FLOAT(XmlStr, Var) \
        snprintf(str, 31, "%f", Var); \
        xmlNewTextChild(root, NULL, (const xmlChar *) XmlStr, (xmlChar *) str);

#define SAVE_FLOAT_1(Var) SAVE_FLOAT(#Var, Var)

#define SAVE_OPTION_FLOAT(XmlStr, OptVar) SAVE_FLOAT(XmlStr, options.OptVar)

#define SAVE_OPTION_FLOAT_1(Var) SAVE_OPTION_FLOAT(#Var, Var)


void
save_config(void) {

        xmlDocPtr doc;
        xmlNodePtr root;
        char c, d;
        FILE * fin;
        FILE * fout;
        char tmpname[MAXLEN];
        char config_file[MAXLEN];
	char str[32];

	GtkTreeIter iter;
	int i = 0;
	char * path;

        sprintf(config_file, "%s/config.xml", options.confdir);

        doc = xmlNewDoc((const xmlChar *) "1.0");
        root = xmlNewNode(NULL, (const xmlChar *) "aqualung_config");
        xmlDocSetRootElement(doc, root);


	SAVE_OPTION_STR_1(currdir)
	SAVE_OPTION_STR_1(default_param)
	SAVE_OPTION_STR_1(title_format)
	SAVE_OPTION_STR_1(skin)
	SAVE_INT_1(src_type)
	SAVE_OPTION_INT_1(ladspa_is_postfader)
	SAVE_OPTION_INT_1(auto_save_playlist)
	SAVE_OPTION_INT_1(show_rva_in_playlist)
	SAVE_OPTION_INT_1(pl_statusbar_show_size)
	SAVE_OPTION_INT_1(ms_statusbar_show_size)
	SAVE_OPTION_INT_1(show_length_in_playlist)
	SAVE_OPTION_INT_1(show_active_track_name_in_bold)
	SAVE_OPTION_INT_1(enable_pl_rules_hint)
	SAVE_OPTION_INT_1(enable_ms_rules_hint)
	SAVE_OPTION_INT_SH_1(enable_ms_tree_icons)
	SAVE_OPTION_INT_1(auto_use_meta_artist)
	SAVE_OPTION_INT_1(auto_use_meta_record)
	SAVE_OPTION_INT_1(auto_use_meta_track)
	SAVE_OPTION_INT_1(auto_use_ext_meta_artist)
	SAVE_OPTION_INT_1(auto_use_ext_meta_record)
	SAVE_OPTION_INT_1(auto_use_ext_meta_track)
	SAVE_OPTION_INT_1(enable_tooltips)
	SAVE_OPTION_INT_SH_1(buttons_at_the_bottom)
	SAVE_OPTION_INT_1(disable_buttons_relief)
	SAVE_OPTION_INT_SH_1(simple_view_in_fx)
	SAVE_OPTION_INT("show_song_name_in_window_title", show_sn_title)
	SAVE_OPTION_INT("united_windows_minimization", united_minimization)
	SAVE_OPTION_INT_1(magnify_smaller_images)
	SAVE_OPTION_INT_1(cover_width)
	SAVE_OPTION_INT_SH_1(hide_comment_pane)
	SAVE_OPTION_INT_SH_1(enable_mstore_toolbar)
	SAVE_OPTION_INT_SH_1(enable_mstore_statusbar)
	SAVE_OPTION_INT_1(autoexpand_stores)
	SAVE_OPTION_INT_1(show_hidden)
	SAVE_OPTION_INT_1(main_window_always_on_top)
	SAVE_OPTION_INT_1(tags_tab_first)
	SAVE_OPTION_INT_1(override_skin_settings)
	SAVE_OPTION_INT_1(replaygain_tag_to_use)
	SAVE_FLOAT("volume", vol)
	SAVE_FLOAT("balance", bal)
	SAVE_OPTION_INT_1(rva_is_enabled)
	SAVE_OPTION_INT_1(rva_env)
	SAVE_OPTION_FLOAT_1(rva_refvol)
	SAVE_OPTION_FLOAT_1(rva_steepness)
	SAVE_OPTION_INT_1(rva_use_averaging)
	SAVE_OPTION_INT_1(rva_use_linear_thresh)
	SAVE_OPTION_FLOAT_1(rva_avg_linear_thresh)
	SAVE_OPTION_FLOAT_1(rva_avg_stddev_thresh)
	SAVE_INT_1(main_pos_x)
	SAVE_INT_1(main_pos_y)
	SAVE_INT_1(main_size_x)
	if (options.playlist_is_embedded && !options.playlist_is_embedded_shadow && playlist_on) {
		snprintf(str, 31, "%d", main_size_y - playlist_window->allocation.height - 6);
	} else {
		snprintf(str, 31, "%d", main_size_y);
	}
        xmlNewTextChild(root, NULL, (const xmlChar *) "main_size_y", (xmlChar *) str);
	SAVE_INT_1(browser_pos_x)
	SAVE_INT_1(browser_pos_y)
	SAVE_INT_1(browser_size_x)
	SAVE_INT_1(browser_size_y)
	SAVE_INT("browser_is_visible", browser_on)
	SAVE_INT_1(browser_paned_pos)
	SAVE_INT_1(playlist_pos_x)
	SAVE_INT_1(playlist_pos_y)
	SAVE_INT_1(playlist_size_x)
	SAVE_INT_1(playlist_size_y)
	SAVE_INT("playlist_is_visible", playlist_on)
	SAVE_OPTION_INT_SH_1(playlist_is_embedded)
	SAVE_OPTION_INT_1(playlist_is_tree)
	SAVE_OPTION_INT_1(album_shuffle_mode)
	SAVE_OPTION_INT_SH_1(enable_playlist_statusbar)
	SAVE_FONT(browser_font)
	SAVE_FONT(playlist_font)
	SAVE_FONT(bigtimer_font)
	SAVE_FONT(smalltimer_font)
	SAVE_FONT(songtitle_font)
	SAVE_FONT(songinfo_font)
	SAVE_FONT(statusbar_font)

	snprintf(str, MAX_COLORNAME_LEN, "%s", options.activesong_color);
        xmlNewTextChild(root, NULL, (const xmlChar *) "activesong_color", (xmlChar *) str);

	SAVE_INT_1(repeat_on)
	SAVE_INT_1(repeat_all_on)
	SAVE_INT_1(shuffle_on)
	SAVE_INT("time_idx_0", time_idx[0])
	SAVE_INT("time_idx_1", time_idx[1])
	SAVE_INT("time_idx_2", time_idx[2])
	SAVE_OPTION_INT("plcol_idx_0", plcol_idx[0])
	SAVE_OPTION_INT("plcol_idx_1", plcol_idx[1])
	SAVE_OPTION_INT("plcol_idx_2", plcol_idx[2])
	SAVE_INT_1(search_pl_flags)
	SAVE_INT_1(search_ms_flags)
	SAVE_OPTION_INT_1(cdda_drive_speed)
	SAVE_OPTION_INT_1(cdda_paranoia_mode)
	SAVE_OPTION_INT_1(cdda_paranoia_maxretries)
	SAVE_OPTION_INT_1(cdda_force_drive_rescan)
	SAVE_OPTION_STR_1(cddb_server)
	SAVE_OPTION_INT_1(cddb_timeout)
	SAVE_OPTION_STR_1(cddb_email)
	SAVE_OPTION_STR_1(cddb_local)
	SAVE_OPTION_INT_1(cddb_cache_only)
	SAVE_OPTION_INT_1(cddb_use_http)
	SAVE_OPTION_INT_1(cddb_use_proxy)
	SAVE_OPTION_STR_1(cddb_proxy)
	SAVE_OPTION_INT_1(cddb_proxy_port)

#ifdef HAVE_LOOP
	SAVE_FLOAT_1(loop_range_start)
	SAVE_FLOAT_1(loop_range_end)
#endif /* HAVE_LOOP */

	i = 0;
	while (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(ms_pathlist_store), &iter, NULL, i++)) {
		char * utf8;
		gtk_tree_model_get(GTK_TREE_MODEL(ms_pathlist_store), &iter, 0, &path, -1);
		utf8 = g_locale_to_utf8(path, -1, NULL, NULL, NULL);
		xmlNewTextChild(root, NULL, (const xmlChar *) "music_store", (xmlChar *) utf8);
		g_free(path);
		g_free(utf8);
	}

	
        sprintf(tmpname, "%s/config.xml.temp", options.confdir);
        xmlSaveFormatFile(tmpname, doc, 1);
	xmlFreeDoc(doc);

        if ((fin = fopen(config_file, "rt")) == NULL) {
                fprintf(stderr, "Error opening file: %s\n", config_file);
                return;
        }
        if ((fout = fopen(tmpname, "rt")) == NULL) {
                fprintf(stderr, "Error opening file: %s\n", tmpname);
                return;
        }

        c = 0; d = 0;
        while (((c = fgetc(fin)) != EOF) && ((d = fgetc(fout)) != EOF)) {
                if (c != d) {
                        fclose(fin);
                        fclose(fout);
                        unlink(config_file);
                        rename(tmpname, config_file);
                        return;
                }
        }

        fclose(fin);
        fclose(fout);
        unlink(tmpname);
}


#define LOAD_OPTION_STR(XmlStr, OptVar) \
                if ((!xmlStrcmp(cur->name, (const xmlChar *)XmlStr))) { \
			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1); \
                        if (key != NULL) \
                                strncpy(options.OptVar, (char *) key, MAXLEN-1); \
                        xmlFree(key); \
                }

#define LOAD_OPTION_STR_1(Var) LOAD_OPTION_STR(#Var, Var)

#define LOAD_FONT(Font) \
                if ((!xmlStrcmp(cur->name, (const xmlChar *)#Font))) { \
			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1); \
                        if (key != NULL) \
                                strncpy(options.Font, (char *) key, MAX_FONTNAME_LEN-1); \
                        xmlFree(key); \
                }

#define LOAD_INT(XmlStr, Var) \
                if ((!xmlStrcmp(cur->name, (const xmlChar *)XmlStr))) { \
			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1); \
                        if (key != NULL) \
				sscanf((char *) key, "%d", &Var); \
                        xmlFree(key); \
                }

#define LOAD_INT_1(Var) LOAD_INT(#Var, Var)

#define LOAD_OPTION_INT(XmlStr, OptVar) LOAD_INT(XmlStr, options.OptVar)

#define LOAD_OPTION_INT_1(Var) LOAD_OPTION_INT(#Var, Var)

#define LOAD_OPTION_INT_SH(XmlStr, OptVar) \
                if ((!xmlStrcmp(cur->name, (const xmlChar *)XmlStr))) { \
			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1); \
                        if (key != NULL) { \
				sscanf((char *) key, "%d", &options.OptVar); \
				options.OptVar##_shadow = options.OptVar; \
			} \
                        xmlFree(key); \
                }

#define LOAD_OPTION_INT_SH_1(Var) LOAD_OPTION_INT_SH(#Var, Var)

#define LOAD_FLOAT(XmlStr, Var) \
                if ((!xmlStrcmp(cur->name, (const xmlChar *)XmlStr))) { \
			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1); \
                        if (key != NULL) { \
				Var = convf((char *) key); \
			} \
                        xmlFree(key); \
                }

#define LOAD_FLOAT_1(Var) LOAD_FLOAT(#Var, Var)

#define LOAD_OPTION_FLOAT(XmlStr, OptVar) LOAD_FLOAT(XmlStr, options.OptVar)

#define LOAD_OPTION_FLOAT_1(Var) LOAD_OPTION_FLOAT(#Var, Var)


void
load_config(void) {

        xmlDocPtr doc;
        xmlNodePtr cur;
        xmlNodePtr root;
	xmlChar * key;
        char config_file[MAXLEN];
        FILE * f;


        sprintf(config_file, "%s/config.xml", options.confdir);

        if ((f = fopen(config_file, "rt")) == NULL) {
		/* no warning -- done that in core.c::load_default_cl() */
                doc = xmlNewDoc((const xmlChar *) "1.0");
                root = xmlNewNode(NULL, (const xmlChar *) "aqualung_config");
                xmlDocSetRootElement(doc, root);
                xmlSaveFormatFile(config_file, doc, 1);
		xmlFreeDoc(doc);
                return;
        }
        fclose(f);

        doc = xmlParseFile(config_file);
        if (doc == NULL) {
                fprintf(stderr, "An XML error occured while parsing %s\n", config_file);
                return;
        }

        cur = xmlDocGetRootElement(doc);
        if (cur == NULL) {
                fprintf(stderr, "load_config: empty XML document\n");
                xmlFreeDoc(doc);
                return;
        }

        if (xmlStrcmp(cur->name, (const xmlChar *)"aqualung_config")) {
                fprintf(stderr, "load_config: XML document of the wrong type, "
			"root node != aqualung_config\n");
                xmlFreeDoc(doc);
                return;
        }


	vol = 0.0f;
	bal = 0.0f;
	browser_paned_pos = 400;

	options.skin[0] = '\0';

	options.default_param[0] = '\0';
	options.title_format[0] = '\0';
        options.enable_tooltips = 1;
        options.show_sn_title = 1;
        options.united_minimization = 1;
        options.buttons_at_the_bottom = options.buttons_at_the_bottom_shadow = 1;
	options.playlist_is_embedded = options.playlist_is_embedded_shadow = 1;
	options.playlist_is_tree = 1;

	options.enable_mstore_statusbar = options.enable_mstore_statusbar_shadow = 1;
       	options.enable_mstore_toolbar = options.enable_mstore_toolbar_shadow = 1;
        options.enable_ms_tree_icons = options.enable_ms_tree_icons_shadow = 1;
	options.ms_statusbar_show_size = 1;

        options.cover_width = 2;

	options.autoexpand_stores = 1;

	options.auto_save_playlist = 1;
	options.show_length_in_playlist = 1;
	options.enable_playlist_statusbar = options.enable_playlist_statusbar_shadow = 1;
	options.pl_statusbar_show_size = 1;

	options.rva_refvol = -12.0f;
	options.rva_steepness = 1.0f;
	options.rva_use_averaging = 1;
	options.rva_use_linear_thresh = 0;
	options.rva_avg_linear_thresh = 3.0f;
	options.rva_avg_stddev_thresh = 2.0f;

	options.auto_use_ext_meta_artist = 1;
	options.auto_use_ext_meta_record = 1;
	options.auto_use_ext_meta_track = 1;

	options.cdda_drive_speed = 4;
	options.cdda_paranoia_mode = 0; /* no paranoia */
	options.cdda_paranoia_maxretries = 20;
	options.cdda_force_drive_rescan = 0;

	options.cddb_server[0] = '\0';
	options.cddb_email[0] = '\0';
	options.cddb_local[0] = '\0';
	options.cddb_proxy[0] = '\0';
	options.cddb_timeout = 10;

	options.plcol_idx[0] = 0;
	options.plcol_idx[1] = 1;
	options.plcol_idx[2] = 2;

	ms_pathlist_store = gtk_list_store_new(3,
					       G_TYPE_STRING,   /* path */
					       G_TYPE_STRING,   /* displayed name */
					       G_TYPE_STRING);  /* state (rw, r, unreachable) */

        cur = cur->xmlChildrenNode;
        while (cur != NULL) {
		LOAD_OPTION_STR_1(currdir)
		LOAD_OPTION_STR_1(default_param)
		LOAD_OPTION_STR_1(title_format)
		LOAD_OPTION_STR_1(skin)

                if ((!xmlStrcmp(cur->name, (const xmlChar *)"src_type"))) {
			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
                        if ((key != NULL) && (!src_type_parsed))
				sscanf((char *) key, "%d", &src_type);
                        xmlFree(key);
                }

		LOAD_OPTION_INT_1(ladspa_is_postfader)
		LOAD_OPTION_INT_1(auto_save_playlist)
		LOAD_OPTION_INT_1(auto_use_meta_artist)
		LOAD_OPTION_INT_1(auto_use_meta_record)
		LOAD_OPTION_INT_1(auto_use_meta_track)
		LOAD_OPTION_INT_1(auto_use_ext_meta_artist)
		LOAD_OPTION_INT_1(auto_use_ext_meta_record)
		LOAD_OPTION_INT_1(auto_use_ext_meta_track)
		LOAD_OPTION_INT_1(show_rva_in_playlist)
		LOAD_OPTION_INT_1(pl_statusbar_show_size)
		LOAD_OPTION_INT_1(ms_statusbar_show_size)
		LOAD_OPTION_INT_1(show_length_in_playlist)
		LOAD_OPTION_INT_1(show_active_track_name_in_bold)
		LOAD_OPTION_INT_1(enable_pl_rules_hint)
		LOAD_OPTION_INT_1(enable_ms_rules_hint)
		LOAD_OPTION_INT_SH_1(enable_ms_tree_icons)
		LOAD_OPTION_INT_1(enable_tooltips)
		LOAD_OPTION_INT_SH_1(buttons_at_the_bottom)
		LOAD_OPTION_INT_1(disable_buttons_relief)
		LOAD_OPTION_INT_SH_1(simple_view_in_fx)
		LOAD_OPTION_INT("show_song_name_in_window_title", show_sn_title)
		LOAD_OPTION_INT("united_windows_minimization", united_minimization)
		LOAD_OPTION_INT_1(magnify_smaller_images)
		LOAD_OPTION_INT_1(cover_width)
		LOAD_OPTION_INT_SH_1(hide_comment_pane)
		LOAD_OPTION_INT_SH_1(enable_mstore_toolbar)
		LOAD_OPTION_INT_SH_1(enable_mstore_statusbar)
		LOAD_OPTION_INT_1(autoexpand_stores)
		LOAD_OPTION_INT_1(show_hidden)
		LOAD_OPTION_INT_1(main_window_always_on_top)
		LOAD_OPTION_INT_1(tags_tab_first)
		LOAD_OPTION_INT_1(override_skin_settings)
		LOAD_OPTION_INT_1(replaygain_tag_to_use)
		LOAD_FLOAT("volume", vol)
		LOAD_FLOAT("balance", bal)
		LOAD_OPTION_INT_1(rva_is_enabled)
		LOAD_OPTION_INT_1(rva_env)
		LOAD_OPTION_FLOAT_1(rva_refvol)
		LOAD_OPTION_FLOAT_1(rva_steepness)
		LOAD_OPTION_INT_1(rva_use_averaging)
		LOAD_OPTION_INT_1(rva_use_linear_thresh)
		LOAD_OPTION_FLOAT_1(rva_avg_linear_thresh)
		LOAD_OPTION_FLOAT_1(rva_avg_stddev_thresh)
		LOAD_INT_1(main_pos_x)
		LOAD_INT_1(main_pos_y)
		LOAD_INT_1(main_size_x)
		LOAD_INT_1(main_size_y)
		LOAD_INT_1(browser_pos_x)
		LOAD_INT_1(browser_pos_y)
		LOAD_INT_1(browser_size_x)
		LOAD_INT_1(browser_size_y)
		LOAD_INT("browser_is_visible", browser_on)
		LOAD_INT_1(browser_paned_pos)
		LOAD_INT_1(playlist_pos_x)
		LOAD_INT_1(playlist_pos_y)
		LOAD_INT_1(playlist_size_x)
		LOAD_INT_1(playlist_size_y)
		LOAD_INT("playlist_is_visible", playlist_on)
		LOAD_OPTION_INT_SH_1(playlist_is_embedded)
		LOAD_OPTION_INT_1(playlist_is_tree)
		LOAD_OPTION_INT_1(album_shuffle_mode)
		LOAD_OPTION_INT_SH_1(enable_playlist_statusbar)
		LOAD_FONT(browser_font)
		LOAD_FONT(playlist_font)
		LOAD_FONT(bigtimer_font)
		LOAD_FONT(smalltimer_font)
		LOAD_FONT(songtitle_font)
		LOAD_FONT(songinfo_font)
		LOAD_FONT(statusbar_font)

                if ((!xmlStrcmp(cur->name, (const xmlChar *)"activesong_color"))) {
			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
                        if (key != NULL)
                                strncpy(options.activesong_color, (char *) key, MAX_COLORNAME_LEN-1);
                        xmlFree(key);
                }

		LOAD_INT_1(repeat_on)
		LOAD_INT_1(repeat_all_on)
		LOAD_INT_1(shuffle_on)
		LOAD_INT("time_idx_0", time_idx[0])
		LOAD_INT("time_idx_1", time_idx[1])
		LOAD_INT("time_idx_2", time_idx[2])
		LOAD_OPTION_INT("plcol_idx_0", plcol_idx[0])
		LOAD_OPTION_INT("plcol_idx_1", plcol_idx[1])
		LOAD_OPTION_INT("plcol_idx_2", plcol_idx[2])
		LOAD_INT_1(search_pl_flags)
		LOAD_INT_1(search_ms_flags)
		LOAD_OPTION_INT_1(cdda_drive_speed)
		LOAD_OPTION_INT_1(cdda_paranoia_mode)
		LOAD_OPTION_INT_1(cdda_paranoia_maxretries)
		LOAD_OPTION_INT_1(cdda_force_drive_rescan)
		LOAD_OPTION_STR_1(cddb_server)
		LOAD_OPTION_INT_1(cddb_timeout)
		LOAD_OPTION_STR_1(cddb_email)
		LOAD_OPTION_STR_1(cddb_local)
		LOAD_OPTION_INT_1(cddb_cache_only)
		LOAD_OPTION_INT_1(cddb_use_http)
		LOAD_OPTION_STR_1(cddb_proxy)
		LOAD_OPTION_INT_1(cddb_proxy_port)
		LOAD_OPTION_INT_1(cddb_use_proxy)

#ifdef HAVE_LOOP
		LOAD_FLOAT_1(loop_range_start)
		LOAD_FLOAT_1(loop_range_end)
#endif /* HAVE_LOOP */

                if ((!xmlStrcmp(cur->name, (const xmlChar *)"music_store"))) {
			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
                        if (key != NULL) {

				char path[MAXLEN];
				char * ppath;

				snprintf(path, MAXLEN - 1, "%s", (char *)key);
				ppath = g_locale_from_utf8(path, -1, NULL, NULL, NULL);

				append_ms_pathlist(ppath, path);

				g_free(ppath);
			}

                        xmlFree(key);
		}
                cur = cur->next;
        }

        xmlFreeDoc(doc);
        return;
}


/* create button with stock item 
 *
 * in: label - label for buttor        (label=NULL  to disable label, label=-1 to disable button relief)
 *     stock - stock icon identifier                                
 */

GtkWidget* 
gui_stock_label_button(gchar *label, const gchar *stock) {

	GtkWidget *button;
	GtkWidget *alignment;
	GtkWidget *hbox;
	GtkWidget *image;

	button = g_object_new (GTK_TYPE_BUTTON, "visible", TRUE, NULL);

        if (label== (gchar *)-1) {
                gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);  
        }

	alignment = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
	hbox = gtk_hbox_new (FALSE, 2);
	gtk_container_add (GTK_CONTAINER (alignment), hbox);

	image = gtk_image_new_from_stock (stock, GTK_ICON_SIZE_BUTTON);

        if (image) {
		gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
        }

        if (label != NULL && label != (gchar *)-1) {
		gtk_box_pack_start (GTK_BOX (hbox),
		g_object_new (GTK_TYPE_LABEL, "label", label, "use_underline", TRUE, NULL),
		FALSE, TRUE, 0);
        }

	gtk_widget_show_all (alignment);
	gtk_container_add (GTK_CONTAINER (button), alignment);

	return button;
}


void
assign_playlist_fc_filters(GtkFileChooser *fc) {

        gchar *file_filters[] = { 
                _("Aqualung playlist (*.xml)"),      "*.[xX][mM][lL]",
                _("MP3 Playlist (*.m3u)"),           "*.[mM]3[uU]",
                _("Multimedia Playlist (*.pls)"),    "*.[pP][lL][sS]",
        };

        gint i, len;
        GtkFileFilter *filter_1, *filter_2, *filter_3;

        len = sizeof(file_filters)/sizeof(gchar*)/2;

        gtk_widget_realize(GTK_WIDGET(fc));

        /* all files filter */
        filter_1 = gtk_file_filter_new();
        gtk_file_filter_add_pattern(filter_1, "*");
        gtk_file_filter_set_name(GTK_FILE_FILTER(filter_1), _("All Files")); 
        gtk_file_chooser_add_filter(fc, filter_1);

        /* all playlist files filter */
        filter_2 = gtk_file_filter_new();

        for (i = 0; i < len; i++) {
                gtk_file_filter_add_pattern(filter_2, file_filters[2*i+1]);
	}

        gtk_file_filter_set_name(GTK_FILE_FILTER(filter_2), _("All Playlist Files")); 
        gtk_file_chooser_add_filter(fc, filter_2);
        gtk_file_chooser_set_filter(fc, filter_2);

        /* single extensions */
        for (i = 0; i < len; i++) {

                filter_3 = gtk_file_filter_new();
                gtk_file_filter_add_pattern(filter_3, file_filters[2*i+1]);
                gtk_file_filter_set_name(GTK_FILE_FILTER(filter_3), file_filters[2*i]); 
                gtk_file_chooser_add_filter(fc, filter_3);
        }

}


void
build_filter_from_extensions(GtkFileFilter * f1, GtkFileFilter * f2, char * extensions[]) {

	int i, j, k;

	for (i = 0; extensions[i]; i++) {
		char buf[32];
		buf[0] = '*';
		buf[1] = '.';
		k = 2;
		for (j = 0; extensions[i][j]; j++) {
			if (isalpha(extensions[i][j]) && k < 28) {
				buf[k++] = '[';
				buf[k++] = tolower(extensions[i][j]);
				buf[k++] = toupper(extensions[i][j]);
				buf[k++] = ']';
			} else if (k < 31) {
				buf[k++] = extensions[i][j];
			}
		}
		buf[k] = '\0';
		gtk_file_filter_add_pattern(f1, buf);
		gtk_file_filter_add_pattern(f2, buf);
	}
}


void
assign_audio_fc_filters(GtkFileChooser * fc) {

        GtkFileFilter * filter;
        GtkFileFilter * filter_all;

        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("All Files")); 
        gtk_file_filter_add_pattern(filter, "*");
        gtk_file_chooser_add_filter(fc, filter);

        filter_all = gtk_file_filter_new();
        gtk_file_filter_set_name(filter_all, _("All Audio Files")); 
        gtk_file_chooser_add_filter(fc, filter_all);
        gtk_file_chooser_set_filter(fc, filter_all);

#ifdef HAVE_SNDFILE
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("Sound Files (*.wav, *.aiff, *.au, ...)"));
	build_filter_from_extensions(filter, filter_all, valid_extensions_sndfile);
        gtk_file_chooser_add_filter(fc, filter);
#endif /* HAVE_SNDFILE */

#ifdef HAVE_FLAC
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("Free Lossless Audio Codec (*.flac)")); 
        gtk_file_filter_add_pattern(filter, "*.[fF][lL][aA][cC]");
        gtk_file_filter_add_pattern(filter_all, "*.[fF][lL][aA][cC]");
        gtk_file_chooser_add_filter(fc, filter);
#endif /* HAVE_FLAC */

#ifdef HAVE_MPEG
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("MPEG Audio (*.mp3, *.mpa, *.mpega, ...)"));
	build_filter_from_extensions(filter, filter_all, valid_extensions_mpeg);
        gtk_file_chooser_add_filter(fc, filter);
#endif /* HAME_MPEG */

#ifdef HAVE_OGG_VORBIS
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("Ogg Vorbis (*.ogg)")); 
        gtk_file_filter_add_pattern(filter, "*.[oO][gG][gG]");
        gtk_file_filter_add_pattern(filter_all, "*.[oO][gG][gG]");
        gtk_file_chooser_add_filter(fc, filter);
#endif /* HAVE_OGG_VORBIS */

#ifdef HAVE_SPEEX
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("Ogg Speex (*.spx)")); 
        gtk_file_filter_add_pattern(filter, "*.[sS][pP][xX]");
        gtk_file_filter_add_pattern(filter_all, "*.[sS][pP][xX]");
        gtk_file_chooser_add_filter(fc, filter);
#endif /* HAVE_SPEEX */

#ifdef HAVE_MPC
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("Musepack (*.mpc)")); 
        gtk_file_filter_add_pattern(filter, "*.[mM][pP][cC]");
        gtk_file_filter_add_pattern(filter_all, "*.[mM][pP][cC]");
        gtk_file_chooser_add_filter(fc, filter);
#endif /* HAVE_MPC */

#ifdef HAVE_MAC
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("Monkey's Audio Codec (*.ape)")); 
        gtk_file_filter_add_pattern(filter, "*.[aA][pP][eE]");
        gtk_file_filter_add_pattern(filter_all, "*.[aA][pP][eE]");
        gtk_file_chooser_add_filter(fc, filter);
#endif /* HAVE_MAC */

#ifdef HAVE_MOD
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("Modules (*.xm, *.mod, *.it, *.s3m, ...)")); 
	build_filter_from_extensions(filter, filter_all, valid_extensions_mod);
        gtk_file_chooser_add_filter(fc, filter);
#endif /* HAVE_MOD */

#if defined(HAVE_MOD) && (defined(HAVE_LIBZ) || defined(HAVE_LIBBZ2))
        filter = gtk_file_filter_new();
#if defined(HAVE_LIBZ) && defined(HAVE_LIBBZ2)
        gtk_file_filter_set_name(filter, _("Compressed modules (*.gz, *.bz2)")); 
#elif defined(HAVE_LIBZ)
        gtk_file_filter_set_name(filter, _("Compressed modules (*.gz)")); 
#elif defined(HAVE_LIBBZ2)
        gtk_file_filter_set_name(filter, _("Compressed modules (*.bz2)")); 
#endif /* HAVE_LIBZ, HAVE_LIBBZ2 */

#ifdef HAVE_LIBZ
        gtk_file_filter_add_pattern(filter, "*.[gG][zZ]");
        gtk_file_filter_add_pattern(filter_all, "*.[gG][zZ]");
#endif /* HAVE_LIBZ */
#ifdef HAVE_LIBBZ2
        gtk_file_filter_add_pattern(filter, "*.[bB][zZ]2");
        gtk_file_filter_add_pattern(filter_all, "*.[bB][zZ]2");
#endif /* HAVE_LIBBZ2 */
        gtk_file_chooser_add_filter(fc, filter);
#endif /* (HAVE_MOD && HAVE LIBZ) */

#ifdef HAVE_LAVC
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("LAVC audio/video files"));
	{
		char * valid_ext_lavc[] = {
			"aac", "ac3", "asf", "avi", "mpeg", "mpg", "mp3", "ra",
			"wav", "wma", "wv", NULL };
		build_filter_from_extensions(filter, filter_all, valid_ext_lavc);
	}
        gtk_file_chooser_add_filter(fc, filter);
#endif /* HAVE_LAVC */
}


void
set_buttons_relief(void) {

	GtkWidget *rbuttons_table[] = {
		prev_button, stop_button, next_button, play_button,
		pause_button, repeat_button, repeat_all_button, shuffle_button,
		playlist_toggle, musicstore_toggle, plugin_toggle
	};

	gint i, n;

        i = sizeof(rbuttons_table)/sizeof(GtkWidget*);

        for (n = 0; n < i; n++) {
	        if (options.disable_buttons_relief) {
                        gtk_button_set_relief (GTK_BUTTON (rbuttons_table[n]), GTK_RELIEF_NONE); 
		} else {
                        gtk_button_set_relief (GTK_BUTTON (rbuttons_table[n]), GTK_RELIEF_NORMAL);
		}
	}
                
}

// vim: shiftwidth=8:tabstop=8:softtabstop=8 :  

