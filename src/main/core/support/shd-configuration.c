/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

/* an internal module to help parse the XML file */
typedef struct _Parser Parser;
struct _Parser {
    GMarkupParseContext* context;

    GMarkupParser xmlRootParser;
    GMarkupParser xmlNodeParser;
    GMarkupParser xmlTopologyParser;

    /* help verify that application element plugins match plugin element ids */
    GHashTable* pluginIDStrings;
    GHashTable* pluginIDRefStrings;

    /* our final parsed config state */
    ConfigurationShadowElement* shadow;
    ConfigurationKillElement* kill;
    ConfigurationTopologyElement* topology;
    GList* plugins; // ConfigurationPluginElement
    GList* nodes; // ConfigurationNodeElement
    MAGIC_DECLARE;
};

static GString* _parser_findPathToFile(const gchar* relativeFilePathSuffix, const gchar* defaultShadowPath) {
    GString* foundPath = NULL;

    if(relativeFilePathSuffix && defaultShadowPath == NULL){
        /* ok, first check in current directory, then in ~/.shadow/plugins */
        gchar* currentDirStr = g_get_current_dir();
        gchar* currentPathStr = g_build_path("/", currentDirStr, relativeFilePathSuffix, NULL);

        if(g_file_test(currentPathStr, G_FILE_TEST_EXISTS) && g_file_test(currentPathStr, G_FILE_TEST_IS_REGULAR)) {
            foundPath = g_string_new(currentPathStr);
        }

        g_free(currentDirStr);
        g_free(currentPathStr);
    } else if(relativeFilePathSuffix) {
        gchar* pluginsPathStr = g_build_path("/", g_get_home_dir(), ".shadow", defaultShadowPath, relativeFilePathSuffix, NULL);

        if(g_file_test(pluginsPathStr, G_FILE_TEST_EXISTS) && g_file_test(pluginsPathStr, G_FILE_TEST_IS_REGULAR)) {
            foundPath = g_string_new(pluginsPathStr);
        }

        g_free(pluginsPathStr);
    }

    return foundPath;
}

static GString* _parser_expandUserPath(GString* string) {
    utility_assert(string);

    if(g_strstr_len(string->str, (gssize)1, "~")) {
        string = g_string_erase(string, (gssize)0, (gssize) 1);
        string = g_string_insert(string, (gssize)0, g_get_home_dir());
    }

    return string;
}

static GError* _parser_checkPath(ConfigurationStringAttribute* path) {
    utility_assert(path->string != NULL);

    GError* error = NULL;

    /* if path starts with '~', replace it with home directory path */
    path->string = _parser_expandUserPath(path->string);

    /* make sure the path is absolute */
    if(!g_path_is_absolute(path->string->str)) {
        /* search in current directory */
        GString* foundPath = _parser_findPathToFile(path->string->str, NULL);

        /* if not found, search in some default shadow install paths */
        if(!foundPath) {
            foundPath = _parser_findPathToFile(path->string->str, "plugins");
        }
        if(!foundPath) {
            foundPath = _parser_findPathToFile(path->string->str, "lib");
        }
        if(!foundPath) {
            foundPath = _parser_findPathToFile(path->string->str, "share");
        }

        if(foundPath) {
            g_string_free(path->string, TRUE);
            path->string = foundPath;
        }
    }

    if(!g_file_test(path->string->str, G_FILE_TEST_EXISTS) ||
            !g_file_test(path->string->str, G_FILE_TEST_IS_REGULAR)) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                "attribute 'path': '%s' is not a valid path to an existing regular file",
                path->string->str);
    }

    return error;
}

struct _Configuration {
    /* the parser we use to parse the shadow xml file */
    Parser* parser;
    /* a pointer to the cli options */
    Options* options;
    MAGIC_DECLARE;
};

static void _parser_freeTopologyElement(ConfigurationTopologyElement* topology) {
    utility_assert(topology != NULL);

    if(topology->path.isSet) {
        utility_assert(topology->path.string != NULL);
        g_string_free(topology->path.string, TRUE);
    }
    if(topology->cdata.isSet) {
        utility_assert(topology->cdata.string != NULL);
        g_string_free(topology->cdata.string, TRUE);
    }

    g_free(topology);
}

static void _parser_freePluginElement(ConfigurationPluginElement* plugin) {
    utility_assert(plugin != NULL);

    if(plugin->path.isSet) {
        utility_assert(plugin->path.string != NULL);
        g_string_free(plugin->path.string, TRUE);
    }
    if(plugin->id.isSet) {
        utility_assert(plugin->id.string != NULL);
        g_string_free(plugin->id.string, TRUE);
    }

    g_free(plugin);
}

static void _parser_freeApplicationElement(ConfigurationApplicationElement* process) {
    utility_assert(process != NULL);

    if(process->plugin.isSet) {
        utility_assert(process->plugin.string != NULL);
        g_string_free(process->plugin.string, TRUE);
    }
    if(process->preload.isSet) {
        utility_assert(process->preload.string != NULL);
        g_string_free(process->preload.string, TRUE);
    }
    if(process->arguments.isSet) {
        utility_assert(process->arguments.string != NULL);
        g_string_free(process->arguments.string, TRUE);
    }

    g_free(process);
}

static void _parser_freeNodeElement(ConfigurationNodeElement* node) {
    utility_assert(node != NULL);

    if(node->id.isSet) {
        utility_assert(node->id.string != NULL);
        g_string_free(node->id.string, TRUE);
    }
    if(node->ipHint.isSet) {
        utility_assert(node->ipHint.string != NULL);
        g_string_free(node->ipHint.string, TRUE);
    }
    if(node->geocodeHint.isSet) {
        utility_assert(node->geocodeHint.string != NULL);
        g_string_free(node->geocodeHint.string, TRUE);
    }
    if(node->typeHint.isSet) {
        utility_assert(node->typeHint.string != NULL);
        g_string_free(node->typeHint.string, TRUE);
    }
    if(node->loglevel.isSet) {
        utility_assert(node->loglevel.string != NULL);
        g_string_free(node->loglevel.string, TRUE);
    }
    if(node->heartbeatloglevel.isSet) {
        utility_assert(node->heartbeatloglevel.string != NULL);
        g_string_free(node->heartbeatloglevel.string, TRUE);
    }
    if(node->heartbeatloginfo.isSet) {
        utility_assert(node->heartbeatloginfo.string != NULL);
        g_string_free(node->heartbeatloginfo.string, TRUE);
    }
    if(node->logpcap.isSet) {
        utility_assert(node->logpcap.string != NULL);
        g_string_free(node->logpcap.string, TRUE);
    }
    if(node->pcapdir.isSet) {
        utility_assert(node->pcapdir.string != NULL);
        g_string_free(node->pcapdir.string, TRUE);
    }
    if(node->applications) {
        g_list_free_full(node->applications, (GDestroyNotify)_parser_freeApplicationElement);
    }

    g_free(node);
}

static void _parser_freeShadowElement(ConfigurationShadowElement* shadow) {
    utility_assert(shadow != NULL);

    if(shadow->preloadPath.isSet) {
        utility_assert(shadow->preloadPath.string != NULL);
        g_string_free(shadow->preloadPath.string, TRUE);
    }
    if(shadow->environment.isSet) {
        utility_assert(shadow->environment.string != NULL);
        g_string_free(shadow->environment.string, TRUE);
    }

    g_free(shadow);
}

static void _parser_freeKillElement(ConfigurationKillElement* kill) {
    utility_assert(kill != NULL);
    g_free(kill);
}

static gboolean _parser_hasTopology(Parser* parser) {
    if(parser->topology && (parser->topology->path.isSet || parser->topology->cdata.isSet)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static GError* _parser_handleTopologyAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
    if(_parser_hasTopology(parser)) {
        return NULL;
    }

    GError* error = NULL;
    ConfigurationTopologyElement* topology = g_new0(ConfigurationTopologyElement, 1);

    const gchar **nameCursor = attributeNames;
    const gchar **valueCursor = attributeValues;

    /* check the attributes */
    while (!error && *nameCursor) {
        const gchar* name = *nameCursor;
        const gchar* value = *valueCursor;

        debug("found attribute '%s=%s'", name, value);

        if (!topology->path.isSet && !g_ascii_strcasecmp(name, "path")) {
            topology->path.string = g_string_new(utility_getHomePath(value));
            topology->path.isSet = TRUE;
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                            "unknown 'topology' attribute '%s'", name);
        }

        nameCursor++;
        valueCursor++;
    }

    /* validate the values */
    if(!error && topology->path.isSet) {
        error = _parser_checkPath(&(topology->path));
    }

    if(!error && topology->path.isSet) {
        utility_assert(topology->path.string != NULL);
        utility_assert(parser->topology == NULL);
        parser->topology = topology;
    } else {
        _parser_freeTopologyElement(topology);
    }

    return error;
}

static void _parser_handleTopologyText(GMarkupParseContext *context,
        const gchar* text, gsize textLength, gpointer userData, GError** error) {
    Parser* parser = (Parser*) userData;

    if(_parser_hasTopology(parser)) {
        return;
    }

    ConfigurationTopologyElement* topology = g_new0(ConfigurationTopologyElement, 1);

    /* note: the text is not null-terminated! */

    if(textLength > 0) {
        GString* textBuffer = g_string_new_len(text, (gssize)textLength);
        gchar* strippedText = g_strstrip(textBuffer->str);

        /* look for text wrapped in <![CDATA[TEXT]]>
         * note - we could also use a processing instruction by wrapping it in <?embedded TEXT ?>,
         * but processing instructions can not be nested
         * http://www.w3.org/TR/REC-xml/#sec-pi */
        if(strippedText && g_str_has_prefix(strippedText, "<![CDATA[") &&
                g_str_has_suffix(strippedText, "]]>")) {
            gchar* cdata = &strippedText[9];
            gssize cdataLength = (gssize) (textLength - 12);

            topology->cdata.string = g_string_new_len(cdata, cdataLength);
            topology->cdata.isSet = TRUE;
        }

        g_string_free(textBuffer, TRUE);
    }

    if(topology->cdata.isSet) {
        utility_assert(topology->cdata.string != NULL);
        utility_assert(parser->topology == NULL);
        parser->topology = topology;
    } else {
        _parser_freeTopologyElement(topology);
    }
}

static GError* _parser_handlePluginAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
    ConfigurationPluginElement* plugin = g_new0(ConfigurationPluginElement, 1);
    GError* error = NULL;

    const gchar **nameCursor = attributeNames;
    const gchar **valueCursor = attributeValues;

    /* check the attributes */
    while (!error && *nameCursor) {
        const gchar* name = *nameCursor;
        const gchar* value = *valueCursor;

        debug("found attribute '%s=%s'", name, value);

        if(!plugin->id.isSet && !g_ascii_strcasecmp(name, "id")) {
            plugin->id.string = g_string_new(value);
            plugin->id.isSet = TRUE;
        } else if (!plugin->path.isSet && !g_ascii_strcasecmp(name, "path")) {
            plugin->path.string = g_string_new(utility_getHomePath(value));
            plugin->path.isSet = TRUE;
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                            "unknown 'plugin' attribute '%s'", name);
        }

        nameCursor++;
        valueCursor++;
    }

    /* validate the values */
    if(!error && (!plugin->id.isSet || !plugin->path.isSet)) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "element 'plugin' requires attributes 'id' 'path'");
    }
    if(!error && plugin->path.isSet) {
        error = _parser_checkPath(&(plugin->path));
    }

    if(error) {
        /* clean up */
        _parser_freePluginElement(plugin);
    } else {
        /* no error, store the resulting config */
        parser->plugins = g_list_append(parser->plugins, plugin);

        /* make sure all references to plugins from application elements point to
         * an existing registered plugin element. */
        if(!g_hash_table_lookup(parser->pluginIDStrings, plugin->id.string->str)) {
            gchar* s = g_strdup(plugin->id.string->str);
            g_hash_table_replace(parser->pluginIDStrings, s, s);
        }
    }

    return error;
}

static GError* _parser_handleNodeAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
    ConfigurationNodeElement* node = g_new0(ConfigurationNodeElement, 1);
    GError* error = NULL;

    const gchar **nameCursor = attributeNames;
    const gchar **valueCursor = attributeValues;

    /* check the attributes */
    while (!error && *nameCursor) {
        const gchar* name = *nameCursor;
        const gchar* value = *valueCursor;

        debug("found attribute '%s=%s'", name, value);

        if(!node->id.isSet && !g_ascii_strcasecmp(name, "id")) {
            node->id.string = g_string_new(value);
            node->id.isSet = TRUE;
        } else if (!node->ipHint.isSet && !g_ascii_strcasecmp(name, "iphint")) {
            node->ipHint.string = g_string_new(value);
            node->ipHint.isSet = TRUE;
        } else if (!node->geocodeHint.isSet && !g_ascii_strcasecmp(name, "geocodehint")) {
            node->geocodeHint.string = g_string_new(value);
            node->geocodeHint.isSet = TRUE;
        } else if (!node->typeHint.isSet && !g_ascii_strcasecmp(name, "typehint")) {
            node->typeHint.string = g_string_new(value);
            node->typeHint.isSet = TRUE;
        } else if (!node->loglevel.isSet && !g_ascii_strcasecmp(name, "loglevel")) {
            node->loglevel.string = g_string_new(value);
            node->loglevel.isSet = TRUE;
        } else if (!node->heartbeatloglevel.isSet && !g_ascii_strcasecmp(name, "heartbeatloglevel")) {
            node->heartbeatloglevel.string = g_string_new(value);
            node->heartbeatloglevel.isSet = TRUE;
        } else if (!node->heartbeatloginfo.isSet && !g_ascii_strcasecmp(name, "heartbeatloginfo")) {
            node->heartbeatloginfo.string = g_string_new(value);
            node->heartbeatloginfo.isSet = TRUE;
        } else if (!node->logpcap.isSet && !g_ascii_strcasecmp(name, "logpcap")) {
            node->logpcap.string = g_string_new(value);
            node->logpcap.isSet = TRUE;
        } else if (!node->pcapdir.isSet && !g_ascii_strcasecmp(name, "pcapdir")) {
            node->pcapdir.string = g_string_new(value);
            node->pcapdir.isSet = TRUE;
        } else if (!node->quantity.isSet && !g_ascii_strcasecmp(name, "quantity")) {
            node->quantity.integer = g_ascii_strtoull(value, NULL, 10);
            node->quantity.isSet = TRUE;
        } else if (!node->bandwidthdown.isSet && !g_ascii_strcasecmp(name, "bandwidthdown")) {
            node->bandwidthdown.integer  = g_ascii_strtoull(value, NULL, 10);
            node->bandwidthdown.isSet = TRUE;
        } else if (!node->bandwidthup.isSet && !g_ascii_strcasecmp(name, "bandwidthup")) {
            node->bandwidthup.integer = g_ascii_strtoull(value, NULL, 10);
            node->bandwidthup.isSet = TRUE;
        } else if (!node->heartbeatfrequency.isSet && !g_ascii_strcasecmp(name, "heartbeatfrequency")) {
            node->heartbeatfrequency.integer = g_ascii_strtoull(value, NULL, 10);
            node->heartbeatfrequency.isSet = TRUE;
        } else if (!node->cpufrequency.isSet && !g_ascii_strcasecmp(name, "cpufrequency")) {
            node->cpufrequency.integer = g_ascii_strtoull(value, NULL, 10);
            node->cpufrequency.isSet = TRUE;
        } else if (!node->socketrecvbuffer.isSet && !g_ascii_strcasecmp(name, "socketrecvbuffer")) {
            /* socketReceiveBufferSize */
            node->socketrecvbuffer.integer = g_ascii_strtoull(value, NULL, 10);
            node->socketrecvbuffer.isSet = TRUE;
        } else if (!node->socketsendbuffer.isSet && !g_ascii_strcasecmp(name, "socketsendbuffer")) {
            /* socketSendBufferSize */
            node->socketsendbuffer.integer = g_ascii_strtoull(value, NULL, 10);
            node->socketsendbuffer.isSet = TRUE;
        } else if (!node->interfacebuffer.isSet && !g_ascii_strcasecmp(name, "interfacebuffer")) {
            /* interfaceReceiveBufferLength */
            node->interfacebuffer.integer = g_ascii_strtoull(value, NULL, 10);
            node->interfacebuffer.isSet = TRUE;
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                            "unknown 'node' attribute '%s'", name);
        }

        nameCursor++;
        valueCursor++;
    }

    /* validate the values */
    if(!error && !node->id.isSet) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "element 'node' requires attributes 'id'");
    }

    if(error) {
        /* clean up */
        _parser_freeNodeElement(node);
    } else {
        /* no error, store the config */
        parser->nodes = g_list_append(parser->nodes, node);
    }

    return error;
}

static GError* _parser_handleKillAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
    ConfigurationKillElement* kill = g_new0(ConfigurationKillElement, 1);
    GError* error = NULL;

    const gchar **nameCursor = attributeNames;
    const gchar **valueCursor = attributeValues;

    /* check the attributes */
    while (!error && *nameCursor) {
        const gchar* name = *nameCursor;
        const gchar* value = *valueCursor;

        debug("found attribute '%s=%s'", name, value);

        if (!kill->time.isSet && !g_ascii_strcasecmp(name, "time")) {
            kill->time.integer = g_ascii_strtoull(value, NULL, 10);
            kill->time.isSet = TRUE;
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                            "unknown 'kill' attribute '%s'", name);
        }

        nameCursor++;
        valueCursor++;
    }

    /* validate the values */
    if(!error && !kill->time.isSet) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "element 'kill' requires attributes 'time'");
    }

    if(error) {
        _parser_freeKillElement(kill);
    } else {
        /* no error, store the config */
        utility_assert(parser->kill == NULL);
        parser->kill = kill;
    }

    /* nothing to clean up */

    return error;
}

static GError* _parser_handleApplicationAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
    ConfigurationApplicationElement* process = g_new0(ConfigurationApplicationElement, 1);
    GError* error = NULL;

    const gchar **nameCursor = attributeNames;
    const gchar **valueCursor = attributeValues;

    /* check the attributes */
    while (!error && *nameCursor) {
        const gchar* name = *nameCursor;
        const gchar* value = *valueCursor;

        debug("found attribute '%s=%s'", name, value);

        if(!process->plugin.isSet && !g_ascii_strcasecmp(name, "plugin")) {
            process->plugin.string = g_string_new(value);
            process->plugin.isSet = TRUE;
        } else if (!process->arguments.isSet && !g_ascii_strcasecmp(name, "arguments")) {
            process->arguments.string = g_string_new(value);
            process->arguments.isSet = TRUE;
        } else if (!process->starttime.isSet && (!g_ascii_strcasecmp(name, "starttime") ||
                !g_ascii_strcasecmp(name, "time"))) { /* TODO deprecate 'time' */
            process->starttime.integer = g_ascii_strtoull(value, NULL, 10);
            process->starttime.isSet = TRUE;
        } else if (!process->stoptime.isSet && !g_ascii_strcasecmp(name, "stoptime")) {
            process->stoptime.integer = g_ascii_strtoull(value, NULL, 10);
            process->stoptime.isSet = TRUE;
        } else if(!process->preload.isSet && !g_ascii_strcasecmp(name, "preload")) {
            process->preload.string = g_string_new(value);
            process->preload.isSet = TRUE;
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                            "unknown 'application' attribute '%s'", name);
        }

        nameCursor++;
        valueCursor++;
    }

    /* validate the values */
    if(!error && (!process->plugin.isSet || !process->arguments.isSet || !process->starttime.isSet)) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "element 'application' requires attributes 'plugin' 'arguments' 'starttime'");
    }

    if(error) {
        /* clean up */
        _parser_freeApplicationElement(process);
    } else {
        /* no error, application configs get added to the most recent node */
        GList* nodeItem = g_list_last(parser->nodes);
        utility_assert(nodeItem != NULL);
        ConfigurationNodeElement* node = nodeItem->data;
        utility_assert(node != NULL);

        node->applications = g_list_append(node->applications, process);

        /* plugin was required, so we know we have one */
        if(!g_hash_table_lookup(parser->pluginIDRefStrings, process->plugin.string->str)) {
            gchar* s = g_strdup(process->plugin.string->str);
            g_hash_table_replace(parser->pluginIDRefStrings, s, s);
        }

        /* preload was optional, so only act on it if it was set */
        if(process->preload.isSet && !g_hash_table_lookup(parser->pluginIDRefStrings, process->preload.string->str)) {
            gchar* s = g_strdup(process->preload.string->str);
            g_hash_table_replace(parser->pluginIDRefStrings, s, s);
        }
    }

    return error;
}

static void _parser_handleNodeChildStartElement(GMarkupParseContext* context,
        const gchar* elementName, const gchar** attributeNames,
        const gchar** attributeValues, gpointer userData, GError** error) {
    Parser* parser = (Parser*) userData;
    MAGIC_ASSERT(parser);
    utility_assert(context && error);

    debug("found 'node' child starting element '%s'", elementName);

    /* check for cluster child-level elements */
    if (!g_ascii_strcasecmp(elementName, "application")) {
        *error = _parser_handleApplicationAttributes(parser, attributeNames, attributeValues);
    } else {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                "unknown 'node' child starting element '%s'", elementName);
    }
}

static void _parser_handleNodeChildEndElement(GMarkupParseContext* context,
        const gchar* elementName, gpointer userData, GError** error) {
    Parser* parser = (Parser*) userData;
    MAGIC_ASSERT(parser);
    utility_assert(context && error);

    debug("found 'node' child ending element '%s'", elementName);

    /* check for cluster child-level elements */
    if (!(!g_ascii_strcasecmp(elementName, "application"))) {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                "unknown 'node' child ending element '%s'", elementName);
    }
}

static GError* _parser_handleShadowAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
    ConfigurationShadowElement* shadow = g_new0(ConfigurationShadowElement, 1);
    GError* error = NULL;

    const gchar **nameCursor = attributeNames;
    const gchar **valueCursor = attributeValues;

    /* check the attributes */
    while (!error && *nameCursor) {
        const gchar* name = *nameCursor;
        const gchar* value = *valueCursor;

        debug("found attribute '%s=%s'", name, value);

        if (!shadow->environment.isSet && !g_ascii_strcasecmp(name, "environment")) {
            shadow->environment.string = g_string_new(value);
            shadow->environment.isSet = TRUE;
        } else if (!shadow->preloadPath.isSet && !g_ascii_strcasecmp(name, "preload")) {
            shadow->preloadPath.string = g_string_new(value);
            shadow->preloadPath.isSet = TRUE;
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                    "unknown 'shadow' attribute '%s'", name);
        }

        nameCursor++;
        valueCursor++;
    }

    /* validate the values */
//    if(!error && !shadow->preload.isSet) {
//        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
//                "element 'shadow' requires attributes 'preload'");
//    }

    if(!error && shadow->preloadPath.isSet) {
        error = _parser_checkPath(&(shadow->preloadPath));
    }

    if(error) {
        _parser_freeShadowElement(shadow);
    } else {
        /* no error, store the config */
        utility_assert(parser->shadow == NULL);
        parser->shadow = shadow;
    }

    /* nothing to clean up */

    return error;
}

static void _parser_handleRootStartElement(GMarkupParseContext* context,
        const gchar* elementName, const gchar** attributeNames,
        const gchar** attributeValues, gpointer userData, GError** error) {
    Parser* parser = (Parser*) userData;
    MAGIC_ASSERT(parser);
    utility_assert(context && error);

    debug("found start element '%s'", elementName);

    /* check for root-level elements */
    if (!g_ascii_strcasecmp(elementName, "plugin")) {
        *error = _parser_handlePluginAttributes(parser, attributeNames, attributeValues);
    } else if (!g_ascii_strcasecmp(elementName, "node")) {
        *error = _parser_handleNodeAttributes(parser, attributeNames, attributeValues);
        /* handle internal elements in a sub parser */
        g_markup_parse_context_push(context, &(parser->xmlNodeParser), parser);
    } else if (!g_ascii_strcasecmp(elementName, "kill")) {
        *error = _parser_handleKillAttributes(parser, attributeNames, attributeValues);
    } else if (!g_ascii_strcasecmp(elementName, "topology")) {
        *error = _parser_handleTopologyAttributes(parser, attributeNames, attributeValues);
        g_markup_parse_context_push(context, &(parser->xmlTopologyParser), parser);
    } else if (!g_ascii_strcasecmp(elementName, "shadow")) {
        *error = _parser_handleShadowAttributes(parser, attributeNames, attributeValues);
    } else {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                "unknown 'root' child starting element '%s'", elementName);
    }
}

static void _parser_handleRootEndElement(GMarkupParseContext* context,
        const gchar* elementName, gpointer userData, GError** error) {
    Parser* parser = (Parser*) userData;
    MAGIC_ASSERT(parser);
    utility_assert(context && error);

    debug("found end element '%s'", elementName);

    /* check for root-level elements */
    if (!g_ascii_strcasecmp(elementName, "node")) {
        /* validate children */
        GList* nodeItem = g_list_last(parser->nodes);
        utility_assert(nodeItem != NULL);
        ConfigurationNodeElement* node = nodeItem->data;
        utility_assert(node != NULL);
        if (g_list_length(node->applications) <= 0) {
            *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY,
                    "element 'node' requires at least 1 child 'application'");
        }
        g_markup_parse_context_pop(context);
    } else if(!g_ascii_strcasecmp(elementName, "topology")) {
        if (!_parser_hasTopology(parser)) {
            *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY,
                    "element 'topology' requires either attribute 'path' which specifies a path "
                    "to a graphml file, or internal graphml text");
        }
        g_markup_parse_context_pop(context);
    } else if(!g_ascii_strcasecmp(elementName, "shadow")) {
        if (!_parser_hasTopology(parser)) {
            *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY,
                    "element 'shadow' requires at least 1 child 'topology'");
        }
    } else {
        if(!(!g_ascii_strcasecmp(elementName, "plugin") ||
                !g_ascii_strcasecmp(elementName, "kill"))) {
            *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                            "unknown 'root' child ending element '%s'", elementName);
        }
    }
}

static gboolean _parser_verifyPluginIDsExist(Parser* parser, GError** error) {
    MAGIC_ASSERT(parser);
    utility_assert(error);

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, parser->pluginIDRefStrings);

    while(g_hash_table_iter_next(&iter, &key, &value)) {
        gchar* s = value;
        if(!g_hash_table_lookup(parser->pluginIDStrings, s)) {
            *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "plug-in id '%s' was referenced in an application element without being defined in a plugin element", s);;
            return FALSE;
        }
    }

    return TRUE;
}

static Parser* _parser_new() {
    Parser* parser = g_new0(Parser, 1);
    MAGIC_INIT(parser);

    parser->pluginIDStrings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    parser->pluginIDRefStrings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* we handle the start_element and end_element callbacks, but ignore
     * text, passthrough (comments), and errors
     * handle both hosts and topology files.
     */

    /* main root parser */
    parser->xmlRootParser.start_element = &_parser_handleRootStartElement;
    parser->xmlRootParser.end_element = &_parser_handleRootEndElement;
    parser->context = g_markup_parse_context_new(&(parser->xmlRootParser), 0, parser, NULL);

    /* sub parsers, without their own context */
    parser->xmlNodeParser.start_element = &_parser_handleNodeChildStartElement;
    parser->xmlNodeParser.end_element = &_parser_handleNodeChildEndElement;
    parser->xmlTopologyParser.text = &_parser_handleTopologyText;
    parser->xmlTopologyParser.passthrough = &_parser_handleTopologyText;

    return parser;
}

static gboolean _parser_parseContents(Parser* parser, gchar* contents, gsize length) {

    /* parse the contents, collecting actions. we store a pointer
     * to it in parser so we have access while parsing elements. */
    GError *error = NULL;
    gboolean success = g_markup_parse_context_parse(parser->context, contents, (gssize) length, &error);

    if(success) {
        success = _parser_verifyPluginIDsExist(parser, &error);
    }

    /* check for success in parsing and validating the XML */
    if(success && !error) {
        return TRUE;
    } else {
        /* some kind of error occurred, check the parser */
        utility_assert(error);
        error("g_markup_parse_context_parse: Shadow XML parsing error %i: %s",
                error->code, error->message);
        g_error_free(error);

        return FALSE;
    }
}

static void _parser_free(Parser* parser) {
    MAGIC_ASSERT(parser);

    /* cleanup */
    if(parser->topology) {
        _parser_freeTopologyElement(parser->topology);
    }
    if(parser->kill) {
        _parser_freeKillElement(parser->kill);
    }
    if(parser->nodes) {
        g_list_free_full(parser->nodes, (GDestroyNotify)_parser_freeNodeElement);
    }
    if(parser->plugins) {
        g_list_free_full(parser->plugins, (GDestroyNotify)_parser_freePluginElement);
    }
    g_hash_table_destroy(parser->pluginIDStrings);
    g_hash_table_destroy(parser->pluginIDRefStrings);
    g_markup_parse_context_free(parser->context);

    MAGIC_CLEAR(parser);
    g_free(parser);
}

Configuration* configuration_new(Options* options, const GString* file) {
    utility_assert(options && file);

    Parser* parser = _parser_new();
    gboolean success = _parser_parseContents(parser, file->str, file->len);

    if(success) {
        Configuration* config = g_new0(Configuration, 1);
        MAGIC_INIT(config);

        config->parser = parser;
        config->options = options;

        return config;
    } else {
        _parser_free(parser);
        return NULL;
    }
}

void configuration_free(Configuration* config) {
    MAGIC_ASSERT(config);

    if(config->parser) {
        _parser_free(config->parser);
    }
    /* we do not own the options object */
    config->options = NULL;

    MAGIC_CLEAR(config);
    g_free(config);
}

ConfigurationShadowElement* configuration_getShadowElement(Configuration* config) {
    MAGIC_ASSERT(config);
    utility_assert(config->parser && config->parser->shadow);
    return config->parser->shadow;
}

ConfigurationKillElement* configuration_getKillElement(Configuration* config) {
    MAGIC_ASSERT(config);
    utility_assert(config->parser && config->parser->kill);
    return config->parser->kill;
}

ConfigurationTopologyElement* configuration_getTopologyElement(Configuration* config) {
    MAGIC_ASSERT(config);
    utility_assert(config->parser && config->parser->topology);
    return config->parser->topology;
}

GList* configuration_getPluginElements(Configuration* config) {
    MAGIC_ASSERT(config);
    utility_assert(config->parser && config->parser->plugins);
    return config->parser->plugins;
}

GList* configuration_getNodeElements(Configuration* config) {
    MAGIC_ASSERT(config);
    utility_assert(config->parser && config->parser->nodes);
    return config->parser->nodes;
}
