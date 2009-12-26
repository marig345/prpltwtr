/**
 * Copyright (C) 2009 Tan Miaoqing
 * Contact: Tan Miaoqing <rabbitrun84@gmail.com>
 */

#include "config.h"

#include <glib.h>
#include <json-glib/json-glib.h>
#include <json-glib/json-gobject.h>

#include <debug.h>
#include <request.h>

#include "twitter_search.h"

typedef struct {
    PurpleAccount *account;
    TwitterSearchSuccessFunc success_func;
    TwitterSearchErrorFunc error_func;
    gpointer user_data;
} TwitterSearchContext;

static void _free_search_results (GArray *search_results)
{
    guint i, len;

    if (!search_results)
        return ;

    len = search_results->len;

    for (i = 0; i < len; i++) {
        TwitterSearchData *search_data;

        search_data = g_array_index (search_results,
                TwitterSearchData *, i);
        g_free (search_data->from_user);
        g_free (search_data->text);
        g_slice_free (TwitterSearchData, search_data);
    }
    g_array_free (search_results, TRUE);
}

static void _get_search_result (JsonArray *array,
        guint index, JsonNode *node, gpointer user_data)
{
    GArray *search_results = (GArray *)user_data;
    TwitterSearchData *search_data;
    JsonObject *result_obj;

    result_obj = json_node_get_object (node);

    search_data = g_slice_new0 (TwitterSearchData);
    search_data->text = g_strdup (
            json_object_get_string_member (result_obj, "text"));
    search_data->from_user = g_strdup (
            json_object_get_string_member (result_obj, "from_user"));
    search_data->id = json_object_get_int_member (result_obj, "id");

    /*
    purple_debug_info("twitter", "text: %s, from_user: %s, id: %lld\n",
            search_data->text, search_data->from_user, search_data->id); */

    g_array_append_val (search_results, search_data);
}

static void twitter_send_search_cb (PurpleUtilFetchUrlData *url_data,
        gpointer user_data, const gchar *url_text,
        gsize len, const gchar *error_message)
{
    TwitterSearchContext *ctx = user_data;
    JsonParser *parser;
    GError *error = NULL;
    GArray *search_results = NULL;
    const gchar *refresh_url = NULL;
    long long max_id = 0; /* id of last search result */

    parser = json_parser_new ();

    if (!json_parser_load_from_data (parser, url_text, -1, &error)) {
        // error
    }
    else {
        JsonNode *root;
        JsonObject *root_obj;
        JsonArray *results;

        root = json_parser_get_root (parser);
        g_return_if_fail (JSON_NODE_TYPE (root) == JSON_NODE_OBJECT);

        root_obj = json_node_get_object (root);

        results = json_object_get_array_member (root_obj, "results");
        search_results = g_array_new (FALSE, FALSE, sizeof (TwitterSearchData *));
        json_array_foreach_element (results, _get_search_result, search_results);

        refresh_url = json_object_get_string_member (root_obj, "refresh_url");
        max_id = json_object_get_int_member (root_obj, "max_id");

        purple_debug_info("twitter", "refresh_url: %s, max_id: %lld\n",
                refresh_url, max_id);
    }

    ctx->success_func (ctx->account, search_results,
            refresh_url, max_id, ctx->user_data);

    _free_search_results (search_results);
    g_slice_free (TwitterSearchContext, ctx);
    g_object_unref (parser);
}

void twitter_search (PurpleAccount *account, const char *query,
        TwitterSearchSuccessFunc success_cb, TwitterSearchErrorFunc error_cb,
        gpointer data)
{
    /* by default "search.twitter.com" */
    const char *search_host_url = purple_account_get_string (
            account, TWITTER_PREF_SEARCH_HOST_URL,
            TWITTER_PREF_SEARCH_HOST_URL_DEFAULT);
    gchar *full_url = g_strdup_printf ("http://%s/search.json", search_host_url);

    gchar *request = g_strdup_printf (
            "GET %s%s HTTP/1.0\r\n"
            "User-Agent: Mozilla/4.0 (compatible; MSIE 5.5)\r\n"
            "Host: %s\r\n\r\n",
            full_url,
            query,
            search_host_url);

    TwitterSearchContext *ctx = g_slice_new0 (TwitterSearchContext);
    ctx->account = account;
    ctx->user_data = data;
    ctx->success_func = success_cb;
    ctx->error_func = error_cb;

    purple_util_fetch_url_request (full_url, TRUE,
                "Mozilla/4.0 (compatible; MSIE 5.5)", TRUE, request, FALSE,
                twitter_send_search_cb, ctx);

    g_free (full_url);
    g_free (request);
}
