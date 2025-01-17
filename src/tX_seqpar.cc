/*
    terminatorX - realtime audio scratching software
    Copyright (C) 1999, 2000 Alexander König

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

    File: tX_seqpar.cc

    Description: This implements the "sequenceable parameters".
*/

#include "tX_seqpar.h"
#include "tX_engine.h"
#include "tX_extdial.h"
#include "tX_global.h"
#include "tX_maingui.h"
#include "tX_sequencer.h"
#include "tX_vtt.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define TX_SEQPAR_DEFAULT_SCALE 0.05

list<tX_seqpar*>* tX_seqpar ::all = NULL;
list<tX_seqpar*>* tX_seqpar ::update = NULL;
pthread_mutex_t tX_seqpar ::update_lock = PTHREAD_MUTEX_INITIALIZER;

#define tt ((vtt_class*)vtt)

#ifdef DEBUG_SEQPAR_LOCK
#define seqpar_mutex_lock(lock)         \
    {                                   \
        tX_debug("lock: %i", __LINE__); \
        pthread_mutex_lock(lock);       \
    }
#define seqpar_mutex_unlock(lock)         \
    {                                     \
        tX_debug("unlock: %i", __LINE__); \
        pthread_mutex_unlock(lock);       \
    }
#else
#define seqpar_mutex_lock(lock) pthread_mutex_lock(lock)
#define seqpar_mutex_unlock(lock) pthread_mutex_unlock(lock)
#endif

tX_seqpar ::tX_seqpar()
    : bound_midi_event() {
    touched = 0;
    gui_active = 1;
    vtt = NULL;
    max_value = 0;
    min_value = 0;
    scale_value = 0;
    is_boolean = false;
    is_mappable = 1;
    if (!all) {
        all = new list<tX_seqpar*>();
        update = new list<tX_seqpar*>();
    }
    all->push_back(this);
    last_event_recorded = NULL;

    midi_lower_bound_set = false;
    midi_upper_bound_set = false;
    reverse_midi = false;
}

void tX_seqpar ::set_mapping_parameters(float max, float min, float scale, int mappable) {
    max_value = max;
    min_value = min;
    scale_value = scale;
    is_mappable = mappable;
}

void tX_seqpar ::handle_mouse_input(float adjustment) {
    float tmpvalue;

    tmpvalue = get_value() + adjustment * scale_value;
    if (tmpvalue > max_value)
        tmpvalue = max_value;
    if (tmpvalue < min_value)
        tmpvalue = min_value;

    /*printf("Handling %s, max %f, min %f, scale %f,  val: %f\n", get_name(), max_value, min_value, scale_value, tmpvalue);*/

    receive_input_value(tmpvalue);
}

#ifdef USE_ALSA_MIDI_IN
void tX_seqpar ::handle_midi_input(const tX_midievent& event) {
    double tmpvalue = -1000;
    double max = max_value;
    double min = min_value;

    if (midi_upper_bound_set) {
        max = midi_upper_bound;
    }

    if (midi_lower_bound_set) {
        min = midi_lower_bound;
    }

    if (!is_boolean) {
        switch (event.type) {
        case tX_midievent::CC:
        case tX_midievent::CC14:
        case tX_midievent::PITCHBEND:
        case tX_midievent::RPN:
        case tX_midievent::NRPN:
            tmpvalue = event.value * (max - min) + min;
            break;
        case tX_midievent::NOTE:
            tmpvalue = event.is_noteon;
            break;
        default:
            return;
        }

        if (reverse_midi)
            tmpvalue = (max - tmpvalue) + min;

        if (tmpvalue > max)
            tmpvalue = max;
        else if (tmpvalue < min)
            tmpvalue = min;
    } else {
        tmpvalue = event.value;

        if (reverse_midi) {
            if (tmpvalue > 0)
                tmpvalue = 0;
            else
                tmpvalue = 1;
        }
    }

    touch();

    /* Not using receive() as we want immediate GUI update... */
    do_exec(tmpvalue);
    record_value(tmpvalue);
    do_update_graphics();
}
#endif

void tX_seqpar ::set_vtt(void* mytt) {
    vtt = mytt;
}

tX_seqpar ::~tX_seqpar() {
    seqpar_mutex_lock(&update_lock);
    update->remove(this);
    seqpar_mutex_unlock(&update_lock);
    sequencer.delete_all_events_for_sp(this, tX_sequencer::DELETE_ALL);
    all->remove(this);
}

void tX_seqpar ::do_touch() {
    if (sequencer.is_recording()) {
        touched = 1;
        touch_timestamp = sequencer.get_timestamp();
    }
}

void tX_seqpar ::untouch_all() {
    list<tX_seqpar*>::iterator sp;

    for (sp = all->begin(); sp != all->end(); sp++) {
        (*sp)->untouch();
    }
}

void tX_seqpar ::create_persistence_ids() {
    list<tX_seqpar*>::iterator sp;
    int pid = 0;

    for (sp = all->begin(); sp != all->end(); sp++) {
        pid++;
        (*sp)->set_persistence_id(pid);
    }
}

tX_seqpar* tX_seqpar ::get_sp_by_persistence_id(unsigned int pid) {
    list<tX_seqpar*>::iterator sp;

    for (sp = all->begin(); sp != all->end(); sp++) {
        if ((*sp)->get_persistence_id() == pid)
            return (*sp);
    }

    //tX_error("failed to resolve persistence id [%i].", pid);
    return NULL;
}

void tX_seqpar ::record_value(const float value) {
#define last_event ((tX_event*)last_event_recorded)

    /* recording more than one event per seqpar for
	  one timestamp doesn't make sense... so if the 
	  last_event_recorded was for the current timestamp
	  we simply set that event's value to the current one.
	*/
    if ((last_event) && (last_event->get_timestamp() == sequencer.get_timestamp())) {
        last_event->set_value(value);
    } else {
        last_event_recorded = (void*)sequencer.record(this, value);
    }
}

void tX_seqpar ::receive_gui_value(const float value) {
    if (gui_active) {
        touch();
        do_exec(value);
        record_value(value);
    }
}

void tX_seqpar ::receive_input_value(const float value) {
    touch();
    exec_value(value);
    record_value(value);
}

void tX_seqpar ::receive_forward_value(const float value) {
    fwd_value = value;
}

void tX_seqpar ::materialize_forward_values() {
    list<tX_seqpar*>::iterator sp;

    for (sp = all->begin(); sp != all->end(); sp++) {
        (*sp)->exec_value((*sp)->fwd_value);
    }
    gdk_display_flush(gdk_display_get_default());
}

const char* tX_seqpar ::get_vtt_name() {
    if (vtt)
        return tt->name;
    else
        return "Main Track";
}

void tX_seqpar ::restore_meta(xmlNodePtr node) {
    char* buffer;
    double temp;

    buffer = (char*)xmlGetProp(node, (xmlChar*)"id");
    if (buffer) {
        sscanf(buffer, "%i", &persistence_id);
    } else {
        tX_error("no ID for seqpar %s", this->get_name());
    }

    buffer = (char*)xmlGetProp(node, (xmlChar*)"midiUpperBound");
    if (buffer) {
        sscanf(buffer, "%lf", &temp);
        set_upper_midi_bound(temp);
    }

    buffer = (char*)xmlGetProp(node, (xmlChar*)"midiLowerBound");
    if (buffer) {
        sscanf(buffer, "%lf", &temp);
        set_lower_midi_bound(temp);
    }

    buffer = (char*)xmlGetProp(node, (xmlChar*)"midiType");
    if (buffer) {
        if (strcmp("cc", buffer) == 0) {
            bound_midi_event.type = tX_midievent::CC;
        } else if (strcmp("note", buffer) == 0) {
            bound_midi_event.type = tX_midievent::NOTE;
        } else if (strcmp("pitchbend", buffer) == 0) {
            bound_midi_event.type = tX_midievent::PITCHBEND;
        } else if (strcmp("cc14", buffer) == 0) {
            bound_midi_event.type = tX_midievent::CC14;
        } else if (strcmp("rpn", buffer) == 0) {
            bound_midi_event.type = tX_midievent::RPN;
        } else if (strcmp("nrpn", buffer) == 0) {
            bound_midi_event.type = tX_midievent::NRPN;
        } else {
            tX_error("unknown midiType \"%s\" for seqpar %s", buffer, this->get_name());
        }

        buffer = (char*)xmlGetProp(node, (xmlChar*)"midiChannel");
        if (buffer) {
            sscanf(buffer, "%i", &bound_midi_event.channel);
        } else {
            tX_error("no midiChannel for seqpar %s", this->get_name());
        }

        buffer = (char*)xmlGetProp(node, (xmlChar*)"midiNumber");
        if (buffer) {
            sscanf(buffer, "%i", &bound_midi_event.number);
        } else {
            tX_error("no midiNumber for seqpar %s", this->get_name());
        }
    }

    buffer = (char*)xmlGetProp(node, (xmlChar*)"midiReverse");
    if (buffer) {
        if (strcmp("true", buffer) == 0) {
            reverse_midi = true;
        } else {
            reverse_midi = false;
        }
    }

    /* else: no MIDI init.... */
}

void tX_seqpar ::store_meta(FILE* rc, gzFile rz) {
    char buffer[512];
    char buffer2[256];

    if (bound_midi_event.type != tX_midievent::NONE) {
        const char* type;

        switch (bound_midi_event.type) {
        case tX_midievent::NOTE:
            type = "note";
            break;
        case tX_midievent::CC:
            type = "cc";
            break;
        case tX_midievent::PITCHBEND:
            type = "pitchbend";
            break;
        case tX_midievent::CC14:
            type = "cc14";
            break;
        case tX_midievent::RPN:
            type = "rpn";
            break;
        case tX_midievent::NRPN:
            type = "nrpn";
            break;
        default:
            type = "error";
        }
        sprintf(buffer, "id=\"%i\" midiType=\"%s\" midiChannel=\"%i\" midiNumber=\"%i\" midiReverse=\"%s\"", persistence_id, type, bound_midi_event.channel, bound_midi_event.number, (reverse_midi ? "true" : "false"));
    } else {
        sprintf(buffer, "id=\"%i\"", persistence_id);
    }

    if (midi_upper_bound_set) {
        sprintf(buffer2, " midiUpperBound=\"%lf\"", midi_upper_bound);
        strcat(buffer, buffer2);
    }

    if (midi_lower_bound_set) {
        sprintf(buffer2, " midiLowerBound=\"%lf\"", midi_lower_bound);
        strcat(buffer, buffer2);
    }

    tX_store("%s", buffer);
}

const char* tX_seqpar ::get_name() {
    return "This string means trouble!";
}

float tX_seqpar ::get_value() {
    printf("Ooops. tX_seqpar::get_value() called. Trouble.");
    return 0.0;
}

void tX_seqpar ::update_graphics(bool gtk_flush) {
    gui_active = 0;
    do_update_graphics();
    if (gtk_flush) {
        while (gtk_events_pending())
            gtk_main_iteration(); /* gtk_flush */
    }
    gui_active = 1;
}

void tX_seqpar ::update_all_graphics() {
    list<tX_seqpar*>::iterator sp;

    seqpar_mutex_lock(&update_lock);

    if (!update->size()) {
        seqpar_mutex_unlock(&update_lock);
        return;
    }

    for (sp = update->begin(); sp != update->end(); sp++) {
        (*sp)->update_graphics(false);
    }
    update->erase(update->begin(), update->end());
    seqpar_mutex_unlock(&update_lock);

    while (gtk_events_pending())
        gtk_main_iteration();
}

void tX_seqpar ::init_all_graphics() {
    list<tX_seqpar*>::iterator sp;

    seqpar_mutex_lock(&update_lock);

    for (sp = all->begin(); sp != all->end(); sp++) {
        (*sp)->update_graphics();
    }
    seqpar_mutex_unlock(&update_lock);

    while (gtk_events_pending())
        gtk_main_iteration();
}

void tX_seqpar_update ::exec_value(const float value) {
    do_exec(value);
    seqpar_mutex_lock(&update_lock);
    update->push_front(this);
    seqpar_mutex_unlock(&update_lock);
}

void tX_seqpar_no_update ::exec_value(const float value) {
    do_exec(value);
}

void tX_seqpar_no_update ::do_update_graphics() {
    /* NOP */
}

void tX_seqpar_no_update_active_forward ::receive_forward_value(const float value) {
    fwd_value = value;
    do_exec(value);
}

void tX_seqpar_update_active_forward ::receive_forward_value(const float value) {
    fwd_value = value;
    do_exec(value);
}

/* "real" classes */

/**** Sequencable Parameter: MASTER VOLUME ****/

tX_seqpar_main_volume ::tX_seqpar_main_volume() {
    set_mapping_parameters(2.5, 0, 0.1, 0);
}

void tX_seqpar_main_volume ::do_exec(const float value) {
    vtt_class ::set_main_volume(value);
}

void tX_seqpar_main_volume ::do_update_graphics() {
    gtk_adjustment_set_value(volume_adj, vtt_class::main_volume);
}

const char* tX_seqpar_main_volume ::get_name() {
    return "Main Volume";
}

/**** Sequencable Parameter: MASTER PITCH ****/

tX_seqpar_main_pitch ::tX_seqpar_main_pitch() {
    set_mapping_parameters(3.0, -3.0, 0.1, 0);
}

void tX_seqpar_main_pitch ::do_exec(const float value) {
    vtt_class ::set_main_pitch(value);
}

void tX_seqpar_main_pitch ::do_update_graphics() {
    gtk_adjustment_set_value(pitch_adj, globals.pitch);
}

const char* tX_seqpar_main_pitch ::get_name() {
    return "Main Pitch";
}

/**** Sequencable Parameter: TURNTABLE SPEED ****/

tX_seqpar_vtt_speed ::tX_seqpar_vtt_speed() {
    // min max scale are not required for this parameter
    set_mapping_parameters(3.0, -3.0, 0.1, 1);
}

/* speed works differently so we need an extra input-handler */

void tX_seqpar_vtt_speed ::handle_mouse_input(float adjustment) {
    if (tt->do_scratch)
        tt->sp_speed.receive_input_value(adjustment);
    tt->sense_cycles = globals.sense_cycles;
}

void tX_seqpar_vtt_speed ::do_exec(const float value) {
    tt->speed = value * tt->audiofile_pitch_correction;
}

const char* tX_seqpar_vtt_speed ::get_name() {
    return "Speed (Scratching)";
}

/**** Sequencable Parameter: TURNTABLE SPIN ****/

tX_seqpar_spin ::tX_seqpar_spin() {
    set_mapping_parameters(1, 0, 0, 0);
}

void tX_seqpar_spin ::do_exec(const float value) {
    if (value > 0)
        tt->speed = tt->res_pitch;
    else
        tt->speed = 0;
}

const char* tX_seqpar_spin ::get_name() {
    return "Motor Spin (On/Off)";
}

/**** Sequencable Parameter: TURNTABLE VOLUME ****/

tX_seqpar_vtt_volume ::tX_seqpar_vtt_volume() {
    set_mapping_parameters(2.0, 0, TX_SEQPAR_DEFAULT_SCALE, 1);
}

float tX_seqpar_vtt_volume ::get_value() { return tt->rel_volume; }

void tX_seqpar_vtt_volume ::do_exec(const float value) {
    tt->set_volume(value);
}

void tX_seqpar_vtt_volume ::do_update_graphics() {
    gtk_adjustment_set_value(tt->gui.volume, 2.0 - tt->rel_volume);
}

const char* tX_seqpar_vtt_volume ::get_name() {
    return "Volume";
}

/**** Sequencable Parameter : Pan ****/

tX_seqpar_vtt_pan ::tX_seqpar_vtt_pan() {
    set_mapping_parameters(1.0, -1.0, TX_SEQPAR_DEFAULT_SCALE, 1);
}

float tX_seqpar_vtt_pan ::get_value() { return tt->pan; }

void tX_seqpar_vtt_pan ::do_exec(const float value) {
    tt->set_pan(value);
}

void tX_seqpar_vtt_pan ::do_update_graphics() {
    gtk_adjustment_set_value(tt->gui.pan, tt->pan);
}

const char* tX_seqpar_vtt_pan ::get_name() {
    return "Pan";
}

/**** Sequencable Parameter: TURNTABLE PITCH ****/

tX_seqpar_vtt_pitch ::tX_seqpar_vtt_pitch() {
    set_mapping_parameters(3.0, -3.0, TX_SEQPAR_DEFAULT_SCALE, 1);
}

float tX_seqpar_vtt_pitch ::get_value() { return tt->rel_pitch; }

void tX_seqpar_vtt_pitch ::do_exec(const float value) {
    tt->set_pitch(value);
}

void tX_seqpar_vtt_pitch ::do_update_graphics() {
    gtk_adjustment_set_value(tt->gui.pitch, tt->rel_pitch);
}

const char* tX_seqpar_vtt_pitch ::get_name() {
    return "Pitch";
}

/**** Sequencable Parameter: TURNTABLE TRIGGER ****/

tX_seqpar_vtt_trigger ::tX_seqpar_vtt_trigger() {
    set_mapping_parameters(0.01, 0, 1, 1);
    is_boolean = true;
}

void tX_seqpar_vtt_trigger ::do_exec(const float value) {
    if (value > 0)
        tt->trigger();
    else
        tt->stop();
}

const char* tX_seqpar_vtt_trigger ::get_name() {
    return "Trigger (Start/Stop)";
}

/**** Sequencable Parameter: TURNTABLE LOOP ****/

tX_seqpar_vtt_loop ::tX_seqpar_vtt_loop() {
    set_mapping_parameters(0, 0, 0, 0);

    is_boolean = true;
}

void tX_seqpar_vtt_loop ::do_exec(const float value) {
    tt->set_loop(value > 0);
}

const char* tX_seqpar_vtt_loop ::get_name() {
    return "Loop (On/Off)";
}

void tX_seqpar_vtt_loop ::do_update_graphics() {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tt->gui.loop), tt->loop);
}

/**** Sequencable Parameter: TURNTABLE SYNC CLIENT ****/

tX_seqpar_vtt_sync_follower ::tX_seqpar_vtt_sync_follower() {
    set_mapping_parameters(0, 0, 0, 0);
    is_boolean = true;
}

void tX_seqpar_vtt_sync_follower ::do_exec(const float value) {
    tt->set_sync_follower((value > 0), tt->sync_cycles);
}

void tX_seqpar_vtt_sync_follower ::do_update_graphics() {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tt->gui.sync_follower), tt->is_sync_follower);
}

const char* tX_seqpar_vtt_sync_follower ::get_name() {
    return "Sync Client (On/Off)";
}

/**** Sequencable Parameter: TURNTABLE SYNC CYCLES ****/

tX_seqpar_vtt_sync_cycles ::tX_seqpar_vtt_sync_cycles() {
    set_mapping_parameters(0, 0, 0, 0);
}

void tX_seqpar_vtt_sync_cycles ::do_exec(const float value) {
    tt->set_sync_follower(tt->is_sync_follower, (int)value);
}

void tX_seqpar_vtt_sync_cycles ::do_update_graphics() {
    gtk_adjustment_set_value(tt->gui.cycles, tt->sync_cycles);
}

const char* tX_seqpar_vtt_sync_cycles ::get_name() {
    return "Sync Cycles";
}

/**** Sequencable Parameter: TURNTABLE LP ENABLE ****/

tX_seqpar_vtt_lp_enable ::tX_seqpar_vtt_lp_enable() {
    set_mapping_parameters(0.01, 0, 1, 1);
    is_boolean = true;
}

void tX_seqpar_vtt_lp_enable ::do_exec(const float value) {
    tt->lp_set_enable(value > 0);
}

void tX_seqpar_vtt_lp_enable ::do_update_graphics() {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tt->gui.lp_enable), tt->lp_enable);
}

const char* tX_seqpar_vtt_lp_enable ::get_name() {
    return "Lowpass: Enable (On/Off)";
}

/**** Sequencable Parameter: TURNTABLE LP GAIN ****/

tX_seqpar_vtt_lp_gain ::tX_seqpar_vtt_lp_gain() {
    set_mapping_parameters(2.0, 0, TX_SEQPAR_DEFAULT_SCALE, 1);
}

float tX_seqpar_vtt_lp_gain ::get_value() { return tt->lp_gain; }

void tX_seqpar_vtt_lp_gain ::do_exec(const float value) {
    tt->lp_set_gain(value);
}

const char* tX_seqpar_vtt_lp_gain ::get_name() {
    return "Lowpass: Input Gain";
}

void tX_seqpar_vtt_lp_gain ::do_update_graphics() {
    gtk_adjustment_set_value(tt->gui.lp_gain, tt->lp_gain);
}

/**** Sequencable Parameter: TURNTABLE LP RESO ****/

tX_seqpar_vtt_lp_reso ::tX_seqpar_vtt_lp_reso() {
    set_mapping_parameters(0.99, 0, TX_SEQPAR_DEFAULT_SCALE, 1);
}

float tX_seqpar_vtt_lp_reso ::get_value() { return tt->lp_reso; }

void tX_seqpar_vtt_lp_reso ::do_exec(const float value) {
    tt->lp_set_reso(value);
}

void tX_seqpar_vtt_lp_reso ::do_update_graphics() {
    gtk_adjustment_set_value(tt->gui.lp_reso, tt->lp_reso);
}

const char* tX_seqpar_vtt_lp_reso ::get_name() {
    return "Lowpass: Resonance";
}

/**** Sequencable Parameter: TURNTABLE LP FREQUENCY ****/

tX_seqpar_vtt_lp_freq ::tX_seqpar_vtt_lp_freq() {
    set_mapping_parameters(0.99, 0, TX_SEQPAR_DEFAULT_SCALE, 1);
}

float tX_seqpar_vtt_lp_freq ::get_value() { return tt->lp_freq; }

void tX_seqpar_vtt_lp_freq ::do_exec(const float value) {
    tt->lp_set_freq(value);
}

const char* tX_seqpar_vtt_lp_freq ::get_name() {
    return "Lowpass: Cutoff Frequency";
}

void tX_seqpar_vtt_lp_freq ::do_update_graphics() {
    gtk_adjustment_set_value(tt->gui.lp_freq, tt->lp_freq);
}

/**** Sequencable Parameter: TURNTABLE ECHO ENABLE ****/

tX_seqpar_vtt_ec_enable ::tX_seqpar_vtt_ec_enable() {
    set_mapping_parameters(0.01, 0, 1, 1);
    is_boolean = true;
}

void tX_seqpar_vtt_ec_enable ::do_exec(const float value) {
    tt->ec_set_enable(value > 0);
}

void tX_seqpar_vtt_ec_enable ::do_update_graphics() {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tt->gui.ec_enable), tt->ec_enable);
}

const char* tX_seqpar_vtt_ec_enable ::get_name() {
    return "Echo: Enable (On/Off)";
}

/**** Sequencable Parameter: TURNTABLE ECHO LENGTH ****/

tX_seqpar_vtt_ec_length ::tX_seqpar_vtt_ec_length() {
    set_mapping_parameters(1.0, 0, TX_SEQPAR_DEFAULT_SCALE, 1);
}

float tX_seqpar_vtt_ec_length ::get_value() { return tt->ec_length; }

void tX_seqpar_vtt_ec_length ::do_exec(const float value) {
    tt->ec_set_length(value);
}

void tX_seqpar_vtt_ec_length ::do_update_graphics() {
    gtk_adjustment_set_value(tt->gui.ec_length, tt->ec_length);
}

const char* tX_seqpar_vtt_ec_length ::get_name() {
    return "Echo: Duration";
}

/**** Sequencable Parameter: TURNTABLE ECHO FEEDBACK ****/

tX_seqpar_vtt_ec_feedback ::tX_seqpar_vtt_ec_feedback() {
    set_mapping_parameters(1.0, 0, TX_SEQPAR_DEFAULT_SCALE, 1);
}

float tX_seqpar_vtt_ec_feedback ::get_value() { return tt->ec_feedback; }

void tX_seqpar_vtt_ec_feedback ::do_exec(const float value) {
    tt->ec_set_feedback(value);
}

void tX_seqpar_vtt_ec_feedback ::do_update_graphics() {
    gtk_adjustment_set_value(tt->gui.ec_feedback, tt->ec_feedback);
}

const char* tX_seqpar_vtt_ec_feedback ::get_name() {
    return "Echo: Feedback";
}

/**** Sequencable Parameter: TURNTABLE ECHO PAN ****/

tX_seqpar_vtt_ec_pan ::tX_seqpar_vtt_ec_pan() {
    set_mapping_parameters(1.0, -1.0, TX_SEQPAR_DEFAULT_SCALE, 1);
}

float tX_seqpar_vtt_ec_pan ::get_value() { return tt->ec_pan; }

void tX_seqpar_vtt_ec_pan ::do_exec(const float value) {
    tt->ec_set_pan(value);
}

void tX_seqpar_vtt_ec_pan ::do_update_graphics() {
    gtk_adjustment_set_value(tt->gui.ec_pan, tt->ec_pan);
}

const char* tX_seqpar_vtt_ec_pan ::get_name() {
    return "Echo: Pan";
}

/**** Sequencable Parameter: TURNTABLE ECHO VOLUME ****/

tX_seqpar_vtt_ec_volume ::tX_seqpar_vtt_ec_volume() {
    set_mapping_parameters(0.0, 3.0, TX_SEQPAR_DEFAULT_SCALE, 1);
}

float tX_seqpar_vtt_ec_volume ::get_value() { return tt->ec_volume; }

void tX_seqpar_vtt_ec_volume ::do_exec(const float value) {
    tt->ec_set_volume(value);
}

void tX_seqpar_vtt_ec_volume ::do_update_graphics() {
    gtk_adjustment_set_value(tt->gui.ec_volume, tt->ec_volume);
}

const char* tX_seqpar_vtt_ec_volume ::get_name() {
    return "Echo: Volume";
}

/**** Sequencable Parameter: TURNTABLE MUTE ****/

tX_seqpar_vtt_mute ::tX_seqpar_vtt_mute() {
    set_mapping_parameters(0.01, 0, 1, 1);
    is_boolean = true;
}

void tX_seqpar_vtt_mute ::do_exec(const float value) {
    tt->set_mute(value > 0);
}

const char* tX_seqpar_vtt_mute ::get_name() {
    return "Mute (On/Off)";
}

/** LADSPA fx parameters **/

tX_seqpar_vttfx ::tX_seqpar_vttfx() {
    fx_value = (float*)malloc(sizeof(float));
    *fx_value = 0;
    set_mapping_parameters(0, 0, 0, 0);
}

tX_seqpar_vttfx ::~tX_seqpar_vttfx() {
    free(fx_value);
}

void tX_seqpar_vttfx ::set_name(const char* n, const char* sn) {
    strcpy(name, n);
    strcpy(label_name, sn);
    create_widget();
}

float tX_seqpar_vttfx ::get_value() {
    return *fx_value;
}

void tX_seqpar_vttfx ::create_widget() {
    fprintf(stderr, "tX: Ooops. create_widget() for tX_seqpar_vttfx.\n");
}

const char* tX_seqpar_vttfx ::get_name() {
    return name;
}

void tX_seqpar_vttfx_float ::create_widget() {
    double tmp = (max_value - min_value) / 1000.0;

    *fx_value = min_value;
    //myadj=GTK_ADJUSTMENT(gtk_adjustment_new(*fx_value, min_value+tmp/10, max_value-tmp/10, tmp, tmp, tmp));
    myadj = GTK_ADJUSTMENT(gtk_adjustment_new(*fx_value, min_value, max_value, tmp, tmp, tmp));
    mydial = new tX_extdial(label_name, myadj, this);
    g_signal_connect(G_OBJECT(myadj), "value_changed", (GCallback)tX_seqpar_vttfx_float ::gtk_callback, this);
    widget = mydial->get_widget();
}

tX_seqpar_vttfx_float ::~tX_seqpar_vttfx_float() {
    delete mydial;
}

void tX_seqpar_vttfx_float ::do_exec(const float value) {
    *fx_value = value;
}

void tX_seqpar_vttfx_float ::do_update_graphics() {
    gtk_adjustment_set_value(myadj, *fx_value);
}

GCallback tX_seqpar_vttfx_float ::gtk_callback(GtkWidget* w, tX_seqpar_vttfx_float* sp) {
    sp->receive_gui_value(gtk_adjustment_get_value(sp->myadj));
    return NULL;
}

#define WID_DYN TRUE, TRUE, 0
#define WID_FIX FALSE, FALSE, 0

void tX_seqpar_vttfx_int ::create_widget() {
    GtkWidget* tmpwid;

    *fx_value = min_value;
    myadj = GTK_ADJUSTMENT(gtk_adjustment_new(*fx_value, min_value, max_value, 1, 10, 0));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    tmpwid = gtk_spin_button_new(myadj, 1.0, 0);
    gtk_widget_show(tmpwid);
    gtk_box_pack_start(GTK_BOX(box), tmpwid, WID_DYN);
    g_signal_connect(G_OBJECT(tmpwid), "button_press_event", (GCallback)tX_seqpar::tX_seqpar_press, this);

    g_signal_connect(G_OBJECT(myadj), "value_changed", (GCallback)tX_seqpar_vttfx_int ::gtk_callback, this);

    tmpwid = gtk_label_new(label_name);
    gtk_widget_show(tmpwid);
    gtk_box_pack_start(GTK_BOX(box), tmpwid, WID_FIX);

    gtk_widget_show(box);

    widget = gtk_event_box_new();
    g_signal_connect(G_OBJECT(widget), "button_press_event", (GCallback)tX_seqpar::tX_seqpar_press, this);

    gtk_container_add(GTK_CONTAINER(widget), box);
}

tX_seqpar_vttfx_int ::~tX_seqpar_vttfx_int() {
    gtk_widget_destroy(widget);
}

void tX_seqpar_vttfx_int ::do_exec(const float value) {
    *fx_value = value;
}

void tX_seqpar_vttfx_int ::do_update_graphics() {
    gtk_adjustment_set_value(myadj, *fx_value);
}

GCallback tX_seqpar_vttfx_int ::gtk_callback(GtkWidget* w, tX_seqpar_vttfx_int* sp) {
    sp->receive_gui_value(gtk_adjustment_get_value(sp->myadj));
    return NULL;
}

void tX_seqpar_vttfx_bool ::create_widget() {
    *fx_value = min_value;
    widget = gtk_check_button_new_with_label(label_name);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), 0);
    g_signal_connect(G_OBJECT(widget), "button_press_event", (GCallback)tX_seqpar::tX_seqpar_press, this);
    g_signal_connect(G_OBJECT(widget), "clicked", (GCallback)tX_seqpar_vttfx_bool ::gtk_callback, this);
}

tX_seqpar_vttfx_bool ::~tX_seqpar_vttfx_bool() {
    gtk_widget_destroy(widget);
}

void tX_seqpar_vttfx_bool ::do_exec(const float value) {
    *fx_value = value;
}

GCallback tX_seqpar_vttfx_bool ::gtk_callback(GtkWidget* w, tX_seqpar_vttfx_bool* sp) {
    sp->receive_gui_value(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sp->widget)));
    return NULL;
}

void tX_seqpar_vttfx_bool ::do_update_graphics() {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), *fx_value == max_value);
}

gboolean tX_seqpar::tX_seqpar_press(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    tX_seqpar* sp = (tX_seqpar*)data;

    if (event->button == 3) {
        GtkWidget* menu = gtk_menu_new();

        GtkWidget* item = gtk_menu_item_new_with_label(sp->get_name());
        gtk_widget_set_sensitive(item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);

        item = gtk_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_set_sensitive(item, FALSE);
        gtk_widget_show(item);

        item = gtk_menu_item_new_with_label("MIDI Learn");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);
#ifdef USE_ALSA_MIDI_IN
        g_signal_connect(item, "activate", (GCallback)tX_seqpar::learn_midi_binding, sp);
#else
        gtk_widget_set_sensitive(item, FALSE);
#endif

        item = gtk_menu_item_new_with_label("Remove MIDI Binding");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);

        if (sp->bound_midi_event.type == tX_midievent::NONE) {
            gtk_widget_set_sensitive(item, FALSE);
        }
#ifdef USE_ALSA_MIDI_IN
        g_signal_connect(item, "activate", (GCallback)tX_seqpar::remove_midi_binding, sp);
#else
        gtk_widget_set_sensitive(item, FALSE);
#endif

        item = gtk_check_menu_item_new_with_label("Map MIDI Reverse");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);

        if (sp->bound_midi_event.type == tX_midievent::NONE) {
            gtk_widget_set_sensitive(item, FALSE);
        }

        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), sp->get_reverse_midi());

#ifdef USE_ALSA_MIDI_IN
        g_signal_connect(item, "activate", (GCallback)tX_seqpar::reverse_midi_binding, sp);
#else
        gtk_widget_set_sensitive(item, FALSE);
#endif

        if (!sp->is_boolean) {
            item = gtk_menu_item_new();
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            gtk_widget_set_sensitive(item, FALSE);
            gtk_widget_show(item);

            item = gtk_menu_item_new_with_label("Set Upper MIDI Bound");
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            gtk_widget_show(item);

#ifdef USE_ALSA_MIDI_IN
            g_signal_connect(item, "activate", (GCallback)tX_seqpar::set_midi_upper_bound, sp);
#else
            gtk_widget_set_sensitive(item, FALSE);
#endif

            item = gtk_menu_item_new_with_label("Reset Upper MIDI Bound");
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            gtk_widget_show(item);
#ifdef USE_ALSA_MIDI_IN
            g_signal_connect(item, "activate", (GCallback)tX_seqpar::reset_midi_upper_bound, sp);
#else
            gtk_widget_set_sensitive(item, FALSE);
#endif

            if (!sp->midi_upper_bound_set) {
                gtk_widget_set_sensitive(item, FALSE);
            }

            item = gtk_menu_item_new_with_label("Set Lower MIDI Bound");
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            gtk_widget_show(item);
#ifdef USE_ALSA_MIDI_IN
            g_signal_connect(item, "activate", (GCallback)tX_seqpar::set_midi_lower_bound, sp);
#else
            gtk_widget_set_sensitive(item, FALSE);
#endif

            item = gtk_menu_item_new_with_label("Reset Lower MIDI Bound");
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            gtk_widget_show(item);
#ifdef USE_ALSA_MIDI_IN
            g_signal_connect(item, "activate", (GCallback)tX_seqpar::reset_midi_lower_bound, sp);
#else
            gtk_widget_set_sensitive(item, FALSE);
#endif

            if (!sp->midi_lower_bound_set) {
                gtk_widget_set_sensitive(item, FALSE);
            }
        }

        item = gtk_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_set_sensitive(item, FALSE);
        gtk_widget_show(item);

        item = gtk_menu_item_new_with_label("Delete Sequencer Events");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);
        g_signal_connect(item, "activate", (GCallback)menu_delete_all_events_for_sp, sp);

        gtk_widget_show(menu);

        gtk_menu_popup_at_widget(GTK_MENU(menu), widget, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, (GdkEvent*)event);
        //gtk_grab_remove(gtk_grab_get_current());

        return TRUE;
    }

    return FALSE;
}

#ifdef USE_ALSA_MIDI_IN
gboolean tX_seqpar::reverse_midi_binding(GtkWidget* widget, gpointer data) {
    tX_seqpar* sp = (tX_seqpar*)data;
    sp->set_reverse_midi(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)));

    return TRUE;
}

gboolean tX_seqpar::remove_midi_binding(GtkWidget* widget, gpointer data) {
    tX_seqpar* sp = (tX_seqpar*)data;

    sp->bound_midi_event.type = tX_midievent::NONE;
    sp->bound_midi_event.number = 0;
    sp->bound_midi_event.channel = 0;

    return TRUE;
}

gboolean tX_seqpar::learn_midi_binding(GtkWidget* widget, gpointer data) {
    tX_seqpar* sp = (tX_seqpar*)data;

    tX_engine::get_instance()->get_midi()->set_midi_learn_sp(sp);

    return TRUE;
}

gboolean tX_seqpar::set_midi_upper_bound(GtkWidget* widget, gpointer data) {
    tX_seqpar* sp = (tX_seqpar*)data;
    sp->set_upper_midi_bound(sp->get_value());

    return TRUE;
}

gboolean tX_seqpar::reset_midi_upper_bound(GtkWidget* widget, gpointer data) {
    tX_seqpar* sp = (tX_seqpar*)data;
    sp->reset_upper_midi_bound();

    return TRUE;
}

gboolean tX_seqpar::set_midi_lower_bound(GtkWidget* widget, gpointer data) {
    tX_seqpar* sp = (tX_seqpar*)data;
    sp->set_lower_midi_bound(sp->get_value());

    return TRUE;
}

gboolean tX_seqpar::reset_midi_lower_bound(GtkWidget* widget, gpointer data) {
    tX_seqpar* sp = (tX_seqpar*)data;
    sp->reset_lower_midi_bound();

    return TRUE;
}

#endif // USE_ALSA_MIDI_IN
