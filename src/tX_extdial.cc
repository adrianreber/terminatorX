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

*/

#include "tX_extdial.h"
#include "tX_seqpar.h"
#include <string.h>

#define WID_DYN TRUE, TRUE, 0
#define WID_FIX FALSE, FALSE, 0

GCallback tX_extdial ::f_entry(GtkWidget* w, tX_extdial* ed) {
    strcpy(ed->sval, gtk_entry_get_text(GTK_ENTRY(ed->entry)));
    ed->s2f();
    gtk_adjustment_set_value(ed->adj, ed->fval);
    return NULL;
}

GCallback tX_extdial ::f_adjustment(GtkWidget* w, tX_extdial* ed) {
    ed->fval = gtk_adjustment_get_value(ed->adj);
    ed->f2s();
    gtk_entry_set_text(GTK_ENTRY(ed->entry), ed->sval);
    return NULL;
}

tX_extdial ::tX_extdial(const char* l, GtkAdjustment* a, tX_seqpar* sp, bool text_below, bool hide_entry) {
    adj = a;
    fval = gtk_adjustment_get_value(adj);
    f2s();
    if (l) {
        label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), l);
    }
    dial = gtk_tx_dial_new(adj);
    entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 5);
    gtk_entry_set_text(GTK_ENTRY(entry), sval);
#if GTK_CHECK_VERSION(2, 4, 0)
    gtk_entry_set_alignment(GTK_ENTRY(entry), 0.5);
#endif
    ignore_adj = 0;

    eventbox = gtk_event_box_new();
    mainbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, text_below ? 5 : 0);

    gtk_container_add(GTK_CONTAINER(eventbox), mainbox);

    subbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(subbox), TRUE);
    gtk_box_pack_start(GTK_BOX(subbox), dial, WID_FIX);
    gtk_box_pack_start(GTK_BOX(mainbox), subbox, WID_FIX);
    if (text_below) {
        gtk_box_pack_start(GTK_BOX(mainbox), entry, WID_DYN);
    } else {
        gtk_box_pack_start(GTK_BOX(subbox), entry, TRUE, TRUE, 3);
    }
    if (l)
        gtk_box_pack_start(GTK_BOX(mainbox), label, WID_FIX);

    if (l)
        gtk_widget_show(label);
    if (!hide_entry) {
        gtk_widget_show(entry);
    }
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 4);
    gtk_entry_set_max_width_chars(GTK_ENTRY(entry), 4);
    gtk_widget_show(dial);
    gtk_widget_show(subbox);
    gtk_widget_show(mainbox);
    gtk_widget_show(eventbox);

    g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(tX_extdial::f_entry), (void*)this);
    g_signal_connect(G_OBJECT(adj), "value_changed", G_CALLBACK(tX_extdial::f_adjustment), (void*)this);

    if (sp) {
        g_signal_connect(G_OBJECT(dial), "button_press_event", G_CALLBACK(tX_seqpar::tX_seqpar_press), sp);
        g_signal_connect(G_OBJECT(entry), "button_press_event", G_CALLBACK(tX_seqpar::tX_seqpar_press), sp);
        g_signal_connect(G_OBJECT(eventbox), "button_press_event", G_CALLBACK(tX_seqpar::tX_seqpar_press), sp);
    }
}

tX_extdial ::~tX_extdial() {
    gtk_widget_destroy(entry);
    gtk_widget_destroy(label);
    gtk_widget_destroy(dial);
    gtk_widget_destroy(subbox);
    gtk_widget_destroy(mainbox);
    gtk_widget_destroy(eventbox);
}
