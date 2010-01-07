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

static PurplePlugin *_twitter_protocol = NULL;

#define TWITTER_STATUS_ONLINE   "online"
#define TWITTER_STATUS_OFFLINE   "offline"

#define MAX_TWEET_LENGTH 140

#define TWITTER_URI_ACTION_USER		"user"
#define TWITTER_URI_ACTION_SEARCH	"search"

typedef enum
{
	TWITTER_CHAT_SEARCH = 0,
	TWITTER_CHAT_TIMELINE = 1
} TwitterChatType;

typedef struct _TwitterConvChatContext TwitterConvChatContext;
typedef void (*TwitterChatLeaveFunc)(TwitterConvChatContext *ctx);
typedef gint (*TwitterChatSendMessageFunc)(TwitterConvChatContext *ctx, const char *message);

/* When I have time, I'd like to make this event driven
 * Where there is an object with attached events when the timeout completes
 * Then depending on actions, events will be detached. If event count = 0
 * then the timer stops. That would be nice... */
struct _TwitterConvChatContext
{
	TwitterChatType type;
	PurpleAccount *account;
	guint timer_handle;
	gchar *chat_name;
	gpointer endpoint_data;
};

typedef struct
{
	TwitterConvChatContext *base;

	gchar *search_text; /* e.g. N900 */
	gchar *refresh_url; /* e.g. ?since_id=6276370030&q=n900 */

	long long last_tweet_id;
} TwitterSearchTimeoutContext;

typedef struct
{
	TwitterConvChatContext *base;
	guint timeline_id;
} TwitterTimelineTimeoutContext;

typedef struct
{
	guint get_replies_timer;
	guint get_friends_timer;
	long long last_reply_id;
	long long last_home_timeline_id;
	long long failed_get_replies_count;

	/* a table of TwitterConvChatContext
	 * where the key will be the chat name
	 * Alternatively, we only really need chat contexts when
	 * we have a blist node with auto_open = TRUE or a chat
	 * already open. So we could pass the context between the two
	 * but that would be much more annoying to write/maintain */
	GHashTable *chat_contexts;

	/* key: gchar *screen_name,
	 * value: gchar *reply_id (then converted to long long)
	 * Store the id of last reply sent from any user to @me
	 * This is used as in_reply_to_status_id
	 * when @me sends a tweet to others */
	GHashTable *user_reply_id_table;

	gboolean requesting;
} TwitterConnectionData;

static long long purple_account_get_long_long(PurpleAccount *account, const gchar *key, long long default_value)
{
	const char* tmp_str;

	tmp_str = purple_account_get_string(account, key, NULL);
	if(tmp_str)
		return strtoll(tmp_str, NULL, 10);
	else
		return 0;
}

static void purple_account_set_long_long(PurpleAccount *account, const gchar *key, long long value)
{
	gchar* tmp_str;

	tmp_str = g_strdup_printf("%lld", value);
	purple_account_set_string(account, key, tmp_str);
	g_free(tmp_str);
}


//TODO: lots of copy and pasting here... let's refactor next
static long long twitter_account_get_last_home_timeline_id(PurpleAccount *account)
{
	return purple_account_get_long_long(account, "twitter_last_home_timeline_id", 0);
}

static void twitter_account_set_last_home_timeline_id(PurpleAccount *account, long long reply_id)
{
	purple_account_set_long_long(account, "twitter_last_home_timeline_id", reply_id);
}

static long long twitter_connection_get_last_home_timeline_id(PurpleConnection *gc)
{
	long long reply_id = 0;
	TwitterConnectionData *connection_data = gc->proto_data;
	reply_id = connection_data->last_home_timeline_id;
	return (reply_id ? reply_id : twitter_account_get_last_home_timeline_id(purple_connection_get_account(gc)));
}

static void twitter_connection_set_last_home_timeline_id(PurpleConnection *gc, long long reply_id)
{
	TwitterConnectionData *connection_data = gc->proto_data;

	connection_data->last_home_timeline_id = reply_id;
	twitter_account_set_last_home_timeline_id(purple_connection_get_account(gc), reply_id);
}

static long long twitter_account_get_last_reply_id(PurpleAccount *account)
{
	return purple_account_get_long_long(account, "twitter_last_reply_id", 0);
}

static void twitter_account_set_last_reply_id(PurpleAccount *account, long long reply_id)
{
	purple_account_set_long_long(account, "twitter_last_reply_id", reply_id);
}

static long long twitter_connection_get_last_reply_id(PurpleConnection *gc)
{
	long long reply_id = 0;
	TwitterConnectionData *connection_data = gc->proto_data;
	reply_id = connection_data->last_reply_id;
	return (reply_id ? reply_id : twitter_account_get_last_reply_id(purple_connection_get_account(gc)));
}

static void twitter_connection_set_last_reply_id(PurpleConnection *gc, long long reply_id)
{
	TwitterConnectionData *connection_data = gc->proto_data;

	connection_data->last_reply_id = reply_id;
	twitter_account_set_last_reply_id(purple_connection_get_account(gc), reply_id);
}

static void twitter_user_data_free(TwitterUserData *user_data)
{
	if (!user_data)
		return;
	if (user_data->name)
		g_free(user_data->name);
	if (user_data->screen_name)
		g_free(user_data->screen_name);
	if (user_data->profile_image_url)
		g_free(user_data->profile_image_url);
	if (user_data->description)
		g_free(user_data->description);
	g_free(user_data);
	user_data = NULL;
}

static void twitter_status_data_free(TwitterStatusData *status)
{
	if (status == NULL)
		return;

	if (status->text != NULL)
		g_free(status->text);
	status->text = NULL;

	if (status->in_reply_to_screen_name)
		g_free (status->in_reply_to_screen_name);
	status->in_reply_to_screen_name = NULL;

	g_free(status);
}

static TwitterBuddyData *twitter_buddy_get_buddy_data(PurpleBuddy *b)
{
	if (b->proto_data == NULL)
		b->proto_data = g_new0(TwitterBuddyData, 1);
	return b->proto_data;
}

static void twitter_buddy_update_icon_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message)
{
	PurpleBuddy *buddy = user_data;
	TwitterBuddyData *buddy_data = buddy->proto_data;
	if (buddy_data != NULL && buddy_data->user != NULL)
	{
		purple_buddy_icons_set_for_user(buddy->account, buddy->name,
				g_memdup(url_text, len), len, buddy_data->user->profile_image_url);
	}
}
static void twitter_buddy_update_icon(PurpleBuddy *buddy)
{
	const char *previous_url = purple_buddy_icons_get_checksum_for_user(buddy);
	TwitterBuddyData *twitter_buddy_data = buddy->proto_data;
	if (twitter_buddy_data == NULL || twitter_buddy_data->user == NULL)
	{
		return;
	} else {
		const char *url = twitter_buddy_data->user->profile_image_url;
		if (url == NULL)
		{
			purple_buddy_icons_set_for_user(buddy->account, buddy->name, NULL, 0, NULL);
		} else {
			if (!previous_url || !g_str_equal(previous_url, url)) {
				purple_util_fetch_url(url, TRUE, NULL, FALSE, twitter_buddy_update_icon_cb, buddy);
			}
		}
	}
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

//Taken mostly from blist.c
static PurpleChat *_twitter_blist_chat_find(PurpleAccount *account, TwitterChatType chat_type,
	const char *component_key, const char *component_value)
{
	const char *node_chat_name;
	gint node_chat_type = 0;
	const char *node_chat_type_str;
	PurpleChat *chat;
	PurpleBlistNode *node, *group;
	char *normname;
	PurpleBuddyList *purplebuddylist = purple_get_blist();
	GHashTable *components;

	g_return_val_if_fail(purplebuddylist != NULL, NULL);
	g_return_val_if_fail((component_value != NULL) && (*component_value != '\0'), NULL);

	normname = g_strdup(purple_normalize(account, component_value));
	purple_debug_info(TWITTER_PROTOCOL_ID, "Account %s searching for chat %s type %d\n",
		account->username,
		component_value == NULL ? "NULL" : component_value,
		chat_type);

	if (normname == NULL)
	{
		return NULL;
	}
	for (group = purplebuddylist->root; group != NULL; group = group->next) {
		for (node = group->child; node != NULL; node = node->next) {
			if (PURPLE_BLIST_NODE_IS_CHAT(node)) {

				chat = (PurpleChat*)node;

				if (account != chat->account)
					continue;

				components = purple_chat_get_components(chat);
				if (components != NULL)
				{
					node_chat_type_str = g_hash_table_lookup(components, "chat_type");
					node_chat_name = g_hash_table_lookup(components, component_key);
					node_chat_type = node_chat_type_str == NULL ? 0 : strtol(node_chat_type_str, NULL, 10);

					if (node_chat_name != NULL && node_chat_type == chat_type && !strcmp(purple_normalize(account, node_chat_name), normname))
					{
						g_free(normname);
						return chat;
					}
				}
			}
		}
	}

	g_free(normname);
	return NULL;
}

static PurpleChat *twitter_blist_chat_find_search(PurpleAccount *account, const char *name)
{
	return _twitter_blist_chat_find(account, TWITTER_CHAT_SEARCH, "search", name);
}
static PurpleChat *twitter_blist_chat_find_timeline(PurpleAccount *account, gint timeline_id)
{
	char *tmp = g_strdup_printf("%d", timeline_id);
	PurpleChat *chat = _twitter_blist_chat_find(account, TWITTER_CHAT_TIMELINE, "timeline_id", tmp);
	g_free(tmp);
	return chat;
}

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

PurpleChat *twitter_find_blist_chat(PurpleAccount *account, const char *name)
{
	static char *timeline = "Timeline: ";
	static char *search = "Search: ";
	PurpleChat *c = NULL;
	if (strlen(name) > strlen(timeline) && !strncmp(timeline, name, strlen(timeline)))
	{
		c = twitter_blist_chat_find_timeline(account, 0);
	} else if (strlen(name) > strlen(search) && !strncmp(search, name, strlen(search))) {
		c = twitter_blist_chat_find_search(account, name + strlen(search));
	} else {
		c = twitter_blist_chat_find_search(account, name);
	}
	return c;
}

static char *twitter_chat_name_from_search(const char *search)
{
	char *search_lower = g_utf8_strdown(search, -1);
	char *chat_name = g_strdup_printf("Search: %s", search_lower);
	g_free(search_lower);
	return chat_name;
}

static char *twitter_chat_name_from_timeline_id(const gint timeline_id)
{
	return g_strdup("Timeline: Home");
}
static char *twitter_get_chat_name(GHashTable *components) {
	const char *chat_type_str = g_hash_table_lookup(components, "chat_type");
	TwitterChatType chat_type = chat_type_str == NULL ? 0 : strtol(chat_type_str, NULL, 10);

	switch (chat_type)
	{
		case TWITTER_CHAT_TIMELINE:
			return twitter_chat_name_from_timeline_id(0);
			break;
		case TWITTER_CHAT_SEARCH:
			return twitter_chat_name_from_search((char *) g_hash_table_lookup(components, "search"));
			break;
		default:
			purple_debug_info(TWITTER_PROTOCOL_ID, "%s unknown chat_type %d\n", G_STRFUNC, chat_type);
			return g_strdup("Unknown");
			break;
	}
}         

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
static PurpleBuddy *twitter_buddy_new(PurpleAccount *account, const char *screenname, const char *alias)
{
	PurpleGroup *g;
	PurpleBuddy *b = purple_find_buddy(account, screenname);
	const char *group_name;
	if (b != NULL)
	{
		if (b->proto_data == NULL)
			b->proto_data = g_new0(TwitterBuddyData, 1);
		return b;
	}

	group_name = twitter_option_buddy_group(account);
	g = purple_find_group(group_name);
	if (g == NULL)
		g = purple_group_new(group_name);
	b = purple_buddy_new(account, screenname, alias);
	purple_blist_add_buddy(b, NULL, g, NULL);
	b->proto_data = g_new0(TwitterBuddyData, 1);
	return b;
}

static void twitter_buddy_set_user_data(PurpleAccount *account, TwitterUserData *u, gboolean add_missing_buddy)
{
	PurpleBuddy *b;
	TwitterBuddyData *buddy_data;
	if (!u || !account)
		return;

	if (!strcmp(u->screen_name, account->username))
	{
		twitter_user_data_free(u);
		return;
	}

	b = purple_find_buddy(account, u->screen_name);
	if (!b && add_missing_buddy)
	{
		/* set alias as screenname (name) */
		gchar *alias = g_strdup_printf ("%s | %s", u->name, u->screen_name);
		b = twitter_buddy_new(account, u->screen_name, alias);
		g_free (alias);
	}

	if (!b)
	{
		twitter_user_data_free(u);
		return;
	}

	buddy_data = twitter_buddy_get_buddy_data(b);

	if (buddy_data == NULL)
		return;
	if (buddy_data->user != NULL && u != buddy_data->user)
		twitter_user_data_free(buddy_data->user);
	buddy_data->user = u;
	twitter_buddy_update_icon(b);
}

#if _HAVE_PIDGIN_
static const char *_find_first_delimiter(const char *text, const char *delimiters, int *delim_id)
{
	const char *delimiter;
	if (text == NULL || text[0] == '\0')
		return NULL;
	do
	{
		for (delimiter = delimiters; *delimiter != '\0'; delimiter++)
		{
			if (*text == *delimiter)
			{
				if (delim_id != NULL)
					*delim_id = delimiter - delimiters;
				return text;
			}
		}
	} while (*++text != '\0');
	return NULL;
}
#endif

static const char *twitter_linkify(PurpleAccount *account, const char *message)
{
#if _HAVE_PIDGIN_
	GString *ret;
	static char symbols[] = "#@";
	static char *symbol_actions[] = {TWITTER_URI_ACTION_SEARCH, TWITTER_URI_ACTION_USER};
	static char delims[] = " :"; //I don't know if this is how I want to do this...
	const char *ptr = message;
	const char *end = message + strlen(message);
	const char *delim = NULL;
	g_return_val_if_fail(message != NULL, NULL);

	ret = g_string_new("");

	while (ptr != NULL && ptr < end)
	{
		const char *first_token = NULL;
		char *current_action = NULL;
		char *link_text = NULL;
		int symbol_index = 0;
		first_token = _find_first_delimiter(ptr, symbols, &symbol_index);
		if (first_token == NULL)
		{
			g_string_append(ret, ptr);
			break;
		}
		current_action = symbol_actions[symbol_index];
		g_string_append_len(ret, ptr, first_token - ptr);
		ptr = first_token;
		delim = _find_first_delimiter(ptr, delims, NULL);
		if (delim == NULL)
			delim = end;
		link_text = g_strndup(ptr, delim - ptr);
		//Added the 'a' before the account name because of a highlighting issue... ugly hack
		g_string_append_printf(ret, "<a href=\"" TWITTER_URI ":///%s?account=a%s&text=%s\">%s</a>",
				current_action,
				purple_account_get_username(account),
				purple_url_encode(link_text),
				purple_markup_escape_text(link_text, -1));
		ptr = delim;
	}

	return g_string_free(ret, FALSE);
#else
	return message;
#endif
}

static char *twitter_format_tweet(PurpleAccount *account, const char *src_user, const char *message, long long id)
{
	const char *linkified_message = twitter_linkify(account, message);
	gboolean add_link = twitter_option_add_link_to_tweet(account);

	g_return_val_if_fail(linkified_message != NULL, NULL);
	g_return_val_if_fail(src_user != NULL, NULL);

	if (add_link && id) {
		return g_strdup_printf("%s\nhttp://twitter.com/%s/status/%lld\n",
				linkified_message,
				src_user,
				id);
	}
	else {
		return g_strdup_printf("%s", linkified_message);
	}
}

static void twitter_status_data_update_conv(PurpleAccount *account,
		char *src_user,
		TwitterStatusData *s)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	gchar *tweet;

	if (!s || !s->text)
		return;

	if (s->id && s->id > twitter_connection_get_last_reply_id(gc))
	{
		twitter_connection_set_last_reply_id(gc, s->id);
	}
	tweet = twitter_format_tweet(account, src_user, s->text, s->id);

	//Account received an im
	/* TODO get in_reply_to_status? s->in_reply_to_screen_name
	 * s->in_reply_to_status_id */

	serv_got_im(gc, src_user,
			tweet,
			PURPLE_MESSAGE_RECV,
			s->created_at);

	g_free(tweet);
}

static void twitter_buddy_set_status_data(PurpleAccount *account, const char *src_user, TwitterStatusData *s)
{
	PurpleBuddy *b;
	TwitterBuddyData *buddy_data;
	gboolean status_text_same = FALSE;

	if (!s)
		return;

	if (!s->text)
	{
		twitter_status_data_free(s);
		return;
	}


	b = purple_find_buddy(account, src_user);
	if (!b)
	{
		twitter_status_data_free(s);
		return;
	}

	buddy_data = twitter_buddy_get_buddy_data(b);

	if (buddy_data->status && s->created_at < buddy_data->status->created_at)
	{
		twitter_status_data_free(s);
		return;
	}

	if (buddy_data->status != NULL && s != buddy_data->status)
	{
		status_text_same = (strcmp(buddy_data->status->text, s->text) == 0);
		twitter_status_data_free(buddy_data->status);
	}

	buddy_data->status = s;

	if (!status_text_same)
	{
		purple_prpl_got_user_status(b->account, b->name, TWITTER_STATUS_ONLINE,
				"message", s ? s->text : NULL, NULL);
	}
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
static gboolean twitter_get_friends_verify_error_cb(PurpleAccount *account,
		const TwitterRequestErrorData *error_data,
		gpointer user_data)
{
	twitter_verify_connection_error_handler(account, error_data);
	return FALSE;
}

/******************************************************
 *  Twitter friends
 ******************************************************/

static void twitter_buddy_datas_set_all(PurpleAccount *account, GList *buddy_datas)
{
	GList *l;
	for (l = buddy_datas; l; l = l->next)
	{
		TwitterBuddyData *data = l->data;
		TwitterUserData *user = data->user;
		TwitterStatusData *status = data->status;
		char *screen_name = g_strdup(user->screen_name);

		g_free(data);

		twitter_buddy_set_user_data(account, user, TRUE);
		if (status != NULL)
			twitter_buddy_set_status_data(account, screen_name, status);

		g_free(screen_name);
	}
	g_list_free(buddy_datas);
}

static void twitter_get_friends_cb(PurpleAccount *account, GList *nodes, gpointer user_data)
{
	GList *buddy_datas = twitter_users_nodes_parse(nodes);
	twitter_buddy_datas_set_all(account, buddy_datas);
}

static gboolean twitter_get_friends_timeout(gpointer data)
{
	PurpleAccount *account = data;
	//TODO handle errors
	twitter_api_get_friends(account, twitter_get_friends_cb, NULL, NULL);
	return TRUE;
}

/******************************************************
 *  Twitter relies/mentions
 ******************************************************/


static void _process_replies (PurpleAccount *account,
		GList *statuses,
		TwitterConnectionData *twitter)
{
	GList *l;

	for (l = statuses; l; l = l->next)
	{
		TwitterBuddyData *data = l->data;
		TwitterStatusData *status = data->status;
		TwitterUserData *user_data = data->user;
		g_free(data);

		if (!user_data)
		{
			twitter_status_data_free(status);
		} else {
			char *screen_name = g_strdup(user_data->screen_name);
			twitter_buddy_set_user_data(account, user_data, FALSE);
			twitter_status_data_update_conv(account, screen_name, status);
			twitter_buddy_set_status_data(account, screen_name, status);

			/* update user_reply_id_table table */
			gchar *reply_id = g_strdup_printf ("%lld", status->id);
			g_hash_table_insert (twitter->user_reply_id_table,
					g_strdup (screen_name), reply_id);
			g_free(screen_name);
		}
	}

	twitter->failed_get_replies_count = 0;
	twitter->requesting = FALSE;
}

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

	twitter->requesting = FALSE;
}

static void twitter_get_replies_cb (PurpleAccount *account,
		xmlnode *node,
		gpointer user_data)
{
	PurpleConnection *gc = purple_account_get_connection (account);
	TwitterConnectionData *twitter = gc->proto_data;

	GList *statuses = twitter_statuses_node_parse (node);
	_process_replies (account, statuses, twitter);

	g_list_free(statuses);
}

static gboolean twitter_get_replies_all_timeout_error_cb (PurpleAccount *account,
		const TwitterRequestErrorData *error_data,
		gpointer user_data)
{
	twitter_get_replies_timeout_error_cb (account, error_data, user_data);
	return FALSE;
}

static void twitter_chat_add_tweet(PurpleConvChat *chat, const char *who, const char *message, long long id, time_t time)
{
	gchar *tweet;
	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);
	g_return_if_fail(chat != NULL);
	g_return_if_fail(who != NULL);
	g_return_if_fail(message != NULL);

	tweet = twitter_format_tweet(
			purple_conversation_get_account(purple_conv_chat_get_conversation(chat)),
			who,
			message,
			id);
	if (!purple_conv_chat_find_user(chat, who))
	{
		purple_debug_info(TWITTER_PROTOCOL_ID, "added %s to chat %s\n",
				who,
				purple_conversation_get_name(purple_conv_chat_get_conversation(chat)));
		purple_conv_chat_add_user(chat,
				who,
				NULL,   /* user-provided join message, IRC style */
				PURPLE_CBFLAGS_NONE,
				FALSE);  /* show a join message */
	}
	purple_debug_info(TWITTER_PROTOCOL_ID, "message %s\n", message);
	serv_got_chat_in(purple_conversation_get_gc(purple_conv_chat_get_conversation(chat)),
			purple_conv_chat_get_id(chat),
			who,
			PURPLE_MESSAGE_RECV,
			tweet,
			time);
	g_free(tweet);
}

static PurpleConversation *twitter_chat_context_find_conv(TwitterConvChatContext *ctx)
{
	return purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, ctx->chat_name, ctx->account);
}

static gboolean twitter_chat_auto_open(PurpleChat *chat)
{
	g_return_val_if_fail(chat != NULL, FALSE);
	GHashTable *components = purple_chat_get_components(chat);
	char *auto_open = g_hash_table_lookup(components, "auto_open");
	return (auto_open != NULL && auto_open[0] != '0');
}

static gint twitter_get_next_chat_id()
{
	static gint chat_id = 1;
	return chat_id++;
}

static void twitter_get_home_timeline_parse_statuses(PurpleAccount *account,
		TwitterTimelineTimeoutContext *ctx, GList *statuses)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	PurpleConvChat *chat;
	PurpleConversation *conv;
	PurpleChat *blist_chat;
	GList *l;

	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);

	g_return_if_fail(account != NULL);

	//TODO combine this and twitter search cb. Lots of dup code
	conv = twitter_chat_context_find_conv(ctx->base);
	if (conv == NULL && statuses && (blist_chat = twitter_find_blist_chat(account, ctx->base->chat_name)))
	{
		if (twitter_chat_auto_open(blist_chat))
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "%s, recreated conv for auto open chat (%s)\n", G_STRFUNC, ctx->base->chat_name);
			guint chat_id = twitter_get_next_chat_id();
			conv = serv_got_joined_chat(purple_account_get_connection(account), chat_id, ctx->base->chat_name);
		}
	}
	g_return_if_fail (conv != NULL); //TODO: destroy context

	chat = PURPLE_CONV_CHAT(conv);

	for (l = statuses; l; l = l->next)
	{
		TwitterBuddyData *data = l->data;
		TwitterStatusData *status = data->status;
		TwitterUserData *user_data = data->user;
		g_free(data);

		if (!user_data)
		{
			twitter_status_data_free(status);
		} else {
			const char *screen_name = user_data->screen_name;
			const char *text = status->text;
			twitter_chat_add_tweet(chat, screen_name, text, status->id, status->created_at);
			if (status->id && status->id > twitter_connection_get_last_home_timeline_id(gc))
			{
				twitter_connection_set_last_home_timeline_id(gc, status->id);
			}
			twitter_buddy_set_status_data(account, screen_name, status);
			twitter_buddy_set_user_data(account, user_data, FALSE);

			/* update user_reply_id_table table */
			//gchar *reply_id = g_strdup_printf ("%lld", status->id);
			//g_hash_table_insert (twitter->user_reply_id_table,
					//g_strdup (screen_name), reply_id);
		}
	}
}

static void twitter_get_home_timeline_cb(PurpleAccount *account, xmlnode *node, gpointer user_data)
{
	TwitterTimelineTimeoutContext *ctx = (TwitterTimelineTimeoutContext *)user_data;

	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);

	GList *statuses = twitter_statuses_node_parse(node);
	twitter_get_home_timeline_parse_statuses(account, ctx, statuses);
	g_list_free(statuses);

}

static void twitter_get_home_timeline_all_cb(PurpleAccount *account,
		GList *nodes,
		gpointer user_data)
{
	TwitterTimelineTimeoutContext *ctx = (TwitterTimelineTimeoutContext *)user_data;

	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);

	GList *statuses = twitter_statuses_nodes_parse(nodes);
	twitter_get_home_timeline_parse_statuses(account, ctx, statuses);
	g_list_free(statuses);
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

static gboolean twitter_get_replies_timeout (gpointer data)
{
	PurpleAccount *account = data;
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = gc->proto_data;

	if (!twitter->requesting) {
		twitter->requesting = TRUE;
		twitter_api_get_replies_all(account,
				twitter_connection_get_last_reply_id(purple_account_get_connection(account)),
				twitter_get_replies_all_cb, twitter_get_replies_all_timeout_error_cb,
				NULL);
	}
	return TRUE;
}


/******************************************************
 *  Twitter search
 ******************************************************/
static void twitter_conv_chat_context_free(TwitterConvChatContext *ctx)
{
	PurpleConnection *gc;
	TwitterConnectionData *twitter;
	gc = purple_account_get_connection(ctx->account);
	twitter = gc->proto_data;

	g_hash_table_remove(twitter->chat_contexts, ctx->chat_name);
	if (ctx->timer_handle)
	{
		purple_timeout_remove(ctx->timer_handle);
		ctx->timer_handle = 0;
	}
	if (ctx->chat_name)
	{
		g_free(ctx->chat_name);
		ctx->chat_name = NULL;
	}
	g_slice_free(TwitterConvChatContext, ctx);
}

static void twitter_search_timeout_context_free(TwitterSearchTimeoutContext *ctx)
{
	g_return_if_fail(ctx != NULL);

	twitter_conv_chat_context_free(ctx->base);

	ctx->last_tweet_id = 0;

	g_free (ctx->search_text);
	ctx->search_text = NULL;

	g_free (ctx->refresh_url);
	ctx->refresh_url = NULL;

	g_slice_free (TwitterSearchTimeoutContext, ctx);
} 

static void twitter_timeline_timeout_context_free(TwitterTimelineTimeoutContext *ctx)
{
	g_return_if_fail(ctx != NULL);

	twitter_conv_chat_context_free(ctx->base);

	g_slice_free (TwitterTimelineTimeoutContext, ctx);
} 

static void twitter_search_cb(PurpleAccount *account,
		const GArray *search_results,
		const gchar *refresh_url,
		long long max_id,
		gpointer user_data)
{
	TwitterSearchTimeoutContext *ctx = (TwitterSearchTimeoutContext *)user_data;
	gint i, len = search_results->len;
	PurpleConversation *conv;
	PurpleConvChat *chat;
	PurpleChat *blist_chat; //PurpleChat and PurpleConvChat... that won't get confusing

	g_return_if_fail (ctx != NULL);
	//
	//TODO DEBUG stuff

	conv = twitter_chat_context_find_conv(ctx->base);
	if (conv == NULL && len && (blist_chat = twitter_find_blist_chat(account, ctx->base->chat_name)))
	{
		if (twitter_chat_auto_open(blist_chat))
		{
			purple_debug_info(TWITTER_PROTOCOL_ID, "%s, recreated conv for auto open chat (%s)\n", G_STRFUNC, ctx->base->chat_name);
			guint chat_id = twitter_get_next_chat_id();
			conv = serv_got_joined_chat(purple_account_get_connection(account), chat_id, ctx->base->chat_name);
		}
	}
	g_return_if_fail (conv != NULL); //destroy context

	chat = PURPLE_CONV_CHAT(conv);

	for (i = len-1; i >= 0; i--) {
		TwitterSearchData *search_data;

		search_data = g_array_index (search_results,
				TwitterSearchData *, i);

		twitter_chat_add_tweet(chat, search_data->from_user, search_data->text, search_data->id, search_data->created_at);
	}

	ctx->last_tweet_id = max_id;
	g_free (ctx->refresh_url);
	ctx->refresh_url = g_strdup (refresh_url);
}

static gboolean twitter_search_timeout(gpointer data)
{
	TwitterSearchTimeoutContext *ctx = (TwitterSearchTimeoutContext *)data;

	if (ctx->refresh_url) {
		purple_debug_info(TWITTER_PROTOCOL_ID, "%s, refresh_url exists: %s\n",
				G_STRFUNC, ctx->refresh_url);

		twitter_api_search_refresh(ctx->base->account, ctx->refresh_url,
				twitter_search_cb, NULL, ctx);
	}
	else {
		gchar *refresh_url;

		refresh_url = g_strdup_printf ("?q=%s&since_id=%lld",
				purple_url_encode(ctx->search_text), ctx->last_tweet_id);

		purple_debug_info(TWITTER_PROTOCOL_ID, "%s, create refresh_url: %s\n",
				G_STRFUNC, refresh_url);

		twitter_api_search_refresh (ctx->base->account, refresh_url,
				twitter_search_cb, NULL, ctx);

		g_free (refresh_url);
	}

	return TRUE;
}

static int twitter_chat_timeline_send(TwitterConvChatContext *ctx_base, const gchar *message)
{
	PurpleAccount *account = ctx_base->account;
	PurpleConversation *conv = twitter_chat_context_find_conv(ctx_base);

	if (conv == NULL) return -1; //TODO: error?

	if (strlen(message) > MAX_TWEET_LENGTH)
	{
		//TODO: SHOW ERROR
		return -1;
	}
	else
	{
		twitter_api_set_status(account,
				message, 0,
				NULL, NULL,//TODO: verify & error
				NULL);
		twitter_chat_add_tweet(PURPLE_CONV_CHAT(conv), account->username, message, 0, time(NULL));//TODO: FIX TIME
		return 0;
	}
}
static int twitter_chat_search_send(TwitterConvChatContext *ctx_base, const gchar *message)
{
	PurpleAccount *account = ctx_base->account;
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterSearchTimeoutContext *ctx = (TwitterSearchTimeoutContext *) ctx_base->endpoint_data;
	PurpleConversation *conv = twitter_chat_context_find_conv(ctx_base);
	char *status;
	char *message_lower, *search_text_lower;

	if (conv == NULL) return -1; //TODO: error?

	//TODO: verify that we want utf8 case insensitive, and not ascii
	message_lower = g_utf8_strdown(message, -1);
	search_text_lower = g_utf8_strdown(ctx->search_text, -1);
	if (strstr(message_lower, search_text_lower))
	{
		status = g_strdup(message);
	} else {
		status = g_strdup_printf("%s %s", ctx->search_text, message);
	}
	g_free(message_lower);
	g_free(search_text_lower);

	if (strlen(status) > MAX_TWEET_LENGTH)
	{
		//TODO: SHOW ERROR
		g_free(status);
		return -1;
	}
	else
	{
		PurpleAccount *account = purple_connection_get_account(gc);
		twitter_api_set_status(purple_connection_get_account(gc),
				status, 0,
				NULL, NULL,//TODO: verify & error
				NULL);
		twitter_chat_add_tweet(PURPLE_CONV_CHAT(conv), account->username, status, 0, time(NULL));//TODO: FIX TIME
		g_free(status);
		return 0;
	}
}

static TwitterConvChatContext *twitter_conv_chat_context_new(
	TwitterChatType type, PurpleAccount *account, const gchar *chat_name,
	gpointer endpoint_data)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = gc->proto_data;

	TwitterConvChatContext *ctx = g_slice_new0(TwitterConvChatContext);
	ctx->type = type;
	ctx->account = account;
	ctx->chat_name = g_strdup(chat_name);
	ctx->endpoint_data = endpoint_data;

	g_hash_table_insert(twitter->chat_contexts, g_strdup(chat_name), ctx);

	return ctx;
}

static TwitterSearchTimeoutContext *twitter_search_timeout_context_new(PurpleAccount *account,
		const char *search_text, const gchar *chat_name)
{
	TwitterSearchTimeoutContext *ctx = g_slice_new0(TwitterSearchTimeoutContext);

	ctx->base = twitter_conv_chat_context_new(TWITTER_CHAT_SEARCH, account, chat_name, ctx);
	ctx->search_text = g_strdup(search_text);
	return ctx;
}

static TwitterTimelineTimeoutContext *twitter_timeline_timeout_context_new(PurpleAccount *account,
		guint timeline_id, const gchar *chat_name)
{
	TwitterTimelineTimeoutContext *ctx = g_slice_new0(TwitterTimelineTimeoutContext);

	ctx->base = twitter_conv_chat_context_new(TWITTER_CHAT_TIMELINE, account, chat_name, ctx);
	ctx->timeline_id = timeline_id;
	return ctx;
}

static void get_saved_searches_cb (PurpleAccount *account,
		xmlnode *node,
		gpointer user_data)
{
	xmlnode *search;

	purple_debug_info (TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);

	for (search = node->child; search; search = search->next) {
		if (search->name && !g_strcmp0 (search->name, "saved_search")) {
			gchar *query = xmlnode_get_child_data (search, "query");

			twitter_blist_chat_new(account, query);
			g_free (query);
		}
	}
}

static void twitter_chat_context_endpoint_free(TwitterChatType type, gpointer data)
{
	//I don't particularly like this...
	g_return_if_fail(data != NULL);
	switch (type)
	{
		case TWITTER_CHAT_SEARCH:
			twitter_search_timeout_context_free((TwitterSearchTimeoutContext *)data);
			break;
		case TWITTER_CHAT_TIMELINE:
			twitter_timeline_timeout_context_free((TwitterTimelineTimeoutContext *)data);
			break;
		default:
			purple_debug_info(TWITTER_PROTOCOL_ID, "Unknown chat type %d\n", type);
	}
}

static void twitter_chat_leave(PurpleConnection *gc, int id) {
	PurpleConversation *conv = purple_find_chat(gc, id);
	TwitterConnectionData *twitter = gc->proto_data;
	TwitterConvChatContext *ctx = (TwitterConvChatContext *) g_hash_table_lookup(twitter->chat_contexts, purple_conversation_get_name(conv));

	g_return_if_fail(ctx != NULL);

	PurpleChat *blist_chat = twitter_find_blist_chat(purple_connection_get_account(gc), ctx->chat_name);
	if (blist_chat != NULL && twitter_chat_auto_open(blist_chat))
	{
		return;
	}

	twitter_chat_context_endpoint_free(ctx->type, ctx->endpoint_data);
}

static int twitter_chat_send(PurpleConnection *gc, int id, const char *message,
		PurpleMessageFlags flags) {
	PurpleConversation *conv = purple_find_chat(gc, id);
	TwitterConnectionData *twitter = gc->proto_data;
	TwitterConvChatContext *ctx = (TwitterConvChatContext *) g_hash_table_lookup(twitter->chat_contexts, purple_conversation_get_name(conv));

	g_return_val_if_fail(ctx != NULL, -1);

	switch (ctx->type)
	{
		case TWITTER_CHAT_SEARCH:
			return twitter_chat_search_send(ctx, message);
			break;
		case TWITTER_CHAT_TIMELINE:
			return twitter_chat_timeline_send(ctx, message);
			break;
		default:
			purple_debug_info(TWITTER_PROTOCOL_ID, "Unknown chat type %d\n", ctx->type);
			return 0;
	}
}

static TwitterConvChatContext *twitter_find_chat_context(PurpleAccount *account, const char *chat_name)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = gc->proto_data;
	return (TwitterConvChatContext *) g_hash_table_lookup(twitter->chat_contexts, chat_name);
}
static gpointer twitter_find_chat_context_endpoint_data(PurpleAccount *account, const char *chat_name)
{
	TwitterConvChatContext *ctx_base = twitter_find_chat_context(account, chat_name);
	if (!ctx_base)
		return NULL;
	return ctx_base->endpoint_data;
}

static void twitter_chat_search_join(PurpleConnection *gc, const char *search, int interval)
{
	PurpleAccount *account = purple_connection_get_account(gc);
        int default_interval = twitter_option_search_timeout(purple_connection_get_account(gc));

	if (search == NULL || search[0] == '\0')
	{
		purple_notify_info(gc,  /* plugin handle or PurpleConnection */
				("Chat Join"),
				("Invalid Search"),
				("Search must be filled in when joining a search chat"));
		return;
	}

        purple_debug_info(TWITTER_PROTOCOL_ID, "%s is performing search %s\n", gc->account->username, search);

        if (interval < 1)
                interval = default_interval;

	char *chat_name = twitter_chat_name_from_search(search);

        if (!purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, chat_name, account)) {
                guint chat_id = twitter_get_next_chat_id();
                serv_got_joined_chat(gc, chat_id, chat_name);
		if (!twitter_find_chat_context(account, chat_name))
		{
			TwitterSearchTimeoutContext *ctx = twitter_search_timeout_context_new(account,
					search, chat_name);

			twitter_api_search(account,
					search, ctx->last_tweet_id,
					TWITTER_SEARCH_RPP_DEFAULT,
					twitter_search_cb, NULL, ctx);


			ctx->base->timer_handle = purple_timeout_add_seconds(
					60 * interval,
					twitter_search_timeout, ctx);
		}
        } else {
                purple_debug_info(TWITTER_PROTOCOL_ID, "Search %s is already open.", search);
        }
	g_free(chat_name);
}
static void twitter_chat_search_join_components(PurpleConnection *gc, GHashTable *components) {
        const char *search = g_hash_table_lookup(components, "search");
        const char *interval_str = g_hash_table_lookup(components, "interval");
        int interval = 0;

        interval = interval_str == NULL ? 0 : strtol(interval_str, NULL, 10);
	twitter_chat_search_join(gc, search, interval);
}

static gboolean twitter_timeline_timeout(gpointer data)
{
	TwitterTimelineTimeoutContext *ctx = (TwitterTimelineTimeoutContext *)data;
	PurpleAccount *account = ctx->base->account;
	PurpleConnection *gc = purple_account_get_connection(account);
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
				ctx);
	} else {
		purple_debug_info(TWITTER_PROTOCOL_ID, "Retrieving %s statuses since %lld\n", account->username, since_id);
		twitter_api_get_home_timeline_all(account,
				since_id,
				twitter_get_home_timeline_all_cb,
				NULL,
				ctx);
	}

	return TRUE;
}

static void twitter_chat_timeline_join(PurpleConnection *gc, GHashTable *components) {
	PurpleAccount *account = purple_connection_get_account(gc);
        const char *interval_str = g_hash_table_lookup(components, "interval");
	guint timeline_id = 0;
        int interval = 0;
        int default_interval = twitter_option_timeline_timeout(account);

        interval = strtol(interval_str, NULL, 10);
        if (interval < 1)
                interval = default_interval;

	purple_debug_info(TWITTER_PROTOCOL_ID, "%s is joined timeline\n", account->username);

	char *chat_name = twitter_chat_name_from_timeline_id(timeline_id);
        if (!purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, chat_name, account)) {
		guint chat_id = twitter_get_next_chat_id();
		serv_got_joined_chat(gc, chat_id, chat_name);
		if (!twitter_find_chat_context(account, chat_name))
		{
			TwitterTimelineTimeoutContext *ctx = twitter_timeline_timeout_context_new(
					account, timeline_id, chat_name);


			long long since_id = twitter_connection_get_last_home_timeline_id(gc);

			purple_debug_info(TWITTER_PROTOCOL_ID, "%s creating new timeline context\n", account->username);
			//TODO: free

			if (since_id == 0)
			{
				purple_debug_info(TWITTER_PROTOCOL_ID, "Retrieving %s statuses for first time\n", gc->account->username);
				twitter_api_get_home_timeline(account,
						since_id,
						20,
						1,
						twitter_get_home_timeline_cb,
						NULL,
						ctx);
			} else {
				purple_debug_info(TWITTER_PROTOCOL_ID, "Retrieving %s statuses since %lld\n", gc->account->username, since_id);
				twitter_api_get_home_timeline_all(account,
						since_id,
						twitter_get_home_timeline_all_cb,
						NULL,
						ctx);
			}
			//TODO: I don't think this timer should be here, but for the time being...
			ctx->base->timer_handle = purple_timeout_add_seconds(
					60 * interval,
					twitter_timeline_timeout, ctx);
		} else {
			purple_debug_info(TWITTER_PROTOCOL_ID, "%s using previous timeline context\n", account->username);
		}
		/*twitter_api_search(account,
				search, ctx->last_tweet_id,
				TWITTER_SEARCH_RPP_DEFAULT,
				twitter_search_cb, NULL, ctx);*/


		/*ctx->timer_handle = purple_timeout_add_seconds(
				60 * interval,
				twitter_search_timeout, ctx);*/
	} else {
		purple_debug_info(TWITTER_PROTOCOL_ID, "Timeline %d is already open.\n", timeline_id);
	}
	g_free(chat_name);

}
static void twitter_chat_join(PurpleConnection *gc, GHashTable *components) {
	const char *conv_type_str = g_hash_table_lookup(components, "chat_type");
	gint conv_type = conv_type_str == NULL ? 0 : strtol(conv_type_str, NULL, 10);
	switch (conv_type)
	{
		case TWITTER_CHAT_SEARCH:
			twitter_chat_search_join_components(gc, components);
			break;
		case TWITTER_CHAT_TIMELINE:
			twitter_chat_timeline_join(gc, components);
			break;
		default:
			purple_debug_info(TWITTER_PROTOCOL_ID, "Unknown chat type %d\n", conv_type);
	}
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

static void twitter_connected(PurpleAccount *account)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	TwitterConnectionData *twitter = gc->proto_data;

	purple_connection_update_progress(gc, "Connected",
			2,   /* which connection step this is */
			3);  /* total number of steps */
	purple_connection_set_state(gc, PURPLE_CONNECTED);

	twitter_blist_chat_timeline_new(account, 0);

	/* Retrieve user's saved search queries */
	twitter_api_get_saved_searches (account,
			get_saved_searches_cb, NULL, NULL);

	/* We want to retrieve all mentions/replies since
	 * last reply we have retrieved and stored locally */
	twitter_connection_set_last_reply_id(gc,
			twitter_account_get_last_reply_id(account));

	/* Immediately retrieve replies */
	twitter->requesting = TRUE;
	twitter_api_get_replies (account,
			twitter_connection_get_last_reply_id(purple_account_get_connection(account)),
			TWITTER_INITIAL_REPLIES_COUNT, 1,
			twitter_get_replies_cb,
			twitter_get_replies_timeout_error_cb,
			NULL);

	/* Install periodic timers to retrieve replies and friend list */
	twitter->get_replies_timer = purple_timeout_add_seconds(
			60 * twitter_option_replies_timeout(account),
			twitter_get_replies_timeout, account);

	int get_friends_timer_timeout = twitter_option_user_status_timeout(account);
	gboolean get_following = twitter_option_get_following(account);

	/* Only update the buddy list if the user set the timeout to a positive number
	 * and the user wants to retrieve his following list */
	if (get_friends_timer_timeout > 0 && get_following)
	{
		twitter->get_friends_timer = purple_timeout_add_seconds(
				60 * get_friends_timer_timeout,
				twitter_get_friends_timeout, account);
	} else {
		twitter->get_friends_timer = 0;
	}
}
static void twitter_get_friends_verify_connection_cb(PurpleAccount *account,
		GList *nodes,
		gpointer user_data)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	GList *l_users_data = NULL;

	if (purple_connection_get_state(gc) == PURPLE_CONNECTING)
	{
		twitter_connected(account);

		l_users_data = twitter_users_nodes_parse(nodes);

		/* setup buddy list */
		twitter_buddy_datas_set_all(account, l_users_data);

	}
}

static void twitter_get_rate_limit_status_cb(PurpleAccount *account, xmlnode *node, gpointer user_data)
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
static const char *twitter_list_icon(PurpleAccount *acct, PurpleBuddy *buddy)
{
	/* shamelessly steal (er, borrow) the meanwhile protocol icon. it's cute! */
	return "meanwhile";
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
		gboolean full) {

	if (PURPLE_BUDDY_IS_ONLINE(buddy))
	{
		PurplePresence *presence = purple_buddy_get_presence(buddy);
		PurpleStatus *status = purple_presence_get_active_status(presence);
		char *msg = twitter_status_text(buddy);
		purple_notify_user_info_add_pair(info, purple_status_get_name(status),
				msg);
		g_free(msg);

		if (full) {
			/*const char *user_info = purple_account_get_user_info(gc->account);
			  if (user_info)
			  purple_notify_user_info_add_pair(info, _("User info"), user_info);*/
		}

		purple_debug_info(TWITTER_PROTOCOL_ID, "showing %s tooltip for %s\n",
				(full) ? "full" : "short", buddy->name);
	}
}

/* everyone will be online
 * Future: possibly have offline mode for people who haven't updated in a while
 */
static GList *twitter_status_types(PurpleAccount *acct)
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
	types = g_list_prepend(types, type);

	return g_list_reverse(types);
}


/*
 * UI callbacks
 */
static void twitter_action_get_user_info(PurplePluginAction *action)
{
	PurpleConnection *gc = (PurpleConnection *)action->context;
	PurpleAccount *acct = purple_connection_get_account(gc);
	twitter_api_get_friends(acct, twitter_get_friends_cb, NULL, NULL);
}

static void twitter_request_id_ok(PurpleConnection *gc, PurpleRequestFields *fields)
{
	PurpleAccount *acct = purple_connection_get_account(gc);
	TwitterConnectionData *twitter = gc->proto_data;
	const char* str = purple_request_fields_get_string(fields, "id");
	long long id = 0;
	if(str)
		id = strtoll(str, NULL, 10);

	if (!twitter->requesting) {
		twitter->requesting = TRUE;
		twitter_api_get_replies_all(acct,
				id, twitter_get_replies_all_cb,
				twitter_get_replies_all_timeout_error_cb, NULL);
	}
}
static void twitter_action_get_replies(PurplePluginAction *action)
{
	PurpleRequestFields *request;
	PurpleRequestFieldGroup *group;
	PurpleRequestField *field;
	PurpleConnection *gc = (PurpleConnection *)action->context;

	group = purple_request_field_group_new(NULL);

	gchar* str = g_strdup_printf("%lld", twitter_connection_get_last_reply_id(gc));
	field = purple_request_field_string_new("id", ("Minutes"), str, FALSE);
	g_free(str);
	purple_request_field_group_add_field(group, field);

	request = purple_request_fields_new();
	purple_request_fields_add_group(request, group);

	purple_request_fields(action->plugin,
			("Timeline"),
			("Set last time"),
			NULL,
			request,
			("_Set"), G_CALLBACK(twitter_request_id_ok),
			("_Cancel"), NULL,
			purple_connection_get_account(gc), NULL, NULL,
			gc);
}
static void twitter_action_set_status_ok(PurpleConnection *gc, PurpleRequestFields *fields)
{
	PurpleAccount *acct = purple_connection_get_account(gc);
	const char* status = purple_request_fields_get_string(fields, "status");
	purple_account_set_status(acct, TWITTER_STATUS_ONLINE, TRUE, "message", status, NULL);
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
	PurpleAccount *acct = purple_connection_get_account(gc);
	twitter_api_get_rate_limit_status(acct, twitter_get_rate_limit_status_cb, NULL, NULL);
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

	action = purple_plugin_action_new("Debug - Retrieve replies", twitter_action_get_replies);
	l = g_list_append(l, action);

	return l;
}

static void twitter_get_replies_verify_connection_cb(PurpleAccount *acct, xmlnode *node, gpointer user_data)
{
	PurpleConnection *gc = purple_account_get_connection(acct);

	if (purple_connection_get_state(gc) == PURPLE_CONNECTING)
	{
		long long id = 0;
		xmlnode *status_node = xmlnode_get_child(node, "status");
		if (status_node != NULL)
		{
			TwitterStatusData *status_data = twitter_status_node_parse(status_node);
			if (status_data != NULL)
			{
				id = status_data->id;

				twitter_status_data_free(status_data);
				if (id > twitter_account_get_last_reply_id(acct))
					twitter_account_set_last_reply_id(acct, id);
			}
		}

		purple_connection_update_progress(gc, "Connecting...",
				1,   /* which connection step this is */
				3);  /* total number of steps */

	}

	if (twitter_option_get_following(acct))
	{
		twitter_api_get_friends(acct,
				twitter_get_friends_verify_connection_cb,
				twitter_get_friends_verify_error_cb,
				NULL);
	} else {
		twitter_connected(acct);
		twitter_set_all_buddies_online(acct);
	}
}

static void twitter_get_replies_verify_connection_error_cb(PurpleAccount *acct, const TwitterRequestErrorData *error_data, gpointer user_data)
{
	twitter_verify_connection_error_handler(acct, error_data);
}

static void twitter_verify_connection(PurpleAccount *acct)
{
	gboolean retrieve_history;

	//To verify the connection, we get the user's friends.
	//With that we'll update the buddy list and set the last known reply id

	/* If history retrieval enabled, read last reply id from config file.
	 * There's no config file, just set last reply id to 0 */
	retrieve_history = twitter_option_get_history(acct);

	//If we don't have a stored last reply id, we don't want to get the entire history (EVERY reply)
	if (retrieve_history && twitter_account_get_last_reply_id(acct) != 0) {
		PurpleConnection *gc = purple_account_get_connection(acct);

		if (purple_connection_get_state(gc) == PURPLE_CONNECTING) {

			purple_connection_update_progress(gc, "Connecting...",
					1,   /* which connection step this is */
					3);  /* total number of steps */
		}

		if (twitter_option_get_following(acct))
		{
			twitter_api_get_friends(acct,
					twitter_get_friends_verify_connection_cb,
					twitter_get_friends_verify_error_cb,
					NULL);
		} else {
			twitter_connected(acct);
			twitter_set_all_buddies_online(acct);
		}
	}
	else {
		/* Simply get the last reply */
		twitter_api_get_replies(acct,
				0, 1, 1,
				twitter_get_replies_verify_connection_cb,
				twitter_get_replies_verify_connection_error_cb,
				NULL);
	}
}

static void twitter_login(PurpleAccount *acct)
{
	PurpleConnection *gc = purple_account_get_connection(acct);
	TwitterConnectionData *twitter = g_new0(TwitterConnectionData, 1);
	gc->proto_data = twitter;

	purple_debug_info(TWITTER_PROTOCOL_ID, "logging in %s\n", acct->username);

	/* key: gchar *, value: TwitterConvChatContext */
	twitter->chat_contexts = g_hash_table_new_full(
			g_str_hash, g_str_equal, g_free, NULL);


	/* key: gchar *, value: gchar * (of a long long) */
	twitter->user_reply_id_table = g_hash_table_new_full (
			g_str_hash, g_str_equal, g_free, g_free);

	/* purple wants a minimum of 2 steps */
	purple_connection_update_progress(gc, ("Connecting"),
			0,   /* which connection step this is */
			2);  /* total number of steps */


	twitter_verify_connection(acct);
}

static void twitter_close(PurpleConnection *gc)
{
	/* notify other twitter accounts */
	TwitterConnectionData *twitter = gc->proto_data;

	if (twitter->get_replies_timer)
		purple_timeout_remove(twitter->get_replies_timer);

	if (twitter->get_friends_timer)
		purple_timeout_remove(twitter->get_friends_timer);

	//TODO XXX clear out all chat_contexts

	if (twitter->chat_contexts)
		g_hash_table_destroy(twitter->chat_contexts);
	twitter->chat_contexts = NULL;

	if (twitter->user_reply_id_table)
		g_hash_table_destroy (twitter->user_reply_id_table);
	twitter->user_reply_id_table = NULL;


	g_free(twitter);
}

static void twitter_set_status_error_cb(PurpleAccount *acct, const TwitterRequestErrorData *error_data, gpointer user_data)
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

static void twitter_send_dm_error_cb(PurpleAccount *acct, const TwitterRequestErrorData *error_data, gpointer user_data)
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
			("Twitter Send Direct Message"),
			("Error sending Direct Message"),
			(message));
}

static void twitter_send_dm_cb(PurpleAccount *account, xmlnode *node, gpointer user_data)
{
	//TODO: verify dm was sent
	return;
}
static void twitter_send_im_cb(PurpleAccount *account, xmlnode *node, gpointer user_data)
{
	//TODO: verify status was sent
	return;
	/*
	   TwitterStatusData *s = twitter_status_node_parse(node);
	   xmlnode *user_node = xmlnode_get_child(node, "user");
	   TwitterUserData *u = twitter_user_node_parse(user_node);

	   if (!s)
	   return;

	   if (!u)
	   {
	   twitter_status_data_free(s);
	   return;
	   }

	   twitter_buddy_set_status_data(account, u->screen_name, s, FALSE);
	   twitter_user_data_free(u);
	 */
}

static int twitter_send_dm(PurpleConnection *gc, const char *who,
		const char *message, PurpleMessageFlags flags)
{
	if (strlen(message) > MAX_TWEET_LENGTH)
	{
		purple_conv_present_error(who, purple_connection_get_account(gc), "Message is too long");
		return 0;
	}
	else
	{
		//TODO handle errors
		twitter_api_send_dm(purple_connection_get_account(gc),
				who, message, 
				twitter_send_dm_cb, twitter_send_dm_error_cb,
				NULL);
		return 1;
	}
}
static int twitter_send_im(PurpleConnection *gc, const char *who,
		const char *message, PurpleMessageFlags flags)
{
	/* TODO should truncate it rather than drop it????? */
	if (strlen(who) + strlen(message) + 2 > MAX_TWEET_LENGTH)
	{
		purple_conv_present_error(who, purple_connection_get_account(gc), "Message is too long");
		return 0;
	}
	else
	{
		TwitterConnectionData *twitter = gc->proto_data;
		long long in_reply_to_status_id = 0;
		const gchar *reply_id;
		char *status;

		status = g_strdup_printf("@%s %s", who, message);
		reply_id = (const gchar *)g_hash_table_lookup (
				twitter->user_reply_id_table, who);
		if (reply_id)
			in_reply_to_status_id = strtoll (reply_id, NULL, 10);

		//TODO handle errors
		twitter_api_set_status(purple_connection_get_account(gc),
				status, in_reply_to_status_id, twitter_send_im_cb,
				twitter_set_status_error_cb, NULL);
		g_free(status);
		return 1;
	}
	//  const char *from_username = gc->account->username;
	//  PurpleMessageFlags receive_flags = ((flags & ~PURPLE_MESSAGE_SEND)
	//				      | PURPLE_MESSAGE_RECV);
	//  PurpleAccount *to_acct = purple_accounts_find(who, TWITTER_PROTOCOL_ID);
	//  PurpleConnection *to;
	//
	//  purple_debug_info(TWITTER_PROTOCOL_ID, "sending message from %s to %s: %s\n",
	//		    from_username, who, message);
	//
	//  /* is the sender blocked by the recipient's privacy settings? */
	//  if (to_acct && !purple_privacy_check(to_acct, gc->account->username)) {
	//    char *msg = g_strdup_printf(
	//      _("Your message was blocked by %s's privacy settings."), who);
	//    purple_debug_info(TWITTER_PROTOCOL_ID,
	//		      "discarding; %s is blocked by %s's privacy settings\n",
	//		      from_username, who);
	//    purple_conv_present_error(who, gc->account, msg);
	//    g_free(msg);
	//    return 0;
	//  }
	//
	//  /* is the recipient online? */
	//  to = get_twitter_gc(who);
	//  if (to) {  /* yes, send */
	//  PurpleMessageFlags receive_flags = ((flags & ~PURPLE_MESSAGE_SEND)
	//				      | PURPLE_MESSAGE_RECV);
	//    serv_got_im(to, from_username, message, receive_flags, time(NULL));
	//
	//  } else {  /* nope, store as an offline message */
	//    GOfflineMessage *offline_message;
	//    GList *messages;
	//
	//    purple_debug_info(TWITTER_PROTOCOL_ID,
	//		      "%s is offline, sending as offline message\n", who);
	//    offline_message = g_new0(GOfflineMessage, 1);
	//    offline_message->from = g_strdup(from_username);
	//    offline_message->message = g_strdup(message);
	//    offline_message->mtime = time(NULL);
	//    offline_message->flags = receive_flags;
	//
	//    messages = g_hash_table_lookup(goffline_messages, who);
	//    messages = g_list_append(messages, offline_message);
	//    g_hash_table_insert(goffline_messages, g_strdup(who), messages);
	//  }
	//
	//   return 1;
}

static void twitter_set_info(PurpleConnection *gc, const char *info) {
	purple_debug_info(TWITTER_PROTOCOL_ID, "setting %s's user info to %s\n",
			gc->account->username, info);
}

static void twitter_get_info(PurpleConnection *gc, const char *username) {
	//TODO: error check
	//TODO: fix for buddy not on list?
	PurpleNotifyUserInfo *info = purple_notify_user_info_new();
	PurpleBuddy *b = purple_find_buddy(purple_connection_get_account(gc), username);

	if (b)
	{
		TwitterBuddyData *data = twitter_buddy_get_buddy_data(b);
		if (data)
		{
			TwitterUserData *user_data = data->user;
			TwitterStatusData *status_data = data->status;


			if (user_data)
			{
				purple_notify_user_info_add_pair(info, "Description:", user_data->description);
			}
			if (status_data)
			{
				purple_notify_user_info_add_pair(info, "Status:", status_data->text);
			}

			/* show a buddy's user info in a nice dialog box */
			purple_notify_userinfo(gc,	/* connection the buddy info came through */
					username,  /* buddy's username */
					info,      /* body */
					NULL,      /* callback called when dialog closed */
					NULL);     /* userdata for callback */
			return;
		}
	}
	purple_notify_user_info_add_pair(info, "Description:", "No user info");
	purple_notify_userinfo(gc,
		username,
		info,
		NULL,
		NULL);

}


static void twitter_set_status(PurpleAccount *acct, PurpleStatus *status) {
	gboolean sync_status = twitter_option_sync_status(acct);
	if (!sync_status)
		return ;

	//TODO: I'm pretty sure this is broken
	const char *msg = purple_status_get_attr_string(status, "message");
	purple_debug_info(TWITTER_PROTOCOL_ID, "setting %s's status to %s: %s\n",
			acct->username, purple_status_get_name(status), msg);

	if (msg && strcmp("", msg))
	{
		//TODO, sucecss && fail
		twitter_api_set_status(acct, msg, 0,
				NULL, twitter_set_status_error_cb, NULL);
	}
}

static void twitter_add_buddy(PurpleConnection *gc, PurpleBuddy *buddy,
		PurpleGroup *group)
{
	//TODO
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
	TwitterBuddyData *twitter_buddy_data = buddy->proto_data;

	purple_debug_info(TWITTER_PROTOCOL_ID, "removing %s from %s's buddy list\n",
			buddy->name, gc->account->username);

	if (!twitter_buddy_data)
		return;
	twitter_user_data_free(twitter_buddy_data->user);
	twitter_status_data_free(twitter_buddy_data->status);
	g_free(twitter_buddy_data);
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

static void blist_example_menu_item(PurpleBlistNode *node, gpointer userdata) {
	TwitterConvChatContext *ctx;
	PurpleChat *chat = PURPLE_CHAT(node);
	PurpleAccount *account = purple_chat_get_account(chat);
	GHashTable *components = purple_chat_get_components(chat);
	const char *chat_name = twitter_get_chat_name(components);

	gboolean new_state = !twitter_chat_auto_open(chat);

	//If no conversation exists and we've set this to NOT auto open, let's free some memory
	if (!new_state 
		&& !purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, chat_name, account)
		&& (ctx = twitter_find_chat_context(account, chat_name)))

	{
		purple_debug_info(TWITTER_PROTOCOL_ID, "No more auto open, destroying context\n");
		twitter_chat_context_endpoint_free(ctx->type, ctx->endpoint_data);
	}

	g_hash_table_replace(components, g_strdup("auto_open"), 
			(new_state ? g_strdup("1") : g_strdup("0")));
}


static GList *twitter_blist_node_menu(PurpleBlistNode *node) {
	purple_debug_info(TWITTER_PROTOCOL_ID, "providing buddy list context menu item\n");

	if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
		PurpleChat *chat = PURPLE_CHAT(node);
		char *label = g_strdup_printf("Automatically open chat on new tweets (Currently: %s)",
			(twitter_chat_auto_open(chat) ? "On" : "Off"));

		PurpleMenuAction *action = purple_menu_action_new(
				label,
				PURPLE_CALLBACK(blist_example_menu_item),
				NULL,   /* userdata passed to the callback */
				NULL);  /* child menu items */
		g_free(label);
		return g_list_append(NULL, action);
	} else {
		return NULL;
	}
}


/* normalize a username (e.g. remove whitespace, add default domain, etc.)
 * for twitter, this is a noop.
 */
static const char *twitter_normalize(const PurpleAccount *acct,
		const char *input) {
	return NULL;
}

static void twitter_set_buddy_icon(PurpleConnection *gc,
		PurpleStoredImage *img) {
	purple_debug_info(TWITTER_PROTOCOL_ID, "setting %s's buddy icon to %s\n",
			gc->account->username, purple_imgstore_get_filename(img));
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
	twitter_get_chat_name,	      /* get_chat_name */
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
	NULL,	       /* convo_closed */
	twitter_normalize,		  /* normalize */
	twitter_set_buddy_icon,	     /* set_buddy_icon */
	NULL,	       /* remove_group */
	NULL,//TODO?				/* get_cb_real_name */
	NULL,	     /* set_chat_topic */
	twitter_find_blist_chat,				/* find_blist_chat */
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
static gboolean twitter_uri_handler(const char *proto, const char *cmd_arg, GHashTable *params)
{
	const char *text;
	const char *username;
	PurpleAccount *account;
	purple_debug_info(TWITTER_PROTOCOL_ID, "%s PROTO %s CMD_ARG %s\n", G_STRFUNC, proto, cmd_arg);

	g_return_val_if_fail(proto != NULL, FALSE);
	g_return_val_if_fail(cmd_arg != NULL, FALSE);

	//don't handle someone elses proto
	if (strcmp(proto, TWITTER_URI))
		return FALSE;

	text = purple_url_decode(g_hash_table_lookup(params, "text"));
	username = g_hash_table_lookup(params, "account");

	if (text == NULL || username == NULL || username[0] == '\0')
	{
		purple_debug_info(TWITTER_PROTOCOL_ID, "malformed uri.\n");
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

	purple_debug_info(TWITTER_PROTOCOL_ID, "Account %s got action %s with text %s\n", username, cmd_arg, text);
	if (!strcmp(cmd_arg, TWITTER_URI_ACTION_USER))
	{
		purple_notify_info(purple_account_get_connection(account),
				"Clicked URI",
				"@name clicked",
				"Sorry, this has not been implemented yet");
	} else if (!strcmp(cmd_arg, TWITTER_URI_ACTION_SEARCH)) {
		twitter_chat_search_join(purple_account_get_connection(account), text, 0);
	}
	return TRUE;
}

static gboolean twitter_url_clicked_cb(GtkIMHtml *imhtml, GtkIMHtmlLink *link)
{
	const gchar *url = gtk_imhtml_link_get_url(link);
	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);
	purple_got_protocol_handler_uri(url);

	return TRUE;
}

static gboolean twitter_context_menu(GtkIMHtml *imhtml, GtkIMHtmlLink *link, GtkWidget *menu)
{
	purple_debug_info(TWITTER_PROTOCOL_ID, "%s\n", G_STRFUNC);
	return TRUE;
}
#endif


static void twitter_init(PurplePlugin *plugin)
{

	purple_debug_info(TWITTER_PROTOCOL_ID, "starting up\n");

	prpl_info.protocol_options = twitter_get_protocol_options();

#if _HAVE_PIDGIN_
	purple_signal_connect(purple_get_core(), "uri-handler", plugin,
			PURPLE_CALLBACK(twitter_uri_handler), NULL);

	gtk_imhtml_class_register_protocol(TWITTER_URI "://", twitter_url_clicked_cb, twitter_context_menu);
#endif

	_twitter_protocol = plugin;
}

static void twitter_destroy(PurplePlugin *plugin) {
	purple_debug_info(TWITTER_PROTOCOL_ID, "shutting down\n");
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
	"0.2",						   /* version */
	"Twitter Protocol Plugin",				  /* summary */
	"Twitter Protocol Plugin",				  /* description */
	"neaveru <neaveru@gmail.com>",		     /* author */
	"http://code.google.com/p/libpurple-twitter-protocol/",  /* homepage */
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
