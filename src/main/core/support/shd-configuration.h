/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PARSER_H_
#define SHD_PARSER_H_

#include "shadow.h"

/**
 * @addtogroup Configuration
 * @{
 * Use this module to parse XML input files, and store/access configuration settings.
 */

/**
 * An opaque object used to parse XML files and store Shadow simulation configuration
 * settings. The member of this struct are private and should not be accessed directly.
 */
typedef struct _Configuration Configuration;

typedef struct _ConfigurationStringAttribute ConfigurationStringAttribute;
struct _ConfigurationStringAttribute {
    GString* string;
    gboolean isSet;
};

typedef struct _ConfigurationIntegerAttribute ConfigurationIntegerAttribute;
struct _ConfigurationIntegerAttribute {
    guint64 integer;
    gboolean isSet;
};

typedef struct _ConfigurationKillElement ConfigurationKillElement;
struct _ConfigurationKillElement {
    /* required */
    ConfigurationIntegerAttribute time;
    /* optional*/
};

typedef struct _ConfigurationPluginElement ConfigurationPluginElement;
struct _ConfigurationPluginElement {
    /* required */
    ConfigurationStringAttribute id;
    ConfigurationStringAttribute path;
    /* optional*/
};

typedef struct _ConfigurationTopologyElement ConfigurationTopologyElement;
struct _ConfigurationTopologyElement {
    /* at least one is required, which one is optional */
    ConfigurationStringAttribute path;
    ConfigurationStringAttribute cdata;
};

typedef struct _ConfigurationApplicationElement ConfigurationApplicationElement;
struct _ConfigurationApplicationElement {
    /* required */
    ConfigurationStringAttribute plugin;
    ConfigurationIntegerAttribute starttime;
    ConfigurationStringAttribute arguments;
    /* optional*/
    ConfigurationIntegerAttribute stoptime;
};

typedef struct _ConfigurationNodeElement ConfigurationNodeElement;
struct _ConfigurationNodeElement {
    /* required */
    ConfigurationStringAttribute id;
    GList* applications;
    /* optional*/
    ConfigurationStringAttribute ipHint;
    ConfigurationStringAttribute geocodeHint;
    ConfigurationStringAttribute typeHint;
    ConfigurationIntegerAttribute quantity;
    ConfigurationIntegerAttribute bandwidthdown;
    ConfigurationIntegerAttribute bandwidthup;
    ConfigurationIntegerAttribute interfacebuffer;
    ConfigurationIntegerAttribute socketrecvbuffer;
    ConfigurationIntegerAttribute socketsendbuffer;
    ConfigurationStringAttribute loglevel;
    ConfigurationStringAttribute heartbeatloglevel;
    ConfigurationStringAttribute heartbeatloginfo;
    ConfigurationIntegerAttribute heartbeatfrequency;
    ConfigurationIntegerAttribute cpufrequency;
    ConfigurationStringAttribute logpcap;
    ConfigurationStringAttribute pcapdir;
};

Configuration* configuration_new(Options* options, const GString* file);
void configuration_free(Configuration* config);

ConfigurationKillElement* configuration_getKillElement(Configuration* config);
ConfigurationTopologyElement* configuration_getTopologyElement(Configuration* config);
GList* configuration_getPluginElements(Configuration* config);
GList* configuration_getNodeElements(Configuration* config);

/** @} */

#endif /* SHD_PARSER_H_ */
