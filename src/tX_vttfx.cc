/*
    terminatorX - realtime audio scratching software
    Copyright (C) 1999-2003  Alexander K�nig
 
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
 
    File: tX_vttfx.cc
 
    Description: This handles the effects in the per vtt fx chain. Supports the
                 buitlin echo/lowpass effects and ladspa plugins.
*/

#include "tX_vttfx.h"
#include <stdio.h>
#include <glib.h>
#include "tX_vtt.h"
#define myvtt ((vtt_class *) vtt)
#include "tX_global.h"

float ladspa_dummy_output_port;

void vtt_fx :: activate ()
{
	fprintf(stderr, "tX: Oops: activate() abstract vtt_fx?");
}

void vtt_fx :: deactivate ()
{
	fprintf(stderr, "tX: Oops: deactivate() abstract vtt_fx?");
}

void vtt_fx :: run ()
{
	fprintf(stderr, "tX: Oops: run() abstract vtt_fx?");
}

void vtt_fx :: save (FILE *output, char *indent) {
	fprintf(stderr, "tX: Oops: run() abstract vtt_fx?");
}

void vtt_fx :: reconnect_buffer()
{
}

int vtt_fx :: isEnabled ()
{
	fprintf(stderr, "tX: Oops: isEnabled() abstract vtt_fx?");
	return 0;
}

const char * vtt_fx :: get_info_string()
{
	return "tX: Oops: Why do you see this info string?";
}

vtt_fx :: ~vtt_fx() {}

/******************* builtin fx ***/

/* lowpass */ 
void vtt_fx_lp :: activate() { myvtt->lp_reset(); }
void vtt_fx_lp :: deactivate() { /* NOP */ }
void vtt_fx_lp :: run() { myvtt->render_lp(); }
int vtt_fx_lp :: isEnabled() { return myvtt->lp_enable; }

void vtt_fx_lp :: save (FILE *output, char *indent) { 
	fprintf(output, "%s<cutoff/>\n", indent);
}

const char *vtt_fx_lp :: get_info_string()
{
	return "TerminatorX built-in resonant lowpass filter.";
}


/* echo */
void vtt_fx_ec :: activate() { /* NOP */ }
void vtt_fx_ec :: deactivate() { myvtt->ec_clear_buffer(); }
void vtt_fx_ec :: run() { myvtt->render_ec(); }
int vtt_fx_ec :: isEnabled() { return myvtt->ec_enable; }

void vtt_fx_ec :: save (FILE *output, char *indent) { 
	fprintf(output, "%s<lowpass/>\n", indent);	
}

const char *vtt_fx_ec :: get_info_string()
{
	return "TerminatorX built-in echo effect.";
}


/******************** LADSPA fx ***/
/* short cut "cpd" macro to current port descriptor */

#define cpd plugin->getDescriptor()->PortDescriptors[port]
#define cpn plugin->getDescriptor()->PortNames[port]
#define cph plugin->getDescriptor()->PortRangeHints[port]

void vtt_fx_ladspa :: reconnect_buffer()
{
	plugin->getDescriptor()->connect_port(instance, input_port, myvtt->output_buffer);	
	plugin->getDescriptor()->connect_port(instance, output_port, myvtt->output_buffer);		
}

static void wrapstr(char *str)
{
	char temp[256]="";
	char target[2048]="";
	char *token;

	token=strtok(str, " ");	
	
	while(token)
	{
		if (strlen(token)+strlen(temp)<10)
		{
			if (strlen(temp)) strcat(temp, " ");
			strcat(temp, token);
		}
		else
		{
			if (strlen(temp))
			{
				if(strlen(target)) strcat(target, "\n");
				if(strlen(temp)>10)
				{
					temp[8]='.';
					temp[9]='.';
					temp[10]='.';
					temp[11]=0;
				}
				strcat(target, temp);
				strcpy(temp,token);
			}
		}
		token=strtok(NULL, " ");
	}

       if (strlen(temp))
       {
               if(strlen(target)) strcat(target, "\n");
               strcat(target, temp);
       }

	strcpy(str, target);
 }

vtt_fx_ladspa :: vtt_fx_ladspa(LADSPA_Plugin *p, void *v)
{
	int port;
	float min, max;
	char buffer[2048];
	char buffer2[2048];
	
	tX_seqpar_vttfx *sp;

	plugin=p; vtt=v;
	
	instance=(LADSPA_Handle *) plugin->getDescriptor()->instantiate(plugin->getDescriptor(), 44100);
	
	if (!instance)
	{
		fprintf (stderr, "tX: Fatal Error: failed to instantiate plugin \"%s\".\n", plugin->getDescriptor()->Name);
		/* How to handle this ? */
	}
	
	sp = sp_enable = new tX_seqpar_vttfx_bool();
	sp->set_mapping_parameters(1, 0, 0, 0);
	sprintf(buffer, "%s: Enable", plugin->getName());
	sp->set_name(buffer, "Enable");
	sp->set_vtt(vtt);
	controls.push_back(sp);	

	
	/* connecting ports */
	for (port=0; port < plugin->getPortCount(); port++)
	{
		if (LADSPA_IS_PORT_AUDIO(cpd))
		{
			if (LADSPA_IS_PORT_INPUT(cpd)) input_port=port;
			else if (LADSPA_IS_PORT_OUTPUT(cpd)) output_port=port;
		}
		else if ((LADSPA_IS_PORT_CONTROL(cpd)) && (LADSPA_IS_PORT_INPUT(cpd)))
		{
			min=-22100;
			max=+22100;
			
			if (LADSPA_IS_HINT_BOUNDED_BELOW(cph.HintDescriptor)) min=cph.LowerBound;
			if (LADSPA_IS_HINT_BOUNDED_ABOVE(cph.HintDescriptor)) max=cph.UpperBound;
			
			if (LADSPA_IS_HINT_SAMPLE_RATE(cph.HintDescriptor)) 
			{
				min*=44100; max*=44100;
			}
			
			if (LADSPA_IS_HINT_TOGGLED(cph.HintDescriptor))
			{
				sp=new tX_seqpar_vttfx_bool();
				sp->set_mapping_parameters(max, min, 0, 0);
			}
			else
			if (LADSPA_IS_HINT_INTEGER(cph.HintDescriptor))
			{
				sp=new tX_seqpar_vttfx_int();
				sp->set_mapping_parameters(max, min, 0, 0);
			}
			else
			{
				sp=new tX_seqpar_vttfx_float();
				sp->set_mapping_parameters(max, min, (max-min)/100.0, 1);
			}
			
			sprintf(buffer, "%s: %s", plugin->getLabel(), cpn);
			strcpy(buffer2, cpn);
			
			wrapstr(buffer2);
			
			sp->set_name(buffer, buffer2);
			sp->set_vtt(vtt);
			plugin->getDescriptor()->connect_port(instance, port, sp->get_value_ptr());
			controls.push_back(sp);
		}
		else if ((LADSPA_IS_PORT_CONTROL(cpd)) && (LADSPA_IS_PORT_OUTPUT(cpd)))
		{
			plugin->getDescriptor()->connect_port(instance, port, &ladspa_dummy_output_port);
		}
	}
	reconnect_buffer();
}

void vtt_fx_ladspa :: activate()
{
	if (plugin->getDescriptor()->activate) plugin->getDescriptor()->activate(instance);
}

void vtt_fx_ladspa :: deactivate()
{
	if (plugin->getDescriptor()->deactivate) plugin->getDescriptor()->deactivate(instance);
}

void vtt_fx_ladspa :: run()
{
	plugin->getDescriptor()->run(instance, (vtt_class :: samples_in_mix_buffer)>>1);
}

int vtt_fx_ladspa :: isEnabled()
{
	return (int) sp_enable->get_value();
}

const char *vtt_fx_ladspa :: get_info_string()
{
	return plugin->get_info_string();
}

vtt_fx_ladspa :: ~vtt_fx_ladspa()
{
	list <tX_seqpar_vttfx *> :: iterator sp;
	
	while (controls.size())
	{
		sp=controls.begin();
		controls.remove((*sp));
		
		delete (*sp);
	}		
	plugin->getDescriptor()->cleanup(instance);
	delete panel;
}


void vtt_fx_ladspa :: save (FILE *rc, char *indent) {
	long ID=plugin->getUniqueID();
	list <tX_seqpar_vttfx *> :: iterator sp;
	
	fprintf(rc, "%s<ladspa_plugin>\n", indent);
	strcat (indent, "\t");
	
	store_int("ladspa_id", ID);
	
	for (sp=controls.begin(); sp!=controls.end(); sp++) {
		store_float_id("param", (*sp)->get_value(), (*sp)->get_persistence_id());
	}
	
	store_bool("panel_hidden", panel->is_hidden());
	
	indent[strlen(indent)-1]=0;
	fprintf(rc, "%s</ladspa_plugin>\n", indent);
}

void vtt_fx_ladspa :: load(xmlDocPtr doc, xmlNodePtr node) {
	int dummy;
	bool hidden;
	list <tX_seqpar_vttfx *> :: iterator sp=controls.begin();
	int elementFound;
	guint32 pid=0;
	double val;
	
	for (xmlNodePtr cur=node->xmlChildrenNode; cur!=NULL; cur=cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			elementFound=0;
			
			restore_int("ladspa_id", dummy);
			restore_bool("hidden", hidden);
			if ((!elementFound) && (xmlStrcmp(cur->name, (xmlChar *) "param")==0)) {
				val=0;
				elementFound=1;
			
				if (sp==controls.end()) {
					tX_warning("found unexpected parameters for ladspa plugin [%i].", dummy);
				} else {			
					char *buff=(char *) xmlGetProp(cur, (xmlChar *) "id");
					sscanf(buff, "%i", &pid);
			
					if  (xmlNodeListGetString(doc, cur->xmlChildrenNode, 1)) {
						sscanf((char *) xmlNodeListGetString(doc, cur->xmlChildrenNode, 1), "%lf", &val); 
					}
					(*sp)->set_persistence_id(pid);
					(*sp)->do_exec(val);
					(*sp)->do_update_graphics();
					sp++;
				}
			}
			
			if (!elementFound) {
				tX_warning("unhandled ladspa_plugin element %s.", cur->name);
			}
		}
	}
	
	panel->hide(hidden);
}
