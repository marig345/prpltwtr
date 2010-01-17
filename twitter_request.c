/**
 * TODO: legal stuff
 *
 * purple
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#include <stdarg.h>
#include <string.h>
#include <time.h>

#include <glib.h>

/* If you're using this as the basis of a prpl that will be distributed
 * separately from libpurple, remove the internal.h include below and replace
 * it with code to include your own config.h or similar.  If you're going to
 * provide for translation, you'll also need to setup the gettext macros. */
#include "config.h"

#include <debug.h>
#include <request.h>
#include "twitter_prefs.h"
#include "twitter_request.h"
#include "twitter_util.h"

#define USER_AGENT "Mozilla/4.0 (compatible; MSIE 5.5)"

//TODO: clean this up to be a bit more robust. 

typedef struct {
	PurpleAccount *account;
	TwitterSendRequestSuccessFunc success_func;
	TwitterSendRequestErrorFunc error_func;
	gpointer user_data;
} TwitterSendRequestData;

typedef struct
{
	GList *nodes;
	TwitterSendRequestMultiPageAllSuccessFunc success_callback;
	TwitterSendRequestMultiPageAllErrorFunc error_callback;
	gint max_count;
	gint current_count;
	gpointer user_data;
} TwitterMultiPageAllRequestData;

typedef struct {
	GList *nodes;
	long long next_cursor;
	gchar *url;
	gchar *query_string;

	TwitterSendRequestMultiPageAllSuccessFunc success_callback;
	TwitterSendRequestMultiPageAllErrorFunc error_callback;
	gpointer user_data;
} TwitterRequestWithCursorData;

void twitter_send_request_multipage_do(PurpleAccount *account,
		TwitterMultiPageRequestData *request_data);

static void twitter_send_request_with_cursor_cb (PurpleAccount *account,
		xmlnode *node,
		gpointer user_data);

void twitter_send_request_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data,
		const gchar *url_text, gsize len,
		const gchar *server_error_message)
{
	TwitterSendRequestData *request_data = user_data;
	const gchar *error_message = NULL;
	gchar *error_node_text = NULL;
	xmlnode *response_node = NULL;
	TwitterRequestErrorType error_type = TWITTER_REQUEST_ERROR_NONE;

	if (server_error_message)
	{
		purple_debug_info(TWITTER_PROTOCOL_ID, "Response error: %s\n", server_error_message);
		error_type = TWITTER_REQUEST_ERROR_SERVER;
		error_message = server_error_message;
	} else {
		response_node = xmlnode_from_str(url_text, len);
		if (!response_node)
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "Response error: invalid xml\n");
			error_type = TWITTER_REQUEST_ERROR_INVALID_XML;
			error_message = url_text;
		} else {
			xmlnode *error_node;
			if ((error_node = xmlnode_get_child(response_node, "error")) != NULL)
			{
				error_type = TWITTER_REQUEST_ERROR_TWITTER_GENERAL;
				error_node_text = xmlnode_get_data(error_node);
				error_message = error_node_text;
				purple_debug_info(TWITTER_PROTOCOL_ID, "Response error: Twitter error %s\n", error_message);
			}
		}
	}

	if (error_type != TWITTER_REQUEST_ERROR_NONE)
	{
		TwitterRequestErrorData *error_data = g_new0(TwitterRequestErrorData, 1);
		error_data->type = error_type;
		error_data->message = error_message;
		//error_data->response_node = response_node;
		if (request_data->error_func)
			request_data->error_func(request_data->account, error_data, request_data->user_data);

		g_free(error_data);
	} else {
		purple_debug_info(TWITTER_PROTOCOL_ID, "Valid response, calling success func\n");
		if (request_data->success_func)
			request_data->success_func(request_data->account, response_node, request_data->user_data);
	}

	if (response_node != NULL)
		xmlnode_free(response_node);
	if (error_node_text != NULL)
		g_free(error_node_text);
	g_free(request_data);
}

//TODO: this should really just take in an object of options. Getting unwieldy
void twitter_send_request(PurpleAccount *account, gboolean post,
		const char *url, const char *query_string,
		TwitterSendRequestSuccessFunc success_callback, TwitterSendRequestErrorFunc error_callback,
		gpointer data)
{
	gchar *request;
	const char *pass = purple_connection_get_password(purple_account_get_connection(account));
	const char *sn = purple_account_get_username(account);
	char *auth_text = g_strdup_printf("%s:%s", sn, pass);
	char *auth_text_b64 = purple_base64_encode((guchar *) auth_text, strlen(auth_text));
	gboolean use_https = twitter_option_use_https(account) && purple_ssl_is_supported();
	char *slash = strchr(url, '/');
	TwitterSendRequestData *request_data = g_new0(TwitterSendRequestData, 1);
	char *host = slash ? g_strndup(url, slash - url) : g_strdup(url);
	char *full_url = g_strdup_printf("%s://%s",
			use_https ? "https" : "http",
			url);
	purple_debug_info(TWITTER_PROTOCOL_ID, "Sending request to: %s ? %s\n",
			full_url,
			query_string ? query_string : "");

	request_data->account = account;
	request_data->user_data = data;
	request_data->success_func = success_callback;
	request_data->error_func = error_callback;

	g_free(auth_text);

	request = g_strdup_printf(
			"%s %s%s%s HTTP/1.1\r\n"
			"User-Agent: " USER_AGENT "\r\n"
			"Host: %s\r\n"
			"Authorization: Basic %s\r\n"
			"%s"
			"Content-Length: %lu\r\n\r\n"
			"%s",
			post ? "POST" : "GET",
			full_url,
			(!post && query_string ? "?" : ""), (!post && query_string ? query_string : ""),
			host,
			auth_text_b64,
			post ? "Content-Type: application/x-www-form-urlencoded\r\n" : "",
			query_string && post ? strlen(query_string) : 0,
			query_string && post ? query_string : "");

	g_free(auth_text_b64);
	purple_util_fetch_url_request(full_url, TRUE,
			USER_AGENT, TRUE, request, FALSE,
			twitter_send_request_cb, request_data);
	g_free(full_url);
	g_free(request);
	g_free(host);
}

static int xmlnode_child_count(xmlnode *parent)
{
	/*xmlnode *child = parent->child;
	int count = 0;

	if (!child)
		return 0;

	while ((child = child->next) != NULL) {
		if (child->name)
			count++;
	}*/
	int count = 0;
	xmlnode *child;
	if (parent == NULL)
		return 0;
	for (child = parent->child; child; child = child->next)
		if (child->name && child->type == XMLNODE_TYPE_TAG)
			count++;
	return count;
}

void twitter_send_request_multipage_cb(PurpleAccount *account, xmlnode *node, gpointer user_data)
{
	TwitterMultiPageRequestData *request_data = user_data;
	xmlnode *child = node->child;
	int count = 0;
	gboolean get_next_page;
	gboolean last_page = FALSE;

	while ((child = child->next) != NULL) {
		if (child->name)
			count++;
	}

	if (count < request_data->expected_count)
		last_page = TRUE;

	purple_debug_info(TWITTER_PROTOCOL_ID,
			"%s: last_page: %s, count: %d, expected_count: %d\n",
			G_STRFUNC, last_page?"yes":"no",
			count, request_data->expected_count);

	if (!request_data->success_callback) {
		get_next_page = TRUE;

		purple_debug_info(TWITTER_PROTOCOL_ID,
				"%s no request_data->success_callback, get_next_page: yes\n",
				G_STRFUNC);
	}
	else {
		get_next_page = request_data->success_callback(account,
				node,
				last_page,
				request_data,
				request_data->user_data);

		purple_debug_info(TWITTER_PROTOCOL_ID,
				"%s get_next_page: %s\n",
				G_STRFUNC, get_next_page?"yes":"no");
	}

	if (last_page || !get_next_page)
	{
		g_free(request_data->url);
		if (request_data->query_string)
			g_free(request_data->query_string);
	} else {
		request_data->page++;
		twitter_send_request_multipage_do(account, request_data);
	}
}
void twitter_send_request_multipage_error_cb(PurpleAccount *account, const TwitterRequestErrorData *error_data, gpointer user_data)
{
	TwitterMultiPageRequestData *request_data = user_data;
	gboolean try_again;

	if (!request_data->error_callback)
		try_again = FALSE;
	else
		try_again = request_data->error_callback(account, error_data, request_data->user_data);

	if (try_again)
		twitter_send_request_multipage_do(account, request_data);
}

void twitter_send_request_multipage_do(PurpleAccount *account,
		TwitterMultiPageRequestData *request_data)
{
	char *full_query_string = g_strdup_printf("%s%spage=%d&count=%d",
			request_data->query_string ? request_data->query_string : "",
			request_data->query_string && strlen(request_data->query_string) > 0 ? "&" : "",
			request_data->page,
			request_data->expected_count);

	purple_debug_info(TWITTER_PROTOCOL_ID, "%s: page: %d\n", G_STRFUNC, request_data->page);

	twitter_send_request(account, FALSE,
			request_data->url, full_query_string,
			twitter_send_request_multipage_cb, twitter_send_request_multipage_error_cb,
			request_data);
	g_free(full_query_string);
}

void twitter_send_request_multipage(PurpleAccount *account,
		const char *url, const char *query_string,
		TwitterSendRequestMultiPageSuccessFunc success_callback,
		TwitterSendRequestMultiPageErrorFunc error_callback,
		int expected_count, gpointer data)
{
	TwitterMultiPageRequestData *request_data = g_new0(TwitterMultiPageRequestData, 1);
	request_data->user_data = data;
	request_data->url = g_strdup(url);
	request_data->query_string = query_string ? g_strdup(query_string) : NULL;
	request_data->success_callback = success_callback;
	request_data->error_callback = error_callback;
	request_data->page = 1;
	request_data->expected_count = expected_count;

	twitter_send_request_multipage_do(account, request_data);
}

static void twitter_multipage_all_request_data_free(TwitterMultiPageAllRequestData *request_data_all)
{
	GList *l = request_data_all->nodes;
	for (l = request_data_all->nodes; l; l = l->next)
	{
		xmlnode_free(l->data);
	}
	g_list_free(request_data_all->nodes);
	g_free(request_data_all);
}

static gboolean twitter_send_request_multipage_all_success_cb(PurpleAccount *acct,
		xmlnode *node,
		gboolean last_page,
		TwitterMultiPageRequestData *request_multi,
		gpointer user_data)
{
	TwitterMultiPageAllRequestData *request_data_all = user_data;

	purple_debug_info (TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);

	request_data_all->nodes = g_list_prepend(request_data_all->nodes, xmlnode_copy(node)); //TODO: update
	request_data_all->current_count += xmlnode_child_count(node);

	purple_debug_info (TWITTER_PROTOCOL_ID, "%s last_page: %d current_count: %d max_count: %d per page: %d\n", G_STRFUNC, last_page ? 1 : 0, request_data_all->current_count, request_data_all->max_count, request_multi->expected_count);
	if (last_page || (request_data_all->max_count > 0 && request_data_all->current_count >= request_data_all->max_count))
	{
		request_data_all->success_callback(acct, request_data_all->nodes, request_data_all->user_data);
		twitter_multipage_all_request_data_free(request_data_all);
		return FALSE;
	} else if (request_data_all->max_count > 0 && (request_data_all->current_count + request_multi->expected_count > request_data_all->max_count)) {
		request_multi->expected_count = request_data_all->max_count - request_data_all->current_count;
	}
	return TRUE;
}

static gboolean twitter_send_request_multipage_all_error_cb(PurpleAccount *acct, const TwitterRequestErrorData *error_data, gpointer user_data)
{
	TwitterMultiPageAllRequestData *request_data_all = user_data;
	if (request_data_all->error_callback && request_data_all->error_callback(acct, error_data, request_data_all->user_data))
		return TRUE;
	twitter_multipage_all_request_data_free(request_data_all);
	return FALSE;
}

void twitter_send_request_multipage_all_max_count(PurpleAccount *account,
		const char *url, const char *query_string,
		TwitterSendRequestMultiPageAllSuccessFunc success_callback,
		TwitterSendRequestMultiPageAllErrorFunc error_callback,
		int expected_count, gint max_count, gpointer data)
{
	TwitterMultiPageAllRequestData *request_data_all = g_new0(TwitterMultiPageAllRequestData, 1);
	request_data_all->success_callback = success_callback;
	request_data_all->error_callback = error_callback;
	request_data_all->nodes = NULL;
	request_data_all->user_data = data;
	request_data_all->max_count = max_count;

	if (max_count > 0 && expected_count > max_count)
		expected_count = max_count;

	twitter_send_request_multipage(account,
			url, query_string,
			twitter_send_request_multipage_all_success_cb,
			twitter_send_request_multipage_all_error_cb,
			expected_count, request_data_all);
}

/******************************************************
 *  Request with cursor
 ******************************************************/

static void twitter_request_with_cursor_data_free (
		TwitterRequestWithCursorData *request_data)
{
	GList *l;

	for (l = request_data->nodes; l; l = l->next)
		xmlnode_free (l->data);
	g_list_free (request_data->nodes);
	g_free (request_data->url);
	g_free (request_data->query_string);
	g_slice_free (TwitterRequestWithCursorData, request_data);
}

static void twitter_send_request_with_cursor_cb (PurpleAccount *account,
		xmlnode *node,
		gpointer user_data)
{
	TwitterRequestWithCursorData *request_data = user_data;
	xmlnode *users;
	gchar *next_cursor_str;

	next_cursor_str = xmlnode_get_child_data (node, "next_cursor");
	if (next_cursor_str)
	{
		request_data->next_cursor = strtoll (next_cursor_str, NULL, 10);
		g_free (next_cursor_str);
	} else {
		request_data->next_cursor = 0;
	}

	purple_debug_info (TWITTER_PROTOCOL_ID, "%s next_cursor: %lld\n",
			G_STRFUNC, request_data->next_cursor);

	users = xmlnode_get_child (node, "users");
	if (!users && node->name && !strcmp(node->name, "users"))
		users = node;

	if (users) 
	{
		request_data->nodes = g_list_prepend (request_data->nodes,
				xmlnode_copy (users));
	}

	if (request_data->next_cursor) {
		gchar *full_query_string;

		if (request_data->query_string)
			full_query_string = g_strdup_printf ("%s&cursor=%lld",
					request_data->query_string,
					request_data->next_cursor);
		else
			full_query_string = g_strdup_printf ("cursor=%lld",
					request_data->next_cursor);

		twitter_send_request (account, FALSE,
				request_data->url, full_query_string,
				twitter_send_request_with_cursor_cb,
				NULL,
				request_data);

		g_free (full_query_string);
	}
	else {
		request_data->success_callback (account,
				request_data->nodes,
				request_data->user_data);
		twitter_request_with_cursor_data_free (request_data);
	}
}

void twitter_send_request_with_cursor (PurpleAccount *account,
		const char *url, const char *query_string, long long cursor,
		TwitterSendRequestMultiPageAllSuccessFunc success_callback,
		TwitterSendRequestMultiPageAllErrorFunc error_callback,
		gpointer data)
{
	gchar *full_query_string;

	if (query_string)
		full_query_string = g_strdup_printf ("%s&cursor=%lld",
				query_string, cursor);
	else
		full_query_string = g_strdup_printf ("cursor=%lld", cursor);

	TwitterRequestWithCursorData *request_data = g_slice_new0 (TwitterRequestWithCursorData);
	request_data->url = g_strdup (url);
	request_data->query_string = g_strdup (query_string);
	request_data->success_callback = success_callback;
	request_data->error_callback = error_callback;
	request_data->user_data = data;

	twitter_send_request (account, FALSE,
			url, full_query_string,
			twitter_send_request_with_cursor_cb,
			NULL,
			request_data);

	g_free (full_query_string);
}
