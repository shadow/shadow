/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-action-internal.h"

struct _GenerateCDFAction {
	Action super;
	GQuark id;
	guint64 center;
	guint64 width;
	guint64 tail;
	MAGIC_DECLARE;
};

RunnableFunctionTable generatecdf_functions = {
	(RunnableRunFunc) generatecdf_run,
	(RunnableFreeFunc) generatecdf_free,
	MAGIC_VALUE
};

GenerateCDFAction* generatecdf_new(GString* name, guint64 center, guint64 width,
		guint64 tail)
{
	g_assert(name);
	GenerateCDFAction* action = g_new0(GenerateCDFAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &generatecdf_functions);

	action->id = g_quark_from_string((const gchar*)name->str);
	action->center = center;
	action->width = width;
	action->tail = tail;

	return action;
}

void generatecdf_run(GenerateCDFAction* action) {
	MAGIC_ASSERT(action);

//	CumulativeDistribution* cdf = cdf_generate(action->id, action->center, action->width, action->tail);
	warning("cdf '%s' not supported", g_quark_to_string(action->id));
}

void generatecdf_free(GenerateCDFAction* action) {
	MAGIC_ASSERT(action);

	MAGIC_CLEAR(action);
	g_free(action);
}
