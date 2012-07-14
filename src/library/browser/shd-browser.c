/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

#include "shd-browser.h"

static browser_download_tasks_tp browser_init_host(browser_tp b, gchar* hostname) {
	browser_download_tasks_tp tasks = g_hash_table_lookup(b->download_tasks, hostname);
	assert(b);

	/* Not initialized yet */
	if (tasks == NULL) {
		tasks = g_new0(browser_download_tasks_t, 1);
		tasks->pending = g_queue_new();
		tasks->finished = g_hash_table_new(g_str_hash, g_str_equal);
		g_hash_table_insert(b->download_tasks, hostname, tasks);
	}

	return tasks;
}

static void browser_get_embedded_objects(browser_tp b, filegetter_tp fg, gint* obj_count) {
	assert(b);
	assert(fg);
	
	GSList* objs = NULL;
 	gchar* html = g_string_free(fg->content, FALSE);
	
	/* Parse with libxml2. The result is a linked list with all relative and absolute URLs */
	html_parse(html, &objs);
	
	while (objs != NULL) {
		gchar* url = (gchar*) objs->data;
		gchar* hostname = NULL;
		gchar* path = NULL;
		
		if (url_is_absolute(url)) {
			url_get_parts(url, &hostname ,&path);
		} else {
			hostname = b->first_hostname;
			
			if (!g_str_has_prefix(url, "/")) {
				path = g_strconcat("/", url, NULL);
			} else {
				path = g_strdup(url);
			}
		}
		
		browser_download_tasks_tp tasks = browser_init_host(b, hostname);

		/* Unless the path was already downloaded ...*/
		if (!g_hash_table_lookup_extended(tasks->finished, path, NULL, NULL)) {
			b->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "%s -> %s", hostname, path);
			
			/* ... add it to the end of the queue */
			g_queue_push_tail(tasks->pending, path);
			
			/* And mark that it was downloaded */
			g_hash_table_replace(tasks->finished, path, path);
			
			(*obj_count)++;
		}
		
		objs = g_slist_next(objs);
	}
	
	g_slist_free_full(objs, NULL);
}

static in_addr_t browser_getaddr(browser_tp b, browser_server_args_tp server) {
	assert(b);
	
	gchar* hostname = server->host;
	/* check if we have an address as a string */
	struct in_addr in;
	gint is_ip_address = inet_aton(hostname, &in);

	if(is_ip_address) {
		return in.s_addr;
	} else {
		in_addr_t addr = 0;

		/* get the address in network order */
		if(g_ascii_strncasecmp(hostname, "none", 4) == 0) {
			addr = htonl(INADDR_NONE);
		} else if(g_ascii_strncasecmp(hostname, "localhost", 9) == 0) {
			addr = htonl(INADDR_LOOPBACK);
		} else {
			struct addrinfo* info;
			if(getaddrinfo((gchar*) hostname, NULL, NULL, &info) != -1) {
				addr = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
			} else {
				b->shadowlib->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "unable to create client: error in getaddrinfo");
			}
			freeaddrinfo(info);
		}

		return addr;
	}
}

static browser_connection_tp browser_prepare_filegetter(browser_tp b, browser_server_args_tp http_server, browser_server_args_tp socks_proxy, gchar* filepath) {
	assert(b);
	
	/* absolute file path to get from server */
	if(filepath[0] != '/') {
		b->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "filepath %s does not begin with '/'", filepath);
		return NULL;
	}

	/* we require http info */
	if(http_server == NULL) {
		b->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "no HTTP server specified");
		return NULL;
	}

	in_addr_t http_addr = browser_getaddr(b, http_server);
	in_port_t http_port = htons((in_port_t) atoi(http_server->port));
	
	if(http_addr == 0 || http_port == 0) {
		b->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "HTTP server specified but 0");
		return NULL;
	}

	/* there may not be a socks proxy, so NULL is ok */
	in_addr_t socks_addr = 0;
	in_port_t socks_port = 0;
	
	if(socks_proxy != NULL) {
		socks_addr = browser_getaddr(b, socks_proxy);
		socks_port = htons((in_port_t) atoi(socks_proxy->port));
	}

	/* validation successful, let's create the actual connection */
	browser_connection_tp conn = g_new0(browser_connection_t, 1);
	strncpy(conn->fspec.remote_path, filepath, sizeof(conn->fspec.remote_path));
	strncpy(conn->sspec.http_hostname, http_server->host, sizeof(conn->sspec.http_hostname));
	conn->sspec.http_addr = http_addr;
	conn->sspec.http_port = http_port;
	conn->sspec.socks_addr = socks_addr;
	conn->sspec.socks_port = socks_port;
	conn->sspec.persistent = TRUE; /* Always create persistent connections */
	
	if (b->state == SB_DOCUMENT) {
		conn->fspec.save_to_memory = TRUE;
	}
	
	/* init the filegetter */
	enum filegetter_code result = filegetter_start(&conn->fg, b->epolld);
	b->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "filegetter startup code: %s", filegetter_codetoa(result));

	/* set the sepcs */
	result = filegetter_download(&conn->fg, &conn->sspec, &conn->fspec);
	b->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "filegetter set specs code: %s", filegetter_codetoa(result));
	
	return conn;
}

static gboolean browser_reuse_connection(browser_tp b, browser_connection_tp conn) {
	browser_download_tasks_tp tasks = g_hash_table_lookup(b->download_tasks, conn->sspec.http_hostname);
	
	if (g_queue_is_empty(tasks->pending)) {
		return FALSE;
	}
	
	gchar* new_path = g_queue_pop_head(tasks->pending);
	strncpy(conn->fspec.remote_path, new_path, sizeof(conn->fspec.remote_path));
	enum filegetter_code result = filegetter_download(&conn->fg, &conn->sspec, &conn->fspec);
	b->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "Adding Path %s -> %s", conn->sspec.http_hostname, new_path);
	b->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "filegetter set specs code: %s", filegetter_codetoa(result));
	
	return TRUE;
}

static void browser_start_tasks(gpointer key, gpointer value, gpointer user_data) {
	browser_tp b = user_data;
	browser_download_tasks_tp tasks = (browser_download_tasks_tp) value;
	gchar* hostname = key;
  
	for (gint i = 0; i < b->max_concurrent_downloads && !g_queue_is_empty(tasks->pending); i++) {
		/* Get new task from the queue */
		gchar* path = g_queue_pop_head(tasks->pending);

		b->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "%s -> %s", hostname, path);
	
		/* Initialize the download tasks with the first hostname */
		browser_init_host(b, b->first_hostname);
		
		/* Create server_args for HTTP server */
		browser_server_args_tp http_server = g_new0(browser_server_args_t, 1);
		http_server->host = hostname;
		http_server->port = "80";

		/* Create a connection object and start establishing a connection */
		browser_connection_tp conn =  browser_prepare_filegetter(b, http_server, b->socks_proxy, path);
		b->connections = g_slist_prepend(b->connections, conn);
	}
}

static void browser_completed_download(browser_tp b, browser_activate_result_tp result) {
	assert(b);
	assert(result);

	if (b->state == SB_DOCUMENT) {
		gint obj_count = 0;

		/* Get embedded objects as a hashtable which associates a hostname with a linked list of paths to download */
		browser_get_embedded_objects(b, &result->connection->fg, &obj_count);

		b->shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "first document downloaded and parsed, now getting %i additional objects...", obj_count);

		/* TODO: Should actually be reused */
		filegetter_shutdown(&result->connection->fg);
		b->connections = g_slist_remove(b->connections, result->connection);

		if (!obj_count) {
			/* if website contains no embedded objectes set the state that we are done */
			b->state = SB_DONE;
		} else {
			/* Set state to downloading embedded objects */
			b->state = SB_EMBEDDED_OBJECTS;

			/* Start as many downloads as allowed by sfg->browser->max_concurrent_downloads */
			g_hash_table_foreach(b->download_tasks, browser_start_tasks, b);
		}
	} else if (b->state == SB_EMBEDDED_OBJECTS) {
		b->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "%s -> %s", result->connection->sspec.http_hostname, result->connection->fspec.remote_path);
		
		if (!browser_reuse_connection(b, result->connection)) {
			filegetter_shutdown(&result->connection->fg);
			b->connections = g_slist_remove(b->connections, result->connection);
		}
	}
}

void browser_start(browser_tp b, gint argc, gchar** argv) {
	assert(b);
	
	if (argc != 7) {
		b->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "USAGE: %s <server> <port> <socksserver/none> <port> <max concurrent download> <path>", argv[0]);
	}
	
	/* Interpret the arguments */
	browser_args_t args;

	args.http_server.host = argv[1];
	args.http_server.port = argv[2];
	args.socks_proxy.host = argv[3];
	args.socks_proxy.port = argv[4];
	args.max_concurrent_downloads = argv[5];
	args.document_path = argv[6];

	/* create an epoll so we can wait for IO events */
	b->epolld = epoll_create(1);

	if(b->epolld == -1) {
		b->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in server epoll_create");
		close(b->epolld);
		b->epolld = 0;
	}

	b->max_concurrent_downloads = atoi(args.max_concurrent_downloads);
	b->first_hostname = g_strdup(args.http_server.host);
	b->state = SB_DOCUMENT;
	b->download_tasks = g_hash_table_new(g_str_hash, g_str_equal);

	/* Initialize the download tasks with the first hostname */
	browser_init_host(b, b->first_hostname);

	/* Create a connection object and start establishing a connection */
	browser_connection_tp conn =  browser_prepare_filegetter(b, &args.http_server, &args.socks_proxy, args.document_path);

	/* Save the socks proxy address and port for later use in other server specs */
	b->socks_proxy = g_new0(browser_server_args_t, 1);
	b->socks_proxy->host = g_strdup(args.socks_proxy.host);
	b->socks_proxy->port = g_strdup(args.socks_proxy.port);

	/* Add the first connection for the document */
	b->connections = g_slist_prepend(NULL, conn);

	b->shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Trying to simulate browser access to %s on %s", args.document_path, b->first_hostname);
}

void browser_activate(browser_tp b) {
	assert(b);

	GSList* curr_task = b->connections;

	/* Activate every filegetter in each download task group */
	while (curr_task) {
		browser_activate_result_t result;
		browser_connection_tp conn = curr_task->data;
		result.code = filegetter_activate(&conn->fg);
		result.connection = conn;

		if (result.code == FG_OK_200) {
			browser_completed_download(b, &result);
		} else if (result.code == FG_ERR_404) {
			if (b->state == SB_DOCUMENT) {
				b->shadowlib->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "First document wasn't found");
				b->state = SB_DONE;
			} else {
				b->shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Error 404: %s -> %s", result.connection->sspec.http_hostname, result.connection->fspec.remote_path);
		
				/* try to reuse the connection */
				if (!browser_reuse_connection(b, result.connection)) {
					filegetter_shutdown(&result.connection->fg);
					b->connections = g_slist_remove(b->connections, result.connection);
				}
			}
		} else if (result.code == FG_ERR_FATAL || result.code == FG_ERR_SOCKSCONN || result.code != FG_ERR_WOULDBLOCK) {
			b->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "filegetter shutdown due to error '%s' for %s -> %s",
						filegetter_codetoa(result.code), result.connection->sspec.http_hostname, result.connection->fspec.remote_path);
			b->state = SB_DONE;
		}

		curr_task = g_slist_next(curr_task);
	}
	
	if (b->connections == NULL) {
		b->shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "done downloading embedded files");
	}
}