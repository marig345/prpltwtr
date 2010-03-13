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

#include <glib/gstdio.h>

#include "twitter.h"

#if _HAVE_PIDGIN_
#include <gtkconv.h>
#include <gtkimhtml.h>
#endif

#include "twitter_charcount.h"
#include "twitter_convicon.h"
#include "twitter_mbprefs.h"


static PurplePlugin *_twitter_protocol = NULL;

static TwitterEndpointChatSettings *TwitterEndpointChatSettingsLookup[TWITTER_CHAT_UNKNOWN];

static const gchar *twitter_account_get_oauth_access_token(PurpleAccount *account)
{
	return purple_account_get_string(account, "oauth_token", NULL);
}
static void twitter_account_set_oauth_access_token(PurpleAccount *account, const gchar *oauth_token)
{
	purple_account_set_string(account, "oauth_token", oauth_token);
}
static const gchar *twitter_account_get_oauth_access_token_secret(PurpleAccount *account)
{
	return purple_account_get_string(account, "oauth_token_secret", NULL);
}
static void twitter_account_set_oauth_access_token_secret(PurpleAccount *account, const gchar *oauth_token)
{
	purple_account_set_string(account, "oauth_token_secret", oauth_token);
}
static void twitter_account_invalidate_token(PurpleAccount *account)
{
	twitter_account_set_oauth_access_token(account, NULL);
	twitter_account_set_oauth_access_token_secret(account, NULL);
}

/******************************************************
 *  Chat
 ******************************************************/
static GList *twitter_chat_info(PurpleConnection *gc) {
	struct proto_chat_entry *pce; /* defined in prpl.h */
	GList *chat_info = NULL;

	pce = g_new0(struct proto_chat_entry, 1);
	pce->label = "Search";
	pce->identifier = "search";
	pce->required = FALSE;

	chat_info = g_list_append(chat_info, pce);

	pce = g_new0(struct proto_chat_entry, 1);
	pce->label = "Update Interval";
	pce->identifier = "interval";
	pce->required = TRUE;
	pce->is_int = TRUE;
	pce->min = 1;
	pce->max = 60;

	chat_info = g_list_append(chat_info, pce);

	return chat_info;
}

GHashTable *twitter_chat_info_defaults(PurpleConnection *gc, const char *chat_name)
{
	GHashTable *defaults;

	defaults = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);

	g_hash_table_insert(defaults, "search", g_strdup(chat_name));

	//bug in pidgin prevents this from working
	g_hash_table_insert(defaults, "interval",
			g_strdup_printf("%d", twitter_option_search_timeout(purple_connection_get_account(gc))));
	return defaults;
}


#if _HAZE_
static PurpleBuddy *twitter_blist_chat_timeline_new(PurpleAccount *account, gint timeline_id)
{
	return twitter_buddy_new(account, "Timeline: Home", NULL);
}
#else
static PurpleChat *twitter_blist_chat_timeline_new(PurpleAccount *account, gint timeline_id)
{
	PurpleGroup *g;
	PurpleChat *c = twitter_blist_chat_find_timeline(account, timeline_id);
	GHashTable *components;
	if (c != NULL)
	{
		return c;
	}
	/* No point in making this a preference (yet?)
	 * the idea is that this will only be done once, and the user can move the
	 * chat to wherever they want afterwards */
	g = purple_find_group(TWITTER_PREF_DEFAULT_TIMELINE_GROUP);
	if (g == NULL)
		g = purple_group_new(TWITTER_PREF_DEFAULT_TIMELINE_GROUP);

	components = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);

	//TODO: fix all of this
	//1) FIXED: search shouldn't be set, but is currently a hack to fix purple_blist_find_chat (persistent chat, etc)
	//2) need this to work with multiple timelines.
	//3) this should be an option. Some people may not want the home timeline
	g_hash_table_insert(components, "interval",
			g_strdup_printf("%d", twitter_option_timeline_timeout(account)));
	g_hash_table_insert(components, "chat_type",
			g_strdup_printf("%d", TWITTER_CHAT_TIMELINE));
	g_hash_table_insert(components, "timeline_id",
			g_strdup_printf("%d", timeline_id));

	c = purple_chat_new(account, "Home Timeline", components);
	purple_blist_add_chat(c, g, NULL);
	return c;
}
#endif

static PurpleChat *twitter_blist_chat_new(PurpleAccount *account, const char *searchtext)
{
	PurpleGroup *g;
	PurpleChat *c = twitter_blist_chat_find_search(account, searchtext);
	const char *group_name;
	GHashTable *components;
	if (c != NULL)
	{
		return c;
	}
	/* This is an option for when we sync our searches, the user
	 * doesn't have to continuously move the chats */
	group_name = twitter_option_search_group(account);
	g = purple_find_group(group_name);
	if (g == NULL)
		g = purple_group_new(group_name);

	components = twitter_chat_info_defaults(purple_account_get_connection(account), searchtext);

	c = purple_chat_new(account, searchtext, components);
	purple_blist_add_chat(c, g, NULL);
	return c;
}

static void twitter_verify_connection_error_handler(PurpleAccount *account, const TwitterRequestErrorData *error_data)
{
	const gchar *error_message;
	PurpleConnectionError reason;
	//purple_connection_error_reason(gc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, error_data->message);
	switch (error_data->type)
	{
		case TWITTER_REQUEST_ERROR_SERVER:
			reason = PURPLE_CONNECTION_ERROR_NETWORK_ERROR;
			error_message = error_data->message;
			break;
		case TWITTER_REQUEST_ERROR_INVALID_XML:
			reason = PURPLE_CONNECTION_ERROR_NETWORK_ERROR;
			error_message = "Received Invalid XML";
			break;
		case TWITTER_REQUEST_ERROR_TWITTER_GENERAL:
			if (!strcmp(error_data->message, "This method requires authentication."))
			{
				reason = PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED;
				error_message = "Invalid password";
				break;
			} else {
				reason = PURPLE_CONNECTION_ERROR_NETWORK_ERROR;
				error_message = error_data->message;
			}
			break;
		default:
			reason = PURPLE_CONNECTION_ERROR_OTHER_ERROR;
			error_message = "Unknown error";
			break;
	}
	purple_connection_error_reason(purple_account_get_connection(account), reason, error_message);
}
static gboolean twitter_get_friends_verify_error_cb(TwitterRequestor *r,
		const TwitterRequestErrorData *error_data,
		gpointer user_data)
{
	twitter_verify_connection_error_handler(r->account, error_data);
	return FALSE;
}

/******************************************************
 *  Twitter friends
 ******************************************************/
static gboolean twitter_update_presence_timeout(gpointer _account)
{
	/* If someone is bored and wants to do this the right way, 
	 * they would have a list of buddies sorted by time
	 * so we don't have to go through all buddies. 
	 * Of course, then you'd have to have each PurpleBuddy point to 
	 * the position in the list, so when a status gets updated
	 * we push the buddy back to the end of the list
	 *
	 * Additionally, we would want the timer to be reset to run
	 * at the time when the next buddy should go offline 
	 * (min(last_tweet_time) - current_time)
	 *
	 * However, this should be good enough. If we find that this
	 * drains a lot of batteries on mobile phones (doubt it), then we can
	 * look back into it.
	 */
	PurpleAccount *account = _account;
	twitter_buddy_touch_state_all(account);
	return TRUE;
}

static void twitter_buddy_datas_set_all(PurpleAccount *account, GList *buddy_datas)
{
	GList *l;
	for (l = buddy_datas; l; l = l->next)
	{
		TwitterUserTweet *data = l->data;
		TwitterUserData *user = twitter_user_tweet_take_user_data(data);
		TwitterTweet *status = twitter_user_tweet_take_tweet(data);

		if (user)
			twitter_buddy_set_user_data(account, user, twitter_option_get_following(account));
		if (status)
			twitter_buddy_set_status_data(account, data->screen_name, status);

		twitter_user_tweet_free(data);
	}
	g_list_free(buddy_datas);
}

static void twitter_get_friends_cb(TwitterRequestor *r, GList *nodes, gpointer user_data)
{
	GList *buddy_datas = twitter_users_nodes_parse(nodes);
	twitter_buddy_datas_set_all(r->account, buddy_datas);
}

static gboolean twitter_get_friends_timeout(gpointer data)
{
	PurpleAccount *account = data;
	//TODO handle errors
	twitter_api_get_friends(purple_account_get_requestor(account),
			twitter_get_friends_cb, NULL, NULL);
	return TRUE;
}

/******************************************************
 *  Twitter relies/mentions
 ******************************************************/


/******************************************************
 *  Twitter search
 ******************************************************/




static TwitterEndpointChatSettings *twitter_get_endpoint_chat_settings(TwitterChatType type)
{
	if (type >= 0 && type < TWITTER_CHAT_UNKNOWN)
		return TwitterEndpointChatSettingsLookup[type];
	return NULL;
}
static char *twitter_chat_get_name(GHashTable *components) {
	const char *chat_type_str = g_hash_table_lookup(components, "chat_type");
	TwitterChatType chat_type = chat_type_str == NULL ? 0 : strtol(chat_type_str, NULL, 10);

	TwitterEndpointChatSettings *settings = twitter_get_endpoint_chat_settings(chat_type);
	if (settings && settings->get_name)
		return settings->get_name(components);
	return NULL;
}


static void get_saved_searches_cb(TwitterRequestor *r,
		xmlnode *node,
		gpointer user_data)
{
	xmlnode *search;

	purple_debug_info (TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);

	for (search = node->child; search; search = search->next) {
		if (search->name && !g_strcmp0 (search->name, "saved_search")) {
			gchar *query = xmlnode_get_child_data (search, "query");
#if _HAZE_
			char *buddy_name = g_strdup_printf("#%s", query);

			twitter_buddy_new(r->account, buddy_name, NULL);
			purple_prpl_got_user_status(r->account, buddy_name, TWITTER_STATUS_ONLINE, NULL);
			g_free(buddy_name);
#else
			twitter_blist_chat_new(r->account, query);
#endif
			g_free (query);
		}
	}
}

static void twitter_chat_leave(PurpleConnection *gc, int id) {
	PurpleConversation *conv = purple_find_chat(gc, id);
	TwitterConnectionData *twitter = gc->proto_data;
	PurpleAccount *account = purple_connection_get_account(gc);
	TwitterEndpointChat *ctx = twitter_endpoint_chat_find(account, purple_conversation_get_name(conv));

	g_return_if_fail(ctx != NULL);
	//TODO move me to twitter_endpoint_chat

	PurpleChat *blist_chat = twitter_blist_chat_find(account, ctx->chat_name);
	if (blist_chat != NULL && twitter_blist_chat_is_auto_open(blist_chat))
	{
		return;
	}

	g_hash_table_remove(twitter->chat_contexts, purple_normalize(account, ctx->chat_name));
}

static int twitter_chat_send(PurpleConnection *gc, int id, const char *message,
		PurpleMessageFlags flags) {
	PurpleConversation *conv = purple_find_chat(gc, id);
	PurpleAccount *account = purple_connection_get_account(gc);
	TwitterEndpointChat *ctx = twitter_endpoint_chat_find(account, purple_conversation_get_name(conv));
	char *stripped_message;
	int rv;

	g_return_val_if_fail(ctx != NULL, -1);

	stripped_message = purple_markup_strip_html(message);

	rv = twitter_endpoint_chat_send(ctx, stripped_message);
	g_free(stripped_message);
	return rv;
}


static void twitter_chat_join_do(PurpleConnection *gc, GHashTable *components, gboolean open_conv) {
	const char *conv_type_str = g_hash_table_lookup(components, "chat_type");
	gint conv_type = conv_type_str == NULL ? 0 : strtol(conv_type_str, NULL, 10);
	twitter_endpoint_chat_start(gc,
			twitter_get_endpoint_chat_settings(conv_type),
			components,
			open_conv);
}
static void twitter_chat_join(PurpleConnection *gc, GHashTable *components) {
	twitter_chat_join_do(gc, components, TRUE);
}

static void twitter_set_all_buddies_online(PurpleAccount *account)
{
	GSList *buddies = purple_find_buddies(account, NULL);
	GSList *l;
	for (l = buddies; l; l = l->next)
	{
		purple_prpl_got_user_status(account, ((PurpleBuddy *) l->data)->name, TWITTER_STATUS_ONLINE,
				"message", NULL, NULL);
	}
	g_slist_free(buddies);
}

static void twitter_init_auto_open_contexts(PurpleAccount *account)
{
	PurpleChat *chat;
	PurpleBlistNode *node, *group;
	PurpleBuddyList *purplebuddylist = purple_get_blist();
	PurpleConnection *gc = purple_account_get_connection(account);
	GHashTable *components;

	g_return_if_fail(purplebuddylist != NULL);

	for (group = purplebuddylist->root; group != NULL; group = group->next) {
		for (node = group->child; node != NULL; node = node->next) {
			if (PURPLE_BLIST_NODE_IS_CHAT(node)) {

				chat = (PurpleChat*)node;

				if (account != chat->account)
					continue;

				if (twitter_blist_chat_is_auto_open(chat))
				{
					components = purple_chat_get_components(chat);
					twitter_chat_join_do(gc, components, FALSE);
				}
			}
		}
	}

	return;
}

#if _HAZE_
static void conversation_created_cb(PurpleConversation *conv, gpointer *unused)
{
	const char *name = purple_conversation_get_name(conv);
	PurpleAccount *account = purple_conversation_get_account(conv);
	g_return_if_fail(name != NULL && name[0] != '\0');

	if (name[0] == '#')
	{
		GHashTable *components = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
		g_hash_table_insert(components, "search", g_strdup(name + 1));
		g_hash_table_insert(components, "chat_type", g_strdup_printf("%d", TWITTER_CHAT_SEARCH));
		twitter_endpoint_chat_start(purple_account_get_connection(account),
				twitter_get_endpoint_chat_settings(TWITTER_CHAT_SEARCH),
				components,
				 TRUE) ;
	} else if (twitter_usernames_match(account, name, "Timeline: Home")) {
		GHashTable *components = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
		g_hash_table_insert(components, "timeline_id", g_strdup("0"));
		g_hash_table_insert(components, "chat_type", g_strdup_printf("%d", TWITTER_CHAT_TIMELINE)); //i don't think this is needed
		twitter_endpoint_chat_start(purple_account_get_connection(account),
				twitter_get_endpoint_chat_settings(TWITTER_CHAT_TIMELINE),
				components,
				TRUE) ;
	}
}

static void deleting_conversation_cb(PurpleConversation *conv, gpointer *unused)
{
	const char *name = purple_conversation_get_name(conv);
	PurpleAccount *account = purple_conversation_get_account(conv);

	g_return_if_fail(name != NULL && name[0] != '\0');

	if (name[0] == '#' || twitter_usernames_match(account, name, "Timeline: Home"))
	{
		PurpleConnection *gc = purple_conversation_get_gc(conv);
		TwitterConnectionData *twitter = gc->proto_data;
		TwitterEndpointChat *ctx = twitter_endpoint_chat_find(account, purple_conversation_get_name(conv));
		if (ctx)
		{
			//TODO: move to endpoint_chat.c
			purple_debug_info(TWITTER_PROTOCOL_ID, "destroying haze im chat %s\n",
				ctx->chat_name);
			g_hash_table_remove(twitter->chat_contexts, purple_normalize(account, ctx->chat_name));
		}
	}
}
#endif

static void twitter_endpoint_im_start_foreach(TwitterConnectionData *twitter, TwitterEndpointIm *im, gpointer data)
{
	twitter_endpoint_im_start(im);
}

static gboolean TWITTER_SIGNALS_CONNECTED = FALSE;

static void twitter_connected(PurpleAccount *account)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = gc->proto_data;

	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);

	twitter->mb_prefs = twitter_mb_prefs_new(account);

	twitter_connection_set_endpoint_im(twitter,
			TWITTER_IM_TYPE_AT_MSG,
			twitter_endpoint_im_new(account,
				twitter_endpoint_reply_get_settings(),
				twitter_option_get_history(account),
				TWITTER_INITIAL_REPLIES_COUNT));
	twitter_connection_set_endpoint_im(twitter,
			TWITTER_IM_TYPE_DM,
			twitter_endpoint_im_new(account,
				twitter_endpoint_dm_get_settings(),
				twitter_option_get_history(account),
				TWITTER_INITIAL_DMS_COUNT));

	/* Since protocol plugins are loaded before conversations_init is called
	 * we cannot connect these signals in plugin->load.
	 * So we have this here, with a global var that tells us to only run this
	 * once, regardless of number of accounts connecting */
	if (!TWITTER_SIGNALS_CONNECTED)
	{
		TWITTER_SIGNALS_CONNECTED = TRUE;
#if _HAZE_
		purple_signal_connect(purple_conversations_get_handle(), "conversation-created",
				twitter, PURPLE_CALLBACK(conversation_created_cb), NULL);
		purple_signal_connect(purple_conversations_get_handle(), "deleting-conversation",
				twitter, PURPLE_CALLBACK(deleting_conversation_cb), NULL);

#endif


#if _HAVE_PIDGIN_
		//FIXME: disconnect signals!
		purple_signal_connect(purple_conversations_get_handle(),
				"conversation-created",
				_twitter_protocol, PURPLE_CALLBACK(twitter_charcount_conv_created_cb), NULL);
		purple_signal_connect(purple_conversations_get_handle(),
				"deleting-conversation",
				_twitter_protocol, PURPLE_CALLBACK(twitter_charcount_conv_destroyed_cb), NULL);
#endif
	}

	purple_connection_update_progress(gc, "Connected",
			2,   /* which connection step this is */
			3);  /* total number of steps */
	purple_connection_set_state(gc, PURPLE_CONNECTED);

	twitter_blist_chat_timeline_new(account, 0);
#if _HAZE_
	//Set home timeline online
	purple_prpl_got_user_status(account, "Timeline: Home", TWITTER_STATUS_ONLINE, NULL);
#endif


	/* Retrieve user's saved search queries */
	twitter_api_get_saved_searches(purple_account_get_requestor(account),
			get_saved_searches_cb, NULL, NULL);

	/* Install periodic timers to retrieve replies and dms */
	twitter_connection_foreach_endpoint_im(twitter, twitter_endpoint_im_start_foreach, NULL);

	/* Immediately retrieve replies */

	int get_friends_timer_timeout = twitter_option_user_status_timeout(account);

	//We will try to get all our friends' statuses, whether they're in the buddylist or not
	if (get_friends_timer_timeout > 0)
	{
		twitter->get_friends_timer = purple_timeout_add_seconds(
				60 * get_friends_timer_timeout,
				twitter_get_friends_timeout, account);
		if (!twitter_option_get_following(account) && twitter_option_cutoff_time(account) > 0)
			twitter_get_friends_timeout(account);
	} else {
		twitter->get_friends_timer = 0;
	}
	if (twitter_option_cutoff_time(account) > 0)
		twitter->update_presence_timer = purple_timeout_add_seconds(
			TWITTER_UPDATE_PRESENCE_TIMEOUT * 60, twitter_update_presence_timeout, account);
	twitter_init_auto_open_contexts(account);
}
static void twitter_get_friends_verify_connection_cb(TwitterRequestor *r,
		GList *nodes,
		gpointer user_data)
{
	PurpleConnection *gc = purple_account_get_connection(r->account);
	GList *l_users_data = NULL;

	if (purple_connection_get_state(gc) == PURPLE_CONNECTING)
	{
		twitter_connected(r->account);

		l_users_data = twitter_users_nodes_parse(nodes);

		/* setup buddy list */
		twitter_buddy_datas_set_all(r->account, l_users_data);

	}
}

static void twitter_get_rate_limit_status_cb(TwitterRequestor *r, xmlnode *node, gpointer user_data)
{
	/*
	 * <hash>
	 * <reset-time-in-seconds type="integer">1236529763</reset-time-in-seconds>
	 * <remaining-hits type="integer">100</remaining-hits>
	 * <hourly-limit type="integer">100</hourly-limit>
	 * <reset-time type="datetime">2009-03-08T16:29:23+00:00</reset-time>
	 * </hash>
	 */

	xmlnode *child;
	int remaining_hits = 0;
	int hourly_limit = 0;
	char *message;
	for (child = node->child; child; child = child->next)
	{
		if (child->name)
		{
			if (!strcmp(child->name, "remaining-hits"))
			{
				char *data = xmlnode_get_data_unescaped(child);
				remaining_hits = atoi(data);
				g_free(data);
			}
			else if (!strcmp(child->name, "hourly-limit"))
			{
				char *data = xmlnode_get_data_unescaped(child);
				hourly_limit = atoi(data);
				g_free(data);
			}
		}
	}
	message = g_strdup_printf("%d/%d %s", remaining_hits, hourly_limit, "Remaining");
	purple_notify_info(NULL,  /* plugin handle or PurpleConnection */
			("Rate Limit Status"),
			("Rate Limit Status"),
			(message));
	g_free(message);
}

/*
 * prpl functions
 */
static const char *twitter_list_icon(PurpleAccount *account, PurpleBuddy *buddy)
{
	return "prpltwtr";
}

static char *twitter_status_text(PurpleBuddy *buddy) {
	purple_debug_info(TWITTER_PROTOCOL_ID, "getting %s's status text for %s\n",
			buddy->name, buddy->account->username);

	if (purple_find_buddy(buddy->account, buddy->name)) {
		PurplePresence *presence = purple_buddy_get_presence(buddy);
		PurpleStatus *status = purple_presence_get_active_status(presence);
		const char *message = status ? purple_status_get_attr_string(status, "message") : NULL;

		if (message && strlen(message) > 0)
			return g_strdup(g_markup_escape_text(message, -1));

	}
	return NULL;
}

static void twitter_tooltip_text(PurpleBuddy *buddy,
		PurpleNotifyUserInfo *info,
		gboolean full) 
{

	PurplePresence *presence = purple_buddy_get_presence(buddy);
	PurpleStatus *status = purple_presence_get_active_status(presence);
	char *msg;

	purple_debug_info(TWITTER_PROTOCOL_ID, "showing %s tooltip for %s\n",
			(full) ? "full" : "short", buddy->name);

	if ((msg = twitter_status_text(buddy)))
	{
		purple_notify_user_info_add_pair(info, purple_status_get_name(status),
				msg);
		g_free(msg);
	}

	if (full) {
		/*const char *user_info = purple_account_get_user_info(gc->account);
		  if (user_info)
		  purple_notify_user_info_add_pair(info, _("User info"), user_info);*/
	}
}

/* everyone will be online
 * Future: possibly have offline mode for people who haven't updated in a while
 */
static GList *twitter_status_types(PurpleAccount *account)
{
	GList *types = NULL;
	PurpleStatusType *type;
	PurpleStatusPrimitive status_primitives[] = {
		PURPLE_STATUS_UNAVAILABLE,
		PURPLE_STATUS_INVISIBLE,
		PURPLE_STATUS_AWAY,
		PURPLE_STATUS_EXTENDED_AWAY
	};
	int status_primitives_count = sizeof(status_primitives) / sizeof(status_primitives[0]);
	int i;

	type = purple_status_type_new(PURPLE_STATUS_AVAILABLE, TWITTER_STATUS_ONLINE,
			TWITTER_STATUS_ONLINE, TRUE);
	purple_status_type_add_attr(type, "message", ("Online"),
			purple_value_new(PURPLE_TYPE_STRING));
	types = g_list_prepend(types, type);

	//This is a hack to get notified when another protocol goes into a different status.
	//Eg aim goes "away", we still want to get notified
	for (i = 0; i < status_primitives_count; i++)
	{
		type = purple_status_type_new(status_primitives[i], TWITTER_STATUS_ONLINE,
				TWITTER_STATUS_ONLINE, FALSE);
		purple_status_type_add_attr(type, "message", ("Online"),
				purple_value_new(PURPLE_TYPE_STRING));
		types = g_list_prepend(types, type);
	}

	type = purple_status_type_new(PURPLE_STATUS_OFFLINE, TWITTER_STATUS_OFFLINE,
			TWITTER_STATUS_OFFLINE, TRUE);
	purple_status_type_add_attr(type, "message", ("Offline"),
			purple_value_new(PURPLE_TYPE_STRING));
	types = g_list_prepend(types, type);

	return g_list_reverse(types);
}


/*
 * UI callbacks
 */
static void twitter_action_get_user_info(PurplePluginAction *action)
{
	PurpleConnection *gc = (PurpleConnection *)action->context;
	PurpleAccount *account = purple_connection_get_account(gc);
	twitter_api_get_friends(purple_account_get_requestor(account),
			twitter_get_friends_cb, NULL, NULL);
}

static void twitter_action_set_status_ok(PurpleConnection *gc, PurpleRequestFields *fields)
{
	PurpleAccount *account = purple_connection_get_account(gc);
	const char* status = purple_request_fields_get_string(fields, "status");
	purple_account_set_status(account, TWITTER_STATUS_ONLINE, TRUE, "message", status, NULL);
}
static void twitter_action_set_status(PurplePluginAction *action)
{
	PurpleRequestFields *request;
	PurpleRequestFieldGroup *group;
	PurpleRequestField *field;
	PurpleConnection *gc = (PurpleConnection *)action->context;

	group = purple_request_field_group_new(NULL);

	field = purple_request_field_string_new("status", ("Status"), "", FALSE);
	purple_request_field_group_add_field(group, field);

	request = purple_request_fields_new();
	purple_request_fields_add_group(request, group);

	purple_request_fields(action->plugin,
			("Status"),
			("Set Account Status"),
			NULL,
			request,
			("_Set"), G_CALLBACK(twitter_action_set_status_ok),
			("_Cancel"), NULL,
			purple_connection_get_account(gc), NULL, NULL,
			gc);
}
static void twitter_action_get_rate_limit_status(PurplePluginAction *action)
{
	PurpleConnection *gc = (PurpleConnection *)action->context;
	TwitterConnectionData *twitter = gc->proto_data;
	twitter_api_get_rate_limit_status(twitter->requestor, twitter_get_rate_limit_status_cb, NULL, NULL);
}

/* this is set to the actions member of the PurplePluginInfo struct at the
 * bottom.
 */
static GList *twitter_actions(PurplePlugin *plugin, gpointer context)
{
	GList *l = NULL;
	PurplePluginAction *action;

	action = purple_plugin_action_new("Set status", twitter_action_set_status);
	l = g_list_append(l, action);

	action = purple_plugin_action_new("Rate Limit Status", twitter_action_get_rate_limit_status);
	l = g_list_append(l, action);

	l = g_list_append(l, NULL);

	action = purple_plugin_action_new("Debug - Retrieve users", twitter_action_get_user_info);
	l = g_list_append(l, action);

	return l;
}


typedef struct 
{
	void (*success_cb)(PurpleAccount *account, long long id, gpointer user_data);
	void (*error_cb)(PurpleAccount *account, const TwitterRequestErrorData *error_data, gpointer user_data);
	gpointer user_data;
} TwitterLastSinceIdRequest;

//TODO: rename
static void twitter_verify_connection(PurpleAccount *account)
{
	gboolean retrieve_history;

	//To verify the connection, we get the user's friends.
	//With that we'll update the buddy list and set the last known reply id

	/* If history retrieval enabled, read last reply id from config file.
	 * There's no config file, just set last reply id to 0 */
	retrieve_history = twitter_option_get_history(account);

	//If we don't have a stored last reply id, we don't want to get the entire history (EVERY reply)
	PurpleConnection *gc = purple_account_get_connection(account);

	if (purple_connection_get_state(gc) == PURPLE_CONNECTING) {

		purple_connection_update_progress(gc, "Connecting...",
				1,   /* which connection step this is */
				3);  /* total number of steps */
	}

	if (twitter_option_get_following(account))
	{
		twitter_api_get_friends(purple_account_get_requestor(account),
				twitter_get_friends_verify_connection_cb,
				twitter_get_friends_verify_error_cb,
				NULL);
	} else {
		twitter_connected(account);
		if (twitter_option_cutoff_time(account) <= 0)
			twitter_set_all_buddies_online(account);
	}
}

static GHashTable *twitter_oauth_result_to_hashtable(const gchar *txt)
{
	gchar **pieces = g_strsplit(txt, "&", 0);
	GHashTable *results = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	gchar **p;
	
	for (p = pieces; *p; p++)
	{
		gchar *equalpos = strchr(*p, '=');
		if (equalpos)
		{
			equalpos[0] = '\0';
			g_hash_table_replace(results, g_strdup(*p), g_strdup(equalpos+1));
		}
	}
	g_strfreev(pieces);
	return results;
}

static void twitter_oauth_disconnect(PurpleAccount *account, const char *message)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	purple_connection_error_reason(gc,
			PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED,
			(message));
}

typedef struct
{
	PurpleAccount *account;
	gchar *username;
} TwitterAccountUserNameChange;

static void twitter_account_mismatch_screenname_change_cancel_cb(TwitterAccountUserNameChange *change, gint action_id)
{
	PurpleAccount *account = change->account;
	twitter_account_invalidate_token(account);
	g_free(change->username);
	g_free(change);
	twitter_oauth_disconnect(account, "Username mismatch");
}
static void twitter_account_mismatch_screenname_change_ok_cb(TwitterAccountUserNameChange *change, gint action_id)
{
	PurpleAccount *account = change->account;
	purple_account_set_username(account, change->username);
	g_free(change->username);
	g_free(change);
	twitter_verify_connection(account);
}

static void twitter_account_username_change_verify(PurpleAccount *account, const gchar *username)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	gchar *secondary = g_strdup_printf("Do you wish to change the name on this account to %s?",
			username);
	TwitterAccountUserNameChange *change_data = (TwitterAccountUserNameChange *) g_new0(TwitterAccountUserNameChange *, 1);

	change_data->account = account;
	change_data->username = g_strdup(username);

	purple_request_action(gc,
			"Mismatched Screen Names",
			"Authorized screen name does not match with account screen name",
			secondary,
			0,
			account,
			NULL,
			NULL,
			change_data,
			2, 
			"Cancel", twitter_account_mismatch_screenname_change_cancel_cb,
			"Yes", twitter_account_mismatch_screenname_change_ok_cb,
			NULL);
	
	g_free(secondary);
}

static void twitter_oauth_access_token_success_cb(TwitterRequestor *r,
		const gchar *response,
		gpointer user_data)
{
	PurpleAccount *account = r->account;
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = gc->proto_data;

	GHashTable *results = twitter_oauth_result_to_hashtable(response);
	const gchar *oauth_token = g_hash_table_lookup(results, "oauth_token");
	const gchar *oauth_token_secret = g_hash_table_lookup(results, "oauth_token_secret");
	const gchar *response_screen_name = g_hash_table_lookup(results, "screen_name");
	if (oauth_token && oauth_token_secret)
	{
		if (twitter->oauth_token)
			g_free(twitter->oauth_token);
		if (twitter->oauth_token_secret)
			g_free(twitter->oauth_token_secret);

		twitter->oauth_token = g_strdup(oauth_token);
		twitter->oauth_token_secret = g_strdup(oauth_token_secret);

		twitter_account_set_oauth_access_token(account, oauth_token);
		twitter_account_set_oauth_access_token_secret(account, oauth_token_secret);

		//FIXME: set this to be case insensitive
		
		if (response_screen_name 
			&& !twitter_usernames_match(account, response_screen_name, purple_account_get_username(account)))
		{
			twitter_account_username_change_verify(account, response_screen_name);
		} else {
			twitter_verify_connection(account);
		}
	} else {
		twitter_oauth_disconnect(account, "Unknown response getting access token");
		purple_debug_info(TWITTER_PROTOCOL_ID, "Unknown error receiving access token: %s\n",
				response);
	}
}

static void twitter_oauth_access_token_error_cb(TwitterRequestor *r, const TwitterRequestErrorData *error_data, gpointer user_data)
{
	twitter_oauth_disconnect(r->account, "Error verifying PIN");
}

static void twitter_oauth_request_pin_ok(PurpleAccount *account, const gchar *pin)
{
	twitter_api_oauth_access_token(purple_account_get_requestor(account),
			pin,
			twitter_oauth_access_token_success_cb,
			twitter_oauth_access_token_error_cb,
			NULL);
}

static void twitter_oauth_request_pin_cancel(PurpleAccount *account, const gchar *pin)
{
	twitter_oauth_disconnect(account, "Canceled PIN entry");
}

static void twitter_oauth_request_token_success_cb(TwitterRequestor *r,
		const gchar *response,
		gpointer user_data)
{
	PurpleAccount *account = r->account;
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = gc->proto_data;

	GHashTable *results = twitter_oauth_result_to_hashtable(response);
	const gchar *oauth_token = g_hash_table_lookup(results, "oauth_token");
	const gchar *oauth_token_secret = g_hash_table_lookup(results, "oauth_token_secret");
	if (oauth_token && oauth_token_secret)
	{
		gchar *msg = g_strdup_printf("http://twitter.com/oauth/authorize?oauth_token=%s",
			purple_url_encode(oauth_token));

		twitter->oauth_token = g_strdup(oauth_token);
		twitter->oauth_token_secret = g_strdup(oauth_token_secret);
		purple_notify_uri(twitter, msg);

		purple_request_input(twitter,
				"OAuth Authentication", //title
				"Please enter pin", //primary
				msg, //secondary
				NULL, //default
				FALSE, //multiline,
				FALSE, //password
				NULL, //hint
				"Submit", //ok text
				G_CALLBACK(twitter_oauth_request_pin_ok),
				"Cancel",
				G_CALLBACK(twitter_oauth_request_pin_cancel),
				account,
				NULL,
				NULL,
				account);
		g_free(msg);
	} else {
		twitter_oauth_disconnect(account, "Invalid response receiving request token");
		purple_debug_info(TWITTER_PROTOCOL_ID, "Unknown error receiving request token: %s\n",
				response);
	}
	g_hash_table_destroy(results);
}

static void twitter_oauth_request_token_error_cb(TwitterRequestor *r, const TwitterRequestErrorData *error_data, gpointer user_data)
{
	twitter_oauth_disconnect(r->account, "Error receiving request token");
}

void twitter_verify_credentials_success_cb(TwitterRequestor *r, xmlnode *node, gpointer user_data)
{
	PurpleAccount *account = r->account;
	TwitterUserTweet *user_tweet = twitter_verify_credentials_parse(node);
	if (!user_tweet || !user_tweet->screen_name)
	{
		twitter_oauth_disconnect(account, "Could not verify credentials");
	} else if (!twitter_usernames_match(account,user_tweet->screen_name, purple_account_get_username(account))) 
	{
		twitter_account_username_change_verify(account, user_tweet->screen_name);
	} else 
	{
		twitter_verify_connection(account);
	}
	twitter_user_tweet_free(user_tweet);
}

static void twitter_requestor_pre_send_auth_basic(TwitterRequestor *r, gboolean *post, const char **url, TwitterRequestParams **params, gchar ***header_fields, gpointer *requestor_data)
{
	const char *pass = purple_connection_get_password(purple_account_get_connection(r->account));
	const char *sn = purple_account_get_username(r->account);
	char *auth_text = g_strdup_printf("%s:%s", sn, pass);
	char *auth_text_b64 = purple_base64_encode((guchar *) auth_text, strlen(auth_text));
	*header_fields = g_new(gchar *, 2);

	(*header_fields)[0] = g_strdup_printf("Authorization: Basic %s", auth_text_b64);
	(*header_fields)[1] = NULL;

	g_free(auth_text);
	g_free(auth_text_b64);
}
static void twitter_requestor_post_send_auth_basic(TwitterRequestor *r, gboolean *post, const char **url, TwitterRequestParams **params, gchar ***header_fields, gpointer *requestor_data)
{
	g_strfreev(*header_fields);
}

static void twitter_requestor_pre_send_oauth(TwitterRequestor *r, gboolean *post, const char **url, TwitterRequestParams **params, gchar ***header_fields, gpointer *requestor_data)
{
	PurpleAccount *account = r->account;
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = gc->proto_data;
	gchar *signing_key = g_strdup_printf("%s&%s",
			TWITTER_OAUTH_SECRET,
			twitter->oauth_token_secret ? twitter->oauth_token_secret : "");
	TwitterRequestParams *oauth_params = twitter_request_params_add_oauth_params(
			account, *post, *url,
			*params, twitter->oauth_token, signing_key);

	if (oauth_params == NULL)
	{
		TwitterRequestErrorData *error = g_new0(TwitterRequestErrorData, 1);
		gchar *error_msg = g_strdup("Could not sign request");
		error->type = TWITTER_REQUEST_ERROR_NO_OAUTH;
		error->message = error_msg;
		g_free(error_msg);
		g_free(error);
		g_free(signing_key);
		//TODO: error if couldn't sign
		return;
	} 
	*requestor_data = *params;
	*params = oauth_params;
}

static void twitter_requestor_post_send_oauth(TwitterRequestor *r, gboolean *post, const char **url, TwitterRequestParams **params, gchar ***header_fields, gpointer *requestor_data)
{
	twitter_request_params_free(*params);
	*params = (TwitterRequestParams *) *requestor_data;
}

static void twitter_requestor_post_failed(TwitterRequestor *r, const TwitterRequestErrorData **error_data)
{
	purple_debug_info(TWITTER_PROTOCOL_ID,
			"post_failed called for account %s, error %d, message %s\n",
			r->account->username,
			(*error_data)->type,
			(*error_data)->message);
	switch ((*error_data)->type)
	{
		case TWITTER_REQUEST_ERROR_UNAUTHORIZED:
			twitter_account_invalidate_token(r->account);
			twitter_oauth_disconnect(r->account, "Unauthorized");
			break;
		default:
			break;
	}
}

static void twitter_login(PurpleAccount *account)
{
	const gchar *oauth_token;
	const gchar *oauth_token_secret;
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = g_new0(TwitterConnectionData, 1);
	gc->proto_data = twitter;

	purple_debug_info(TWITTER_PROTOCOL_ID, "logging in %s\n", account->username);

	twitter->requestor = g_new0(TwitterRequestor, 1);
	twitter->requestor->account = account;
	twitter->requestor->post_failed = twitter_requestor_post_failed;

	if (!twitter_option_use_oauth(account))
	{
		twitter->requestor->pre_send = twitter_requestor_pre_send_auth_basic;
		twitter->requestor->post_send = twitter_requestor_post_send_auth_basic;
	} else {
		twitter->requestor->pre_send = twitter_requestor_pre_send_oauth;
		twitter->requestor->post_send = twitter_requestor_post_send_oauth;
	}

	/* key: gchar *, value: TwitterEndpointChat */
	twitter->chat_contexts = g_hash_table_new_full(
			g_str_hash, g_str_equal, g_free, (GDestroyNotify) twitter_endpoint_chat_free);


	/* key: gchar *, value: gchar * (of a long long) */
	twitter->user_reply_id_table = g_hash_table_new_full (
			g_str_hash, g_str_equal, g_free, g_free);

#if _HAVE_PIDGIN_
	if (twitter_option_enable_conv_icon(account))
		twitter_conv_icon_account_load(account);
#endif

	/* purple wants a minimum of 2 steps */
	purple_connection_update_progress(gc, ("Connecting"),
			0,   /* which connection step this is */
			2);  /* total number of steps */


	if (twitter_option_use_oauth(account))
	{
		//If we want to use oauth, go through the oauth steps 
		oauth_token = twitter_account_get_oauth_access_token(account);
		oauth_token_secret = twitter_account_get_oauth_access_token_secret(account);
		if (oauth_token && oauth_token_secret)
		{
			twitter->oauth_token = g_strdup(oauth_token);
			twitter->oauth_token_secret = g_strdup(oauth_token_secret);
			twitter_api_verify_credentials(purple_account_get_requestor(account),
					twitter_verify_credentials_success_cb,
					NULL,
					NULL);
		} else {
			twitter_api_oauth_request_token(purple_account_get_requestor(account),
					twitter_oauth_request_token_success_cb,
					twitter_oauth_request_token_error_cb,
					NULL);
		}
	} else {
		//No oauth, just do the verification step, skipping previous oauth steps
		twitter_verify_connection(account);
	}
}

static void twitter_endpoint_im_free_foreach(TwitterConnectionData *conn, TwitterEndpointIm *im, gpointer data)
{
	twitter_endpoint_im_free(im);
}

static void twitter_close(PurpleConnection *gc)
{
	/* notify other twitter accounts */
	PurpleAccount *account = purple_connection_get_account(gc);
	TwitterConnectionData *twitter = gc->proto_data;

	twitter_connection_foreach_endpoint_im(twitter, twitter_endpoint_im_free_foreach, NULL);

	if (twitter->get_friends_timer)
		purple_timeout_remove(twitter->get_friends_timer);

	if (twitter->chat_contexts)
		g_hash_table_destroy(twitter->chat_contexts);
	twitter->chat_contexts = NULL;

	if (twitter->update_presence_timer)
		purple_timeout_remove(twitter->update_presence_timer);

	if (twitter->user_reply_id_table)
		g_hash_table_destroy (twitter->user_reply_id_table);
	twitter->user_reply_id_table = NULL;

#if _HAVE_PIDGIN_
	twitter_conv_icon_account_unload(account);
#endif

	if (twitter->mb_prefs)
		twitter_mb_prefs_free(twitter->mb_prefs);

	if (twitter->oauth_token)
		g_free(twitter->oauth_token);

	if (twitter->oauth_token_secret)
		g_free(twitter->oauth_token_secret);

	g_free(twitter->requestor);

	g_free(twitter);
}

static void twitter_set_status_error_cb(TwitterRequestor *r, const TwitterRequestErrorData *error_data, gpointer user_data)
{
	const char *message;
	if (error_data->type == TWITTER_REQUEST_ERROR_SERVER || error_data->type == TWITTER_REQUEST_ERROR_TWITTER_GENERAL)
	{
		message = error_data->message;
	} else if (error_data->type == TWITTER_REQUEST_ERROR_INVALID_XML) {
		message = "Unknown reply by twitter server";
	} else {
		message = "Unknown error";
	}
	purple_notify_error(NULL,  /* plugin handle or PurpleConnection */
			("Twitter Set Status"),
			("Error setting Twitter Status"),
			(message));
}

/* A few options here
 * Send message to "d buddy" will send a direct message to buddy
 * Send message to "@buddy" will send a @buddy message
 * Send message to "buddy" will send a @buddy message (TODO: will change in future, make it an option)
 * _HAZE_ Send message to "#text" will set status message with appended "text" (eg hello text)
 * _HAZE_ Send message to "Timeline: Home" will set status
 */
static int twitter_send_im(PurpleConnection *gc, const char *conv_name,
		const char *message, PurpleMessageFlags flags)
{
	TwitterEndpointIm *im;
	const char *buddy_name;
	PurpleAccount *account = purple_connection_get_account(gc);
	char *stripped_message;
	int rv = 0;

	g_return_val_if_fail(message != NULL && message[0] != '\0' && conv_name != NULL && conv_name[0] != '\0', 0);

	stripped_message = purple_markup_strip_html(message);

#if _HAZE_
	if (conv_name[0] == '#')
	{
		purple_debug_info(TWITTER_PROTOCOL_ID, "%s of search %s\n", G_STRFUNC, conv_name);
		TwitterEndpointChat *endpoint = twitter_endpoint_chat_find(account, conv_name);
		rv = twitter_endpoint_chat_send(endpoint, stripped_message);
		g_free(stripped_message);
		return rv;
	} else if (twitter_usernames_match(account, conv_name, "Timeline: Home")) {
		purple_debug_info(TWITTER_PROTOCOL_ID, "%s of home timeline\n", G_STRFUNC);
		TwitterEndpointChat *endpoint = twitter_endpoint_chat_find(account, conv_name);
		rv = twitter_endpoint_chat_send(endpoint, stripped_message);
		g_free(stripped_message);
		return rv;
	}
#endif

	//TODO, this should be part of im settings
	im = twitter_conv_name_to_endpoint_im(account, conv_name);
	buddy_name = twitter_conv_name_to_buddy_name(account, conv_name);
	rv = im->settings->send_im(account, buddy_name, stripped_message, flags);
	g_free(stripped_message);
	return rv;
}

static void twitter_set_info(PurpleConnection *gc, const char *info) {
	purple_debug_info(TWITTER_PROTOCOL_ID, "setting %s's user info to %s\n",
			gc->account->username, info);
}

static void twitter_get_info(PurpleConnection *gc, const char *username) {
	//TODO: error check
	//TODO: fix for buddy not on list?
	TwitterConnectionData *twitter = gc->proto_data;
	PurpleNotifyUserInfo *info = purple_notify_user_info_new();
	PurpleBuddy *b = purple_find_buddy(purple_connection_get_account(gc), username);
	gchar *url;

	if (b)
	{
		TwitterUserTweet *data = twitter_buddy_get_buddy_data(b);
		if (data)
		{
			TwitterUserData *user_data = data->user;
			TwitterTweet *status_data = data->status;


			if (user_data)
			{
				purple_notify_user_info_add_pair(info, "Description:", user_data->description);
			}
			if (status_data)
			{
				purple_notify_user_info_add_pair(info, "Status:", status_data->text);
			}
		}
	} else {
		purple_notify_user_info_add_pair(info, "Description:", "No user info");
	}
	url = twitter_mb_prefs_get_user_profile_url(twitter->mb_prefs, username);
	purple_notify_user_info_add_pair(info, "Account Link:", url);
	if (url)
	{
		g_free(url);
	}
	purple_notify_userinfo(gc,
		username,
		info,
		NULL,
		NULL);

}


static void twitter_set_status(PurpleAccount *account, PurpleStatus *status) {
	gboolean sync_status = twitter_option_sync_status(account);
	if (!sync_status)
		return ;

	//TODO: I'm pretty sure this is broken
	const char *msg = purple_status_get_attr_string(status, "message");
	purple_debug_info(TWITTER_PROTOCOL_ID, "setting %s's status to %s: %s\n",
			account->username, purple_status_get_name(status), msg);

	if (msg && strcmp("", msg))
	{
		//TODO, sucecss && fail
		twitter_api_set_status(purple_account_get_requestor(account), msg, 0,
				NULL, twitter_set_status_error_cb, NULL);
	}
}

static void twitter_add_buddy(PurpleConnection *gc, PurpleBuddy *buddy,
		PurpleGroup *group)
{
	//Perform the logic to decide whether this buddy will be online/offline
	twitter_buddy_touch_state(buddy);
}

static void twitter_add_buddies(PurpleConnection *gc, GList *buddies,
		GList *groups) {
	GList *buddy = buddies;
	GList *group = groups;

	purple_debug_info(TWITTER_PROTOCOL_ID, "adding multiple buddies\n");

	while (buddy && group) {
		twitter_add_buddy(gc, (PurpleBuddy *)buddy->data, (PurpleGroup *)group->data);
		buddy = g_list_next(buddy);
		group = g_list_next(group);
	}
}

static void twitter_remove_buddy(PurpleConnection *gc, PurpleBuddy *buddy,
		PurpleGroup *group)
{
	TwitterUserTweet *twitter_buddy_data = buddy->proto_data;

	purple_debug_info(TWITTER_PROTOCOL_ID, "removing %s from %s's buddy list\n",
			buddy->name, gc->account->username);

	if (!twitter_buddy_data)
		return;
	twitter_user_tweet_free(twitter_buddy_data);
	buddy->proto_data = NULL;
}

static void twitter_remove_buddies(PurpleConnection *gc, GList *buddies,
		GList *groups) {
	GList *buddy = buddies;
	GList *group = groups;

	purple_debug_info(TWITTER_PROTOCOL_ID, "removing multiple buddies\n");

	while (buddy && group) {
		twitter_remove_buddy(gc, (PurpleBuddy *)buddy->data,
				(PurpleGroup *)group->data);
		buddy = g_list_next(buddy);
		group = g_list_next(group);
	}
}

static void twitter_get_cb_info(PurpleConnection *gc, int id, const char *who) {
	//TODO FIX ME
	PurpleConversation *conv = purple_find_chat(gc, id);
	purple_debug_info(TWITTER_PROTOCOL_ID,
			"retrieving %s's info for %s in chat room %s\n", who,
			gc->account->username, conv->name);

	twitter_get_info(gc, who);
}

static void twitter_blist_chat_auto_open_toggle(PurpleBlistNode *node, gpointer userdata) {
	TwitterEndpointChat *ctx;
	PurpleChat *chat = PURPLE_CHAT(node);
	PurpleAccount *account = purple_chat_get_account(chat);
	GHashTable *components = purple_chat_get_components(chat);
	const char *chat_name = twitter_chat_get_name(components);

	gboolean new_state = !twitter_blist_chat_is_auto_open(chat);

	//If no conversation exists and we've set this to NOT auto open, let's free some memory
	if (!new_state 
		&& !purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, chat_name, account)
		&& (ctx = twitter_endpoint_chat_find(account, chat_name)))

	{
		TwitterConnectionData *twitter = purple_account_get_connection(account)->proto_data;
		purple_debug_info(TWITTER_PROTOCOL_ID, "No more auto open, destroying context\n");
		g_hash_table_remove(twitter->chat_contexts, purple_normalize(account, ctx->chat_name));
	} else if (new_state && !twitter_endpoint_chat_find(account, chat_name)) {
		//Join the chat, but don't automatically open the conversation
		twitter_chat_join_do(purple_account_get_connection(account), components, FALSE);
	}

	g_hash_table_replace(components, g_strdup("auto_open"), 
			(new_state ? g_strdup("1") : g_strdup("0")));
}

//TODO should be handled in twitter_endpoint_reply
static void twitter_blist_buddy_at_msg(PurpleBlistNode *node, gpointer userdata)
{
	PurpleBuddy *buddy = PURPLE_BUDDY(node);
	char *name = g_strdup_printf("@%s", buddy->name);
	PurpleConversation *conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, purple_buddy_get_account(buddy),
			name);
	purple_conversation_present(conv);
	g_free(name);
}

//TODO should be handled in twitter_endpoint_dm
static void twitter_blist_buddy_dm(PurpleBlistNode *node, gpointer userdata)
{
	PurpleBuddy *buddy = PURPLE_BUDDY(node);
	char *name = g_strdup_printf("d %s", buddy->name);
	PurpleConversation *conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, purple_buddy_get_account(buddy),
			name);
	purple_conversation_present(conv);
	g_free(name);
}


static GList *twitter_blist_node_menu(PurpleBlistNode *node) {
	purple_debug_info(TWITTER_PROTOCOL_ID, "providing buddy list context menu item\n");

	if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
		PurpleChat *chat = PURPLE_CHAT(node);
		char *label = g_strdup_printf("Automatically open chat on new tweets (Currently: %s)",
			(twitter_blist_chat_is_auto_open(chat) ? "On" : "Off"));

		PurpleMenuAction *action = purple_menu_action_new(
				label,
				PURPLE_CALLBACK(twitter_blist_chat_auto_open_toggle),
				NULL,   /* userdata passed to the callback */
				NULL);  /* child menu items */
		g_free(label);
		return g_list_append(NULL, action);
	} else if (PURPLE_BLIST_NODE_IS_BUDDY(node)) {

		PurpleMenuAction *action;
		if (twitter_option_default_dm(purple_buddy_get_account(PURPLE_BUDDY(node))))
		{
			action = purple_menu_action_new(
					"@Message",
					PURPLE_CALLBACK(twitter_blist_buddy_at_msg),
					NULL,   /* userdata passed to the callback */
					NULL);  /* child menu items */
		} else {
			action = purple_menu_action_new(
					"Direct Message",
					PURPLE_CALLBACK(twitter_blist_buddy_dm),
					NULL,   /* userdata passed to the callback */
					NULL);  /* child menu items */
		}
		return g_list_append(NULL, action);
	} else {
		return NULL;
	}
}

static void twitter_set_buddy_icon(PurpleConnection *gc,
		PurpleStoredImage *img) {
	purple_debug_info(TWITTER_PROTOCOL_ID, "setting %s's buddy icon to %s\n",
			gc->account->username, purple_imgstore_get_filename(img));
}


static void twitter_convo_closed(PurpleConnection *gc, const gchar *conv_name)
{
	TwitterEndpointIm *im = twitter_conv_name_to_endpoint_im(purple_connection_get_account(gc), conv_name);
	if (im)
	{
		twitter_endpoint_im_convo_closed(im, conv_name);
	}
}

/*
 * prpl stuff. see prpl.h for more information.
 */
static PurplePluginProtocolInfo prpl_info =
{
	OPT_PROTO_CHAT_TOPIC,  /* options */
	NULL,	       /* user_splits, initialized in twitter_init() */
	NULL,	       /* protocol_options, initialized in twitter_init() */
	{   /* icon_spec, a PurpleBuddyIconSpec */
		"png,jpg,gif",		   /* format */
		0,			       /* min_width */
		0,			       /* min_height */
		48,			     /* max_width */
		48,			     /* max_height */
		10000,			   /* max_filesize */
		PURPLE_ICON_SCALE_DISPLAY,       /* scale_rules */
	},
	twitter_list_icon,		  /* list_icon */ //TODO
	NULL, //twitter_list_emblem,		/* list_emblem */
	twitter_status_text,		/* status_text */
	twitter_tooltip_text,	       /* tooltip_text */
	twitter_status_types,	       /* status_types */
	twitter_blist_node_menu,	    /* blist_node_menu */
	twitter_chat_info,		  /* chat_info */
	twitter_chat_info_defaults,	 /* chat_info_defaults */
	twitter_login,		      /* login */
	twitter_close,		      /* close */
	twitter_send_im, //twitter_send_dm,		    /* send_im */
	twitter_set_info,		   /* set_info */
	NULL, //twitter_send_typing,		/* send_typing */
	twitter_get_info,		   /* get_info */
	twitter_set_status,		 /* set_status */
	NULL,		   /* set_idle */
	NULL,//TODO?	      /* change_passwd */
	twitter_add_buddy,//TODO		  /* add_buddy */
	twitter_add_buddies,//TODO		/* add_buddies */
	twitter_remove_buddy,//TODO	       /* remove_buddy */
	twitter_remove_buddies,//TODO	     /* remove_buddies */
	NULL,//TODO?		 /* add_permit */
	NULL,//TODO?		   /* add_deny */
	NULL,//TODO?		 /* rem_permit */
	NULL,//TODO?		   /* rem_deny */
	NULL,//TODO?	    /* set_permit_deny */
	twitter_chat_join,		  /* join_chat */
	NULL,		/* reject_chat */
	twitter_chat_get_name,	      /* get_chat_name */
	NULL,		/* chat_invite */
	twitter_chat_leave,		 /* chat_leave */
	NULL,//twitter_chat_whisper,	       /* chat_whisper */
	twitter_chat_send,		  /* chat_send */
	NULL,//TODO?				/* keepalive */
	NULL,	      /* register_user */
	twitter_get_cb_info,		/* get_cb_info */
	NULL,				/* get_cb_away */
	NULL,//TODO?		/* alias_buddy */
	NULL,		/* group_buddy */
	NULL,	       /* rename_group */
	NULL,				/* buddy_free */
	twitter_convo_closed,	       /* convo_closed */
	purple_normalize_nocase,		  /* normalize */
	twitter_set_buddy_icon,	     /* set_buddy_icon */
	NULL,	       /* remove_group */
	NULL,//TODO?				/* get_cb_real_name */
	NULL,	     /* set_chat_topic */
	twitter_blist_chat_find,				/* find_blist_chat */
	NULL,	  /* roomlist_get_list */
	NULL,	    /* roomlist_cancel */
	NULL,   /* roomlist_expand_category */
	NULL,	   /* can_receive_file */
	NULL,				/* send_file */
	NULL,				/* new_xfer */
	NULL,	    /* offline_message */
	NULL,				/* whiteboard_prpl_ops */
	NULL,				/* send_raw */
	NULL,				/* roomlist_room_serialize */
	NULL,				/* padding... */
	NULL,
	NULL,
	sizeof(PurplePluginProtocolInfo),    /* struct_size */
	NULL
};

#if _HAVE_PIDGIN_
typedef struct
{
	PurpleConversationType type;
	gchar *conv_name;
} TwitterConversationId;

static void twitter_conv_id_write_message(PurpleAccount *account, TwitterConversationId *conv_id,
		PurpleMessageFlags flags, const gchar *message)
{
	PurpleConversation *conv;
	g_return_if_fail(conv_id != NULL);

	conv = purple_find_conversation_with_account(conv_id->type, conv_id->conv_name, account);

	if (conv)
	{
		purple_conversation_write(conv, NULL, message, flags, time(NULL));
	}

	g_free(conv_id->conv_name);
	g_free(conv_id);
}

static void twitter_send_rt_success_cb(TwitterRequestor *r,
		xmlnode *node,
		gpointer user_data)
{
	TwitterConversationId *conv_id = user_data;
	twitter_conv_id_write_message(r->account, conv_id, PURPLE_MESSAGE_SYSTEM, "Successfully retweeted");
}

static void twitter_send_rt_error_cb(TwitterRequestor *r,
		const TwitterRequestErrorData *error_data,
		gpointer user_data)
{
	TwitterConversationId *conv_id = user_data;
	twitter_conv_id_write_message(r->account, conv_id, PURPLE_MESSAGE_ERROR, "Retweet failed");
}

static void twitter_delete_tweet_success_cb(TwitterRequestor *r,
		xmlnode *node,
		gpointer user_data)
{
	TwitterConversationId *conv_id = user_data;
	twitter_conv_id_write_message(r->account, conv_id, PURPLE_MESSAGE_SYSTEM, "Successfully deleted");
}

static void twitter_delete_tweet_error_cb(TwitterRequestor *r,
		const TwitterRequestErrorData *error_data,
		gpointer user_data)
{
	TwitterConversationId *conv_id = user_data;
	twitter_conv_id_write_message(r->account, conv_id, PURPLE_MESSAGE_ERROR, "Delete failed");
}


static gboolean twitter_uri_handler(const char *proto, const char *cmd_arg, GHashTable *params)
{
	/* TODO: refactor all of this
	 * I'll wait until I create the graphical tweet actions portion
	 */
	const char *text;
	const char *username;
	PurpleAccount *account;
	const gchar *action;
	purple_debug_info(TWITTER_PROTOCOL_ID, "%s PROTO %s CMD_ARG %s\n", G_STRFUNC, proto, cmd_arg);

	g_return_val_if_fail(proto != NULL, FALSE);
	g_return_val_if_fail(cmd_arg != NULL, FALSE);

	//don't handle someone elses proto
	if (strcmp(proto, TWITTER_URI))
		return FALSE;

	username = g_hash_table_lookup(params, "account");

	if (username == NULL || username[0] == '\0')
	{
		purple_debug_info(TWITTER_PROTOCOL_ID, "malformed uri. No account username\n");
		return FALSE;
	}

	//ugly hack to fix username highlighting
	account = purple_accounts_find(username+1, TWITTER_PROTOCOL_ID);

	if (account == NULL)
	{
		purple_debug_info(TWITTER_PROTOCOL_ID, "could not find account %s\n", username);
		return FALSE;
	}

	while (cmd_arg[0] == '/')
		cmd_arg++;

	action = g_hash_table_lookup(params, "action");
	if (action)
		cmd_arg = action;
	purple_debug_info(TWITTER_PROTOCOL_ID, "Account %s got action %s\n", username, cmd_arg);
	if (!strcmp(cmd_arg, TWITTER_URI_ACTION_USER))
	{
		purple_notify_info(purple_account_get_connection(account),
				"Clicked URI",
				"@name clicked",
				"Sorry, this has not been implemented yet");
	} else if (!strcmp(cmd_arg, TWITTER_URI_ACTION_REPLY)) {
		const char *id_str, *user;
		long long id;
		PurpleConversation *conv;
		id_str = g_hash_table_lookup(params, "id");
		user = g_hash_table_lookup(params, "user");
		if (id_str == NULL || user == NULL || id_str[0] == '\0' || user[0] == '\0')
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "malformed uri. Invalid id/user for reply\n");
			return FALSE;
		}
		id = strtoll(id_str, NULL, 10);
		if (id == 0)
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "malformed uri. Invalid id for reply\n");
			return FALSE;
		}
		conv = twitter_endpoint_reply_conversation_new(twitter_endpoint_im_find(account, TWITTER_IM_TYPE_AT_MSG),
				user,
				id);
		if (!conv)
		{
			return FALSE;
		}
		purple_conversation_present(conv);
	} else if (!strcmp(cmd_arg, TWITTER_URI_ACTION_RT)) {
		TwitterConversationId *conv_id;

		const char *id_str;
		long long id;

		gchar *conv_type_str;
		PurpleConversationType conv_type;

		gchar *conv_name_encoded;

		id_str = g_hash_table_lookup(params, "id");
		conv_name_encoded = g_hash_table_lookup(params, "conv_name");
		conv_type_str = g_hash_table_lookup(params, "conv_type");

		if (id_str == NULL || id_str[0] == '\0')
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "malformed uri. Invalid id for rt\n");
			return FALSE;
		}
		id = strtoll(id_str, NULL, 10);
		if (id == 0)
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "malformed uri. Invalid id for rt\n");
			return FALSE;
		}

		conv_type = atoi(conv_type_str);

		conv_id = g_new0(TwitterConversationId, 1);
		conv_id->conv_name = g_strdup(purple_url_decode(conv_name_encoded));
		conv_id->type = conv_type;
		twitter_api_send_rt(purple_account_get_requestor(account),
				id,
				twitter_send_rt_success_cb,
				twitter_send_rt_error_cb,
				conv_id);
	} else if (!strcmp(cmd_arg, TWITTER_URI_ACTION_LINK)) {
		const char *id_str, *user;
		long long id;
		gchar *link;
		PurpleConnection *gc = purple_account_get_connection(account);
		TwitterConnectionData *twitter = gc->proto_data;
		id_str = g_hash_table_lookup(params, "id");
		user = g_hash_table_lookup(params, "user");
		if (id_str == NULL || user == NULL || id_str[0] == '\0' || user[0] == '\0')
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "malformed uri. Invalid id/user for link\n");
			return FALSE;
		}
		id = strtoll(id_str, NULL, 10);
		if (id == 0)
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "malformed uri. Invalid id for link\n");
			return FALSE;
		}
		link = twitter_mb_prefs_get_status_url(twitter->mb_prefs, user, id);
		if (link)
		{
			purple_notify_uri(NULL, link);
			g_free(link);
		}
	} else if (!strcmp(cmd_arg, TWITTER_URI_ACTION_DELETE)) {
		TwitterConversationId *conv_id;

		const char *id_str;
		long long id;

		gchar *conv_type_str;
		PurpleConversationType conv_type;

		gchar *conv_name_encoded;

		id_str = g_hash_table_lookup(params, "id");
		conv_name_encoded = g_hash_table_lookup(params, "conv_name");
		conv_type_str = g_hash_table_lookup(params, "conv_type");

		if (id_str == NULL || id_str[0] == '\0')
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "malformed uri. Invalid id for rt\n");
			return FALSE;
		}
		id = strtoll(id_str, NULL, 10);
		if (id == 0)
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "malformed uri. Invalid id for rt\n");
			return FALSE;
		}

		conv_type = atoi(conv_type_str);

		conv_id = g_new0(TwitterConversationId, 1);
		conv_id->conv_name = g_strdup(purple_url_decode(conv_name_encoded));
		conv_id->type = conv_type;
		twitter_api_delete_status(purple_account_get_requestor(account),
				id,
				twitter_delete_tweet_success_cb,
				twitter_delete_tweet_error_cb,
				conv_id);
	} else if (!strcmp(cmd_arg, TWITTER_URI_ACTION_SEARCH)) {
		//join chat with default interval, open in conv window
		GHashTable *components;
		text = g_hash_table_lookup(params, "text");

		if (text == NULL || text[0] == '\0')
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "malformed uri. No text for search\n");
			return FALSE;
		}
		components = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
		g_hash_table_insert(components, "search", g_strdup(purple_url_decode(text)));
		twitter_endpoint_chat_start(purple_account_get_connection(account), twitter_get_endpoint_chat_settings(TWITTER_CHAT_SEARCH),
				components, TRUE) ;
	}
	return TRUE;
}


static void twitter_got_uri_action(const gchar *url, const gchar *action)
{
	gchar *url2 = g_strdup_printf("%s&action=%s", url, action);
	purple_got_protocol_handler_uri(url2);
	g_free(url2);
}

static void twitter_context_menu_retweet(GtkWidget *w, const gchar *url)
{
	twitter_got_uri_action(url, TWITTER_URI_ACTION_RT);
}

static void twitter_context_menu_reply(GtkWidget *w, const gchar *url)
{
	twitter_got_uri_action(url, TWITTER_URI_ACTION_REPLY);
}

static void twitter_context_menu_link(GtkWidget *w, const gchar *url)
{
	twitter_got_uri_action(url, TWITTER_URI_ACTION_LINK);
}

static void twitter_context_menu_delete(GtkWidget *w, const gchar *url)
{
	twitter_got_uri_action(url, TWITTER_URI_ACTION_DELETE);
}

static const gchar *url_get_param_value(const gchar *url, const gchar *key, gsize *len)
{
	const gchar *start = strchr(url, '?');
	const gchar *end;
	int key_len;
	*len = 0;
	if (!start)
		return NULL;
	key_len = strlen(key);

	do
	{
		start++;
		if (g_str_has_prefix(start, key) && start[key_len] == '=')
		{
			start += key_len + 1;
			end = strchr(start, '&');
			if (!end)
				*len = strlen(start);
			else
				*len = end - start;
			return start;
		}
	} while ((start = strchr(start, '&')) != NULL);

	return NULL;
}

static void twitter_url_menu_actions(GtkWidget *menu, const char *url)
{
	GtkWidget *img, *item;
	gsize account_len;
	gsize user_len;
	PurpleAccount *account;

	const gchar *account_name_tmp = url_get_param_value(url, "account", &account_len);
	const gchar *user_name_tmp = url_get_param_value(url, "user", &user_len);
	gchar *account_name, *user_name;
	if (!account_name_tmp || !user_name_tmp)
		return;
	account_name_tmp++;
	account_len--;

	account_name = g_strndup(account_name_tmp, account_len);
	user_name = g_strndup(user_name_tmp, user_len);

	account = purple_accounts_find(account_name, TWITTER_PROTOCOL_ID);

	img = gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_MENU);
	item = gtk_image_menu_item_new_with_mnemonic(("Retweet"));
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), img);
	g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(twitter_context_menu_retweet), (gpointer)url);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	img = gtk_image_new_from_stock(GTK_STOCK_REDO, GTK_ICON_SIZE_MENU);
	item = gtk_image_menu_item_new_with_mnemonic(("Reply"));
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), img);
	g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(twitter_context_menu_reply), (gpointer)url);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	img = gtk_image_new_from_stock(GTK_STOCK_HOME, GTK_ICON_SIZE_MENU);
	item = gtk_image_menu_item_new_with_mnemonic(("Goto Site"));
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), img);
	g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(twitter_context_menu_link), (gpointer)url);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	if (account && twitter_usernames_match(account, account_name, user_name))
	{
		img = gtk_image_new_from_stock(GTK_STOCK_DELETE, GTK_ICON_SIZE_MENU);
		item = gtk_image_menu_item_new_with_mnemonic(("Delete"));
		gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), img);
		g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(twitter_context_menu_delete), (gpointer)url);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}


	g_free(account_name);
	g_free(user_name);
}

static gboolean twitter_context_menu(GtkIMHtml *imhtml, GtkIMHtmlLink *link, GtkWidget *menu)
{
	twitter_url_menu_actions(menu, gtk_imhtml_link_get_url(link));
	return TRUE;
}

static gboolean twitter_url_clicked_cb(GtkIMHtml *imhtml, GtkIMHtmlLink *link)
{
	static GtkWidget *menu = NULL;
	gchar *url;
	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);

	if (menu)
	{
		gtk_widget_destroy(menu);
		menu = NULL;
	}

	//If not the action url, handle it by using the uri handler, otherwise, show menu
	if (!g_str_has_prefix(gtk_imhtml_link_get_url(link), TWITTER_URI ":///" TWITTER_URI_ACTION_ACTIONS "?"))
	{
		purple_got_protocol_handler_uri(gtk_imhtml_link_get_url(link));
		return TRUE;
	}

	url = g_strdup(gtk_imhtml_link_get_url(link));

	menu = gtk_menu_new();
	g_object_set_data_full(G_OBJECT(menu), "x-imhtml-url-data", url,
			(GDestroyNotify)g_free);

	twitter_url_menu_actions(menu, url);

	gtk_widget_show_all(menu);
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
			0, gtk_get_current_event_time());

	return TRUE;

}
#endif

static void twitter_init_endpoint_chat_settings(TwitterEndpointChatSettings *settings)
{
	TwitterEndpointChatSettingsLookup[settings->type] = settings;
}

static void twitter_init(PurplePlugin *plugin)
{

	purple_debug_info(TWITTER_PROTOCOL_ID, "starting up\n");

	prpl_info.protocol_options = twitter_get_protocol_options();

#if _HAVE_PIDGIN_
	purple_signal_connect(purple_get_core(), "uri-handler", plugin,
			PURPLE_CALLBACK(twitter_uri_handler), NULL);

	twitter_charcount_attach_to_all_windows();

	gtk_imhtml_class_register_protocol(TWITTER_URI "://", twitter_url_clicked_cb, twitter_context_menu);

#endif
	twitter_init_endpoint_chat_settings(twitter_endpoint_search_get_settings());
	twitter_init_endpoint_chat_settings(twitter_endpoint_timeline_get_settings());

	_twitter_protocol = plugin;
}

static void twitter_destroy(PurplePlugin *plugin) 
{
	purple_debug_info(TWITTER_PROTOCOL_ID, "shutting down\n");
#if _HAVE_PIDGIN_
	twitter_charcount_detach_from_all_windows();
#endif
	purple_signals_disconnect_by_handle(plugin);
}

static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,				     /* magic */
	PURPLE_MAJOR_VERSION,				    /* major_version */
	PURPLE_MINOR_VERSION,				    /* minor_version */
	PURPLE_PLUGIN_PROTOCOL,				  /* type */
	NULL,						    /* ui_requirement */
	0,						       /* flags */
	NULL,						    /* dependencies */
	PURPLE_PRIORITY_DEFAULT,				 /* priority */
	TWITTER_PROTOCOL_ID,					     /* id */
	"Twitter Protocol",					      /* name */
	"0.4.1",						   /* version */
	"Twitter Protocol Plugin",				  /* summary */
	"Twitter Protocol Plugin",				  /* description */
	"neaveru <neaveru@gmail.com>",		     /* author */
	"http://code.google.com/p/prpltwtr/",  /* homepage */
	NULL,						    /* load */
	NULL,						    /* unload */
	twitter_destroy,					/* destroy */
	NULL,						    /* ui_info */
	&prpl_info,					      /* extra_info */
	NULL,						    /* prefs_info */
	twitter_actions,					/* actions */
	NULL,						    /* padding... */
	NULL,
	NULL,
	NULL,
};

PURPLE_INIT_PLUGIN(null, twitter_init, info);
