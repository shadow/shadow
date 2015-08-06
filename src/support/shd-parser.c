/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

/*
 * each action is given a priority so they are created in the correct order.
 * that way when e.g. a node needs a link to its network, the network
 * already exists, etc.
 */

struct _Parser {
    GMarkupParseContext* context;

    GMarkupParser parser;

    GMarkupParser nodeSubParser;
    CreateNodesAction* currentNodeAction;
    gint nChildApplications;

    GMarkupParser topologySubParser;
    GString* topologyPath;
    GString* topologyText;
    gboolean foundTopology;

    GQueue* actions;

    GHashTable* pluginIDStrings;
    GHashTable* pluginIDRefStrings;
    MAGIC_DECLARE;
};

static GString* _parser_findPathToFile(const gchar* relativeFilePathSuffix, const gchar* defaultShadowPath) {
    GString* foundPath = NULL;

    if(relativeFilePathSuffix){
        /* ok, first check in current directory, then in ~/.shadow/plugins */
        gchar* currentDirStr = g_get_current_dir();
        gchar* currentPathStr = g_build_path("/", currentDirStr, relativeFilePathSuffix, NULL);
        gchar* pluginsPathStr = g_build_path("/", g_get_home_dir(), ".shadow", defaultShadowPath, relativeFilePathSuffix, NULL);

        if(g_file_test(currentPathStr, G_FILE_TEST_EXISTS) && g_file_test(currentPathStr, G_FILE_TEST_IS_REGULAR)) {
            foundPath = g_string_new(currentPathStr);
        } else if(g_file_test(pluginsPathStr, G_FILE_TEST_EXISTS) && g_file_test(pluginsPathStr, G_FILE_TEST_IS_REGULAR)) {
            foundPath = g_string_new(pluginsPathStr);
        }

        g_free(currentDirStr);
        g_free(currentPathStr);
        g_free(pluginsPathStr);
    }

    return foundPath;
}

static void _parser_addAction(Parser* parser, Action* action) {
    MAGIC_ASSERT(parser);
    g_queue_insert_sorted(parser->actions, action, action_compare, NULL);
}

static GError* _parser_handleTopologyAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
    if(parser->foundTopology) {
        return NULL;
    }

    GError* error = NULL;
    GString* path = NULL;
    GString* graph = NULL;

    const gchar **nameCursor = attributeNames;
    const gchar **valueCursor = attributeValues;

    /* check the attributes */
    while (!error && *nameCursor) {
        const gchar* name = *nameCursor;
        const gchar* value = *valueCursor;

        debug("found attribute '%s=%s'", name, value);

        if (!path && !g_ascii_strcasecmp(name, "path")) {
            path = g_string_new(utility_getHomePath(value));
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                            "unknown 'topology' attribute '%s'", name);
        }

        nameCursor++;
        valueCursor++;
    }

    /* validate the values */
    if(path) {
        /* make sure the path is absolute */
        if(!g_path_is_absolute(path->str)) {
            /* ok, first search in current directory, then in ~/.shadow/share */
            GString* foundPath = _parser_findPathToFile(path->str, "share");
            if(foundPath) {
                g_string_free(path, TRUE);
                path = foundPath;
            }
        }

        if(!g_file_test(path->str, G_FILE_TEST_EXISTS) || !g_file_test(path->str, G_FILE_TEST_IS_REGULAR)) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "attribute 'topology': '%s' is not a valid path to an existing regular file", path->str);
        }
    }

    if(!error && path) {
        parser->topologyPath = path;
    } else if(path) {
        /* clean up */
        g_string_free(path, TRUE);
    }

    return error;
}

static GError* _parser_handlePluginAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
    GString* id = NULL;
    GString* path = NULL;

    GError* error = NULL;

    const gchar **nameCursor = attributeNames;
    const gchar **valueCursor = attributeValues;

    /* check the attributes */
    while (!error && *nameCursor) {
        const gchar* name = *nameCursor;
        const gchar* value = *valueCursor;

        debug("found attribute '%s=%s'", name, value);

        if(!id && !g_ascii_strcasecmp(name, "id")) {
            id = g_string_new(value);
        } else if (!path && !g_ascii_strcasecmp(name, "path")) {
            path = g_string_new(utility_getHomePath(value));
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                            "unknown 'plugin' attribute '%s'", name);
        }

        nameCursor++;
        valueCursor++;
    }

    /* validate the values */
    if(!error && (!id || !path)) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "element 'plugin' requires attributes 'id' 'path'");
    }
    if(path) {
        /* make sure the path is absolute */
        if(!g_path_is_absolute(path->str)) {
            /* ok, first search in current directory, then in ~/.shadow/plugins */
            GString* foundPath = _parser_findPathToFile(path->str, "plugins");
            if(foundPath) {
                g_string_free(path, TRUE);
                path = foundPath;
            }
        }

        if(!g_file_test(path->str, G_FILE_TEST_EXISTS) || !g_file_test(path->str, G_FILE_TEST_IS_REGULAR)) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "attribute 'path': '%s' is not a valid path to an existing regular file", path->str);
        }
    }

    if(!error) {
        /* no error, create the action */
        Action* a = (Action*) loadplugin_new(id, path);
        action_setPriority(a, 0);
        _parser_addAction(parser, a);

        if(!g_hash_table_lookup(parser->pluginIDStrings, id->str)) {
            gchar* s = g_strdup(id->str);
            g_hash_table_replace(parser->pluginIDStrings, s, s);
        }
    }

    /* clean up */
    if(id) {
        g_string_free(id, TRUE);
    }
    if(path) {
        g_string_free(path, TRUE);
    }

    return error;
}

static GError* _parser_handleNodeAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
    GString* id = NULL;
    GString* ip = NULL;
    GString* geocode = NULL;
    GString* type = NULL;
    GString* loglevel = NULL;
    GString* heartbeatloglevel = NULL;
    GString* heartbeatloginfo = NULL;
    GString* logpcap = NULL;
    GString* pcapdir = NULL;
    guint64 bandwidthdown = 0;
    guint64 bandwidthup = 0;
    guint64 heartbeatfrequency = 0;
    guint64 cpufrequency = 0;
    guint64 socketReceiveBufferSize = 0;
    guint64 socketSendBufferSize = 0;
    gboolean autotuneReceiveBuffer = TRUE;
    gboolean autotuneSendBuffer = TRUE;
    guint64 interfaceReceiveBufferLength = 0;
    /* if there is no quantity value, default should be 1 (allows a value of 0 to be explicitly set) */
    guint64 quantity = 1;
    gboolean quantityIsSet = FALSE;

    GError* error = NULL;

    const gchar **nameCursor = attributeNames;
    const gchar **valueCursor = attributeValues;

    /* check the attributes */
    while (!error && *nameCursor) {
        const gchar* name = *nameCursor;
        const gchar* value = *valueCursor;

        debug("found attribute '%s=%s'", name, value);

        if(!id && !g_ascii_strcasecmp(name, "id")) {
            id = g_string_new(value);
        } else if (!ip && !g_ascii_strcasecmp(name, "iphint")) {
            ip = g_string_new(value);
        } else if (!geocode && !g_ascii_strcasecmp(name, "geocodehint")) {
            geocode = g_string_new(value);
        } else if (!type && !g_ascii_strcasecmp(name, "typehint")) {
            type = g_string_new(value);
        } else if (!loglevel && !g_ascii_strcasecmp(name, "loglevel")) {
            loglevel = g_string_new(value);
        } else if (!heartbeatloglevel && !g_ascii_strcasecmp(name, "heartbeatloglevel")) {
            heartbeatloglevel = g_string_new(value);
        } else if (!heartbeatloginfo && !g_ascii_strcasecmp(name, "heartbeatloginfo")) {
            heartbeatloginfo = g_string_new(value);
        } else if (!logpcap && !g_ascii_strcasecmp(name, "logpcap")) {
            logpcap = g_string_new(value);
        } else if (!pcapdir && !g_ascii_strcasecmp(name, "pcapdir")) {
            pcapdir = g_string_new(value);
        } else if (!quantityIsSet && !g_ascii_strcasecmp(name, "quantity")) {
            quantity = g_ascii_strtoull(value, NULL, 10);
            quantityIsSet = TRUE;
        } else if (!bandwidthdown && !g_ascii_strcasecmp(name, "bandwidthdown")) {
            bandwidthdown  = g_ascii_strtoull(value, NULL, 10);
        } else if (!bandwidthup && !g_ascii_strcasecmp(name, "bandwidthup")) {
            bandwidthup = g_ascii_strtoull(value, NULL, 10);
        } else if (!heartbeatfrequency && !g_ascii_strcasecmp(name, "heartbeatfrequency")) {
            heartbeatfrequency = g_ascii_strtoull(value, NULL, 10);
        } else if (!cpufrequency && !g_ascii_strcasecmp(name, "cpufrequency")) {
            cpufrequency = g_ascii_strtoull(value, NULL, 10);
        } else if (!socketReceiveBufferSize && !g_ascii_strcasecmp(name, "socketrecvbuffer")) {
            socketReceiveBufferSize = g_ascii_strtoull(value, NULL, 10);
        } else if (!socketSendBufferSize && !g_ascii_strcasecmp(name, "socketsendbuffer")) {
            socketSendBufferSize = g_ascii_strtoull(value, NULL, 10);
        } else if (!interfaceReceiveBufferLength && !g_ascii_strcasecmp(name, "interfacebuffer")) {
            interfaceReceiveBufferLength = g_ascii_strtoull(value, NULL, 10);
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                            "unknown 'node' attribute '%s'", name);
        }

        nameCursor++;
        valueCursor++;
    }

    /* validate the values */
    if(!error && !id) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "element 'node' requires attributes 'id'");
    }

    if(!error) {
        /* no error, create the action */
        Action* a = (Action*) createnodes_new(id, ip, geocode, type,
                bandwidthdown, bandwidthup, quantity, cpufrequency,
                heartbeatfrequency, heartbeatloglevel, heartbeatloginfo, loglevel, logpcap, pcapdir,
                socketReceiveBufferSize, socketSendBufferSize, interfaceReceiveBufferLength);
        action_setPriority(a, 5);
        _parser_addAction(parser, a);

        /* save the parent so child applications can reference it */
        utility_assert(!parser->currentNodeAction);
        parser->currentNodeAction = (CreateNodesAction*)a;
    }

    /* clean up */
    if(id) {
        g_string_free(id, TRUE);
    }
    if(ip) {
        g_string_free(ip, TRUE);
    }
    if(geocode) {
        g_string_free(geocode, TRUE);
    }
    if(type) {
        g_string_free(type, TRUE);
    }
    if(loglevel) {
        g_string_free(loglevel, TRUE);
    }
    if(heartbeatloglevel) {
        g_string_free(heartbeatloglevel, TRUE);
    }
    if(heartbeatloginfo) {
        g_string_free(heartbeatloginfo, TRUE);
    }
    if(logpcap) {
        g_string_free(logpcap, TRUE);
    }
    if(pcapdir) {
        g_string_free(pcapdir, TRUE);
    }

    return error;
}

static GError* _parser_handleKillAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
    guint64 time = 0;

    GError* error = NULL;

    const gchar **nameCursor = attributeNames;
    const gchar **valueCursor = attributeValues;

    /* check the attributes */
    while (!error && *nameCursor) {
        const gchar* name = *nameCursor;
        const gchar* value = *valueCursor;

        debug("found attribute '%s=%s'", name, value);

        if (!time && !g_ascii_strcasecmp(name, "time")) {
            time = g_ascii_strtoull(value, NULL, 10);
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                            "unknown 'kill' attribute '%s'", name);
        }

        nameCursor++;
        valueCursor++;
    }

    /* validate the values */
    if(!error && !time) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "element 'kill' requires attributes 'time'");
    }

    if(!error) {
        /* no error, create the action */
        Action* a = (Action*) killengine_new((SimulationTime) time);
        action_setPriority(a, 6);
        _parser_addAction(parser, a);
    }

    /* nothing to clean up */

    return error;
}

static GError* _parser_handleApplicationAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
    GString* plugin = NULL;
    GString* arguments = NULL;
    guint64 starttime = 0;
    guint64 stoptime = 0;

    GError* error = NULL;

    const gchar **nameCursor = attributeNames;
    const gchar **valueCursor = attributeValues;

    /* check the attributes */
    while (!error && *nameCursor) {
        const gchar* name = *nameCursor;
        const gchar* value = *valueCursor;

        debug("found attribute '%s=%s'", name, value);

        if(!plugin && !g_ascii_strcasecmp(name, "plugin")) {
            plugin = g_string_new(value);
        } else if (!arguments && !g_ascii_strcasecmp(name, "arguments")) {
            arguments = g_string_new(value);
        } else if (!starttime && !g_ascii_strcasecmp(name, "starttime")) {
            starttime = g_ascii_strtoull(value, NULL, 10);
        } else if (!starttime && !g_ascii_strcasecmp(name, "time")) { /* TODO deprecate 'time' */
            starttime = g_ascii_strtoull(value, NULL, 10);
        } else if (!stoptime && !g_ascii_strcasecmp(name, "stoptime")) {
            stoptime = g_ascii_strtoull(value, NULL, 10);
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                            "unknown 'application' attribute '%s'", name);
        }

        nameCursor++;
        valueCursor++;
    }

    /* validate the values */
    if(!error && (!plugin || !arguments || !starttime)) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "element 'application' requires attributes 'plugin' 'arguments' 'starttime'");
    }

    if(!error) {
        /* no error, application configs get added to the node creation event
         * in order to handle nodes with quantity > 1 */
        utility_assert(parser->currentNodeAction);
        createnodes_addApplication(parser->currentNodeAction, plugin, arguments, starttime, stoptime);

        (parser->nChildApplications)++;

        if(!g_hash_table_lookup(parser->pluginIDRefStrings, plugin->str)) {
            gchar* s = g_strdup(plugin->str);
            g_hash_table_replace(parser->pluginIDRefStrings, s, s);
        }
    }

    /* clean up */
    if(plugin) {
        g_string_free(plugin, TRUE);
    }
    if(arguments) {
        g_string_free(arguments, TRUE);
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

static void _parser_handleTopologyText(GMarkupParseContext *context,
        const gchar* text, gsize textLength, gpointer userData, GError** error) {
    Parser* parser = (Parser*) userData;

    if(parser->foundTopology) {
        return;
    }

    /* note: the text is not null-terminated! */

    if(!parser->topologyText && textLength > 0) {
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

            parser->topologyText = g_string_new_len(cdata, cdataLength);
            parser->foundTopology = TRUE;
        }

        g_string_free(textBuffer, TRUE);
    }
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
        g_markup_parse_context_push(context, &(parser->nodeSubParser), parser);
    } else if (!g_ascii_strcasecmp(elementName, "kill")) {
        *error = _parser_handleKillAttributes(parser, attributeNames, attributeValues);
    } else if (!g_ascii_strcasecmp(elementName, "topology")) {
        *error = _parser_handleTopologyAttributes(parser, attributeNames, attributeValues);
        g_markup_parse_context_push(context, &(parser->topologySubParser), parser);
    } else if (!g_ascii_strcasecmp(elementName, "shadow")) {
        /* do nothing, this is a root element */
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
        if (parser->nChildApplications <= 0) {
            *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY,
                    "element 'node' requires at least 1 child 'application'");
        }
        g_markup_parse_context_pop(context);

        /* reset child cache */
        parser->nChildApplications = 0;
        if (parser->currentNodeAction) {
            /* this is in the actions queue and will get free'd later */
            parser->currentNodeAction = NULL;
        }
    } else if(!g_ascii_strcasecmp(elementName, "topology")) {
        if (!parser->topologyPath && !parser->topologyText) {
            *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY,
                    "element 'topology' requires either attribute 'path' which specifies a path "
                    "to a graphml file, or internal graphml text");
        } else {
            parser->foundTopology = TRUE;

            Action* a = (Action*) loadtopology_new(parser->topologyPath, parser->topologyText);
            action_setPriority(a, -1);
            _parser_addAction(parser, a);

            if(parser->topologyPath) {
                g_string_free(parser->topologyPath, TRUE);
            }
            if(parser->topologyText) {
                g_string_free(parser->topologyText, TRUE);
            }
        }
        g_markup_parse_context_pop(context);
    } else if(!g_ascii_strcasecmp(elementName, "shadow")) {
        if (!parser->foundTopology) {
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

/* public interface */

Parser* parser_new() {
    Parser* parser = g_new0(Parser, 1);
    MAGIC_INIT(parser);

    parser->pluginIDStrings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    parser->pluginIDRefStrings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* we handle the start_element and end_element callbacks, but ignore
     * text, passthrough (comments), and errors
     * handle both hosts and topology files.
     */

    /* main root parser */
    parser->parser.start_element = &_parser_handleRootStartElement;
    parser->parser.end_element = &_parser_handleRootEndElement;
    parser->context = g_markup_parse_context_new(&(parser->parser), 0, parser, NULL);

    /* sub parsers, without their own context */
    parser->nodeSubParser.start_element = &_parser_handleNodeChildStartElement;
    parser->nodeSubParser.end_element = &_parser_handleNodeChildEndElement;
    parser->topologySubParser.text = &_parser_handleTopologyText;
    parser->topologySubParser.passthrough = &_parser_handleTopologyText;

    return parser;
}

gboolean parser_parseContents(Parser* parser, gchar* contents, gsize length, GQueue* actions) {

    /* parse the contents, collecting actions. we store a pointer
     * to it in parser so we have access while parsing elements. */
    parser->actions = actions;
    GError *error = NULL;
    gboolean success = g_markup_parse_context_parse(parser->context, contents, (gssize) length, &error);
    parser->actions = NULL;

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

gboolean parser_parseFile(Parser* parser, GString* filename, GQueue* actions) {
    MAGIC_ASSERT(parser);
    utility_assert(filename && actions);

    gchar* content;
    gsize length;
    GError *error = NULL;

    /* get the xml file */
    gboolean success = g_file_get_contents(filename->str, &content, &length, &error);

    /* check for success */
    if (!success) {
        error("g_file_get_contents: %s", error->message);
        g_error_free(error);
        return FALSE;
    }

    /* do the actual parsing */
    debug("attempting to parse XML file '%s'", filename->str);
    gboolean result = parser_parseContents(parser, content, length, actions);
    debug("finished parsing XML file '%s'", filename->str);

    g_free(content);

    return result;
}

void parser_free(Parser* parser) {
    MAGIC_ASSERT(parser);

    /* cleanup */
    g_hash_table_destroy(parser->pluginIDStrings);
    g_hash_table_destroy(parser->pluginIDRefStrings);
    g_markup_parse_context_free(parser->context);

    MAGIC_CLEAR(parser);
    g_free(parser);
}
