#include "tX_extdial.h"
#include "tX_seqpar.h"
#include <string.h>

#define WID_DYN TRUE, TRUE, 0
#define WID_FIX FALSE, FALSE, 0

GtkSignalFunc tX_extdial :: f_entry(GtkWidget *w, tX_extdial *ed)
{

	strcpy(ed->sval, gtk_entry_get_text(GTK_ENTRY(ed->entry)));
	ed->s2f();
	gtk_adjustment_set_value(ed->adj, ed->fval);
	return NULL;
}

GtkSignalFunc tX_extdial :: f_adjustment(GtkWidget *w, tX_extdial *ed)
{
	ed->fval=ed->adj->value;
	ed->f2s();
	gtk_entry_set_text(GTK_ENTRY(ed->entry), ed->sval);
	return NULL;	
}

tX_extdial :: tX_extdial(const char *l, GtkAdjustment *a, tX_seqpar * sp, bool text_below)
{
	adj=a;
	fval=adj->value;
	f2s();
	if (l) label=gtk_label_new(l);
	dial=gtk_tx_dial_new(adj);
	entry=gtk_entry_new_with_max_length(5);
	gtk_entry_set_text(GTK_ENTRY(entry), sval);
	ignore_adj=0;
	
	eventbox=gtk_event_box_new();		
	mainbox=gtk_vbox_new(FALSE, text_below ? 5 : 0);
	gtk_container_add(GTK_CONTAINER(eventbox), mainbox);
	
	subbox=gtk_hbox_new(TRUE, 0);
	gtk_box_pack_start(GTK_BOX(subbox), dial, WID_FIX);
	gtk_box_pack_start(GTK_BOX(mainbox), subbox, WID_FIX);
	gtk_box_pack_start(GTK_BOX(text_below ? mainbox : subbox), entry, WID_DYN);
	if (l) gtk_box_pack_start(GTK_BOX(mainbox), label, WID_FIX);
	
	if (l) gtk_widget_show(label);
	gtk_widget_show(entry);
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 4);
	gtk_widget_show(dial);
	gtk_widget_show(subbox);
	gtk_widget_show(mainbox);
	gtk_widget_show(eventbox);
	
	gtk_signal_connect(GTK_OBJECT(entry), "activate", (GtkSignalFunc) tX_extdial::f_entry, (void *) this);
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed", (GtkSignalFunc) tX_extdial::f_adjustment, (void *) this);
	
	if (sp) {
		gtk_signal_connect(GTK_OBJECT(dial), "button_press_event", (GtkSignalFunc) tX_seqpar::tX_seqpar_press, sp);
		gtk_signal_connect(GTK_OBJECT(entry), "button_press_event", (GtkSignalFunc) tX_seqpar::tX_seqpar_press, sp);			
		gtk_signal_connect(GTK_OBJECT(eventbox), "button_press_event", (GtkSignalFunc) tX_seqpar::tX_seqpar_press, sp);			
		//if (l) gtk_signal_connect(GTK_OBJECT(label), "button_press_event", (GtkSignalFunc) tX_seqpar::tX_seqpar_press, sp);	
	}
}

tX_extdial :: ~tX_extdial()
{
	gtk_object_destroy(GTK_OBJECT(adj));
	gtk_widget_destroy(entry);
	gtk_widget_destroy(label);
	gtk_widget_destroy(dial);
	gtk_widget_destroy(subbox);
	gtk_widget_destroy(mainbox);
	gtk_widget_destroy(eventbox);	
}
