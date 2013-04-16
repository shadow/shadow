/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#include "shd-browser.h"

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
			gint result;
			if(!(result = getaddrinfo((gchar*) hostname, NULL, NULL, &info))) {
				addr = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
			} else {
				b->shadowlib->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "unable to resolve hostname '%s': getaddrinfo returned %d", hostname, result);
			}
			freeaddrinfo(info);
		}

		return addr;
	}
}

static browser_download_tasks_tp browser_init_host(browser_tp b, gchar* hostname) {
	browser_download_tasks_tp tasks = g_hash_table_lookup(b->download_tasks, hostname);
	assert(b);

	/* Not initialized yet */
	if (tasks == NULL) {
		tasks = g_new0(browser_download_tasks_t, 1);
		tasks->pending = g_queue_new();
		tasks->running = 0;
		tasks->added = g_hash_table_new(g_str_hash, g_str_equal);
		
		browser_server_args_t server;
		server.host = hostname;
		server.port = "80";
		tasks->reachable = browser_getaddr(b, &server) != 0;
		
		g_hash_table_insert(b->download_tasks, hostname, tasks);
	}

	return tasks;
}

static void browser_destroy_added_tasks(gpointer key, gpointer value, gpointer user_data) {
	browser_download_tasks_tp tasks = value;
	g_hash_table_destroy(tasks->added);
}

static void browser_get_embedded_objects(browser_tp b, filegetter_tp fg, gint* obj_count) {
	assert(b);
	assert(fg);
	
	GSList* objs = NULL;
 	gchar* html = g_string_free(fg->content, FALSE);
	
	/* Parse with libtidy. The result is a linked list with all relative and absolute URLs */
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

		/* Unless the path was already added...*/
		if (tasks->reachable && !g_hash_table_lookup_extended(tasks->added, path, NULL, NULL)) {
			b->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "%s -> %s", hostname, path);
			
			/* ... add it to the end of the queue */
			g_queue_push_tail(tasks->pending, path);
			
			/* And mark that it was added */
			g_hash_table_replace(tasks->added, path, path);
			
			(*obj_count)++;
		}
		
		objs = g_slist_next(objs);
	}
	
	g_hash_table_foreach(b->download_tasks, browser_destroy_added_tasks, NULL);
	g_slist_free_full(objs, NULL);
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
	conn->b = b;
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
	g_free(new_path);
	
	return TRUE;
}

static void browser_start_tasks(gpointer key, gpointer value, gpointer user_data) {
	browser_tp b = user_data;
	browser_download_tasks_tp tasks = (browser_download_tasks_tp) value;
	gchar* hostname = key;
  
	while (tasks->running < b->max_concurrent_downloads && !g_queue_is_empty(tasks->pending)) {
		/* Get new task from the queue */
		gchar* path = g_queue_pop_head(tasks->pending);

		b->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "%s -> %s", hostname, path);
		
		/* Create server_args for HTTP server */
		browser_server_args_tp http_server = g_new0(browser_server_args_t, 1);
		http_server->host = hostname;
		http_server->port = "80";

		/* Create a connection object and start establishing a connection */
		browser_connection_tp conn =  browser_prepare_filegetter(b, http_server, b->socks_proxy, path);
		g_hash_table_insert(b->connections, &conn->fg.sockd, conn);
		tasks->running++;
		g_free(path);
	}
}

static void browser_downloaded_document(browser_tp b, browser_activate_result_tp result) {
	assert(b);
	assert(result);

	gint obj_count = 0;

	/* Get embedded objects as a hashtable which associates a hostname with a linked list of paths to download */
	browser_get_embedded_objects(b, &result->connection->fg, &obj_count);

	/* Get statistics for document download */
	filegetter_filestats_t doc_stats;
	filegetter_stat_download(&result->connection->fg, &doc_stats);
	b->document_size = doc_stats.body_bytes_downloaded;
	
	b->shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
		"first document (%zu bytes) downloaded and parsed in %lu.%.3d seconds, now getting %i additional objects...",
		b->document_size,
		doc_stats.download_time.tv_sec,
		(gint)(doc_stats.download_time.tv_nsec / 1000000),
		obj_count);

	/* Try to reuse initial connection */
	if (!browser_reuse_connection(b, result->connection)) {
		g_hash_table_steal(b->connections, &result->connection->fg.sockd);
	} else {
		browser_download_tasks_tp tasks = g_hash_table_lookup(b->download_tasks, b->first_hostname);
		tasks->running = 1;
	}

	if (!obj_count) {
		/* if website contains no embedded objectes set the state that we are done */
		b->state = SB_SUCCESS;
	} else {
		/* Set state to downloading embedded objects */
		b->state = SB_EMBEDDED_OBJECTS;
		
		/* set counters/timers for embedded downloads */
		clock_gettime(CLOCK_REALTIME, &b->embedded_start_time);
		b->embedded_downloads_expected = obj_count;
		b->embedded_downloads_completed = 0;

		/* Start as many downloads as allowed by sfg->browser->max_concurrent_downloads */
		g_hash_table_foreach(b->download_tasks, browser_start_tasks, b);
	}
}

static void browser_downloaded_object(browser_tp b, browser_activate_result_tp result) {
	assert(b);
	assert(result);

	b->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "%s -> %s", result->connection->sspec.http_hostname, result->connection->fspec.remote_path);
	b->embedded_downloads_completed++;
	
	if (!browser_reuse_connection(b, result->connection)) {
		g_hash_table_remove(b->connections, &result->connection->fg.sockd);
	}
}


static void browser_shutdown_connection(gpointer value) {
	browser_connection_tp conn = value;
	/* Get statistics for download */
	filegetter_filestats_t fg_stats;
	filegetter_stat_aggregate(&conn->fg, &fg_stats);
	conn->b->bytes_downloaded += fg_stats.bytes_downloaded;
	conn->b->bytes_uploaded += fg_stats.bytes_uploaded;
	conn->b->cumulative_size += fg_stats.body_bytes_downloaded;
	filegetter_shutdown(&conn->fg);
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
	gint epolld = epoll_create(1);

	if(epolld == -1) {
		b->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in server epoll_create");
		close(epolld);
		epolld = 0;
	}
	
	browser_launch(b, &args, epolld);
}

gint browser_launch(browser_tp b, browser_args_tp args, gint epolld) {
	b->epolld = epolld;
	b->max_concurrent_downloads = atoi(args->max_concurrent_downloads);
	b->first_hostname = g_strdup(args->http_server.host);
	b->state = SB_DOCUMENT;
	b->download_tasks = g_hash_table_new(g_str_hash, g_str_equal);
	
	/* Stat counters */
	b->bytes_downloaded = 0;
	b->bytes_uploaded = 0;
	b->cumulative_size = 0;

	/* Initialize the download tasks with the first hostname */
	browser_init_host(b, b->first_hostname);

	/* Create a connection object and start establishing a connection */
	browser_connection_tp conn =  browser_prepare_filegetter(b, &args->http_server, &args->socks_proxy, args->document_path);

	/* Save the socks proxy address and port for later use in other server specs */
	b->socks_proxy = g_new0(browser_server_args_t, 1);
	b->socks_proxy->host = g_strdup(args->socks_proxy.host);
	b->socks_proxy->port = g_strdup(args->socks_proxy.port);

	/* Add the first connection for the document */
	b->connections = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, browser_shutdown_connection);
	g_hash_table_insert(b->connections, &conn->fg.sockd, conn);
	b->doc_conn = conn;

	b->shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Trying to simulate browser access to %s on %s", args->document_path, b->first_hostname);
	return conn->fg.sockd;
}

static void browser_wakeup(gpointer data) {
	browser_tp b = data;
	b->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "Rise and shine!");
	filegetter_start(&b->doc_conn->fg, b->epolld);
	filegetter_download(&b->doc_conn->fg, &b->doc_conn->sspec, &b->doc_conn->fspec);
	g_hash_table_insert(b->connections, &b->doc_conn->fg.sockd, b->doc_conn);
	b->state = SB_DOCUMENT;
}

void browser_activate(browser_tp b, gint sockfd) {
	assert(b);
	
	browser_activate_result_t result;
	browser_connection_tp conn = g_hash_table_lookup(b->connections, &sockfd);

	if (!conn) {
		b->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "unknown socket");
		return;
	}
	
	result.code = filegetter_activate(&conn->fg);
	result.connection = conn;
	
	switch (b->state) {
		case SB_DOCUMENT:
			if (result.code == FG_OK_200) {
				browser_downloaded_document(b, &result);
			} else if (result.code == FG_ERR_404) {
				b->shadowlib->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "First document wasn't found");
				b->state = SB_404;
			} else if (result.code == FG_ERR_FATAL || result.code == FG_ERR_SOCKSCONN) {
				/* Retry connection in 60 seconds because the Tor network might not be functional yet */
				b->shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "filegetter shutdown due to error '%s'... retrying in 60 seconds", filegetter_codetoa(result.code));
				g_hash_table_steal(b->connections, &conn->fg.sockd);
				filegetter_shutdown(&conn->fg);		
				b->state = SB_HIBERNATE;
				b->shadowlib->createCallback(&browser_wakeup, b, 60*1000);
			} else if (result.code != FG_ERR_WOULDBLOCK) {
				b->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "filegetter shutdown due to error '%s' for first document", filegetter_codetoa(result.code));
				g_hash_table_steal(b->connections, &sockfd);
				filegetter_shutdown(&conn->fg);
				b->state = SB_FAILURE;
				browser_free(b);
			}
			
			break;
			
		case SB_EMBEDDED_OBJECTS:
			if (result.code == FG_OK_200) {
				browser_downloaded_object(b, &result);
			} else if (result.code == FG_ERR_404) {
				b->shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Error 404: %s -> %s", result.connection->sspec.http_hostname, result.connection->fspec.remote_path);
	
				/* try to reuse the connection */
				if (!browser_reuse_connection(b, result.connection)) {
					g_hash_table_remove(b->connections, &sockfd);
				}
			} else if (result.code == FG_ERR_FATAL || result.code == FG_ERR_SOCKSCONN || result.code != FG_ERR_WOULDBLOCK) {
				b->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "filegetter shutdown due to error '%s' for %s -> %s",
					filegetter_codetoa(result.code), result.connection->sspec.http_hostname, result.connection->fspec.remote_path);
				g_hash_table_steal(b->connections, &sockfd);
				filegetter_shutdown(&conn->fg);
			}
			
			/* If there is no connection left, we are done */
			if (!g_hash_table_size(b->connections)) {
				b->state = SB_SUCCESS;
				clock_gettime(CLOCK_REALTIME, &b->embedded_end_time);
			}
			
			break;
			
		default:
			b->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Activate was called but state is neither SB_DOCUMENT nor SB_EMBEDDED_OBJECTS!");
			break;
	}
}

void browser_free(browser_tp b) {
	/* Clean up */
	g_hash_table_destroy(b->connections);
	
	/* report stats */
	if (b->state == SB_SUCCESS) {
		struct timespec duration_embedded_downloads;
		/* first byte statistics */
		duration_embedded_downloads.tv_sec = b->embedded_end_time.tv_sec - b->embedded_start_time.tv_sec;
		duration_embedded_downloads.tv_nsec = b->embedded_end_time.tv_nsec - b->embedded_start_time.tv_nsec;
		
		while(duration_embedded_downloads.tv_nsec < 0) {
			duration_embedded_downloads.tv_sec--;
			duration_embedded_downloads.tv_nsec += 1000000000;
		}
		
		b->shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
			"Finished downloading %d/%d embedded objects (%zu bytes) in %lu.%.3d seconds, %d total bytes sent, %d total bytes received",
			b->embedded_downloads_completed,
			b->embedded_downloads_expected,
			b->cumulative_size - b->document_size,
			duration_embedded_downloads.tv_sec,
			(gint)(duration_embedded_downloads.tv_nsec / 1000000),
			b->bytes_uploaded,
 			b->bytes_downloaded);
	}
}
