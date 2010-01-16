#include "twitter_endpoint_timeline.h"

//TODO: Should these be here?
long long twitter_account_get_last_home_timeline_id(PurpleAccount *account)
{
	return purple_account_get_long_long(account, "twitter_last_home_timeline_id", 0);
}

void twitter_account_set_last_home_timeline_id(PurpleAccount *account, long long reply_id)
{
	purple_account_set_long_long(account, "twitter_last_home_timeline_id", reply_id);
}

long long twitter_connection_get_last_home_timeline_id(PurpleConnection *gc)
{
	long long reply_id = 0;
	TwitterConnectionData *connection_data = gc->proto_data;
	reply_id = connection_data->last_home_timeline_id;
	return (reply_id ? reply_id : twitter_account_get_last_home_timeline_id(purple_connection_get_account(gc)));
}

void twitter_connection_set_last_home_timeline_id(PurpleConnection *gc, long long reply_id)
{
	TwitterConnectionData *connection_data = gc->proto_data;

	connection_data->last_home_timeline_id = reply_id;
	twitter_account_set_last_home_timeline_id(purple_connection_get_account(gc), reply_id);
}


static gpointer twitter_timeline_timeout_context_new(GHashTable *components)
{
	TwitterTimelineTimeoutContext *ctx = g_slice_new0(TwitterTimelineTimeoutContext);
	ctx->timeline_id = 0;
	return ctx;
}

static void twitter_timeline_timeout_context_free(gpointer _ctx)
{
	g_return_if_fail(_ctx != NULL);
	TwitterTimelineTimeoutContext *ctx = _ctx;

	g_slice_free (TwitterTimelineTimeoutContext, ctx);
} 

typedef struct
{
	gchar *message;
	gchar *pos;
	gint len;
	gchar *add_text;
} SendImContext;


static void twitter_send_im_split_do(PurpleConnection *gc, SendImContext *ctx);
static void twitter_send_im_split_cb(PurpleAccount *account, xmlnode *node, gpointer user_data)
{
	SendImContext *ctx = user_data;
	twitter_send_im_split_do(purple_account_get_connection(account), ctx);
}

static gchar *twitter_utf8_find_last_pos(gchar *str, gchar *needles, glong str_len)
{
	gchar *last;
	gchar *needle;
	for (last = g_utf8_offset_to_pointer(str, str_len); last; last = g_utf8_find_prev_char(str, last))
		for (needle = needles; *needle; needle++)
			if (*last == *needle)
			{
				return last;
			}
	return NULL;
}

static void twitter_send_im_split_do(PurpleConnection *gc, SendImContext *ctx)
{
	int add_text_len = 0;
	int index_add_text = -1;
	char *status;
	int len_left;
	int len = 0;
	ctx->pos += ctx->len;

	while (ctx->pos[0] == ' ')
		ctx->pos++;

	if (ctx->pos[0] == '\0')
		return;

	//TODO: proper case sensitivity
	if (ctx->add_text)
	{
		char *pnt_add_text = strstr(ctx->pos, ctx->add_text);
		add_text_len = g_utf8_strlen(ctx->add_text, -1);
		if (pnt_add_text)
			index_add_text = g_utf8_pointer_to_offset(ctx->pos, pnt_add_text) + add_text_len;
	}

	//add add_text
	len_left = g_utf8_strlen(ctx->pos, -1);
	if (len_left <= MAX_TWEET_LENGTH && (!ctx->add_text || index_add_text != -1))
	{
		status = g_strdup(ctx->pos);
		len = strlen(ctx->pos);
	} else if (len_left <= MAX_TWEET_LENGTH && len_left + add_text_len + 1 <= MAX_TWEET_LENGTH) {
		status = g_strdup_printf("%s %s", ctx->add_text, ctx->pos);
		len = strlen(ctx->pos);
	} else {
		gchar *space;
		if (ctx->add_text 
			&& index_add_text != -1
			&& index_add_text <= MAX_TWEET_LENGTH
			&& (space = twitter_utf8_find_last_pos(ctx->pos + index_add_text, " ", MAX_TWEET_LENGTH - g_utf8_pointer_to_offset(ctx->pos, ctx->pos + index_add_text)))
			&& (g_utf8_pointer_to_offset(ctx->pos, space) <= MAX_TWEET_LENGTH))
		{
			//split already has our word in it
			len = space - ctx->pos;
			status = g_strndup(ctx->pos, len);
		} else if ((space = twitter_utf8_find_last_pos(ctx->pos, " ", MAX_TWEET_LENGTH - (ctx->add_text ? add_text_len + 1 : 0)))) {
			len = space - ctx->pos;
			space[0] = '\0';
			status = ctx->add_text ? g_strdup_printf("%s %s", ctx->add_text, ctx->pos) : g_strdup(ctx->pos);
			space[0] = ' ';
		} else if (index_add_text != -1 && index_add_text <= MAX_TWEET_LENGTH) {
			//one long word, which contains our add_text
			char prev_char;
			gchar *end_pos;
			end_pos = g_utf8_offset_to_pointer(ctx->pos, MAX_TWEET_LENGTH);
			len = end_pos - ctx->pos;
			prev_char = end_pos[0];
			end_pos[0] = '\0';
			status = g_strdup(ctx->pos);
			end_pos[0] = prev_char;
		} else {
			char prev_char;
			gchar *end_pos;
			end_pos = (index_add_text != -1 && index_add_text <= MAX_TWEET_LENGTH ?
					g_utf8_offset_to_pointer(ctx->pos, MAX_TWEET_LENGTH) :
					g_utf8_offset_to_pointer(ctx->pos, MAX_TWEET_LENGTH - (ctx->add_text ? add_text_len + 1 : 0)));
			end_pos = g_utf8_offset_to_pointer(ctx->pos, MAX_TWEET_LENGTH - (ctx->add_text ? add_text_len + 1 : 0));
			len = end_pos - ctx->pos;
			prev_char = end_pos[0];
			end_pos[0] = '\0';
			status = ctx->add_text ? g_strdup_printf("%s %s", ctx->add_text, ctx->pos) : g_strdup(ctx->pos);
			end_pos[0] = prev_char;
		}
	}
	ctx->len = len;
	//debug
	//printf("Status: (%s) (%d)\n", status, g_utf8_strlen(status, -1));
	//twitter_send_im_split_cb(purple_connection_get_account(gc), NULL, ctx);
	twitter_api_set_status(purple_connection_get_account(gc),
			status,
			0,
			twitter_send_im_split_cb,
			NULL,
			ctx);
	g_strdup(status);
}

static int twitter_send_im_split(PurpleConnection *gc, const char *message,
		const char *add_text)
{
	SendImContext *ctx = g_new0(SendImContext, 1);
	ctx->message = g_strdup(message);
	ctx->pos = ctx->message;
	ctx->len = 0;
	if (add_text)
		ctx->add_text = g_strdup(add_text);
	twitter_send_im_split_do(gc, ctx);
	return 1;
}

//TODO merge me
static int twitter_chat_timeline_send(TwitterEndpointChat *ctx_base, const gchar *message)
{
	PurpleAccount *account = ctx_base->account;
	PurpleConversation *conv = twitter_endpoint_chat_find_open_conv(ctx_base);

	if (conv == NULL) return -1; //TODO: error?

	twitter_send_im_split(purple_account_get_connection(account), message, "不不不不不不不不不不不");
#if !_HAZE_
	twitter_chat_add_tweet(PURPLE_CONV_CHAT(conv), account->username, message, 0, time(NULL));
#endif
	return 0;
}

static char *twitter_chat_name_from_timeline_id(const gint timeline_id)
{
	return g_strdup("Timeline: Home");
}
static char *twitter_timeline_chat_name_from_components(GHashTable *components)
{
	return twitter_chat_name_from_timeline_id(0);
}

//TODO: the proper way to do this would be to have this and twitter_search return the same data type
static void twitter_get_home_timeline_parse_statuses(PurpleAccount *account,
		TwitterEndpointChat *endpoint_chat, GList *statuses)
{
	PurpleConnection *gc = purple_account_get_connection(account);
#if _HAZE_
	PurpleConvIm *chat;
#else
	PurpleConvChat *chat;
#endif
	GList *l;

	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);

	g_return_if_fail(account != NULL);

	if (!statuses)
		return;
	chat = twitter_endpoint_chat_get_conv(endpoint_chat);
	g_return_if_fail(chat != NULL); //TODO: destroy context

	for (l = statuses; l; l = l->next)
	{
		TwitterUserTweet *data = l->data;
		TwitterTweet *status = twitter_user_tweet_take_tweet(data);
		TwitterUserData *user_data = twitter_user_tweet_take_user_data(data);

		if (!user_data)
		{
			twitter_status_data_free(status);
		} else {
			const char *text = status->text;
			twitter_chat_add_tweet(chat, data->screen_name, text, status->id, status->created_at);
			if (status->id && status->id > twitter_connection_get_last_home_timeline_id(gc))
			{
				twitter_connection_set_last_home_timeline_id(gc, status->id);
			}
			twitter_buddy_set_status_data(account, data->screen_name, status);
			twitter_buddy_set_user_data(account, user_data, FALSE);

			/* update user_reply_id_table table */
			//gchar *reply_id = g_strdup_printf ("%lld", status->id);
			//g_hash_table_insert (twitter->user_reply_id_table,
					//g_strdup (screen_name), reply_id);
		}
		twitter_user_tweet_free(data);
	}
}

static void twitter_get_home_timeline_cb(PurpleAccount *account, xmlnode *node, gpointer user_data)
{
	TwitterEndpointChatId *chat_id = (TwitterEndpointChatId *)user_data;
	TwitterEndpointChat *endpoint_chat;

	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);

	g_return_if_fail(chat_id != NULL);
	endpoint_chat = twitter_endpoint_chat_find_by_id(chat_id);
	twitter_endpoint_chat_id_free(chat_id);
	
	if (endpoint_chat == NULL)
		return;

	GList *statuses = twitter_statuses_node_parse(node);
	twitter_get_home_timeline_parse_statuses(account, endpoint_chat, statuses);
	g_list_free(statuses);

}

static void twitter_get_home_timeline_all_cb(PurpleAccount *account,
		GList *nodes,
		gpointer user_data)
{
	TwitterEndpointChatId *chat_id = (TwitterEndpointChatId *)user_data;
	TwitterEndpointChat *endpoint_chat;

	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);

	g_return_if_fail(chat_id != NULL);
	endpoint_chat = twitter_endpoint_chat_find_by_id(chat_id);
	twitter_endpoint_chat_id_free(chat_id);
	
	if (endpoint_chat == NULL)
		return;

	GList *statuses = twitter_statuses_nodes_parse(nodes);
	twitter_get_home_timeline_parse_statuses(account, endpoint_chat, statuses);
	g_list_free(statuses);
}
static gboolean twitter_endpoint_timeline_interval_start(TwitterEndpointChat *endpoint)
{
	PurpleAccount *account = endpoint->account;
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterEndpointChatId *chat_id = twitter_endpoint_chat_id_new(endpoint);
	long long since_id = twitter_connection_get_last_home_timeline_id(gc);

	purple_debug_info(TWITTER_PROTOCOL_ID, "%s creating new timeline context\n", account->username);

	if (since_id == 0)
	{
		purple_debug_info(TWITTER_PROTOCOL_ID, "Retrieving %s statuses for first time\n", gc->account->username);
		twitter_api_get_home_timeline(account,
				since_id,
				TWITTER_HOME_TIMELINE_INITIAL_COUNT,
				1,
				twitter_get_home_timeline_cb,
				NULL,
				chat_id);
	} else {
		purple_debug_info(TWITTER_PROTOCOL_ID, "Retrieving %s statuses since %lld\n", gc->account->username, since_id);
		twitter_api_get_home_timeline_all(account,
				since_id,
				twitter_get_home_timeline_all_cb,
				NULL,
				twitter_option_home_timeline_max_tweets(account),
				chat_id);
	}
	return TRUE;
}
static gboolean twitter_timeline_timeout(TwitterEndpointChat *endpoint_chat)
{
	PurpleAccount *account = endpoint_chat->account;
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterEndpointChatId *chat_id = twitter_endpoint_chat_id_new(endpoint_chat);
	long long since_id = twitter_connection_get_last_home_timeline_id(gc);
	if (since_id == 0)
	{
		purple_debug_info(TWITTER_PROTOCOL_ID, "Retrieving %s statuses for first time\n", account->username);
		twitter_api_get_home_timeline(account,
				since_id,
				20,
				1,
				twitter_get_home_timeline_cb,
				NULL,
				chat_id);
	} else {
		purple_debug_info(TWITTER_PROTOCOL_ID, "Retrieving %s statuses since %lld\n", account->username, since_id);
		twitter_api_get_home_timeline_all(account,
				since_id,
				twitter_get_home_timeline_all_cb,
				NULL,
				twitter_option_home_timeline_max_tweets(account),
				chat_id);
	}

	return TRUE;
}

static TwitterEndpointChatSettings TwitterEndpointTimelineSettings =
{
	TWITTER_CHAT_TIMELINE,
#if _HAZE_
	'!',
#endif
	twitter_chat_timeline_send, //send_message
	twitter_timeline_timeout_context_free, //endpoint_data_free
	twitter_option_timeline_timeout, //get_default_interval
	twitter_timeline_chat_name_from_components, //get_name
	NULL, //verify_components
	twitter_timeline_timeout,
	twitter_endpoint_timeline_interval_start,
	twitter_timeline_timeout_context_new,
};

TwitterEndpointChatSettings *twitter_endpoint_timeline_get_settings()
{
	return &TwitterEndpointTimelineSettings;
}

