/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Command {
    /* the virtual host that this command will be executed on */
    Host* host;

    GString* id;
    SimulationTime startTime;
    GString* arguments;
    gint referenceCount;
    MAGIC_DECLARE;
};

SimulationTime command_getStartTime(Command* command) { 
    return command->startTime;
}

Command* command_new(gpointer host, gchar* id, SimulationTime startTime, gchar* arguments) {
    Command* command = g_new0(Command, 1);

    command->host = (Host*)host;
    if(command->host) {
        host_ref(command->host);
    }

    if(id && (g_ascii_strncasecmp(id, "\0", (gsize) 1) != 0)) {
        command->id = g_string_new(id);
    }
    command->startTime = startTime;
    if(arguments && (g_ascii_strncasecmp(arguments, "\0", (gsize) 1) != 0)) {
        command->arguments = g_string_new(arguments);
    }
    command->referenceCount = 1;
    MAGIC_INIT(command);

    worker_countObject(OBJECT_TYPE_COMMAND, COUNTER_TYPE_NEW);
    return command;
}

static void _command_free(Command* command) {
    if (command->id) {
        g_string_free(command->id, TRUE);
    }
    if(command->arguments) {
        g_string_free(command->arguments, TRUE);
    }

    if(command->host) {
        host_unref(command->host);
    }

    MAGIC_CLEAR(command);
    g_free(command);
    worker_countObject(OBJECT_TYPE_COMMAND, COUNTER_TYPE_FREE);
}

void command_ref(Command* command) {
    MAGIC_ASSERT(command);
    command->referenceCount++;
}

void command_unref(Command* command) {
    MAGIC_ASSERT(command);
    command->referenceCount--;
    if(command->referenceCount <= 0) {
        _command_free(command);
    }
}


void command_run(Command* command, gpointer userData) {
    message("command executed!");
    message("currentTime=%d", worker_getCurrentTime());
    if (command->id) 
        message("command id=%s", command->id->str);
    message("command startTime=%d", command->startTime);
    if (command->arguments)
        message("command arg=%s", command->arguments->str);

    if (host_isSetShadowChannel(command->host)) {
        message("shadowchannel is set!");

        gint handle = host_getShadowChannel(command->host);

        gsize bytes = 0;
        GString* shadow_cmd = g_string_new ("");
        g_string_append_printf(shadow_cmd, "%s:", command->id->str);
        if (command->arguments)
            g_string_append_printf(shadow_cmd, "%s", command->arguments->str);
        
        int cmd_length = strlen(shadow_cmd->str);
        g_string_prepend_len(shadow_cmd, (char*)&cmd_length, sizeof(int));

        /* char shadow_cmd[100]; */
        /* if (strlen(command->id->str) + strlen(command->arguments->str) < 100) */
        /*     sprintf(shadow_cmd, "%s:%s", command->id->str, command->arguments->str); */
        /* else  */
        /*     sprintf(shadow_cmd, "%s:too long args", command->id->str);             */
        message("(%s)(%d)", shadow_cmd->str, strlen(shadow_cmd->str));
        
        gint result = host_sendUserData(command->host, handle, shadow_cmd->str, sizeof(int)+cmd_length, 0, 0, &bytes);
        if (result != 0) {
            message ("sendUserData error!");
        }
        g_string_free(shadow_cmd, FALSE);

        /* Descriptor* descriptor = host_lookupDescriptor(command->host, handle); */
        /* if(descriptor == NULL) { */
        /*     warning("command:descriptor handle '%i' not found", handle); */
        /*     return; */
        /* } */

        /* DescriptorStatus status = descriptor_getStatus(descriptor); */
        /* if(status & DS_CLOSED) { */
        /*     warning("command:descriptor handle '%i' not a valid open descriptor", handle); */
        /*     return; */
        /* } */

        /* if(cpu_isBlocked(command->host->cpu)) { */
        /*     warning("command:blocked on CPU");             */
        /* } */
        /* else { */
        /*     Channel* desc = (Channel*)descriptor; */
        /*     channel_sendShadowChannel(desc, "testSend", 8); */
        /* } */
    }
}

