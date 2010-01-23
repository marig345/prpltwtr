#include "twitter_endpoint_dm.h"
#include "twitter_util.h"

static void twitter_send_reply_success_cb(PurpleAccount *account, xmlnode *node, gboolean last, gpointer _who)
{
	if (last && _who)
		g_free(_who);
}

static gboolean twitter_send_reply_error_cb(PurpleAccount *account, const TwitterRequestErrorData *error, gpointer _who)
{
	//TODO: this doesn't work yet
	gchar *who = _who;
	if (who)
	{
		gchar *conv_name = twitter_endpoint_im_buddy_name_to_conv_name(twitter_endpoint_im_find(account, TWITTER_IM_TYPE_AT_MSG), _who);
		purple_conv_present_error(conv_name, account, "Error sending tweet");
		g_free(who);
		g_free(conv_name);
	}

	return FALSE; //give up trying
}

static int twitter_send_reply_do(PurpleAccount *account, const char *who,
		const char *message, PurpleMessageFlags flags)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = gc->proto_data;
	gchar *added_text = g_strdup_printf("@%s", who);
	GArray *statuses = twitter_utf8_get_segments(message, MAX_TWEET_LENGTH, added_text);
	long long in_reply_to_status_id = 0;
	const gchar *reply_id;

	reply_id = (const gchar *)g_hash_table_lookup (
			twitter->user_reply_id_table, who);
	if (reply_id)
		in_reply_to_status_id = strtoll (reply_id, NULL, 10);

	twitter_api_set_statuses(account,
			statuses,
			in_reply_to_status_id,
			twitter_send_reply_success_cb,
			twitter_send_reply_error_cb,
			g_strdup(who)); //TODO

	g_free(added_text);

	return 1;
}


typedef struct 
{
	void (*success_cb)(PurpleAccount *account, long long id, gpointer user_data);
	void (*error_cb)(PurpleAccount *account, const TwitterRequestErrorData *error_data, gpointer user_data);
	gpointer user_data;
} TwitterLastSinceIdRequest;

static void twitter_get_replies_timeout_error_cb (PurpleAccount *account,
		const TwitterRequestErrorData *error_data,
		gpointer user_data)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = gc->proto_data;
	twitter->failed_get_replies_count++;

	if (twitter->failed_get_replies_count >= 3)
	{
		purple_connection_error_reason(gc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, "Could not retrieve replies, giving up trying");
	}
}

static gboolean twitter_get_replies_all_timeout_error_cb (PurpleAccount *account,
		const TwitterRequestErrorData *error_data,
		gpointer user_data)
{
	twitter_get_replies_timeout_error_cb (account, error_data, user_data);
	return TRUE; //restart timer and try again
}

static void _process_replies (PurpleAccount *account,
		GList *statuses,
		TwitterConnectionData *twitter)
{
	GList *l;
	TwitterEndpointIm *ctx = twitter_connection_get_endpoint_im(twitter, TWITTER_IM_TYPE_AT_MSG);

	for (l = statuses; l; l = l->next)
	{
		TwitterUserTweet *data = l->data;
		TwitterTweet *status = twitter_user_tweet_take_tweet(data);
		TwitterUserData *user_data = twitter_user_tweet_take_user_data(data);

		if (!user_data)
		{
			twitter_status_data_free(status);
		} else {
			twitter_buddy_set_user_data(account, user_data, FALSE);
			twitter_status_data_update_conv(ctx, data->screen_name, status);
			twitter_buddy_set_status_data(account, data->screen_name, status);

			/* update user_reply_id_table table */
			gchar *reply_id = g_strdup_printf ("%lld", status->id);
			g_hash_table_insert (twitter->user_reply_id_table,
					g_strdup (data->screen_name), reply_id);
		}
		twitter_user_tweet_free(data);
	}

	twitter->failed_get_replies_count = 0;
}


static void twitter_get_replies_all_cb (PurpleAccount *account,
		GList *nodes,
		gpointer user_data)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = gc->proto_data;

	GList *statuses = twitter_statuses_nodes_parse(nodes);
	_process_replies (account, statuses, twitter);

	g_list_free(statuses);
}

static void twitter_get_replies_get_last_since_id_success_cb(PurpleAccount *account, xmlnode *node, gpointer user_data)
{
	TwitterLastSinceIdRequest *r = user_data;
	long long id = 0;
	xmlnode *status_node = xmlnode_get_child(node, "status");
	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);
	if (status_node != NULL)
	{
		TwitterTweet *status_data = twitter_status_node_parse(status_node);
		if (status_data != NULL)
		{
			id = status_data->id;

			twitter_status_data_free(status_data);
		}
	}
	r->success_cb(account, id, r->user_data);
	g_free(r);
}

static void twitter_get_last_since_id_error_cb(PurpleAccount *account, const TwitterRequestErrorData *error_data, gpointer user_data)
{
	TwitterLastSinceIdRequest *r = user_data;
	r->error_cb(account, error_data, r->user_data);
	g_free(r);
}

static void twitter_get_replies_last_since_id(PurpleAccount *account,
	void (*success_cb)(PurpleAccount *account, long long id, gpointer user_data),
	void (*error_cb)(PurpleAccount *account, const TwitterRequestErrorData *error_data, gpointer user_data),
	gpointer user_data)
{
	TwitterLastSinceIdRequest *request = g_new0(TwitterLastSinceIdRequest, 1);
	request->success_cb = success_cb;
	request->error_cb = error_cb;
	request->user_data = user_data;
	/* Simply get the last reply */
	twitter_api_get_replies(account,
			0, 1, 1,
			twitter_get_replies_get_last_since_id_success_cb,
			twitter_get_last_since_id_error_cb,
			request);
}


static TwitterEndpointImSettings TwitterEndpointReplySettings =
{
	TWITTER_IM_TYPE_AT_MSG,
	"twitter_last_reply_id",
	"@", //conv_id
	twitter_send_reply_do,
	twitter_option_replies_timeout,
	twitter_api_get_replies_all,
	twitter_get_replies_all_cb,
	twitter_get_replies_all_timeout_error_cb,
	twitter_get_replies_last_since_id,
};

TwitterEndpointImSettings *twitter_endpoint_reply_get_settings()
{
	return &TwitterEndpointReplySettings;
}
