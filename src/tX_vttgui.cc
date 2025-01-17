/*
    terminatorX - realtime audio scratching software
    Copyright (C) 1999-2021  Alexander König

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    File: tX_vttgui.cc

    Description: This implements the gtk+ GUI for the virtual turntable
		 class implemented in tX_vtt.cc. This code is not in tX_vtt.cc
		 for mainly to keep the GUI code divided from the audio-rendering
		 code and as gtk+ callback to C++ method call wrapper.
*/

#include "tX_vttgui.h"
#include "tX_dialog.h"
#include "tX_engine.h"
#include "tX_extdial.h"
#include "tX_global.h"
#include "tX_ladspa.h"
#include "tX_ladspa_class.h"
#include "tX_loaddlg.h"
#include "tX_maingui.h"
#include "tX_panel.h"
#include "tX_pbutton.h"
#include "tX_prelis.h"
#include "tX_ui_interface.h"
#include "tX_ui_support.h"
#include "tX_vtt.h"
#include "tX_widget.h"
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#ifdef USE_DIAL
#include "tX_dial.h"
#endif

#include "tX_flash.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define WID_DYN TRUE, TRUE, 0
#define WID_FIX FALSE, FALSE, 0

void nicer_filename(char* dest, char* source) {
    char* fn;
    char temp[PATH_MAX];

    fn = strrchr(source, '/');
    if (fn)
        fn++;
    else
        fn = source;

    strcpy(temp, fn);

    fn = strrchr(temp, '.');
    if (fn)
        *fn = 0;

    if (strlen(temp) > (unsigned int)globals.filename_length) {
        temp[globals.filename_length - 3] = '.';
        temp[globals.filename_length - 2] = '.';
        temp[globals.filename_length - 1] = '.';
        temp[globals.filename_length] = 0;
    }
    strcpy(dest, temp);
}

void name_changed(GtkWidget* wid, vtt_class* vtt) {
    vtt->set_name((char*)gtk_entry_get_text(GTK_ENTRY(wid)));
}

void volume_changed(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_volume.receive_gui_value(2.0 - gtk_adjustment_get_value(GTK_ADJUSTMENT(wid)));
}

void pan_changed(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_pan.receive_gui_value(gtk_adjustment_get_value(GTK_ADJUSTMENT(wid)));
}

void pitch_changed(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_pitch.receive_gui_value(gtk_adjustment_get_value(GTK_ADJUSTMENT(wid)));
}

GCallback chooser_prelis(GtkWidget* w) {
    GtkFileChooser* fc = GTK_FILE_CHOOSER(gtk_widget_get_toplevel(w));
    char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));

    if (filename) {
        prelis_start(filename);
        g_free(filename);
    } else {
        prelis_stop();
    }
    return NULL;
}

char global_filename_buffer[PATH_MAX];

void load_part(char* newfile, vtt_class* vtt) {
    tX_audio_error ret = TX_AUDIO_SUCCESS;

    ld_create_loaddlg(TX_LOADDLG_MODE_SINGLE, 1);
    ld_set_filename(newfile);

    ret = vtt->load_file(newfile);

    ld_destroy();
    if (ret) {
        switch (ret) {
        case TX_AUDIO_ERR_ALLOC:
            tx_note("Failed to load audiofile - there's not enough memory available.", true);
            break;
        case TX_AUDIO_ERR_PIPE_READ:
            tx_note("An error occurred on reading from the piped process - probably the file format of the audiofile is not supported by this configuration - please check terminatorX' INSTALL file on howto configure terminatorX for files of this format.", true);
            break;
        case TX_AUDIO_ERR_SOX:
            tx_note("Failed to run sox - to load the given audiofile please ensure that sox is installed correctly.", true);
            break;
        case TX_AUDIO_ERR_MPG123:
            tx_note("Failed to run mpg123 - to load the given mp3 file please ensure that mpg123 (or mpg321) is installed correctly.", true);
            break;
        case TX_AUDIO_ERR_WAV_NOTFOUND:
            tx_note("Couldn't access the audiofile - file not found.", true);
            break;
        case TX_AUDIO_ERR_NOT_16BIT:
            tx_note("The wav file doesn't use 16 bit wide samples - please compile terminatorX with libaudiofile support to enable loading of such files.", true);
            break;
        case TX_AUDIO_ERR_NOT_MONO:
            tx_note("The wav file is not mono - please compile terminatorX with libaudiofile support to enable loading of such files.", true);
            break;
        case TX_AUDIO_ERR_WAV_READ:
            tx_note("The wav file seems to be corrupt.", true);
            break;
        case TX_AUDIO_ERR_NOT_SUPPORTED:
            tx_note("The file format of the audiofile is not supported - please check terminatorX' INSTALL file on howto configure terminatorX for files of this format.", true);
            break;
        case TX_AUDIO_ERR_MAD_OPEN:
            tx_note("Failed to open this mp3 file - please ensure that the file exists and is readable.", true);
            break;
        case TX_AUDIO_ERR_MAD_STAT:
            tx_note("Failed to 'stat' this mp3 file - please ensure that the file exists and is readable.", true);
            break;
        case TX_AUDIO_ERR_MAD_DECODE:
            tx_note("Failed to decode the mp3 stream - file is corrupt.", true);
            break;
        case TX_AUDIO_ERR_MAD_MMAP:
            tx_note("Failed to map the audiofile to memory - please ensure the file is readable.", true);
            break;
        case TX_AUDIO_ERR_MAD_MUNMAP:
            tx_note("Failed to unmap audiofile.", true);
            break;
        case TX_AUDIO_ERR_VORBIS_OPEN:
            tx_note("Failed to open ogg file - please ensure the file is an ogg stream and that it is readable.", true);
            break;
        case TX_AUDIO_ERR_VORBIS_NODATA:
            tx_note("The vorbis codec failed to decode any data - possibly this ogg stream is corrupt.", true);
            break;
        case TX_AUDIO_ERR_AF_OPEN:
            tx_note("Failed to open this file with libaudiofile - please check terminatorX' INSTALL file on howto configure terminatorX for files of this format.", true);
            break;
        case TX_AUDIO_ERR_AF_NODATA:
            tx_note("libaudiofile failed to decode any data - possilby the audiofile is corrupt.", true);
            break;
        default:
            tx_note("An unknown error occurred - if this bug is reproducible please report it, thanks.", true);
        }
    } else {
        nicer_filename(global_filename_buffer, newfile);
        gtk_button_set_label(GTK_BUTTON(vtt->gui.file), global_filename_buffer);
    }
}

void drop_file(GtkWidget* widget, GdkDragContext* context,
    gint x, gint y, GtkSelectionData* selection_data,
    guint info, guint time, vtt_class* vtt) {
    char filename[PATH_MAX];
    char* fn;

    strncpy(filename, (char*)gtk_selection_data_get_data(selection_data), (size_t)gtk_selection_data_get_length(selection_data));
    gtk_drag_finish(context, TRUE, FALSE, time);
    filename[gtk_selection_data_get_length(selection_data)] = 0;

    fn = strchr(filename, '\r');
    *fn = 0;

    char* realfn = NULL;
    char* host = NULL;

    realfn = g_filename_from_uri(filename, &host, NULL);
    if (realfn) {
        fn = realfn;
    } else {
        fn = strchr(filename, ':');
        if (fn)
            fn++;
        else
            fn = (char*)gtk_selection_data_get_data(selection_data);
    }

    load_part(fn, vtt);

    if (realfn)
        g_free(realfn);
    if (host)
        g_free(host);
}

GCallback load_file(GtkWidget* wid, vtt_class* vtt) {
    const char* extensions[] = { "mp3", "wav", "ogg", "flac", "iff", "aiff", "voc", "au", "spx", NULL };
    char name_buf[512];
    sprintf(name_buf, "Select Audio File for %s", vtt->name);

    GtkWidget* dialog = gtk_file_chooser_dialog_new(name_buf,
        GTK_WINDOW(main_window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, NULL);

    GtkFileFilter* filter = gtk_file_filter_new();
    for (int i = 0; extensions[i] != NULL; i++) {
        char buffer[32] = "*.";

        gtk_file_filter_add_pattern(filter, strcat(buffer, extensions[i]));
        for (unsigned int c = 0; c < strlen(buffer); c++) {
            buffer[c] = toupper(buffer[c]);
        }
        gtk_file_filter_add_pattern(filter, buffer);
    }

    gtk_file_filter_set_name(filter, "Audio Files");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_filter_set_name(filter, "All Files");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (vtt->audiofile) {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), vtt->filename);
    } else if (strlen(globals.current_path) > 0) {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), globals.current_path);
    }

    g_signal_connect(G_OBJECT(dialog), "selection-changed", G_CALLBACK(chooser_prelis), vtt);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_widget_hide(dialog);
        tX_cursor::set_cursor(tX_cursor::WAIT_CURSOR);
        load_part(filename, vtt);
        strcpy(globals.current_path, filename);
        tX_cursor::reset_cursor();
    }

    prelis_stop();
    gtk_widget_destroy(dialog);

    return NULL;
}

void delete_vtt(GtkWidget* wid, vtt_class* vtt) {
    if (audioon)
        tx_note("Sorry, you'll have to stop playback first.");
    else
        delete (vtt);

    mg_update_status();
}

void edit_vtt_buffer(GtkWidget* wid, vtt_class* vtt) {
    char command[2 * PATH_MAX + 32];

    if (vtt->samples_in_buffer == 0) {
        tx_note("No audiofile loaded - so there's nothing to edit.", true);
    } else if (strlen(globals.file_editor) > 0) {
        snprintf(command, sizeof(command), "%s \"%s\" &", globals.file_editor, vtt->filename);
        if (system(command) < 0) {
            tx_note("Error running the soundfile editor.");
        }
    } else {
        tx_note("No soundfile editor has been configured - to do so enter the soundfile editor of your choice in the options dialog.", true);
    }
}

void reload_vtt_buffer(GtkWidget* wid, vtt_class* vtt) {
    char reload_buffer[PATH_MAX];

    while (gtk_events_pending())
        gtk_main_iteration();

    if (vtt->samples_in_buffer > 0) {
        strcpy(reload_buffer, vtt->filename);
        load_part(reload_buffer, vtt);
    } else
        tx_note("No audiofile loaded - so there's nothing to reload.", true);
}

void clone_vtt(GtkWidget* wid, vtt_class* vtt) {
    vtt->stop();
}

void trigger_vtt(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_trigger.receive_gui_value((float)1.0);
}

void stop_vtt(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_trigger.receive_gui_value((float)0.0);
}

void autotrigger_toggled(GtkWidget* wid, vtt_class* vtt) {
    vtt->set_autotrigger(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)));
}

void loop_toggled(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_loop.receive_gui_value(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)));
}

void lp_enabled(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_lp_enable.receive_gui_value(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)));
}

void lp_gain_changed(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_lp_gain.receive_gui_value(gtk_adjustment_get_value(GTK_ADJUSTMENT(wid)));
}

void lp_reso_changed(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_lp_reso.receive_gui_value(gtk_adjustment_get_value(GTK_ADJUSTMENT(wid)));
}

void lp_freq_changed(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_lp_freq.receive_gui_value(gtk_adjustment_get_value(GTK_ADJUSTMENT(wid)));
}

void ec_enabled(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_ec_enable.receive_gui_value(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)));
}

#ifdef USE_ALSA_MIDI_IN
void midi_mapping_clicked(GtkWidget* wid, vtt_class* vtt) {
    tX_engine::get_instance()->get_midi()->configure_bindings(vtt);
}
#endif

void ec_length_changed(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_ec_length.receive_gui_value(gtk_adjustment_get_value(GTK_ADJUSTMENT(wid)));
}

void ec_feedback_changed(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_ec_feedback.receive_gui_value(gtk_adjustment_get_value(GTK_ADJUSTMENT(wid)));
}

void ec_pan_changed(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_ec_pan.receive_gui_value(gtk_adjustment_get_value(GTK_ADJUSTMENT(wid)));
}

void ec_volume_changed(GtkWidget* wid, vtt_class* vtt) {
    vtt->sp_ec_volume.receive_gui_value(gtk_adjustment_get_value(GTK_ADJUSTMENT(wid)));
}

void leader_setup(GtkWidget* wid, vtt_class* vtt) {
    vtt->set_sync_leader(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)));
}

void client_setup(GtkWidget* wid, vtt_class* vtt) {
    int client;

    client = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vtt->gui.sync_follower));
    vtt->sp_sync_follower.receive_gui_value(client);
}

void client_setup_number(GtkWidget* wid, vtt_class* vtt) {
    int cycles;

    cycles = (int)gtk_adjustment_get_value(GTK_ADJUSTMENT(vtt->gui.cycles));

    vtt->sp_sync_cycles.receive_gui_value(cycles);
}

void mute_volume(GtkWidget* widget, vtt_class* vtt) {
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        vtt->set_mix_mute(1);
    } else {
        vtt->set_mix_mute(0);
    }
}

void solo_vtt(GtkWidget* widget, vtt_class* vtt) {
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        vtt->set_mix_solo(1);
    } else {
        vtt->set_mix_solo(0);
    }
}

void minimize_control_panel(GtkWidget* wid, vtt_class* vtt) {
    vtt->hide_control(true);
}

void unminimize_control_panel(GtkWidget* wid, vtt_class* vtt) {
    vtt->hide_control(false);
}

void minimize_audio_panel(GtkWidget* wid, vtt_class* vtt) {
    vtt->hide_audio(true);
}

void unminimize_audio_panel(GtkWidget* wid, vtt_class* vtt) {
    vtt->hide_audio(false);
}

void vg_xcontrol_dis(GtkWidget* wid, vtt_class* vtt) {
    vtt->set_x_input_parameter(NULL);
}

void vg_ycontrol_dis(GtkWidget* wid, vtt_class* vtt) {
    vtt->set_y_input_parameter(NULL);
}

void vg_xcontrol_set(GtkWidget* wid, tX_seqpar* sp) {
    vtt_class* vtt = (vtt_class*)sp->vtt;
    vtt->set_x_input_parameter(sp);
}

void vg_ycontrol_set(GtkWidget* wid, tX_seqpar* sp) {
    vtt_class* vtt = (vtt_class*)sp->vtt;
    vtt->set_y_input_parameter(sp);
}

gboolean vg_delete_pitch_adjust(GtkWidget* wid, vtt_class* vtt) {
    vtt->gui.adjust_dialog = NULL;
    return FALSE;
}

void vg_do_pitch_adjust(GtkWidget* wid, vtt_class* vtt) {
    int leader_cycles = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(lookup_widget(vtt->gui.adjust_dialog, "leader_cycles")));
    int cycles = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(lookup_widget(vtt->gui.adjust_dialog, "cycles")));
    bool create_event = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lookup_widget(vtt->gui.adjust_dialog, "create_event")));

    vtt->adjust_to_main_pitch(leader_cycles, cycles, create_event);

    gtk_widget_destroy(vtt->gui.adjust_dialog);
}

void vg_cancel_pitch_adjust(GtkWidget* wid, vtt_class* vtt) {
    gtk_widget_destroy(vtt->gui.adjust_dialog);
}

void vg_adjust_pitch_vtt(GtkWidget* wid, vtt_class* vtt) {
    if (vtt->gui.adjust_dialog) {
        gtk_widget_destroy(vtt->gui.adjust_dialog);
        return;
    }

    if (!vtt_class::sync_leader) {
        tx_note("No leader turntable to adjust pitch to selected.", true);
        return;
    }

    if (vtt == vtt_class::sync_leader) {
        tx_note("This is the leader turntable - cannot adjust a turntable to itself.", true);
        return;
    }

    vtt->gui.adjust_dialog = create_tx_adjust();
    tX_set_icon(vtt->gui.adjust_dialog);
    gtk_widget_show(vtt->gui.adjust_dialog);

    GtkWidget* ok_button = lookup_widget(vtt->gui.adjust_dialog, "ok");
    GtkWidget* cancel_button = lookup_widget(vtt->gui.adjust_dialog, "cancel");

    g_signal_connect(G_OBJECT(ok_button), "clicked", G_CALLBACK(vg_do_pitch_adjust), vtt);
    g_signal_connect(G_OBJECT(vtt->gui.adjust_dialog), "destroy", G_CALLBACK(vg_delete_pitch_adjust), vtt);
    g_signal_connect(G_OBJECT(cancel_button), "clicked", G_CALLBACK(vg_cancel_pitch_adjust), vtt);
}

static gint vg_mouse_mapping_pressed(GtkWidget* wid, GdkEventButton* event, vtt_class* vtt) {
    if (vtt->gui.mouse_mapping_menu) {
        gtk_widget_destroy(vtt->gui.mouse_mapping_menu);
        vtt->gui.mouse_mapping_menu = NULL;
    }
    /* gtk+ seems to cleanup the submenus automatically */

    vtt->gui.mouse_mapping_menu = gtk_menu_new();
    GtkWidget* x_item;
    GtkWidget* y_item;

    x_item = gtk_menu_item_new_with_label("X-axis (Left <-> Right)");
    gtk_menu_shell_append(GTK_MENU_SHELL(vtt->gui.mouse_mapping_menu), x_item);
    gtk_widget_show(x_item);

    y_item = gtk_menu_item_new_with_label("Y-axis (Up <-> Down)");
    gtk_menu_shell_append(GTK_MENU_SHELL(vtt->gui.mouse_mapping_menu), y_item);
    gtk_widget_show(y_item);

    vtt->gui.mouse_mapping_menu_x = gtk_menu_new();
    vtt->gui.mouse_mapping_menu_y = gtk_menu_new();

    GtkWidget* item;
    GtkWidget* item_to_activate = NULL;

    /* Filling the X menu */
    item = gtk_check_menu_item_new_with_label("Disable");
    gtk_menu_shell_append(GTK_MENU_SHELL(vtt->gui.mouse_mapping_menu_x), item);
    gtk_widget_show(item);
    g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(vg_xcontrol_dis), vtt);
    if (vtt->x_par == NULL)
        item_to_activate = item;

    list<tX_seqpar*>::iterator sp;

    for (sp = tX_seqpar::all->begin(); sp != tX_seqpar::all->end(); sp++) {
        if (((*sp)->is_mappable) && ((*sp)->vtt) == (void*)vtt) {
            item = gtk_check_menu_item_new_with_label((*sp)->get_name());
            gtk_menu_shell_append(GTK_MENU_SHELL(vtt->gui.mouse_mapping_menu_x), item);
            gtk_widget_show(item);
            g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(vg_xcontrol_set), (void*)(*sp));

            if (vtt->x_par == (*sp))
                item_to_activate = item;
        }
    }

    if (item_to_activate)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_to_activate), TRUE);

    /* Filling the Y menu */
    item_to_activate = NULL;

    item = gtk_check_menu_item_new_with_label("Disable");
    gtk_menu_shell_append(GTK_MENU_SHELL(vtt->gui.mouse_mapping_menu_y), item);
    gtk_widget_show(item);
    g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(vg_ycontrol_dis), vtt);
    if (vtt->y_par == NULL)
        item_to_activate = item;

    for (sp = tX_seqpar::all->begin(); sp != tX_seqpar::all->end(); sp++) {
        if (((*sp)->is_mappable) && ((*sp)->vtt) == (void*)vtt) {
            item = gtk_check_menu_item_new_with_label((*sp)->get_name());
            gtk_menu_shell_append(GTK_MENU_SHELL(vtt->gui.mouse_mapping_menu_y), item);
            gtk_widget_show(item);
            g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(vg_ycontrol_set), (void*)(*sp));

            if (vtt->y_par == (*sp))
                item_to_activate = item;
        }
    }

    if (item_to_activate)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_to_activate), TRUE);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(x_item), vtt->gui.mouse_mapping_menu_x);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(y_item), vtt->gui.mouse_mapping_menu_y);
    gtk_menu_popup_at_widget(GTK_MENU(vtt->gui.mouse_mapping_menu), wid, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, (GdkEvent*)event);

    g_signal_emit_by_name(G_OBJECT(wid), "released", vtt);

    return TRUE;
}

static gint vg_file_button_pressed(GtkWidget* wid, GdkEventButton* event, vtt_class* vtt) {
    if (vtt->gui.file_menu == NULL) {
        GtkWidget* item;

        vtt->gui.file_menu = gtk_menu_new();
        item = gtk_menu_item_new_with_label("Load audio file");
        gtk_menu_shell_append(GTK_MENU_SHELL(vtt->gui.file_menu), item);
        gtk_widget_show(item);

        g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(load_file), vtt);

        item = gtk_menu_item_new_with_label("Edit audio file");
        gtk_menu_shell_append(GTK_MENU_SHELL(vtt->gui.file_menu), item);
        gtk_widget_show(item);

        g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(edit_vtt_buffer), vtt);

        item = gtk_menu_item_new_with_label("Reload current audio file");
        gtk_menu_shell_append(GTK_MENU_SHELL(vtt->gui.file_menu), item);
        gtk_widget_show(item);

        g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(reload_vtt_buffer), vtt);
    }

    gtk_menu_popup_at_widget(GTK_MENU(vtt->gui.file_menu), wid, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, (GdkEvent*)event);
    /* gtk+ is really waiting for this.. */
    g_signal_emit_by_name(G_OBJECT(wid), "released", vtt);

    return TRUE;
}

void vg_adjust_zoom(GtkWidget* wid, vtt_class* vtt) {
    GtkAdjustment* adj = gtk_range_get_adjustment(GTK_RANGE(vtt->gui.zoom));
    gtk_tx_set_zoom(GTK_TX(vtt->gui.display), gtk_adjustment_get_value(adj) / 100.0, vtt->is_playing);
}

static gint fx_button_pressed(GtkWidget* wid, GdkEventButton* event, vtt_class* vtt) {
    vtt_gui* g = &vtt->gui;

    LADSPA_Class::set_current_vtt(vtt);

    if (g->ladspa_menu)
        gtk_widget_destroy(GTK_WIDGET(g->ladspa_menu));
    g->ladspa_menu = LADSPA_Class::get_ladspa_menu();
    gtk_menu_popup_at_widget(GTK_MENU(g->ladspa_menu), wid, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, (GdkEvent*)event);

    /* gtk+ is really waiting for this.. */
    g_signal_emit_by_name(G_OBJECT(wid), "released", vtt);

    return TRUE;
}

static gint stereo_fx_button_pressed(GtkWidget* wid, GdkEventButton* event, vtt_class* vtt) {
    vtt_gui* g = &vtt->gui;

    LADSPA_Class::set_current_vtt(vtt);

    if (g->ladspa_menu)
        gtk_widget_destroy(GTK_WIDGET(g->ladspa_menu));
    g->ladspa_menu = LADSPA_Class::get_stereo_ladspa_menu();
    gtk_menu_popup_at_widget(GTK_MENU(g->ladspa_menu), wid, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, (GdkEvent*)event);

    /* gtk+ is really waiting for this.. */
    g_signal_emit_by_name(G_OBJECT(wid), "released", vtt);

    return TRUE;
}

void update_vtt_css(vtt_class* vtt, GdkRGBA* rgba) {
    char css[256];
    GdkRGBA copy;
    memcpy(&copy, rgba, sizeof(GdkRGBA));
    copy.alpha = globals.title_bar_alpha;
    char* color_str = gdk_rgba_to_string(&copy);
    snprintf(css, sizeof(css), ".%08lx { background-color: %s; }", (unsigned long)vtt, color_str);
    g_free(color_str);
    gtk_css_provider_load_from_data(vtt->gui.css_provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(vtt->gui.css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    gtk_tx_update_colors(GTK_TX(vtt->gui.display), rgba);
    gtk_widget_queue_draw(vtt->gui.display);
}

static void gui_color_set(GtkWidget* widget, vtt_class* vtt) {
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(widget), vtt->get_color());

    update_vtt_css(vtt, vtt->get_color());
}

void gui_set_color(vtt_class* vtt, GdkRGBA* rgba) {
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(vtt->gui.color_button), rgba);
    update_vtt_css(vtt, rgba);
}

#define TOOLTIP_LENGTH 2048

void gui_set_name(vtt_class* vtt, char* newname) {
    char bold_name[128];
    strcpy(bold_name, "<b>");
    strcat(bold_name, newname);
    strcat(bold_name, "</b>");

    gtk_label_set_markup(GTK_LABEL(vtt->gui.audio_label), bold_name);
    gtk_label_set_markup(GTK_LABEL(vtt->gui.control_label), bold_name);
    gtk_entry_set_text(GTK_ENTRY(vtt->gui.name), newname);
    char tooltip[2048];

    if (vtt->gui.audio_minimized_panel_bar_button != NULL) {
        gtk_label_set_text(GTK_LABEL(vtt->gui.audio_minimized_panel_bar_label), newname);
        snprintf(tooltip, TOOLTIP_LENGTH, "Show \"%s\" audio panel.", newname);
        gui_set_tooltip(vtt->gui.audio_minimized_panel_bar_button, tooltip);
    }

    if (vtt->gui.control_minimized_panel_bar_button != NULL) {
        gtk_label_set_text(GTK_LABEL(vtt->gui.control_minimized_panel_bar_label), newname);
        snprintf(tooltip, TOOLTIP_LENGTH, "Show \"%s\" control panel.", newname);
        gui_set_tooltip(vtt->gui.control_minimized_panel_bar_button, tooltip);
    }
}

f_prec gui_get_audio_x_zoom(vtt_class* vtt) {
    return gtk_tx_get_zoom(GTK_TX(vtt->gui.display));
}

int vttgui_zoom_depth = 0;

void gui_set_audio_x_zoom(vtt_class* vtt, f_prec value) {
    if (vttgui_zoom_depth == 0) {
        vttgui_zoom_depth = 1;
        gtk_range_set_value(GTK_RANGE(vtt->gui.zoom), value * 100.0);
        vttgui_zoom_depth = 0;
    } else {
        gtk_tx_set_zoom(GTK_TX(vtt->gui.display), value, vtt->is_playing);
    }
}

void gui_scroll_callback(GtkWidget* tx, GdkEventScroll* eventScroll, gpointer userdata) {
    vtt_class* vtt = (vtt_class*)userdata;
    f_prec zoom = gui_get_audio_x_zoom(vtt);

    if ((eventScroll->direction == GDK_SCROLL_UP) || (eventScroll->direction == GDK_SCROLL_RIGHT)) {
        zoom += 0.1;
    } else if ((eventScroll->direction == GDK_SCROLL_DOWN) || (eventScroll->direction == GDK_SCROLL_LEFT)) {
        zoom -= 0.1;
    }
    gui_set_audio_x_zoom(vtt, zoom);
}

#define connect_entry(wid, func) \
    ;                            \
    g_signal_connect(G_OBJECT(g->wid), "activate", G_CALLBACK(func), (void*)vtt);
#define connect_adj(wid, func) \
    ;                          \
    g_signal_connect(G_OBJECT(g->wid), "value_changed", G_CALLBACK(func), (void*)vtt);
#define connect_button(wid, func) \
    ;                             \
    g_signal_connect(G_OBJECT(g->wid), "clicked", G_CALLBACK(func), (void*)vtt);
#define connect_range(wid, func) \
    ;                            \
    g_signal_connect(G_OBJECT(gtk_range_get_adjustment(GTK_RANGE(g->wid))), "value_changed", G_CALLBACK(func), (void*)vtt);
#define connect_scale_format(wid, func) \
    ;                                   \
    g_signal_connect(G_OBJECT(g->wid), "format-value", G_CALLBACK(func), (void*)vtt);
#define connect_press_button(wid, func) \
    ;                                   \
    g_signal_connect(G_OBJECT(g->wid), "button_press_event", G_CALLBACK(func), (void*)vtt);
#define connect_rel_button(wid, func) \
    ;                                 \
    g_signal_connect(G_OBJECT(g->wid), "released", G_CALLBACK(func), (void*)vtt);

gchar dnd_uri[128];

void gui_connect_signals(vtt_class* vtt) {
    vtt_gui* g = &vtt->gui;
    strncpy(dnd_uri, "text/uri-list", 128);

    connect_entry(name, name_changed);
    connect_adj(volume, volume_changed);
    connect_adj(pitch, pitch_changed);
    connect_adj(pan, pan_changed);
    connect_press_button(file, vg_file_button_pressed);

    connect_button(del, delete_vtt);
    connect_button(trigger, trigger_vtt);
    connect_button(stop, stop_vtt);
    connect_button(autotrigger, autotrigger_toggled);
    connect_button(loop, loop_toggled);
    connect_button(sync_leader, leader_setup);
    connect_button(sync_follower, client_setup);
    connect_button(adjust_button, vg_adjust_pitch_vtt);
    connect_adj(cycles, client_setup_number);
    connect_press_button(fx_button, fx_button_pressed);
    connect_press_button(stereo_fx_button, stereo_fx_button_pressed);

    connect_button(lp_enable, lp_enabled);
    connect_adj(lp_gain, lp_gain_changed);
    connect_adj(lp_reso, lp_reso_changed);
    connect_adj(lp_freq, lp_freq_changed);

    connect_button(ec_enable, ec_enabled);
#ifdef USE_ALSA_MIDI_IN
    connect_button(midi_mapping, midi_mapping_clicked);
#endif
    connect_adj(ec_length, ec_length_changed);
    connect_adj(ec_feedback, ec_feedback_changed);
    connect_adj(ec_pan, ec_pan_changed);
    connect_adj(ec_volume, ec_volume_changed);
    connect_range(zoom, vg_adjust_zoom);
    //connect_scale_format(zoom, vg_format_zoom);
    connect_press_button(mouse_mapping, vg_mouse_mapping_pressed);
    connect_button(control_minimize, minimize_control_panel);
    connect_button(audio_minimize, minimize_audio_panel);

    gtk_widget_add_events(GTK_WIDGET(g->display), GDK_SCROLL_MASK);
    g_signal_connect(G_OBJECT(g->display), "scroll-event", G_CALLBACK(gui_scroll_callback), vtt);

    static GtkTargetEntry drop_types[] = {
        { dnd_uri, 0, 0 }
    };
    static gint n_drop_types = sizeof(drop_types) / sizeof(drop_types[0]);

    gtk_drag_dest_set(GTK_WIDGET(g->file), (GtkDestDefaults)(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT | GTK_DEST_DEFAULT_DROP),
        drop_types, n_drop_types,
        GDK_ACTION_COPY);

    g_signal_connect(G_OBJECT(g->file), "drag_data_received",
        G_CALLBACK(drop_file), (void*)vtt);

    gtk_drag_dest_set(GTK_WIDGET(g->display), (GtkDestDefaults)(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT | GTK_DEST_DEFAULT_DROP),
        drop_types, n_drop_types,
        GDK_ACTION_COPY);

    g_signal_connect(G_OBJECT(g->display), "drag_data_received",
        G_CALLBACK(drop_file), (void*)vtt);
}

void build_vtt_gui(vtt_class* vtt) {
    GtkWidget* tempbox;
    GtkWidget* tempbox2;
    GtkWidget* tempbox3;
    GtkWidget* dummy;
    char nice_name[256];

    vtt_gui* g;

    g = &vtt->gui;
    vtt->have_gui = 1;

    snprintf(g->style_class, sizeof(g->style_class), "%08lx", (unsigned long)vtt);

    g->css_provider = gtk_css_provider_new();

    g->par_menu = NULL;
    g->ladspa_menu = NULL;

    /* Building Audio Box */
    g->audio_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_show(g->audio_box);

    tempbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_show(tempbox2);
    gtk_box_pack_start(GTK_BOX(g->audio_box), tempbox2, WID_FIX);

    tempbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_set_homogeneous(GTK_BOX(tempbox), TRUE);
    gtk_widget_show(tempbox);
    gtk_box_pack_start(GTK_BOX(tempbox2), tempbox, WID_DYN);

    GtkWidget* pixmap;
    g->audio_minimize = create_top_button(MINIMIZE);
    gtk_box_pack_end(GTK_BOX(tempbox2), g->audio_minimize, WID_FIX);
    gtk_widget_show(g->audio_minimize);

    g->audio_label = gtk_label_new(vtt->name);
    gtk_style_context_add_class(gtk_widget_get_style_context(tempbox2), g->style_class);
    gtk_widget_set_halign(g->audio_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(g->audio_label, 10);
    gtk_widget_show(g->audio_label);
    gtk_box_pack_start(GTK_BOX(tempbox), g->audio_label, WID_DYN);

    nicer_filename(nice_name, vtt->filename);
    g->file = gtk_button_new_with_label(nice_name);
    gtk_style_context_add_class(gtk_widget_get_style_context(g->file), g->style_class);
    gtk_widget_show(g->file);
    gui_set_tooltip(g->file, "Click to Load/Edit/Reload a sample for this turntable. To load you can also drag a file and drop it over this button or the sound data display below.");
    gtk_box_pack_start(GTK_BOX(tempbox), g->file, WID_DYN);

    g->mouse_mapping = gtk_button_new_with_label("Mouse");
    gtk_widget_show(g->mouse_mapping);
    gui_set_tooltip(g->mouse_mapping, "Determines what parameters should be affected on mouse motion in mouse grab mode.");
    gtk_style_context_add_class(gtk_widget_get_style_context(g->mouse_mapping), g->style_class);
    gtk_box_pack_start(GTK_BOX(tempbox2), g->mouse_mapping, WID_FIX);

#ifdef USE_ALSA_MIDI_IN
    g->midi_mapping = gtk_button_new_with_label("MIDI");
    gtk_widget_show(g->midi_mapping);
    gui_set_tooltip(g->midi_mapping, "Determines what parameters should be bound to what MIDI events.");
    gtk_style_context_add_class(gtk_widget_get_style_context(g->midi_mapping), g->style_class);
    gtk_box_pack_start(GTK_BOX(tempbox2), g->midi_mapping, WID_FIX);

    if (!tX_engine::get_instance()->get_midi()->get_is_open()) {
        gtk_widget_set_sensitive(g->midi_mapping, FALSE);
    }
#endif

    tempbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    g->display = gtk_tx_new(vtt->buffer, vtt->samples_in_buffer);
    gtk_box_pack_start(GTK_BOX(tempbox), g->display, WID_DYN);
    gtk_widget_show(g->display);

    g->zoom = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0, 99.0, 1.0);
    gtk_range_set_inverted(GTK_RANGE(g->zoom), TRUE);
    gtk_scale_set_draw_value(GTK_SCALE(g->zoom), TRUE);
    gtk_scale_set_digits(GTK_SCALE(g->zoom), 0);
    gtk_scale_set_value_pos(GTK_SCALE(g->zoom), GTK_POS_BOTTOM);
    gtk_adjustment_set_value(gtk_range_get_adjustment(GTK_RANGE(g->zoom)), 0);

    gui_set_tooltip(g->zoom, "Set the zoom-level for the audio data display.");
    gtk_box_pack_start(GTK_BOX(tempbox), g->zoom, WID_FIX);
    gtk_widget_show(g->zoom);

    gtk_box_pack_start(GTK_BOX(g->audio_box), tempbox, WID_DYN);
    gtk_widget_show(tempbox);

    /* Building Control Box */

    g->control_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_show(g->control_box);

    tempbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_show(tempbox2);
    gtk_box_pack_start(GTK_BOX(g->control_box), tempbox2, WID_FIX);

    g->control_label = gtk_label_new(vtt->name);
    gtk_style_context_add_class(gtk_widget_get_style_context(tempbox2), g->style_class);
    gtk_widget_show(g->control_label);
    gtk_box_pack_start(GTK_BOX(tempbox2), g->control_label, WID_DYN);

    g->control_minimize = create_top_button(MINIMIZE);
    gtk_box_pack_end(GTK_BOX(tempbox2), g->control_minimize, WID_FIX);
    gtk_widget_show(g->control_minimize);

    g->scrolled_win = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(g->scrolled_win), 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(g->scrolled_win),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_show(g->scrolled_win);
    gtk_box_pack_start(GTK_BOX(g->control_box), g->scrolled_win, WID_DYN);

    g->control_subbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#if GTK_CHECK_VERSION(3, 8, 0)
    gtk_container_add(GTK_CONTAINER(g->scrolled_win), g->control_subbox);
#else
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(g->scrolled_win), g->control_subbox);
#endif
    gtk_widget_show(g->control_subbox);

    g->static_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g->static_box), GTK_SELECTION_NONE);
    gtk_widget_show(g->static_box);
    gtk_container_add(GTK_CONTAINER(g->control_subbox), g->static_box);

    /* Main panel */

    tX_panel* p = new tX_panel("Main", g->control_subbox);
    g->main_panel = p;

    GtkWidget* mainbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    g->name = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(g->name), 256);
    gtk_entry_set_text(GTK_ENTRY(g->name), vtt->name);
    gtk_entry_set_width_chars(GTK_ENTRY(g->name), 10);
    gtk_entry_set_max_width_chars(GTK_ENTRY(g->name), 10);
    gtk_container_add_with_properties(GTK_CONTAINER(mainbox), g->name, "expand", TRUE, NULL);
    g->color_button = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(g->color_button), FALSE);
    gui_set_color(vtt, vtt->get_color());
    g_signal_connect(G_OBJECT(g->color_button), "color_set", (GCallback)gui_color_set, vtt);

    gtk_button_set_relief(GTK_BUTTON(g->color_button), GTK_RELIEF_NONE);
    gtk_container_add(GTK_CONTAINER(mainbox), g->color_button);
    gtk_widget_show(g->color_button);
    gtk_widget_show(g->name);

    p->add_client_widget(mainbox);
    gui_set_tooltip(g->name, "Enter the turntable's name here.");
    //gtk_widget_set_size_request(g->name, 40, -1);

    g->del = gtk_button_new_with_label("Delete");
    gui_set_tooltip(g->del, "Click here to annihilate this turntable. All events recorded for this turntable will be erased, too.");
    p->add_client_widget(g->del);

    g->adjust_button = gtk_button_new_with_label("Pitch Adj.");
    gui_set_tooltip(g->adjust_button, "Activate this button to adjust this turntable's speed to the leader turntable's speed.");
    p->add_client_widget(g->adjust_button);

    gtk_list_box_insert(GTK_LIST_BOX(g->static_box), p->get_list_box_row(), -1);

    p = new tX_panel("Playback", g->control_subbox);
    g->trigger_panel = p;

    g->trigger = gtk_button_new_with_label("Trigger");
    gui_set_tooltip(g->trigger, "Click here to trigger this turntable right now. If the audio engine is disabled this turntable will be triggered as soon as the engine is turned on.");
    p->add_client_widget(g->trigger);

    g->stop = gtk_button_new_with_label("Stop");
    gui_set_tooltip(g->stop, "Stop this turntable's playback.");
    p->add_client_widget(g->stop);
    g_signal_connect(G_OBJECT(g->trigger), "button_press_event", (GCallback)tX_seqpar::tX_seqpar_press, &vtt->sp_trigger);
    g_signal_connect(G_OBJECT(g->stop), "button_press_event", (GCallback)tX_seqpar::tX_seqpar_press, &vtt->sp_trigger);

    g->autotrigger = gtk_check_button_new_with_label("Auto");
    p->add_client_widget(g->autotrigger);
    gui_set_tooltip(g->autotrigger, "If turned on, this turntable will be automagically triggered whenever the audio engine is turned on.");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->autotrigger), vtt->autotrigger);

    g->loop = gtk_check_button_new_with_label("Loop");
    p->add_client_widget(g->loop);
    gui_set_tooltip(g->loop, "Enable this option to make the turntable loop the audio data.");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->loop), vtt->loop);
    g_signal_connect(G_OBJECT(g->loop), "button_press_event", (GCallback)tX_seqpar::tX_seqpar_press, &vtt->sp_loop);

    g->sync_leader = gtk_check_button_new_with_label("Lead");
    p->add_client_widget(g->sync_leader);
    gui_set_tooltip(g->sync_leader, "Click here to make this turntable the sync-leader. All turntables marked as sync-followers will be (re-)triggered in relation to the sync-leader. Note that only *one* turntable can be the sync-leader.");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->sync_leader), vtt->is_sync_leader);

    g->sync_follower = gtk_check_button_new_with_label("Follow");
    p->add_client_widget(g->sync_follower);
    gui_set_tooltip(g->sync_follower, "If enabled this turntable will be (re-)triggered in relation to the sync-leader turntable.");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->sync_follower), vtt->is_sync_follower);
    g_signal_connect(G_OBJECT(g->sync_follower), "button_press_event", (GCallback)tX_seqpar::tX_seqpar_press, &vtt->sp_sync_follower);

    g->cycles = GTK_ADJUSTMENT(gtk_adjustment_new(vtt->sync_cycles, 0, 10.0, 1, 1, 0));
    dummy = gtk_spin_button_new(g->cycles, 1.0, 0);
    p->add_client_widget(dummy);
    gui_set_tooltip(dummy, "Determines how often a sync-follower turntable gets triggered. 0 -> this turntable will be triggered with every trigger of the sync-leader table, 1 -> the table will be triggered every 2nd leader trigger and so on.");
    g_signal_connect(G_OBJECT(dummy), "button_press_event", (GCallback)tX_seqpar::tX_seqpar_press, &vtt->sp_sync_cycles);

    gtk_list_box_insert(GTK_LIST_BOX(g->static_box), p->get_list_box_row(), -1);

    dummy = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(dummy), "FX");
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(dummy), FALSE);
    gtk_widget_show(dummy);
    g->fx_button = create_top_button(ADD_ITEM);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(dummy), g->fx_button);
    gtk_widget_show(g->fx_button);
    gui_set_tooltip(g->fx_button, "Click here to load a LADSPA plugin. You will get a menu from which you can choose which plugin to load.");
    gtk_box_pack_start(GTK_BOX(g->control_subbox), dummy, WID_FIX);

    g->fx_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g->fx_box), GTK_SELECTION_NONE);
    gtk_box_pack_start(GTK_BOX(g->control_subbox), g->fx_box, WID_FIX);
    gtk_widget_show(g->fx_box);

    /* Lowpass Panel */

    p = new tX_panel("Lowpass", g->control_subbox, NULL, vtt->get_lp_effect());
    g->lp_panel = p;

    g->lp_enable = gtk_check_button_new_with_label("Enable");
    gui_set_tooltip(g->lp_enable, "Click here to enable the built-in lowpass effect.");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->lp_enable), vtt->lp_enable);
    g_signal_connect(G_OBJECT(g->lp_enable), "button_press_event", (GCallback)tX_seqpar::tX_seqpar_press, &vtt->sp_lp_enable);

    p->add_client_widget(g->lp_enable);

    g->lp_gain = GTK_ADJUSTMENT(gtk_adjustment_new(vtt->lp_gain, 0, 2, 0.1, 0.01, 0.01));
    g->lp_reso = GTK_ADJUSTMENT(gtk_adjustment_new(vtt->lp_reso, 0, 0.99, 0.1, 0.01, 0.01));
    g->lp_freq = GTK_ADJUSTMENT(gtk_adjustment_new(vtt->lp_freq, 0, 1, 0.1, 0.01, 0.01));

    g->lp_gaind = new tX_extdial("Input Gain", g->lp_gain, &vtt->sp_lp_gain);
    p->add_client_widget(g->lp_gaind->get_widget());
    gui_set_tooltip(g->lp_gaind->get_entry(), "Adjust the input gain. with this parameter you can either amplify or damp the input-signal for the lowpass effect.");

    g->lp_freqd = new tX_extdial("Frequency", g->lp_freq, &vtt->sp_lp_freq);
    p->add_client_widget(g->lp_freqd->get_widget());
    gui_set_tooltip(g->lp_freqd->get_entry(), "Adjust the cutoff frequency of the lowpass filter. 0 is 0 Hz, 1 is 22.1 kHz.");

    g->lp_resod = new tX_extdial("Resonance", g->lp_reso, &vtt->sp_lp_reso);
    p->add_client_widget(g->lp_resod->get_widget());
    gui_set_tooltip(g->lp_resod->get_entry(), "Adjust the resonance of the lowpass filter. This value determines how much the signal at the cutoff frequency will be amplified.");

    gtk_list_box_insert(GTK_LIST_BOX(g->fx_box), p->get_list_box_row(), -1);

    /* Echo Panel */

    p = new tX_panel("Echo", g->control_subbox, NULL, vtt->get_ec_effect());
    g->ec_panel = p;

    g->ec_enable = gtk_check_button_new_with_label("Enable");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->ec_enable), vtt->ec_enable);
    p->add_client_widget(g->ec_enable);
    gui_set_tooltip(g->ec_enable, "Enable the built-in echo effect.");
    g_signal_connect(G_OBJECT(g->ec_enable), "button_press_event", (GCallback)tX_seqpar::tX_seqpar_press, &vtt->sp_ec_enable);

    g->ec_length = GTK_ADJUSTMENT(gtk_adjustment_new(vtt->ec_length, 0, 1, 0.1, 0.01, 0.001));
    g->ec_feedback = GTK_ADJUSTMENT(gtk_adjustment_new(vtt->ec_feedback, 0, 1, 0.1, 0.01, 0.001));
    g->ec_pan = GTK_ADJUSTMENT(gtk_adjustment_new(vtt->ec_pan, -1.0, 1, 0.1, 0.01, 0.001));
    g->ec_volume = GTK_ADJUSTMENT(gtk_adjustment_new(vtt->ec_volume, 0.0, 3.0, 0.1, 0.01, 0.001));

    g->ec_lengthd = new tX_extdial("Duration", g->ec_length, &vtt->sp_ec_length);
    p->add_client_widget(g->ec_lengthd->get_widget());
    gui_set_tooltip(g->ec_lengthd->get_entry(), "Adjust the length of the echo buffer.");

    g->ec_feedbackd = new tX_extdial("Feedback", g->ec_feedback, &vtt->sp_ec_feedback);
    p->add_client_widget(g->ec_feedbackd->get_widget());
    gui_set_tooltip(g->ec_feedbackd->get_entry(), "Adjust the feedback of the echo effect. Note that a value of 1 will result in a constant signal.");

    g->ec_volumed = new tX_extdial("Volume", g->ec_volume, &vtt->sp_ec_volume);
    p->add_client_widget(g->ec_volumed->get_widget());
    gui_set_tooltip(g->ec_volumed->get_entry(), "Adjust the volume of the echo effect.");

    g->ec_pand = new tX_extdial("Pan", g->ec_pan, &vtt->sp_ec_pan);
    p->add_client_widget(g->ec_pand->get_widget());
    gui_set_tooltip(g->ec_pand->get_entry(), "Adjust the panning of the echo effect.");

    gtk_list_box_insert(GTK_LIST_BOX(g->fx_box), p->get_list_box_row(), -1);

    dummy = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(dummy), "Stereo FX");
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(dummy), FALSE);
    gtk_widget_show(dummy);
    g->stereo_fx_button = create_top_button(ADD_ITEM);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(dummy), g->stereo_fx_button);
    gtk_widget_show(g->stereo_fx_button);
    gtk_box_pack_start(GTK_BOX(g->control_subbox), dummy, WID_FIX);

    g->stereo_fx_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g->stereo_fx_box), GTK_SELECTION_NONE);
    gtk_box_pack_start(GTK_BOX(g->control_subbox), g->stereo_fx_box, WID_FIX);
    gtk_widget_show(g->stereo_fx_box);

    /* Output */

    tempbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_show(tempbox);
    gtk_box_pack_end(GTK_BOX(g->control_box), tempbox, WID_FIX);

    tempbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show(tempbox2);
    gtk_box_pack_start(GTK_BOX(tempbox), tempbox2, WID_FIX);

    g->pitch = GTK_ADJUSTMENT(gtk_adjustment_new(vtt->rel_pitch, -3, +3, 0.1, 0.01, 0.001));
    g->pan = GTK_ADJUSTMENT(gtk_adjustment_new(0, -1, 1, 0.1, 0.01, 0.001));

    g->pitchd = new tX_extdial("Pitch", g->pitch, &vtt->sp_pitch);
    gui_set_tooltip(g->pitchd->get_entry(), "Adjust this turntable's pitch.");

    gtk_box_pack_start(GTK_BOX(tempbox2), g->pitchd->get_widget(), WID_FIX);

    g->pand = new tX_extdial("Pan", g->pan, &vtt->sp_pan);
    gtk_box_pack_start(GTK_BOX(tempbox2), g->pand->get_widget(), WID_FIX);
    gui_set_tooltip(g->pand->get_entry(), "Specifies the position of this turntable within the stereo spectrum: -1 -> left, 0-> center, 1->right.");

    tempbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_show(tempbox3);

    g->mute = gtk_check_button_new_with_label("M");
    gtk_box_pack_start(GTK_BOX(tempbox3), g->mute, WID_FIX);
    g_signal_connect(G_OBJECT(g->mute), "clicked", (GCallback)mute_volume, vtt);
    gtk_widget_show(g->mute);
    gui_set_tooltip(g->mute, "Mute this turntable's mixer output.");

    g->solo = gtk_check_button_new_with_label("S");
    gtk_box_pack_start(GTK_BOX(tempbox3), g->solo, WID_FIX);
    g_signal_connect(G_OBJECT(g->solo), "clicked", (GCallback)solo_vtt, vtt);
    gtk_widget_show(g->solo);
    gui_set_tooltip(g->solo, "Allow only this and other solo-switched turntabels' signal to be routed to the mixer.");

    gtk_box_pack_start(GTK_BOX(tempbox2), tempbox3, WID_FIX);

    tempbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_show(tempbox2);
    gtk_box_pack_start(GTK_BOX(tempbox), tempbox2, WID_FIX);

    g->volume = GTK_ADJUSTMENT(gtk_adjustment_new(2.0 - vtt->rel_volume, 0, 2, 0.01, 0.01, 0.01));
    dummy = gtk_scale_new(GTK_ORIENTATION_VERTICAL, GTK_ADJUSTMENT(g->volume));
    gtk_scale_set_draw_value(GTK_SCALE(dummy), FALSE);
    gui_set_tooltip(dummy, "Adjust this turntable's volume.");
    g_signal_connect(G_OBJECT(dummy), "button_press_event", (GCallback)tX_seqpar::tX_seqpar_press, &vtt->sp_volume);

    gtk_box_pack_start(GTK_BOX(tempbox2), dummy, WID_FIX);
    gtk_widget_show(dummy);

    g->flash = gtk_tx_flash_new();
    gtk_box_pack_start(GTK_BOX(tempbox2), g->flash, WID_FIX);
    gtk_widget_show(g->flash);

    gui_connect_signals(vtt);

    g->audio_minimized_panel_bar_button = NULL;
    g->control_minimized_panel_bar_button = NULL;
    g->file_menu = NULL;
    g->mouse_mapping_menu = NULL;
    g->mouse_mapping_menu_x = NULL;
    g->mouse_mapping_menu_y = NULL;

    g->adjust_dialog = NULL;

    gui_set_name(vtt, vtt->name);
}

void fx_up(GtkWidget* wid, vtt_fx* effect) {
    vtt_class* vtt;

    vtt = (vtt_class*)effect->get_vtt();
    vtt->effect_up(effect);
}

void fx_down(GtkWidget* wid, vtt_fx* effect) {
    vtt_class* vtt;

    vtt = (vtt_class*)effect->get_vtt();
    vtt->effect_down(effect);
}

void fx_kill(GtkWidget* wid, vtt_fx_ladspa* effect) {
    vtt_class* vtt;

    vtt = (vtt_class*)effect->get_vtt();
    vtt->effect_remove(effect);
}

int gtk_box_get_widget_pos(GtkBox* box, GtkWidget* child) {
    int i = 0;
    GList* list;

    list = gtk_container_get_children(GTK_CONTAINER(box));
    while (list) {
        GtkWidget* child_widget = (GtkWidget*)list->data;
        if (child_widget == child)
            break;
        list = list->next;
        i++;
    }
    return i;
}

void vg_move_fx_panel_up(tX_panel* panel, vtt_class* vtt, bool stereo) {
    GtkWidget* list_box = (stereo ? vtt->gui.stereo_fx_box : vtt->gui.fx_box);
    GtkWidget* row = panel->get_list_box_row();

    int pos = gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(row));
    g_object_ref(row);
    gtk_container_remove(GTK_CONTAINER(list_box), row);
    gtk_list_box_insert(GTK_LIST_BOX(list_box), row, pos - 1);
    g_object_unref(row);
}

void vg_move_fx_panel_down(tX_panel* panel, vtt_class* vtt, bool stereo) {
    GtkWidget* list_box = (stereo ? vtt->gui.stereo_fx_box : vtt->gui.fx_box);
    GtkWidget* row = panel->get_list_box_row();

    int pos = gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(row));
    g_object_ref(row);
    gtk_container_remove(GTK_CONTAINER(list_box), row);
    gtk_list_box_insert(GTK_LIST_BOX(list_box), row, pos + 1);
    g_object_unref(row);
}

void vg_toggle_drywet(GtkWidget* wid, vtt_fx* effect) {
    tX_panel* panel = effect->get_panel();
    effect->toggle_drywet();

    if (effect->has_drywet_feature() == DRYWET_ACTIVE) {
        gtk_widget_hide(panel->get_add_drywet_button());
        gtk_widget_show(panel->get_remove_drywet_button());
    } else {
        gtk_widget_show(panel->get_add_drywet_button());
        gtk_widget_hide(panel->get_remove_drywet_button());
    }
}

void vg_create_fx_gui(vtt_class* vtt, vtt_fx_ladspa* effect, LADSPA_Plugin* plugin) {
    char buffer[1024];

    vtt_gui* g;
    g = &vtt->gui;
    tX_panel* p;
    list<tX_seqpar_vttfx*>::iterator sp;

    strcpy(buffer, plugin->getName());
    if (strlen(buffer) > 8) {
        buffer[7] = '.';
        buffer[8] = '.';
        buffer[9] = '.';
        buffer[10] = 0;
    }

    p = new tX_panel(buffer, g->control_subbox, G_CALLBACK(fx_kill), effect);

    for (sp = effect->controls.begin(); sp != effect->controls.end(); sp++) {
        if ((strcmp((*sp)->get_label_name(), "Enable") == 0) && ((effect->has_drywet_feature() != NOT_DRYWET_CAPABLE))) {
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_container_add_with_properties(GTK_CONTAINER(box), (*sp)->get_widget(), "expand", TRUE, NULL);
            gtk_widget_show((*sp)->get_widget());

            g_signal_connect(G_OBJECT(p->get_add_drywet_button()), "clicked", G_CALLBACK(vg_toggle_drywet), effect);
            gtk_widget_set_tooltip_text(p->get_add_drywet_button(), "Click to add Dry/Wet controls for this effect.");
            gtk_container_add(GTK_CONTAINER(box), p->get_add_drywet_button());
            if (effect->has_drywet_feature() == DRYWET_AVAILABLE) {
                gtk_widget_show(p->get_add_drywet_button());
            }

            g_signal_connect(G_OBJECT(p->get_remove_drywet_button()), "clicked", G_CALLBACK(vg_toggle_drywet), effect);
            gtk_widget_set_tooltip_text(p->get_remove_drywet_button(), "Click to remove Dry/Wet controls for this effect.");
            gtk_container_add(GTK_CONTAINER(box), p->get_remove_drywet_button());
            if (effect->has_drywet_feature() == DRYWET_ACTIVE) {
                gtk_widget_show(p->get_remove_drywet_button());
            }

            p->add_client_widget(box);
        } else {
            p->add_client_widget((*sp)->get_widget());
        }
    }

    effect->set_panel_widget(p->get_widget());
    effect->set_panel(p);

    gtk_list_box_insert(GTK_LIST_BOX(effect->is_stereo() ? g->stereo_fx_box : g->fx_box), p->get_list_box_row(), -1);
}

void gui_set_filename(vtt_class* vtt, char* newname) {
    gtk_button_set_label(GTK_BUTTON(vtt->gui.file), newname);
}

void gui_update_display(vtt_class* vtt) {
    nicer_filename(global_filename_buffer, vtt->filename);
    gtk_button_set_label(GTK_BUTTON(vtt->gui.file), global_filename_buffer);
    gtk_tx_set_data(GTK_TX(vtt->gui.display), vtt->buffer, vtt->samples_in_buffer);
}

void gui_hide_control_panel(vtt_class* vtt, bool hide) {
    char tooltip[2048];

    if (hide) {
        gtk_widget_hide(vtt->gui.control_box);
        vtt->gui.control_minimized_panel_bar_button = tx_xpm_button_new(MIN_CONTROL, vtt->name, 0, &vtt->gui.control_minimized_panel_bar_label);
        gtk_style_context_add_class(gtk_widget_get_style_context(vtt->gui.control_minimized_panel_bar_button), vtt->gui.style_class);
        g_signal_connect(G_OBJECT(vtt->gui.control_minimized_panel_bar_button), "clicked", (GCallback)unminimize_control_panel, vtt);
        gtk_widget_show(vtt->gui.control_minimized_panel_bar_button);
        snprintf(tooltip, TOOLTIP_LENGTH, "Show \"%s\" control panel.", vtt->name);
        gui_set_tooltip(vtt->gui.control_minimized_panel_bar_button, tooltip);
        add_to_panel_bar(vtt->gui.control_minimized_panel_bar_button);
    } else {
        gtk_widget_show(vtt->gui.control_box);
        remove_from_panel_bar(vtt->gui.control_minimized_panel_bar_button);
        if (!tX_shutdown)
            gtk_widget_destroy(vtt->gui.control_minimized_panel_bar_button);
        vtt->gui.control_minimized_panel_bar_button = NULL;
    }
}

void gui_hide_audio_panel(vtt_class* vtt, bool hide) {
    char tooltip[2048];

    if (hide) {
        gtk_widget_hide(vtt->gui.audio_box);
        vtt->gui.audio_minimized_panel_bar_button = tx_xpm_button_new(MIN_AUDIO, vtt->name, 0, &vtt->gui.audio_minimized_panel_bar_label);
        gtk_style_context_add_class(gtk_widget_get_style_context(vtt->gui.audio_minimized_panel_bar_button), vtt->gui.style_class);
        g_signal_connect(G_OBJECT(vtt->gui.audio_minimized_panel_bar_button), "clicked", (GCallback)unminimize_audio_panel, vtt);
        gtk_widget_show(vtt->gui.audio_minimized_panel_bar_button);
        snprintf(tooltip, TOOLTIP_LENGTH, "Show \"%s\" audio panel.", vtt->name);
        gui_set_tooltip(vtt->gui.audio_minimized_panel_bar_button, tooltip);
        add_to_panel_bar(vtt->gui.audio_minimized_panel_bar_button);
    } else {
        gtk_widget_show(vtt->gui.audio_box);
        remove_from_panel_bar(vtt->gui.audio_minimized_panel_bar_button);
        if (!tX_shutdown)
            gtk_widget_destroy(vtt->gui.audio_minimized_panel_bar_button);
        vtt->gui.audio_minimized_panel_bar_button = NULL;
    }
}

void delete_gui(vtt_class* vtt) {
    if (vtt->gui.control_minimized_panel_bar_button != NULL)
        gui_hide_control_panel(vtt, false);
    if (vtt->gui.audio_minimized_panel_bar_button != NULL)
        gui_hide_audio_panel(vtt, false);

    delete vtt->gui.main_panel;
    delete vtt->gui.trigger_panel;

    delete vtt->gui.pitchd;
    delete vtt->gui.pand;

    delete vtt->gui.lp_gaind;
    delete vtt->gui.lp_resod;
    delete vtt->gui.lp_freqd;
    delete vtt->gui.lp_panel;

    delete vtt->gui.ec_lengthd;
    delete vtt->gui.ec_feedbackd;
    delete vtt->gui.ec_volumed;
    delete vtt->gui.ec_pand;
    delete vtt->gui.ec_panel;

    gtk_widget_destroy(vtt->gui.control_box);
    gtk_widget_destroy(vtt->gui.audio_box);
    if (vtt->gui.file_menu)
        gtk_widget_destroy(vtt->gui.file_menu);
    if (vtt->gui.mouse_mapping_menu)
        gtk_widget_destroy(vtt->gui.mouse_mapping_menu);
    if (vtt->gui.ladspa_menu)
        gtk_widget_destroy(vtt->gui.ladspa_menu);

    gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(vtt->gui.css_provider));
}

void cleanup_vtt(vtt_class* vtt) {
    gtk_tx_cleanup_pos_display(GTK_TX(vtt->gui.display));
    gtk_tx_flash_set_level(vtt->gui.flash, 0.0, 0.0);
    gtk_tx_flash_clear(vtt->gui.flash);
    vtt->cleanup_required = false;
}

void update_all_vtts() {
    list<vtt_class*>::iterator vtt;
    f_prec temp, temp2;

    for (vtt = vtt_class::main_list.begin(); vtt != vtt_class::main_list.end(); vtt++) {
        if ((*vtt)->is_playing) {
            gtk_tx_update_pos_display(GTK_TX((*vtt)->gui.display), (*vtt)->pos_i, (*vtt)->mute);
            temp = (*vtt)->max_value * (*vtt)->res_volume * vtt_class::vol_channel_adjust;
            temp2 = (*vtt)->max_value2 * (*vtt)->res_volume * vtt_class::vol_channel_adjust;
            //			tX_msg("Setting value: %f, %f -> %f; %f, %f -> %f (%f)\n",
            //				(*vtt)->max_value, (*vtt)->res_volume, temp,
            //				(*vtt)->max_value2, (*vtt)->res_volume, temp2,
            //				vtt_class::vol_channel_adjust
            //			);
            gtk_tx_flash_set_level((*vtt)->gui.flash, temp, temp2);

            (*vtt)->max_value = 0;
            (*vtt)->max_value2 = 0;
        }

        if ((*vtt)->needs_cleaning_up()) {
            cleanup_vtt((*vtt));
        }
    }
}

void cleanup_all_vtts() {
    list<vtt_class*>::iterator vtt;

    for (vtt = vtt_class::main_list.begin(); vtt != vtt_class::main_list.end(); vtt++) {
        if ((*vtt)->buffer)
            gtk_tx_cleanup_pos_display(GTK_TX((*vtt)->gui.display));
        gtk_tx_flash_set_level((*vtt)->gui.flash, 0.0, 0.0);
        gtk_tx_flash_clear((*vtt)->gui.flash);
    }
}

void gui_clear_leader_button(vtt_class* vtt) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vtt->gui.sync_leader), 0);
}

void gui_show_focus(vtt_class* vtt, int show) {
    gtk_tx_show_focus(GTK_TX(vtt->gui.display), show);
}

#define vgui (*vtt)->gui
#define v (*vtt)

void vg_enable_critical_buttons(int enable) {
    list<vtt_class*>::iterator vtt;
    for (vtt = vtt_class::main_list.begin(); vtt != vtt_class::main_list.end(); vtt++) {
        gtk_widget_set_sensitive(vgui.del, enable);
        gtk_widget_set_sensitive(vgui.sync_leader, enable);
    }
}

void vg_init_all_non_seqpars() {
    list<vtt_class*>::iterator vtt;

    for (vtt = vtt_class::main_list.begin(); vtt != vtt_class::main_list.end(); vtt++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON((*vtt)->gui.autotrigger), (*vtt)->autotrigger);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON((*vtt)->gui.sync_leader), (*vtt)->is_sync_leader);
    }
}
