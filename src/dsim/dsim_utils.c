/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "dsim_utils.h"
#include "heap.h"
#include "log.h"


/* globals! */
dsim_tp global_current_dsim;

/* for yacc stuff */
extern FILE * yyin;
extern gchar * _dsim_curbuf_input;
extern gint dsim_is_error;

/* define possible functions and their argument formats */
struct {
	gchar operation_text[20];
	gchar operation_argfmt[20];
	enum operation_type type;
} dsim_operation_list[] = {
		/* plugin_path */
		{"load_plugin", "s", OP_LOAD_PLUGIN},
		/* cdf_path */
		{"load_cdf", "s", OP_LOAD_CDF},
		/* base, width, tail */
		{"generate_cdf", "nnn", OP_GENERATE_CDF},
		/* cdf_id, reliability_fraction */
		{"create_network", "in", OP_CREATE_NETWORK},
		/* net1_id, cdf_to_net2_id, reliablity_to_net2, net2_id, cdf_to_net1_id, reliability_to_net1 */
		{"connect_networks", "iiniin", OP_CONNECT_NETWORKS},
		/* base_hostname */
		{"create_hostname", "s", OP_CREATE_HOSTNAME},
		/* quantity, plugin_id, net_id, hostname_id, upstream_cdf_id, downstream_cdf_id, cpu_speed_cdf_id, cmd_line_args */
		{"create_nodes", "niiiiiis", OP_CREATE_NODES},
		{"end", "", OP_END},
		{"", "", OP_NULL}
	};

dsim_vartracker_tp dsim_vartracker_create(void) {
	dsim_vartracker_tp vt = malloc(sizeof(*vt));

	if(!vt)
		printfault(EXIT_NOMEM, "Out of memory: dsim_vartracker_create");
	vt->btree = btree_create(8);

	return vt;
}

void dsim_vartracker_destroy(dsim_vartracker_tp vt) {
	dsim_vartracker_var_tp var;
	gint v;
	if(vt) {
		while((var = btree_get_head(vt->btree, &v)) != NULL) {
			if(var->data && var->freeable)
				free(var->data);
			btree_remove(vt->btree, v);
			free(var);
		}

		btree_destroy(vt->btree);
		vt->btree = NULL;
		free(vt);
	}

	return;
}

gint dsim_vartracker_nameencode(gchar * name) {
	/* adler32 */
	guint a=1,b=1,slen=strlen(name),i;

	for(i=0;i<slen;i++) {
		a += name[i];
		if(i)
			b += b;
		b += name[i];
	}

	return (a % 65521) + (b % 65521)*65536;
}

dsim_vartracker_var_tp dsim_vartracker_findvar(dsim_vartracker_tp vt, gchar * name) {
	gint namekey = dsim_vartracker_nameencode(name);
	dsim_vartracker_var_tp var = btree_get(vt->btree, namekey);

	if(var)
		return var;
	else
		return dsim_vartracker_createvar(vt, name, NULL);
}

dsim_vartracker_var_tp dsim_vartracker_createvar(dsim_vartracker_tp vt, gchar * name, gpointer value) {
	dsim_vartracker_var_tp var = malloc(sizeof(*var));
	gint namekey = dsim_vartracker_nameencode(name);

	if(!var)
		printfault(EXIT_NOMEM, "Out of memory: dsim_vartracker_create");

	strncpy(var->varname, name, DSIM_VARTRACKER_MAXVARLEN);
	var->varname[DSIM_VARTRACKER_MAXVARLEN-1] = 0;
	var->data = value;
	var->data_type = dsim_vartracker_type_null;
	var->freeable = 0;

	btree_insert(vt->btree, namekey, var);
	return var;
}

dsim_tp dsim_create (gchar * dsim_file) {
	dsim_tp dsim = malloc(sizeof(*dsim));

	if(!dsim)
		printfault(EXIT_NOMEM, "Out of memory: dsim_create");

	/* create the tracker for all the oplist events */
	dsim->oplist = evtracker_create(10, 1);

	/* create the variable tracker */
	dsim->vartracker = dsim_vartracker_create();

	global_current_dsim = dsim;
	_dsim_curbuf_input = dsim_file;

	/* send to yacc */
	yyparse();

	global_current_dsim = NULL;

	if(dsim_is_error > 0) {
		dsim_destroy(dsim);
		dsim = NULL;
	}

	return dsim;
}

void dsim_destroy(dsim_tp dsim) {
	operation_tp cur_op;

	/* clear out the ops */
	while((cur_op=evtracker_get_nextevent(dsim->oplist, NULL, 1)))
		dsim_destroy_operation(cur_op);

	/* destroy all variables */
	dsim_vartracker_destroy(dsim->vartracker);

	/* destroy op evtracker */
	evtracker_destroy(dsim->oplist);

	free(dsim);
}

void dsim_destroy_delement_arglist(delement_tp arg) {
	delement_tp next;
	while(arg) {
		next = arg->next;
		if(arg->data && (arg->DTYPE == DT_NUMBER || arg->DTYPE == DT_STRING))
			free(arg->data);
		free(arg);
		arg = next;
	}
	return;
}

gint dsim_construct_arglist(gchar * fmt, operation_arg_tp out_args, delement_tp in_args) {
	delement_tp ca = in_args;
	gint i;

	for(i=0; i<strlen(fmt); i++){
		if(!ca || !ca->data)
			return 0;

		switch(fmt[i]){
			case 'i':
				if(ca->DTYPE == DT_IDEN) {
					out_args[i].v.var_val  = ca->data;
					out_args[i].DTYPE = DT_IDEN;
				} else
					i = -1;
				break;

			case 'n':
				if(ca->DTYPE == DT_NUMBER) {
					out_args[i].v.gdouble_val  = *((gdouble*)ca->data);
					out_args[i].DTYPE = DT_NUMBER;
				} else
					i = -1;
				break;

			case 's':
				if(ca->DTYPE == DT_STRING) {
					/* don't realloc, just rip out the already allocated memory for the string */
					out_args[i].v.string_val  = ca->data;
					ca->data = NULL;
					out_args[i].DTYPE = DT_STRING;
				} else
					i = -1;
				break;

			case 'I':
			case 'O':
				if(ca->DTYPE == DT_IDEN) {
					out_args[i].v.voidptr_val  = ca->data;
					out_args[i].DTYPE = DT_IDEN;
				} else
					i = -1;
				break;

			default:
				return 0;
		}

		if(i<0)
			return 0;

		ca = ca->next;
	}

	if(ca != NULL)
		return 0;

	return 1;
}

operation_tp dsim_create_operation(gchar * fname,delement_tp args,dsim_vartracker_var_tp retval){
	operation_tp op = NULL;
	gint i;

	if(!fname)
		return NULL;

	for(i=0; dsim_operation_list[i].type!=OP_NULL; i++) {
		if(!strcmp(dsim_operation_list[i].operation_text,fname))
			break;
	}

	if(dsim_operation_list[i].type==OP_NULL)
		return NULL;

	/* allocate for operation plus all arguments */
	op = malloc(sizeof(operation_t) + sizeof(operation_arg_t)*strlen(dsim_operation_list[i].operation_argfmt));

	op->type = dsim_operation_list[i].type;
	op->retval = retval;
	op->num_arguments = strlen(dsim_operation_list[i].operation_argfmt);

	/* construct the arguments */
	if(!dsim_construct_arglist(dsim_operation_list[i].operation_argfmt, op->arguments, args)) {
		dsim_destroy_delement_arglist(args);
		free(op);
		return NULL;
	}

	dsim_destroy_delement_arglist(args);
	return op;
}

void dsim_destroy_operation(operation_tp op) {
	for(gint i=0; i<op->num_arguments; i++) {
		if(op->arguments[i].DTYPE == DT_STRING)
			free(op->arguments[i].v.string_val);
	}
	free(op);
}

gint dsim_finalize_operations(dsim_tp dsim, delement_tp de_ops, ptime_t tv){
	delement_tp cur_de = de_ops;
	delement_tp tmp;
	operation_tp cur_op;
	gint rv = 1;

	while(cur_de) {
		if(cur_de->DTYPE == DT_OP) {
			cur_op = cur_de->data;

			if(cur_op) {
				cur_op->target_time = tv;

				evtracker_insert_event(dsim->oplist, tv, cur_op);
			}
		}

		tmp = cur_de;
		cur_de = cur_de->next;
		free(tmp);
	}

	return rv;
}


ptime_t dsim_get_nexttime(dsim_tp dsim) {
	ptime_t pt;

	if(evtracker_get_nextevent(dsim->oplist, &pt, 0))
		return pt;
	else return PTIME_INVALID;
}

operation_tp dsim_get_nextevent(dsim_tp dsim, ptime_t * time, gchar removal) {
	return evtracker_get_nextevent(dsim->oplist, time, removal );
}


