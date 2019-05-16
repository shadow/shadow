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

typedef struct _ConfigurationPluginElement ConfigurationPluginElement;
struct _ConfigurationPluginElement {
    /* required */
    ConfigurationStringAttribute id;
    ConfigurationStringAttribute path;
    /* optional*/
    ConfigurationStringAttribute startsymbol;
};

typedef struct _ConfigurationTopologyElement ConfigurationTopologyElement;
struct _ConfigurationTopologyElement {
    /* at least one is required, which one is optional */
    ConfigurationStringAttribute path;
    ConfigurationStringAttribute cdata;
};

typedef struct _ConfigurationProcessElement ConfigurationProcessElement;
struct _ConfigurationProcessElement {
    /* required */
    ConfigurationStringAttribute plugin;
    ConfigurationIntegerAttribute starttime;
    ConfigurationStringAttribute arguments;
    /* optional*/
    ConfigurationIntegerAttribute stoptime;
    ConfigurationStringAttribute preload;
};

typedef struct _ConfigurationHostElement ConfigurationHostElement;
struct _ConfigurationHostElement {
    /* required */
    ConfigurationStringAttribute id;
    GQueue* processes;
    /* optional*/
    ConfigurationStringAttribute ipHint;
    ConfigurationStringAttribute citycodeHint;
    ConfigurationStringAttribute countrycodeHint;
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

typedef struct _ConfigurationShadowElement ConfigurationShadowElement;
struct _ConfigurationShadowElement {
    /* required */
    ConfigurationIntegerAttribute stoptime;
    /* optional*/
    ConfigurationStringAttribute preloadPath;
    ConfigurationStringAttribute environment;
    ConfigurationIntegerAttribute bootstrapEndTime;
};

Configuration* configuration_new(Options* options, const GString* file);
void configuration_free(Configuration* config);

ConfigurationShadowElement* configuration_getShadowElement(Configuration* config);
ConfigurationTopologyElement* configuration_getTopologyElement(Configuration* config);
ConfigurationPluginElement* configuration_getPluginElementByID(Configuration* config, const gchar* pluginID);
GQueue* configuration_getPluginElements(Configuration* config);
GQueue* configuration_getHostElements(Configuration* config);

/** @} */

#endif /* SHD_PARSER_H_ */
