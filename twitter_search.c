/**
 * Copyright (C) 2009 Tan Miaoqing
 * Contact: Tan Miaoqing <rabbitrun84@gmail.com>
 */

#include "config.h"

#include <glib.h>
#include <glib/gprintf.h>

#include <string.h>
#include <debug.h>
#include <request.h>

#include "twitter_prefs.h"
#include "twitter_util.h"
#include "twitter_search.h"
#include "twitter_request.h"

static const gchar *twitter_search_create_url(PurpleAccount *account,
		const gchar *endpoint)
{
	static char url[1024];
	const gchar *host = twitter_option_search_api_host(account);
	const gchar *subdir = twitter_option_search_api_subdir(account);
	g_return_val_if_fail(host != NULL && host[0] != '\0' && endpoint != NULL && endpoint[0] != '\0', NULL);
	
	if (subdir == NULL || subdir[0] == '\0')
		 subdir = "/";

	snprintf(url, 1023, "%s%s%s%s%s", 
			host,
			subdir[0] == '/' ? "" : "/",
			subdir,
			subdir[strlen(subdir) - 1] == '/' || endpoint[0] == '/' ? "" : "/",
			subdir[strlen(subdir) - 1] == '/' && endpoint[0] == '/' ? endpoint + 1 : endpoint);
	return url;
}

typedef struct {
	PurpleAccount *account;
	TwitterSearchSuccessFunc success_func;
	TwitterSearchErrorFunc error_func;
	gpointer user_data;
} TwitterSearchContext;


static void twitter_send_search_success_cb(PurpleAccount *account, xmlnode *response_node, gpointer user_data)
{
	TwitterSearchContext *ctx = user_data;
	TwitterSearchResults *results = twitter_search_results_node_parse(response_node);

	ctx->success_func(ctx->account, results->tweets,
			results->refresh_url, results->max_id, ctx->user_data);

	twitter_search_results_free(results);
	g_slice_free (TwitterSearchContext, ctx);
}

void twitter_search(PurpleAccount *account, TwitterRequestParams *params,
		TwitterSearchSuccessFunc success_cb, TwitterSearchErrorFunc error_cb,
		gpointer data)
{
	const gchar *search_url = twitter_search_create_url(account, "/search.atom");
	TwitterSearchContext *ctx;
	if (search_url && search_url[0] != '\0')
	{
		ctx = g_slice_new0 (TwitterSearchContext);
		ctx->account = account;
		ctx->user_data = data;
		ctx->success_func = success_cb;
		ctx->error_func = error_cb;
		twitter_send_xml_request(account, FALSE,
				search_url, params,
				twitter_send_search_success_cb, NULL, //TODO error
				ctx);

	}
}
