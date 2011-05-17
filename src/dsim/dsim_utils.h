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

#ifndef _dsim_utils_h
#define _dsim_utils_h

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "global.h"
#include "evtracker.h"
#include "btree.h"

/**
 * Returns the type of a named variable.
 */
unsigned char dsim_get_vartype(char *name);

#define DT_NONE			0

#define DT_IDEN 		1
#define DT_STRING 		2
#define DT_NUMBER 		3
#define DT_VOID			4

#define DT_MODULE 		5
#define DT_OP 			6

#define DT_FLOAT		7

#define DSIM_VARTRACKER_MAXVARLEN 50

enum dsim_vartype {
	dsim_vartracker_type_nettrack, dsim_vartracker_type_modtrack,
	dsim_vartracker_type_cdftrack, dsim_vartracker_type_basehostnametrack,
	dsim_vartracker_type_null
};


typedef struct dsim_vartracker_var_t {
	char varname[DSIM_VARTRACKER_MAXVARLEN];
	char freeable;
	void * data;
	enum dsim_vartype data_type;
}dsim_vartracker_var_t,*dsim_vartracker_var_tp;

typedef struct dsim_vartracker_t {
	btree_tp btree;
} dsim_vartracker_t, *dsim_vartracker_tp;

typedef struct dsim_t {
	evtracker_tp oplist;
	dsim_vartracker_tp vartracker;
} dsim_t, *dsim_tp;

typedef struct delement_t {
	void * data;
	unsigned char DTYPE;

	struct delement_t* next;
} delement_t, *delement_tp;

typedef struct operation_arg_t {
	union {
		double double_val;
		char * string_val;
		dsim_vartracker_var_tp var_val;
		void * voidptr_val;
	} v;
	unsigned char DTYPE;
}operation_arg_t, *operation_arg_tp;

typedef struct operation_t {
	enum operation_type type;
	dsim_vartracker_var_tp retval;
	int num_arguments;
	ptime_t target_time;
	operation_arg_t arguments[];
} operation_t, *operation_tp;

extern dsim_tp global_current_dsim;

/**
 * Creates a dsim_t object based on the given DSIM file. Returns NULL
 * on errors
 */
dsim_tp dsim_create (char * dsim_file);

/**
 * Returns the next time an operation occurs on the given dsim object.
 */
ptime_t dsim_get_nexttime(dsim_tp dsim);

/**
 * Returns the next soonest operation from the DSIM
 */
operation_tp dsim_get_nextevent(dsim_tp dsim, ptime_t * time, char removal);

/**
 * For Yacc parsing.. converts the delement_tp linked list into the internal format used by the
 * dsim_t object.
 */
int dsim_finalize_operations(dsim_tp dsim, delement_tp de_ops, ptime_t tv);

/**
 * Creates an operation for the given call name and linked list of arguments
 */
operation_tp dsim_create_operation(char * fname,delement_tp args,dsim_vartracker_var_tp retval);
void dsim_destroy_operation(operation_tp op);
/**
 * Constructs an argument list into the output argument out_args based on the
 * linked list in_args
 */
int dsim_construct_arglist(char * fmt, operation_arg_tp out_args, delement_tp in_args);

/**
 * Destroys an argument list in the linkedlist form
 */
void dsim_destroy_delement_arglist(delement_tp arg);

/**
 * Cleans up an entire dsim object
 **/
void dsim_destroy(dsim_tp dsim);

/**
 * Creates a variable in the vartracker
 */
dsim_vartracker_var_tp dsim_vartracker_createvar(dsim_vartracker_tp vt, char * name, void * value);

/**
 * Searches for a variable in the vartracker
 */
dsim_vartracker_var_tp dsim_vartracker_findvar(dsim_vartracker_tp vt, char * name) ;

/**
 * Encodes a variable name for hashing
 */
int dsim_vartracker_nameencode(char * name);

/**
 * Destroys a vartracker
 */
void dsim_vartracker_destroy(dsim_vartracker_tp vt) ;

/**
 * Creates a vartracker
 */
dsim_vartracker_tp dsim_vartracker_create(void);


extern int yyparse(void);
extern int yyerror(char* msg,...);
extern int yylex(void);

#endif
