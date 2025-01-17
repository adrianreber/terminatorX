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

    File: tX_vtt.cc

    Description: This implements the new virtual turntable class. It replaces
		 the old turntable.c from terminatorX 3.2 and earlier. The lowpass
		 filter is based on some sample code by Paul Kellett
		 <paul.kellett@maxim.abel.co.uk>

    08 Dec 1999 - Switched to the new audiofile class
*/

#include "tX_vtt.h"
#include "malloc.h"
#include "tX_global.h"
#include "tX_maingui.h"
#include "tX_sequencer.h"
#include <glib.h>
#include <math.h>
#include <stdio.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "tX_loaddlg.h"

#include <string.h>

#define USE_PREFETCH 1

#ifdef USE_PREFETCH
#define my_prefetch(base, index)                \
    ;                                           \
    __asm__ __volatile__("prefetch index(%0)\n" \
                         :                      \
                         : "r"(base));
#define my_prefetchw(base, index)                \
    ;                                            \
    __asm__ __volatile__("prefetchw index(%0)\n" \
                         :                       \
                         : "r"(base));
#else
#define my_prefetch(base, index) \
    ; /* NOP */                  \
    ;
#define my_prefetchw(base, index) \
    ; /* NOP */                   \
    ;
#endif

extern void build_vtt_gui(vtt_class*);
extern void gui_set_name(vtt_class* vtt, char* newname);
extern void gui_set_color(vtt_class* vtt, GdkRGBA* rgba);
extern void gui_set_filename(vtt_class* vtt, char* newname);
extern void delete_gui(vtt_class* vtt);
extern void gui_update_display(vtt_class* vtt);
extern void gui_clear_leader_button(vtt_class* vtt);
extern void cleanup_vtt(vtt_class* vtt);
extern int vg_get_current_page(vtt_class* vtt);
extern f_prec gui_get_audio_x_zoom(vtt_class* vtt);
extern void gui_set_audio_x_zoom(vtt_class* vtt, f_prec);

int vtt_class::last_sample_rate = 44100;
int vtt_class::vtt_amount = 0;
list<vtt_class*> vtt_class::main_list;
list<vtt_class*> vtt_class::render_list;
int16_t* vtt_class::mix_out_buffer = NULL;
f_prec* vtt_class::mix_buffer = NULL;
f_prec* vtt_class::mix_buffer_end = NULL;
int vtt_class::solo_ctr = 0;

unsigned int vtt_class::samples_in_mix_buffer = 0;
pthread_mutex_t vtt_class::render_lock = PTHREAD_MUTEX_INITIALIZER;
f_prec vtt_class::main_volume = 1.0;
f_prec vtt_class::res_main_volume = 1.0;

vtt_class* vtt_class::sync_leader = NULL;
int vtt_class::leader_triggered = 0;
int vtt_class::leader_triggered_at = 0;
vtt_class* vtt_class::focused_vtt = NULL;
f_prec vtt_class::mix_max_l = 0;
f_prec vtt_class::mix_max_r = 0;
f_prec vtt_class::vol_channel_adjust = 1.0;
int vtt_class::mix_buffer_size = 0;

#define GAIN_AUTO_ADJUST 0.8

vtt_class ::vtt_class(int do_create_gui) {
    vtt_amount++;
    cleanup_required = false;

    sprintf(name, "Turntable %i", vtt_amount);

    double rgb[3];

    for (int c = 0; c < 3; c++) {
        double r = (double)rand() / RAND_MAX;
        rgb[c] = 0.7 + r * (0.3);
    }

    color.red = rgb[0];
    color.green = rgb[1];
    color.blue = rgb[2];
    color.alpha = 1;

    strcpy(filename, "NONE");
    buffer = NULL;
    samples_in_buffer = 0;
    pos_i_max = 0;

    pan = 0;
    rel_pitch = 1;
    ec_volume = 1;
    ec_pan = 1;
    audiofile_pitch_correction = 1.0;
    ec_length = 1;
    ec_output_buffer = NULL;
    output_buffer = NULL;
    output_buffer2 = NULL;

    set_volume(1);
    set_pitch(1);

    autotrigger = 1;
    loop = 1;

    is_playing = 0;
    is_sync_leader = 0;
    is_sync_follower = 0;
    sync_cycles = 0,
    sync_countdown = 0;

    lp_enable = 0;
    lp_reso = 0.8;
    lp_freq = 0.3;
    lp_gain = 1;
    lp_setup(lp_gain, lp_reso, lp_freq);
    lp_reset();

    ec_enable = 0;
    ec_length = 0.5;
    ec_feedback = 0.3;
    ec_clear_buffer();
    ec_set_length(0.5);
    ec_set_pan(0);
    ec_set_volume(1);

    main_list.push_back(this);

    /* "connecting" the seq-parameters */

    sp_speed.set_vtt((void*)this);
    sp_volume.set_vtt((void*)this);
    sp_pitch.set_vtt((void*)this);
    sp_pan.set_vtt((void*)this);
    sp_trigger.set_vtt((void*)this);
    sp_loop.set_vtt((void*)this);
    sp_sync_follower.set_vtt((void*)this);
    sp_sync_cycles.set_vtt((void*)this);
    sp_lp_enable.set_vtt((void*)this);
    sp_lp_gain.set_vtt((void*)this);
    sp_lp_reso.set_vtt((void*)this);
    sp_lp_freq.set_vtt((void*)this);
    sp_ec_enable.set_vtt((void*)this);
    sp_ec_length.set_vtt((void*)this);
    sp_ec_pan.set_vtt((void*)this);
    sp_ec_volume.set_vtt((void*)this);
    sp_ec_feedback.set_vtt((void*)this);
    sp_mute.set_vtt((void*)this);
    sp_spin.set_vtt((void*)this);

    x_par = &sp_speed;
    y_par = &sp_lp_freq;

    lp_fx = new vtt_fx_lp();
    lp_fx->set_vtt((void*)this);
    fx_list.push_back(lp_fx);

    ec_fx = new vtt_fx_ec();
    ec_fx->set_vtt((void*)this);
    fx_list.push_back(ec_fx);

    if (do_create_gui) {
        build_vtt_gui(this);
        lp_fx->set_panel_widget(gui.lp_panel->get_widget());
        lp_fx->set_panel(gui.lp_panel);
        ec_fx->set_panel_widget(gui.ec_panel->get_widget());
        ec_fx->set_panel(gui.ec_panel);
    } else
        have_gui = 0;

    set_pan(0);
    set_main_volume(globals.volume);
    set_output_buffer_size(samples_in_mix_buffer / 2);

    audiofile = NULL;
    audiofile_pitch_correction = 1.0;
    mute = 0;
    mix_solo = 0;
    mix_mute = 0;
    res_mute = mute;
    res_mute_old = 0;

    audio_hidden = false;
    control_hidden = false;

    do_scratch = 0;
    speed_last = 1;
    speed_real = 1;
}

vtt_class ::~vtt_class() {
    vtt_fx* effect;
    vtt_fx_stereo_ladspa* stereo_effect;
    stop();

    main_list.remove(this);
    if (audiofile)
        delete audiofile;
    if (output_buffer)
        tX_freemem(output_buffer, "output_buffer", "vtt Destructor");
    if (output_buffer2)
        tX_freemem(output_buffer2, "output_buffer2", "vtt Destructor");

    vtt_amount--;

    if (mix_solo)
        solo_ctr--;

    while (fx_list.size()) {
        effect = (*fx_list.begin());
        fx_list.remove(effect);
        delete effect;
    }

    while (stereo_fx_list.size()) {
        stereo_effect = (*stereo_fx_list.begin());
        stereo_fx_list.remove(stereo_effect);
        delete stereo_effect;
    }

    if (sync_leader == this) {
        sync_leader = NULL;
    }

    delete_gui(this);
}

void vtt_class ::set_name(char* newname) {
    strcpy(name, newname);
    gui_set_name(this, name);
}

tX_audio_error vtt_class ::load_file(char* fname) {
    tX_audio_error res;
    int was_playing = is_playing;

    if (is_playing)
        stop();

    if (audiofile)
        delete audiofile;

    buffer = NULL;
    samples_in_buffer = 0;
    maxpos = 0;
    pos_i_max = 0;
    strcpy(filename, "");

    audiofile = new tx_audiofile();
    res = audiofile->load(fname);

    if (res == TX_AUDIO_SUCCESS) {
        buffer = audiofile->get_buffer();
        double file_rate = audiofile->get_sample_rate();
        audiofile_pitch_correction = file_rate / ((double)last_sample_rate);
        recalc_pitch();
        samples_in_buffer = audiofile->get_no_samples();
        pos_i_max = samples_in_buffer - 1;
        maxpos = audiofile->get_no_samples();
        strcpy(filename, fname);
        if (was_playing)
            trigger();
    }

    if (have_gui) {
        gui_update_display(this);
    }
    ec_set_length(ec_length);

    return res;
}

int vtt_class ::set_output_buffer_size(int newsize) {
    list<vtt_fx*>::iterator effect;

    if (ec_output_buffer)
        tX_freemem(ec_output_buffer, "ec_output_buffer", "vtt set_output_buffer_size()");
    tX_malloc(ec_output_buffer, "ec_output_buffer", "vtt set_output_buffer_size()", sizeof(float) * newsize, (float*));

    if (output_buffer)
        tX_freemem(output_buffer, "output_buffer", "vtt set_output_buffer_size()");
    tX_malloc(output_buffer, "output_buffer", "vtt set_output_buffer_size()", sizeof(float) * newsize, (float*));
    if (output_buffer2)
        tX_freemem(output_buffer2, "output_buffer2", "vtt set_output_buffer2_size()");
    tX_malloc(output_buffer2, "output_buffer2", "vtt set_output_buffer2_size()", sizeof(float) * newsize, (float*));

    end_of_outputbuffer = output_buffer + newsize; //size_t(sizeof(float)*(newsize));

    samples_in_outputbuffer = newsize;
    inv_samples_in_outputbuffer = 1.0 / samples_in_outputbuffer;

    for (effect = fx_list.begin(); effect != fx_list.end(); effect++) {
        (*effect)->reconnect_buffer();
    }

    return 0;
}

void vtt_class ::set_volume(f_prec newvol) {
    rel_volume = newvol;
    recalc_volume();
}

void vtt_class ::recalc_volume() {
    res_volume = rel_volume * res_main_volume;
    f_prec ec_res_volume = res_volume * ec_volume;

    if (pan > 0.0) {
        res_volume_left = (1.0 - pan) * res_volume;
        res_volume_right = res_volume;
    } else if (pan < 0.0) {
        res_volume_left = res_volume;
        res_volume_right = (1.0 + pan) * res_volume;
    } else {
        res_volume_left = res_volume_right = res_volume;
    }

    if (ec_pan > 0.0) {
        ec_volume_left = (1.0 - ec_pan) * ec_res_volume;
        ec_volume_right = ec_res_volume;
    } else if (ec_pan < 0.0) {
        ec_volume_left = ec_res_volume;
        ec_volume_right = (1.0 + ec_pan) * ec_res_volume;
    } else {
        ec_volume_left = ec_volume_right = ec_res_volume;
    }
}

void vtt_class ::set_pan(f_prec newpan) {
    pan = newpan;
    recalc_volume();
}

void vtt_class ::set_pitch(f_prec newpitch) {
    rel_pitch = newpitch;
    recalc_pitch();
}

void vtt_class ::recalc_pitch() {
    res_pitch = globals.pitch * rel_pitch;
    res_pitch *= audiofile_pitch_correction;
    speed = res_pitch;
    ec_set_length(ec_length);
}

void vtt_class ::set_autotrigger(int newstate) {
    autotrigger = newstate;
}

void vtt_class ::set_loop(int newstate) {
    loop = newstate;
}

void vtt_class ::set_mute(int newstate) {
    mute = newstate;
    calc_mute();
}

void vtt_class ::set_mix_mute(int newstate) {
    mix_mute = newstate;
    calc_mute();
}

void vtt_class ::set_mix_solo(int newstate) {
    if (mix_solo && !newstate) {
        /* turning it off */
        mix_solo = 0;
        solo_ctr--;
    } else if (!mix_solo && newstate) {
        /* turning it on */
        mix_solo = 1;
        solo_ctr++;
    }
    calc_mute();

    /* locking ? */
    list<vtt_class*>::iterator vtt;

    for (vtt = main_list.begin(); vtt != main_list.end(); vtt++) {
        (*vtt)->calc_mute();
    }
}

void vtt_class ::lp_set_enable(int newstate) {
    lp_enable = newstate;
    lp_reset();
}

void vtt_class ::lp_reset() {
    lp_buf0 = lp_buf1 = 0;
}

void vtt_class ::lp_set_gain(f_prec gain) {
    lp_gain = gain;
    lp_resgain = lp_gain * lp_autogain;
}

void vtt_class ::lp_set_reso(f_prec reso) {
    lp_reso = reso;

    lp_b = reso * (1.0 + (1.0 / lp_a));
    lp_autogain = 1.0 - reso * GAIN_AUTO_ADJUST;
    lp_resgain = lp_gain * lp_autogain;
}

void vtt_class ::lp_set_freq(f_prec freq) {
    lp_freq = freq;

    lp_a = 0.9999 - freq;
    lp_b = lp_reso * (1.0 + (1.0 / lp_a));
}

void vtt_class ::lp_setup(f_prec gain, f_prec reso, f_prec freq) {
    lp_freq = freq;
    lp_reso = reso;

    lp_a = 1.0 - freq;
    lp_b = reso * (1.0 + (1.0 / lp_a));

    lp_autogain = 1.0 - reso * GAIN_AUTO_ADJUST;
    lp_resgain = lp_gain * lp_autogain;
}

void vtt_class ::ec_set_enable(int newstate) {
    ec_enable = newstate;
    ec_clear_buffer();
}

void vtt_class ::ec_set_pan(f_prec pan) {
    ec_pan = pan;

    recalc_volume();
}

/* Max length is 1.0 */

void vtt_class ::ec_set_length(f_prec length) {
    int delay;

    ec_length = length;
    if (res_pitch == 0) {
        ec_res_length = length * samples_in_buffer;
    } else {
        ec_res_length = length * samples_in_buffer / res_pitch;
    }

    if (ec_res_length < 0)
        ec_res_length *= -1;

    if (ec_res_length >= EC_MAX_BUFFER) {
        ec_res_length = EC_MAX_BUFFER * length;
    }

    delay = (int)floor(ec_res_length);
    delay -= 2;
    ec_delay = &ec_buffer[delay];
}

void vtt_class ::ec_set_feedback(f_prec feedback) {
    ec_feedback = feedback;
}

void vtt_class ::ec_set_volume(f_prec volume) {
    ec_volume = volume;
    recalc_volume();
}

void vtt_class ::ec_clear_buffer() {
    memset(ec_buffer, 0, sizeof(ec_buffer));
    ec_ptr = ec_buffer;
}

void vtt_class ::render() {
    list<vtt_fx*>::iterator effect;

    if (do_scratch) {
        if (sense_cycles > 0) {
            sense_cycles--;
            if (sense_cycles == 0)
                sp_speed.receive_input_value(0);
        }
    }
    render_scratch();

    for (effect = fx_list.begin(); effect != fx_list.end(); effect++) {
        if ((*effect)->isEnabled())
            (*effect)->run();
    }

    if (stereo_fx_list.size() > 0) {
        // fill 2nd channel
        memcpy((void*)output_buffer2, (void*)output_buffer, sizeof(float) * ((int)samples_in_outputbuffer));

        // apply stereo effects.
        list<vtt_fx_stereo_ladspa*>::iterator stereo_effect;

        for (stereo_effect = stereo_fx_list.begin(); stereo_effect != stereo_fx_list.end(); stereo_effect++) {
            if ((*stereo_effect)->isEnabled())
                (*stereo_effect)->run();
        }

        for (int sample = 0; sample < samples_in_outputbuffer; sample++) {
            output_buffer[sample] *= res_volume_left;
            output_buffer2[sample] *= res_volume_right;
        }
    } else {
        for (int sample = 0; sample < samples_in_outputbuffer; sample++) {
            output_buffer2[sample] = output_buffer[sample] * res_volume_right;
            output_buffer[sample] *= res_volume_left;
        }
    }

    if (ec_enable) {
        for (int sample = 0; sample < samples_in_outputbuffer; sample++) {
            f_prec temp = ec_output_buffer[sample];
            output_buffer[sample] += temp * ec_volume_left;
            output_buffer2[sample] += temp * ec_volume_right;
        }
    }

    // find max signal for vu meters...
    for (int sample = 0; sample < samples_in_outputbuffer; sample++) {
        f_prec lmax = fabs(output_buffer[sample]);
        f_prec rmax = fabs(output_buffer2[sample]);

        if (lmax > max_value)
            max_value = lmax;
        if (rmax > max_value2)
            max_value2 = rmax;
    }
}

extern void vg_create_fx_gui(vtt_class* vtt, vtt_fx_ladspa* effect, LADSPA_Plugin* plugin);

vtt_fx_ladspa* vtt_class ::add_effect(LADSPA_Plugin* plugin) {
    vtt_fx_ladspa* new_effect;

    new_effect = new vtt_fx_ladspa(plugin, this);
    pthread_mutex_lock(&render_lock);
    fx_list.push_back(new_effect);
    if (is_playing)
        new_effect->activate();
    pthread_mutex_unlock(&render_lock);
    vg_create_fx_gui(this, new_effect, plugin);

    return new_effect;
}

vtt_fx_stereo_ladspa* vtt_class ::add_stereo_effect(LADSPA_Stereo_Plugin* plugin) {
    vtt_fx_stereo_ladspa* new_effect;

    new_effect = new vtt_fx_stereo_ladspa(plugin, this);
    pthread_mutex_lock(&render_lock);
    stereo_fx_list.push_back(new_effect);
    if (is_playing)
        new_effect->activate();
    pthread_mutex_unlock(&render_lock);
    vg_create_fx_gui(this, new_effect, plugin);

    return new_effect;
}

void vtt_class ::calc_speed() {
    do_mute = 0;
    fade_out = 0;
    fade_in = 0;

    if (speed != speed_target) {
        speed_target = speed;
        speed_step = speed_target - speed_real;
        speed_step /= globals.vtt_inertia;
    }

    if (speed_target != speed_real) {
        speed_real += speed_step;
        if ((speed_step < 0) && (speed_real < speed_target))
            speed_real = speed_target;
        else if ((speed_step > 0) && (speed_real > speed_target))
            speed_real = speed_target;
    }

    if (fade) {
        if ((speed_last == 0) && (speed_real != 0)) {
            fade_in = 1;
            fade = NEED_FADE_OUT;
        }
    } else {
        if ((speed_last != 0) && (speed_real == 0)) {
            fade_out = 1;
            fade = NEED_FADE_IN;
        }
    }

    speed_last = speed_real;

    if (res_mute != res_mute_old) {
        if (res_mute) {
            fade_out = 1;
            fade_in = 0;
            fade = NEED_FADE_IN;
        } else {
            fade_in = 1;
            fade_out = 0;
            fade = NEED_FADE_OUT;
        }
        res_mute_old = res_mute;
    } else {
        if (res_mute)
            do_mute = 1;
    }
}

void vtt_class ::render_scratch() {
    int16_t* ptr;

    int sample;

    d_prec pos_a_f;

    f_prec amount_a;
    f_prec amount_b;

    f_prec sample_a;
    f_prec sample_b;

    f_prec sample_res;

    f_prec* out;
    f_prec fade_vol;

    calc_speed();

    for (sample = 0, out = output_buffer, fade_vol = 0.0; sample < samples_in_outputbuffer; sample++, out++, fade_vol += inv_samples_in_outputbuffer) {
        if ((speed_real != 0) || (fade_out)) {

            pos_f += speed_real;

            if (pos_f > maxpos) {
                pos_f -= maxpos;
                if (res_pitch > 0) {
                    if (loop) {
                        if (is_sync_leader) {
                            leader_triggered = 1;
                            leader_triggered_at = sample;
                        }
                    } else {
                        want_stop = 1;
                    }
                }
            } else if (pos_f < 0) {
                pos_f += maxpos;
                if (res_pitch < 0) {
                    if (loop) {
                        if (is_sync_leader) {
                            leader_triggered = 1;
                            leader_triggered_at = sample;
                        }
                    } else {
                        want_stop = 1;
                    }
                }
            }

            pos_a_f = floor(pos_f);
            pos_i = (unsigned int)pos_a_f;

            amount_b = pos_f - pos_a_f;
            amount_a = 1.0 - amount_b;

            if (do_mute) {
                *out = 0.0;
            } else {
                ptr = &buffer[pos_i];
                sample_a = (f_prec)*ptr;

                if (pos_i == pos_i_max) {
                    sample_b = *buffer;
                } else {
                    ptr++;
                    sample_b = (f_prec)*ptr;
                }

                sample_res = (sample_a * amount_a) + (sample_b * amount_b);

                // scale to 0 db := 1.0f
                sample_res /= FL_SHRT_MAX;

                if (fade_in) {
                    sample_res *= fade_vol;
                } else if (fade_out) {
                    sample_res *= 1.0 - fade_vol;
                }

                *out = sample_res;
            }
        } else {
            *out = 0;
        }
    }
}

void vtt_class ::forward_turntable() {
    int sample;
    double pos_f_tmp;
#ifdef pos_f_test
    int show = 0;
    double diff;
#endif

    calc_speed();

    if ((speed_real == 0) && (!fade_out))
        return;

    /*  the following code is problematic as adding speed_real*n is
	different from adding speed_real n times to pos_f.

	well it speeds things up quite a bit and double precision
	seems to do a satisfying job.

	#define pos_f_test to prove that.
    */

    pos_f_tmp = pos_f + speed_real * samples_in_outputbuffer;

    if ((pos_f_tmp > 0) && (pos_f_tmp < maxpos)) {
#ifdef pos_f_test
        show = 1;
#else
        pos_f = pos_f_tmp;
        return;
#endif
    }

    /* now the slow way ;) */

    for (sample = 0; sample < samples_in_outputbuffer; sample++) {
        pos_f += speed_real;

        if (pos_f > maxpos) {
            pos_f -= maxpos;
            if (res_pitch > 0) {
                if (loop) {
                    if (is_sync_leader) {
                        leader_triggered = 1;
                        leader_triggered_at = sample;
                    }
                } else {
                    want_stop = 1;
                }
            }
        } else if (pos_f < 0) {
            pos_f += maxpos;
            if (res_pitch < 0) {
                if (loop) {
                    if (is_sync_leader) {
                        leader_triggered = 1;
                        leader_triggered_at = sample;
                    }
                } else {
                    want_stop = 1;
                }
            }
        }
    }
#ifdef pos_f_test
    if (show) {
        diff = pos_f_tmp - pos_f;
        if (diff != 0)
            printf("fast: %f, slow: %f, diff: %f, tt: %s\n", pos_f_tmp, pos_f, diff, name);
    }
#endif
}

/*
	The following lowpass filter is based on some sample code by
	Paul Kellett <paul.kellett@maxim.abel.co.uk>
*/

void vtt_class ::render_lp() {
    f_prec* sample;

    for (sample = output_buffer; sample < end_of_outputbuffer; sample++) {
        lp_buf0 = lp_a * lp_buf0 + lp_freq * ((*sample) * lp_resgain + lp_b * (lp_buf0 - lp_buf1));
        lp_buf1 = lp_a * lp_buf1 + lp_freq * lp_buf0;

        *sample = lp_buf1;
    }
}

void vtt_class ::render_ec() {
    f_prec* sample;
    f_prec* ec_sample;
    int i;

    for (i = 0, sample = output_buffer, ec_sample = ec_output_buffer; i < samples_in_outputbuffer; i++, ec_sample++, sample++, ec_ptr++) {
        if (ec_ptr > ec_delay)
            ec_ptr = ec_buffer;
        *ec_sample = (*ec_ptr) * ec_feedback;
        *ec_ptr = *sample + *ec_sample;
    }
}

int vtt_class ::set_mix_buffer_size(int no_samples) {
    list<vtt_class*>::iterator vtt;
    int res = 0;

    if (mix_buffer)
        tX_freemem(mix_buffer, "mix_buffer", "vtt set_mix_buffer_size()");
    samples_in_mix_buffer = no_samples * 2;

    tX_malloc(mix_buffer, "mix_buffer", "vtt set_mix_buffer_size()", sizeof(float) * samples_in_mix_buffer, (float*));
    mix_buffer_end = mix_buffer + samples_in_mix_buffer;

    if (mix_out_buffer)
        tX_freemem(mix_out_buffer, "mix_out_buffer", "vtt set_mix_buffer_size()");
    tX_malloc(mix_out_buffer, "mix_out_buffer", "vtt set_mix_buffer_size()", sizeof(int16_t) * samples_in_mix_buffer + 4, (int16_t*));

    for (vtt = main_list.begin(); vtt != main_list.end(); vtt++) {
        res |= (*vtt)->set_output_buffer_size(no_samples);
    }

    if ((!mix_buffer) || (!mix_out_buffer) || res)
        return 1;

    mix_buffer_size = no_samples;

    return 0;
}

int16_t* vtt_class ::render_all_turntables() {
    list<vtt_class*>::iterator vtt, next;
    unsigned int sample;
    unsigned int mix_sample;

    pthread_mutex_lock(&render_lock);

    if (render_list.size() == 0) {
        memset((void*)mix_out_buffer, 0, sizeof(int16_t) * samples_in_mix_buffer);
        /* We need to memset mix_buffer, too, as the JACK backend
	   acesses this directly.
	*/
        memset((void*)mix_buffer, 0, sizeof(float) * samples_in_mix_buffer);
    } else {
        vtt = render_list.begin();
        (*vtt)->render();

        for (sample = 0, mix_sample = 0; sample < (*vtt)->samples_in_outputbuffer; sample++) {
            mix_buffer[mix_sample++] = (*vtt)->output_buffer[sample] * FL_SHRT_MAX;
            mix_buffer[mix_sample++] = (*vtt)->output_buffer2[sample] * FL_SHRT_MAX;
        }

        if (leader_triggered) {
            for (vtt = main_list.begin(); vtt != main_list.end(); vtt++) {
                if ((*vtt)->is_sync_follower) {
                    if ((*vtt)->sync_countdown) {
                        (*vtt)->sync_countdown--;
                    } else {
                        (*vtt)->sync_countdown = (*vtt)->sync_cycles;
                        (*vtt)->trigger(false);
                    }
                }
            }
        }

        vtt = render_list.begin();

        for (vtt++; vtt != render_list.end(); vtt++) {
            (*vtt)->render();

            for (sample = 0, mix_sample = 0; sample < (*vtt)->samples_in_outputbuffer; sample++) {
                mix_buffer[mix_sample++] += (*vtt)->output_buffer[sample] * FL_SHRT_MAX;
                mix_buffer[mix_sample++] += (*vtt)->output_buffer2[sample] * FL_SHRT_MAX;
            }
        }

        bool right = false;

        for (sample = 0; sample < samples_in_mix_buffer; sample++) {
            f_prec temp = mix_buffer[sample];
#ifndef TX_DO_CLIP
            if (temp < FL_SHRT_MIN) {
                temp = mix_buffer[sample] = FL_SHRT_MIN;
            } else if (temp > FL_SHRT_MAX) {
                temp = mix_buffer[sample] = FL_SHRT_MAX;
            }
#endif
            mix_out_buffer[sample] = (int16_t)temp;

            temp = fabs(temp);
            if (right) {
                if (temp > mix_max_r)
                    mix_max_r = temp;
            } else {
                if (temp > mix_max_l)
                    mix_max_l = temp;
            }
            right = !right;
        }
    }
    leader_triggered = 0;

    vtt = render_list.begin();
    while (vtt != render_list.end()) {
        next = vtt;
        next++;

        if ((*vtt)->want_stop)
            (*vtt)->stop_nolock();
        vtt = next;
    }
    pthread_mutex_unlock(&render_lock);

    return mix_out_buffer;
}

void vtt_class ::forward_all_turntables() {
    list<vtt_class*>::iterator vtt, next;

    if (render_list.size() > 0) {
        vtt = render_list.begin();
        (*vtt)->forward_turntable();

        if (leader_triggered) {
            for (vtt = main_list.begin(); vtt != main_list.end(); vtt++) {
                if ((*vtt)->is_sync_follower) {
                    if ((*vtt)->sync_countdown) {
                        (*vtt)->sync_countdown--;
                    } else {
                        (*vtt)->sync_countdown = (*vtt)->sync_cycles;
                        (*vtt)->trigger();
                    }
                }
            }
        }

        vtt = render_list.begin();
        for (vtt++; vtt != render_list.end(); vtt++) {
            (*vtt)->forward_turntable();
        }
    }

    leader_triggered = 0;
    vtt = render_list.begin();
    while (vtt != render_list.end()) {
        next = vtt;
        next++;

        if ((*vtt)->want_stop)
            (*vtt)->stop_nolock();
        vtt = next;
    }
}

void vtt_class ::retrigger() {
    if (res_pitch >= 0)
        pos_f = 0;
    else
        pos_f = maxpos;

    fade = NEED_FADE_OUT;
    speed = res_pitch;
    speed_real = res_pitch;
    speed_target = res_pitch;
    want_stop = 0;

    max_value = 0;
    max_value2 = 0;

    if (is_sync_leader) {
        leader_triggered = 1;
        leader_triggered_at = 0;
    }
}

int vtt_class ::trigger(bool need_lock) {
    if (!buffer)
        return 1;

    retrigger();

    if (!is_playing) {
        if (need_lock)
            pthread_mutex_lock(&render_lock);
        is_playing = 1;
        cleanup_required = false;

        /* activating plugins */
        for (list<vtt_fx*>::iterator effect = fx_list.begin(); effect != fx_list.end(); effect++) {
            (*effect)->activate();
        }

        for (list<vtt_fx_stereo_ladspa*>::iterator effect = stereo_fx_list.begin(); effect != stereo_fx_list.end(); effect++) {
            (*effect)->activate();
        }

        if (is_sync_leader) {
            render_list.push_front(this);
        } else {
            render_list.push_back(this);
        }

        if (need_lock)
            pthread_mutex_unlock(&render_lock);
    }

    return 0;
}

/* call this only when owning render_lock. */
int vtt_class ::stop_nolock() {
    list<vtt_fx*>::iterator effect;

    if (!is_playing) {
        return 1;
    }

    render_list.remove(this);
    want_stop = 0;
    is_playing = 0;
    max_value = 0;
    max_value2 = 0;

    cleanup_required = true;

    /* deactivating plugins */
    for (effect = fx_list.begin(); effect != fx_list.end(); effect++) {
        (*effect)->deactivate();
    }

    return 0;
}

int vtt_class ::stop() {
    int res;

    pthread_mutex_lock(&render_lock);
    res = stop_nolock();
    pthread_mutex_unlock(&render_lock);

    return res;
}

void vtt_class ::set_sync_leader(int leader) {
    if (leader) {
        if (sync_leader)
            sync_leader->set_sync_leader(0);
        sync_leader = this;
        is_sync_leader = 1;
    } else {
        if (sync_leader == this)
            sync_leader = 0;
        is_sync_leader = 0;
        gui_clear_leader_button(this);
    }
}

void vtt_class ::set_sync_follower(int slave, int cycles) {
    tX_debug("vtt_class::set_sync_follower() setting %i, %i.", slave, cycles);
    is_sync_follower = slave;
    sync_cycles = cycles;
    //	sync_countdown=cycles;
    sync_countdown = 0;
}

void vtt_class ::set_sync_follower_ug(int slave, int cycles) {
    set_sync_follower(slave, cycles);
}

void vtt_class ::set_main_volume(f_prec new_volume) {
    list<vtt_class*>::iterator vtt;

    main_volume = new_volume;
    globals.volume = new_volume;

    if (main_list.size() > 0) {
        vol_channel_adjust = sqrt((f_prec)main_list.size());
        res_main_volume = main_volume / vol_channel_adjust;
    }

    for (vtt = main_list.begin(); vtt != main_list.end(); vtt++) {
        (*vtt)->recalc_volume();
    }
}

void vtt_class ::set_main_pitch(f_prec new_pitch) {
    list<vtt_class*>::iterator vtt;

    globals.pitch = new_pitch;
    for (vtt = main_list.begin(); vtt != main_list.end(); vtt++) {
        (*vtt)->recalc_pitch();
    }
}

void vtt_class ::focus_no(int no) {
    list<vtt_class*>::iterator vtt;
    int i;

    focused_vtt = NULL;

    for (i = 0, vtt = main_list.begin(); vtt != main_list.end(); vtt++, i++) {
        if (i == no) {
            focused_vtt = (*vtt);
        }
    }
}

void vtt_class ::focus_next() {
    list<vtt_class*>::iterator vtt;

    if (!focused_vtt) {
        if (main_list.size()) {
            focused_vtt = (*main_list.begin());
        }
        return;
    }

    for (vtt = main_list.begin(); vtt != main_list.end(); vtt++) {
        if ((*vtt) == focused_vtt) {
            /* Ok, we found ourselves.. */

            vtt++;
            while ((vtt != main_list.end()) && ((*vtt)->audio_hidden)) {
                vtt++;
            }

            if (vtt == main_list.end()) {
                /* No other "focusable" after this vtt so we're looking for the next */

                for (vtt = main_list.begin(); vtt != main_list.end(); vtt++) {
                    if (!(*vtt)->audio_hidden) {
                        focused_vtt = (*vtt);
                        return;
                    }
                }
                /* When we get here there's no "focusable" vtt at all... damn */
                focused_vtt = NULL;
                return;
            } else {
                focused_vtt = (*vtt);
                return;
            }
        }
    }

    focused_vtt = (*main_list.begin());
}

void vtt_class ::set_scratch(int newstate) {
    if (newstate) {
        sp_spin.receive_input_value(0);
        do_scratch = 1;
        sense_cycles = globals.sense_cycles;
    } else {
        sp_spin.receive_input_value(1);
        do_scratch = 0;
    }
}

void vtt_class ::unfocus() {
    focused_vtt = NULL;
}

void vtt_class ::set_x_input_parameter(tX_seqpar* sp) {
    x_par = sp;
}

void vtt_class ::set_y_input_parameter(tX_seqpar* sp) {
    y_par = sp;
}

void vtt_class ::xy_input(f_prec x_value, f_prec y_value) {
    if (x_par)
        x_par->handle_mouse_input(x_value * globals.mouse_speed);
    if (y_par)
        y_par->handle_mouse_input(y_value * globals.mouse_speed);
}

#define store(data)                                         \
    ;                                                       \
    if (fwrite((void*)&data, sizeof(data), 1, output) != 1) \
        res += 1;

int vtt_class ::save(FILE* rc, gzFile rz, char* indent) {
    char tmp_xml_buffer[4096];
    char* color_buffer;

    int res = 0;

    tX_store("%s<turntable>\n", indent);
    strcat(indent, "\t");

    store_string("name", name);
    color_buffer = gdk_rgba_to_string(&color);
    store_string("color", color_buffer);
    g_free(color_buffer);

    if (buffer) {
        store_string("audiofile", filename);
    } else {
        store_string("audiofile", "");
    }
    store_bool("sync_leader", is_sync_leader);
    store_bool("autotrigger", autotrigger);
    store_bool_sp("loop", loop, sp_loop);

    store_bool_sp("sync_follower", is_sync_follower, sp_sync_follower);
    store_int_sp("sync_cycles", sync_cycles, sp_sync_cycles);

    store_float_sp("volume", rel_volume, sp_volume);
    store_float_sp("pitch", rel_pitch, sp_pitch);
    store_bool_sp("mute", mute, sp_mute);
    store_float_sp("pan", pan, sp_pan);

    store_bool_sp("lowpass_enable", lp_enable, sp_lp_enable);
    store_float_sp("lowpass_gain", lp_gain, sp_lp_gain);
    store_float_sp("lowpass_reso", lp_reso, sp_lp_reso);
    store_float_sp("lowpass_freq", lp_freq, sp_lp_freq);

    store_bool_sp("echo_enable", ec_enable, sp_ec_enable);
    store_float_sp("echo_length", ec_length, sp_ec_length);
    store_float_sp("echo_feedback", ec_feedback, sp_ec_feedback);
    store_float_sp("echo_pan", ec_pan, sp_ec_pan);
    store_float_sp("echo_volume", ec_volume, sp_ec_volume);

    store_id_sp("speed", sp_speed);
    store_id_sp("trigger", sp_trigger);
    store_id_sp("spin", sp_spin);

    if (x_par) {
        store_int("x_axis_mapping", x_par->get_persistence_id());
    }

    if (y_par) {
        store_int("y_axis_mapping", y_par->get_persistence_id());
    }

    store_bool("audio_panel_hidden", audio_hidden);
    store_bool("control_panel_hidden", control_hidden);
    store_bool("main_panel_hidden", gui.main_panel->is_hidden());
    store_bool("trigger_panel_hidden", gui.trigger_panel->is_hidden());
    store_bool("lowpass_panel_hidden", gui.lp_panel->is_hidden());
    store_bool("echo_panel_hidden", gui.ec_panel->is_hidden());
    store_bool("mix_mute", mix_mute);
    store_bool("mix_solo", mix_solo);
    store_float("audio_x_zoom", gui_get_audio_x_zoom(this));

    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(gui.scrolled_win));
    store_float("control_adjustment", gtk_adjustment_get_value(adj));

    tX_store("%s<fx>\n", indent);
    strcat(indent, "\t");

    for (list<vtt_fx*>::iterator effect = fx_list.begin(); effect != fx_list.end(); effect++) {
        (*effect)->save(rc, rz, indent);
    }
    indent[strlen(indent) - 1] = 0;
    tX_store("%s</fx>\n", indent);

    tX_store("%s<stereo_fx>\n", indent);
    strcat(indent, "\t");

    for (list<vtt_fx_stereo_ladspa*>::iterator effect = stereo_fx_list.begin(); effect != stereo_fx_list.end(); effect++) {
        (*effect)->save(rc, rz, indent);
    }
    indent[strlen(indent) - 1] = 0;
    tX_store("%s</stereo_fx>\n", indent);

    indent[strlen(indent) - 1] = 0;
    tX_store("%s</turntable>\n", indent);

    return res;
}

#define TX_XML_SETFILE_VERSION "1.1"

#define TX_XML_SETFILE_VERSION_11 "1.1"
#define TX_XML_SETFILE_VERSION_10 "1.0"

int vtt_class ::save_all(FILE* rc, gzFile rz) {
    int res = 0;
    list<vtt_class*>::iterator vtt;
    char indent[256];

    tX_seqpar ::create_persistence_ids();

    tX_store("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n");
    tX_store("<terminatorXset version=\"%s\">\n", TX_XML_SETFILE_VERSION);

    strcpy(indent, "\t");

    store_float_sp("main_volume", main_volume, sp_main_volume);
    store_float_sp("main_pitch", globals.pitch, sp_main_pitch);

    for (vtt = main_list.begin(); vtt != main_list.end(); vtt++) {
        res += (*vtt)->save(rc, rz, indent);
    }

    sequencer.save(rc, rz, indent);

    tX_store("</terminatorXset>\n");

    return res;
}

int vtt_class ::load(xmlDocPtr doc, xmlNodePtr node) {
    char buffer[1024];
    bool hidden;
    int xpar_id = -1;
    int ypar_id = -1;
    int elementFound;
    double dvalue;
    double tmp;
    char tmp_xml_buffer[4096];

    control_scroll_adjustment = 0.0;

    for (xmlNodePtr cur = node->xmlChildrenNode; cur != NULL; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE) {
            elementFound = 0;

            restore_string_ac("name", buffer, set_name(buffer));
            restore_string("color", buffer);
            gdk_rgba_parse(&color, buffer);
            gui_set_color(this, &color);
            restore_string("audiofile", filename);
            restore_bool("sync_leader", is_sync_leader);
            restore_bool("sync_master", is_sync_leader); // pre 4.1.0 compatibilty
            restore_bool("autotrigger", autotrigger);
            restore_bool_id("loop", loop, sp_loop, nop);
            restore_bool_id("sync_client", is_sync_follower, sp_sync_follower, set_sync_follower(is_sync_follower, sync_cycles)); // pre 4.1.0 compatibilty
            restore_bool_id("sync_follower", is_sync_follower, sp_sync_follower, set_sync_follower(is_sync_follower, sync_cycles));
            restore_int_id("sync_cycles", sync_cycles, sp_sync_cycles, set_sync_follower(is_sync_follower, sync_cycles));
            restore_float_id("volume", rel_volume, sp_volume, recalc_volume());
            restore_float_id("pitch", rel_pitch, sp_pitch, recalc_pitch());
            restore_bool_id("mute", mute, sp_mute, set_mute(mute));
            restore_float_id("pan", pan, sp_pan, set_pan(pan));

            restore_bool_id("lowpass_enable", lp_enable, sp_lp_enable, lp_set_enable(lp_enable));
            restore_float_id("lowpass_gain", lp_gain, sp_lp_gain, lp_set_gain(lp_gain));
            restore_float_id("lowpass_reso", lp_reso, sp_lp_reso, lp_set_reso(lp_reso));
            restore_float_id("lowpass_freq", lp_freq, sp_lp_freq, lp_set_freq(lp_freq));

            restore_bool_id("echo_enable", ec_enable, sp_ec_enable, ec_set_enable(ec_enable));
            restore_float_id("echo_length", ec_length, sp_ec_length, ec_set_length(ec_length));
            restore_float_id("echo_feedback", ec_feedback, sp_ec_feedback, ec_set_feedback(ec_feedback));
            restore_float_id("echo_pan", ec_pan, sp_ec_pan, ec_set_pan(ec_pan));
            restore_float_id("echo_volume", ec_volume, sp_ec_volume, ec_set_volume(ec_volume));

            restore_sp_id("speed", sp_speed);
            restore_sp_id("trigger", sp_trigger);
            restore_sp_id("spin", sp_spin);

            restore_int("x_axis_mapping", xpar_id);
            restore_int("y_axis_mapping", ypar_id);

            restore_bool("mix_mute", mix_mute);
            restore_bool("mix_solo", mix_solo);

            restore_bool("audio_panel_hidden", audio_hidden);
            restore_bool("control_panel_hidden", control_hidden);
            restore_bool_ac("main_panel_hidden", hidden, gui.main_panel->hide(!hidden));
            restore_bool_ac("trigger_panel_hidden", hidden, gui.trigger_panel->hide(!hidden));
            restore_bool_ac("lowpass_panel_hidden", hidden, gui.lp_panel->hide(!hidden));
            restore_bool_ac("echo_panel_hidden", hidden, gui.ec_panel->hide(!hidden));
            restore_float_ac("audio_x_zoom", tmp, gui_set_audio_x_zoom(this, tmp));
            restore_float("control_adjustment", control_scroll_adjustment);
            vg_adjust_zoom(gui.zoom, this);

            if ((xmlStrcmp(cur->name, (xmlChar*)"fx") == 0) || (xmlStrcmp(cur->name, (xmlChar*)"stereo_fx") == 0)) {
                bool stereo = (xmlStrcmp(cur->name, (xmlChar*)"stereo_fx") == 0);
                xmlNodePtr fx = cur;
                elementFound = 1;

                for (xmlNodePtr cur = fx->xmlChildrenNode; cur != NULL; cur = cur->next) {
                    if (cur->type == XML_ELEMENT_NODE) {
                        if ((xmlStrcmp(cur->name, (xmlChar*)"cutoff") == 0) && !stereo) {
                            for (unsigned int t = 0; t < fx_list.size(); t++)
                                effect_down(lp_fx);
                        } else if ((xmlStrcmp(cur->name, (xmlChar*)"lowpass") == 0) && !stereo) {
                            for (unsigned int t = 0; t < fx_list.size(); t++)
                                effect_down(ec_fx);
                        } else if (xmlStrcmp(cur->name, (xmlChar*)"ladspa_plugin") == 0) {
                            xmlNodePtr pluginNode = cur;
                            int ladspa_id = -1;

                            for (xmlNodePtr cur = pluginNode->xmlChildrenNode; cur != NULL; cur = cur->next) {
                                int elementFound;
                                if (cur->type == XML_ELEMENT_NODE) {
                                    elementFound = 0;

                                    restore_int("ladspa_id", ladspa_id);
                                    if (elementFound)
                                        break;
                                }
                            }

                            if (ladspa_id != -1) {
                                LADSPA_Plugin* plugin = LADSPA_Plugin::getPluginByUniqueID(ladspa_id);
                                if (!plugin)
                                    plugin = LADSPA_Stereo_Plugin::getPluginByUniqueID(ladspa_id);

                                if (plugin) {
                                    vtt_fx_ladspa* ladspa_effect = NULL;

                                    if (plugin->is_stereo()) {
                                        ladspa_effect = add_stereo_effect((LADSPA_Stereo_Plugin*)plugin);
                                        if (!stereo) {
                                            sprintf(buffer, "Trying to load mono plugin into stereo queue [%i].", ladspa_id);
                                            tx_note(buffer, true, GTK_WINDOW(gtk_widget_get_toplevel(ld_loaddlg)));
                                        }
                                    } else {
                                        ladspa_effect = add_effect(plugin);
                                        if (stereo) {
                                            sprintf(buffer, "Trying to load stereo plugin into mono queue [%i].", ladspa_id);
                                            tx_note(buffer, true, GTK_WINDOW(gtk_widget_get_toplevel(ld_loaddlg)));
                                        }
                                    }

                                    ladspa_effect->load(doc, pluginNode);
                                } else {
                                    sprintf(buffer, "The terminatorX set file you are loading makes use of a LADSPA plugin that is not installed on this machine. The plugin's ID is [%i].", ladspa_id);
                                    tx_note(buffer, true, GTK_WINDOW(gtk_widget_get_toplevel(ld_loaddlg)));
                                }
                            } else {
                                tX_warning("ladspa_plugin section without a ladspa_id element.");
                            }

                        } else {
                            tX_warning("unhandled element %s in fx section.", cur->name);
                        }
                    }
                }
            }

            if (!elementFound) {
                tX_warning("unhandled element %s in turntable section.", cur->name);
            }
        }
    }

    recalc_volume();

    if (mix_solo) {
        solo_ctr++;
    }

    if (xpar_id >= 0) {
        set_x_input_parameter(tX_seqpar::get_sp_by_persistence_id(xpar_id));
    } else
        set_x_input_parameter(NULL);

    if (ypar_id) {
        set_y_input_parameter(tX_seqpar::get_sp_by_persistence_id(ypar_id));
    } else
        set_y_input_parameter(NULL);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gui.mute), mix_mute);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gui.solo), mix_solo);

    return 0;
}

void vtt_class ::delete_all() {
    while (main_list.size()) {
        delete ((*main_list.begin()));
    }

    /* Take care of the main channel events.. */
    sequencer.delete_all_events(tX_sequencer::DELETE_ALL);

    /* Now reset main settings ot the default: */
    set_main_pitch(1.0);
    set_main_volume(1.0);

    sp_main_pitch.do_exec(1.0);
    sp_main_pitch.do_update_graphics();

    sp_main_volume.do_exec(1.0);
    sp_main_volume.do_update_graphics();

    /* Remove main channel MIDI mappings... */
    sp_main_pitch.bound_midi_event.type = tX_midievent::NONE;
    sp_main_volume.bound_midi_event.type = tX_midievent::NONE;

    seq_update();
}

int vtt_class ::load_all(xmlDocPtr doc, char* fname) {
    xmlNodePtr root = xmlDocGetRootElement(doc);
    int elementFound = 0;
    char fn_buff[4096];
    double dvalue;
    int res = 0;
    int restmp = 0;

    if (!root) {
        tX_error("no root element? What kind of XML document is this?");
        return 1;
    }

    if (xmlStrcmp(root->name, (const xmlChar*)"terminatorXset")) {
        tX_error("this is not a terminatorXset file.") return 2;
    }

    if (xmlGetProp(root, (xmlChar*)"version") == NULL) {
        tX_error("the set file lacks a version attribute.");
        return 3;
    }

    if (xmlStrcmp(xmlGetProp(root, (xmlChar*)"version"), (xmlChar*)TX_XML_SETFILE_VERSION_11) &&
	    xmlStrcmp(xmlGetProp(root, (xmlChar*)"version"), (xmlChar*)TX_XML_SETFILE_VERSION_10)) {
        tX_warning("this set file is version %s - while this releases uses version %s - trying to load anyway.", xmlGetProp(root, (xmlChar*)"version"), TX_XML_SETFILE_VERSION);
    }

    /* delete current tables... */
    delete_all();

    int table_ctr = 0;

    /* counting turntables.. */
    for (xmlNodePtr cur = root->xmlChildrenNode; cur != NULL; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE) {
            if (xmlStrcmp(cur->name, (xmlChar*)"turntable") == 0) {
                table_ctr++;
            }
        }
    }

    tX_debug("Found %i turntables in set.", table_ctr);

    ld_create_loaddlg(TX_LOADDLG_MODE_MULTI, table_ctr);
    ld_set_setname(fname);

    /* parsing all */
    for (xmlNodePtr cur = root->xmlChildrenNode; cur != NULL; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE) {
            elementFound = 0;

            /* compatibilty for pre 4.1 set files */
            restore_float_id("master_volume", main_volume, sp_main_volume, set_main_volume(main_volume));
            restore_float_id("master_pitch", globals.pitch, sp_main_pitch, set_main_pitch(globals.pitch));
            /* 4.1+ uses main instead */
            restore_float_id("main_volume", main_volume, sp_main_volume, set_main_volume(main_volume));
            restore_float_id("main_pitch", globals.pitch, sp_main_pitch, set_main_pitch(globals.pitch));

            if ((!elementFound) && (xmlStrcmp(cur->name, (xmlChar*)"turntable") == 0)) {
                elementFound = 1;
                vtt_class* vtt = new vtt_class(1);
                vtt->load(doc, cur);

                if (strlen(vtt->filename)) {
                    strcpy(fn_buff, vtt->filename);
                    ld_set_filename(fn_buff);

                    restmp = (int)vtt->load_file(fn_buff);
                    res += restmp;
                }

                gtk_box_pack_start(GTK_BOX(control_parent), vtt->gui.control_box, TRUE, TRUE, 0);
                gtk_box_pack_start(GTK_BOX(audio_parent), vtt->gui.audio_box, TRUE, TRUE, 0);
                if (vtt->audio_hidden)
                    vtt->hide_audio(vtt->audio_hidden);
                if (vtt->control_hidden)
                    vtt->hide_control(vtt->control_hidden);
            }
            if ((!elementFound) && (xmlStrcmp(cur->name, (xmlChar*)"sequencer") == 0)) {
                elementFound = 1;
                sequencer.load(doc, cur);
            }
            if (!elementFound) {
                tX_warning("unhandled element %s in setfile %s", cur->name, fname);
            }
        }
    }

    sp_main_volume.do_update_graphics();
    sp_main_pitch.do_update_graphics();

    while (gtk_events_pending())
        gtk_main_iteration();

    list<vtt_class*>::iterator vtt;

    for (vtt = main_list.begin(); vtt != main_list.end(); vtt++) {
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW((*vtt)->gui.scrolled_win));
        gtk_adjustment_set_value(adj, (*vtt)->control_scroll_adjustment);
    }

    ld_destroy();

    return res;
}

void add_vtt(GtkWidget* ctrl, GtkWidget* audio, char* fn) {
    vtt_class* new_tt;
    new_tt = new vtt_class(1);
    gtk_box_pack_start(GTK_BOX(ctrl), new_tt->gui.control_box, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(audio), new_tt->gui.audio_box, TRUE, TRUE, 0);
    if (fn)
        new_tt->load_file(fn);
}

extern void vg_move_fx_panel_up(tX_panel* panel, vtt_class* vtt, bool stereo);
extern void vg_move_fx_panel_down(tX_panel* panel, vtt_class* vtt, bool stereo);

//#define debug_fx_stack(); for (i=list->begin(); i != list->end(); i++) puts((*i)->get_info_string());
#define debug_fx_stack() ;

void vtt_class ::effect_move(vtt_fx* effect, int pos) {
    list<vtt_fx*>::iterator i;
    list<vtt_fx*>::iterator previous;
    list<vtt_fx*>* list;
    int ctr = 0;

    if (effect->is_stereo()) {
        list = (std::list<vtt_fx*>*)&stereo_fx_list;
    } else {
        list = &fx_list;
    }

    if (pos == 0) {
        list->remove(effect);
        list->push_front(effect);
    } else if (pos == list->size() - 1) {
        list->remove(effect);
        list->push_back(effect);
    } else {
        list->remove(effect);
        for (previous = i = list->begin(), ctr = 0; ctr <= pos; i++, ctr++) {
            previous = i;
        }
        list->insert(previous, effect);
    }
    debug_fx_stack();
}

void vtt_class ::effect_up(vtt_fx* effect) {
    list<vtt_fx*>::iterator i;
    list<vtt_fx*>::iterator previous;
    list<vtt_fx*>* list;
    int ok = 0;

    if (effect->is_stereo()) {
        list = (std::list<vtt_fx*>*)&stereo_fx_list;
    } else {
        list = &fx_list;
    }

    debug_fx_stack();

    if ((*list->begin()) == effect)
        return;

    for (previous = i = list->begin(); i != list->end(); i++) {
        if ((*i) == effect) {
            ok = 1;
            break;
        }
        previous = i;
    }

    if (ok) {
        pthread_mutex_lock(&render_lock);
        list->remove(effect);
        list->insert(previous, effect);
        pthread_mutex_unlock(&render_lock);

        vg_move_fx_panel_up(effect->get_panel(), this, effect->is_stereo());
    }

    debug_fx_stack();
}

void vtt_class ::effect_down(vtt_fx* effect) {
    list<vtt_fx*>::iterator i;
    list<vtt_fx*>* list;
    int ok = 0;

    if (effect->is_stereo()) {
        list = (std::list<vtt_fx*>*)&stereo_fx_list;
    } else {
        list = &fx_list;
    }

    debug_fx_stack();

    for (i = list->begin(); i != list->end(); i++) {
        if ((*i) == effect) {
            ok = 1;
            break;
        }
    }

    if ((ok) && (i != list->end())) {
        i++;
        if (i == list->end())
            return;
        i++;

        pthread_mutex_lock(&render_lock);
        list->remove(effect);

        list->insert(i, effect);
        vg_move_fx_panel_down(effect->get_panel(), this, effect->is_stereo());
        pthread_mutex_unlock(&render_lock);
    }

    debug_fx_stack();
}

void vtt_class ::effect_remove(vtt_fx_ladspa* effect) {
    pthread_mutex_lock(&render_lock);
    if (effect->is_stereo()) {
        stereo_fx_list.remove((vtt_fx_stereo_ladspa*)effect);
    } else {
        fx_list.remove(effect);
    }
    pthread_mutex_unlock(&render_lock);

    delete effect;
}

extern void gui_hide_control_panel(vtt_class* vtt, bool hide);
extern void gui_hide_audio_panel(vtt_class* vtt, bool hide);

void vtt_class ::hide_audio(bool hide) {
    audio_hidden = hide;
    gui_hide_audio_panel(this, hide);
}

void vtt_class ::hide_control(bool hide) {
    control_hidden = hide;
    gui_hide_control_panel(this, hide);
}

void vtt_class ::set_sample_rate(int samplerate) {
    list<vtt_class*>::iterator vtt;
    double sr = (double)samplerate;

    last_sample_rate = samplerate;

    for (vtt = main_list.begin(); vtt != main_list.end(); vtt++) {
        if ((*vtt)->audiofile) {
            double file_rate = (*vtt)->audiofile->get_sample_rate();
            (*vtt)->audiofile_pitch_correction = file_rate / sr;
        } else {
            (*vtt)->audiofile_pitch_correction = 1.0;
        }
        (*vtt)->recalc_pitch();
    }

    int no_samples = (int)(sr * 0.001); // Forcing 1 ms blocksize

    set_mix_buffer_size(no_samples);
}

void vtt_class ::adjust_to_main_pitch(int leader_cycles, int cycles, bool create_event) {
    if (!sync_leader)
        return;
    if (this == sync_leader)
        return;
    if (!sync_leader->audiofile)
        return;
    if (!audiofile)
        return;

    double leader_time = ((double)leader_cycles) / sync_leader->rel_pitch * sync_leader->audiofile->get_no_samples() / ((double)sync_leader->audiofile->get_sample_rate());
    double my_rel_pitch = ((audiofile->get_no_samples() / ((double)audiofile->get_sample_rate())) * ((double)cycles)) / leader_time;

    if (create_event) {
        sp_pitch.do_exec(my_rel_pitch);
        sp_pitch.record_value(my_rel_pitch);
    } else {
        sp_pitch.do_exec(my_rel_pitch);
    }

    tX_debug("leader_time: %lf, res_pitch: %lf - res time: %lf, (%lf, %lf)", leader_time, my_rel_pitch, ((double)cycles) * my_rel_pitch * audiofile->get_no_samples() / ((double)audiofile->get_sample_rate()), (double)sync_leader->audiofile->get_sample_rate(), (double)audiofile->get_sample_rate());

    sp_pitch.update_graphics();
}
