#include "shd-browser.h"

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
Browser browserstate;

/* function table for Shadow so it knows how to call us */
PluginFunctionTable browser_pluginFunctions = {
	&browserplugin_new, &browserplugin_free, &browserplugin_ready,
};

void __shadow_plugin_init__(ShadowlibFunctionTable* shadowlibFuncs) {
	g_assert(shadowlibFuncs);
	shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "browser-plugin init: nothing implemented yet!");
  
	/* start out with cleared state */
	browserstate = *g_new0(Browser, 1);

	/* save the functions Shadow makes available to us */
	browserstate.shadowlibFuncs = *shadowlibFuncs;

	/*
	 * tell shadow which of our functions it can use to notify our plugin,
	 * and allow it to track our state for each instance of this plugin
	 *
	 * we 'register' our function table, and 1 variable.
	 */
	gboolean success = browserstate.shadowlibFuncs.registerPlugin(&browser_pluginFunctions, 1, sizeof(Browser), &browserstate);

	/* we log through Shadow by using the log function it supplied to us */
	if(success) {
		browserstate.shadowlibFuncs.log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "successfully registered browser plug-in state");
	} else {
		browserstate.shadowlibFuncs.log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "error registering browser plug-in state");
	}
}

static int timer_cb(CURLM *multi, long timeout_ms, void *g) {
	browserstate.shadowlibFuncs.log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "timer_cb called");
	return 0;
}

gint socket_cb(CURL *easy, curl_socket_t s, int action, void *userp, void *socketp) {
	browserstate.shadowlibFuncs.log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "socket_cb called");
  
	if (!socketp) {
		struct epoll_event ev;
		ev.events = EPOLLIN | EPOLLOUT;
		ev.data.fd = s;

		gint result = epoll_ctl(browserstate.epolld, EPOLL_CTL_ADD, s, &ev);
		socketp = malloc(sizeof(int));
		curl_multi_assign(browserstate.multiHandle, s, socketp);
    
		if(result == -1) {
			browserstate.shadowlibFuncs.log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in epoll_ctl");
			exit(1);
		}
	}
  
	return 0;
}

int debug_cb(CURL* handle, curl_infotype it, char* data, size_t length, void* userdata) {
	/* Turn into zero-terminated string without trailing line break */
	gchar* msg = g_strndup(data, length - 1);
	browserstate.shadowlibFuncs.log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "CURL Debug: %s", msg);
}

void browserplugin_new(gint argc, gchar** argv) {
	browserstate.shadowlibFuncs.log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "browserplugin_new called");
	
	/* init a multi stack */
	browserstate.multiHandle = curl_multi_init();
	
	/* Init an easy handle */
	CURL* handle = curl_easy_init();
  
	/* Set options */
	curl_easy_setopt(handle, CURLOPT_URL, argv[1]);
	curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L); /* Follow redirects */
	curl_easy_setopt(handle, CURLOPT_USERAGENT, "Mozilla/5.0"); /* Fake useragent header */
	curl_easy_setopt(handle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4); /* Resolve only to IPv4, because Shadow doesn't support IPv6 */
	curl_easy_setopt(handle, CURLOPT_VERBOSE, 1);
	curl_easy_setopt(handle, CURLOPT_DEBUGFUNCTION, debug_cb);

// 	 struct curl_slist *slist=NULL;
//         slist = curl_slist_append(slist, "server.node:80:7.0.0.0");
// 	curl_easy_setopt(handle, CURLOPT_RESOLVE, slist);
	
	/* Get an Epoll handle */
	browserstate.epolld = epoll_create(1);
	
	if(browserstate.epolld == -1) {
		browserstate.shadowlibFuncs.log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
		curl_multi_cleanup(browserstate.multiHandle);
		curl_easy_cleanup(handle);
		return NULL;
	}
  
	gint running_handles;
  
	curl_multi_setopt(browserstate.multiHandle, CURLMOPT_SOCKETFUNCTION, socket_cb);
	curl_multi_setopt(browserstate.multiHandle, CURLMOPT_TIMERFUNCTION, timer_cb);
	curl_multi_add_handle(browserstate.multiHandle, handle);
	
	/* Kickstart libcurl */
	curl_multi_socket_action(browserstate.multiHandle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
}

void browserplugin_free() {
	browserstate.shadowlibFuncs.log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "browserplugin_free called");
}

void browserplugin_ready() {
	browserstate.shadowlibFuncs.log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "browserplugin_ready called");
}