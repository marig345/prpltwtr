/**
 * Copyright (C) 2009 Tan Miaoqing
 * Contact: Tan Miaoqing <rabbitrun84@gmail.com>
 */

#ifndef _TWITTER_UTIL_H_
#define _TWITTER_UTIL_H_

#include <glib.h>
#include <xmlnode.h>
#include <account.h> //TODO remove
#define TWITTER_URI_ACTION_USER		"user" //TODO: move
#define TWITTER_URI_ACTION_SEARCH	"search" //TODO: move
#include "config.h"
#include "twitter_xml.h"


long long purple_account_get_long_long(PurpleAccount *account, const gchar *key, long long default_value);
void purple_account_set_long_long(PurpleAccount *account, const gchar *key, long long value);

#ifndef g_slice_new0
#define g_slice_new0(a) g_new0(a, 1)
#define g_slice_free(a, b) g_free(b)
#endif

#ifndef g_strcmp0
#define g_strcmp0(a, b) (a == NULL && b == NULL ? 0 : a == NULL ? -1 : b == NULL ? 1 : strcmp(a, b))
#endif

//TODO: move everything below this line these 
const char *twitter_linkify(PurpleAccount *account, const char *message);
char *twitter_format_tweet(PurpleAccount *account, const char *src_user, const char *message, long long id);
void twitter_chat_add_tweet(PurpleConvChat *chat, const char *who, const char *message, long long id, time_t time);
void twitter_buddy_set_status_data(PurpleAccount *account, const char *src_user, TwitterStatusData *s);
TwitterBuddyData *twitter_buddy_get_buddy_data(PurpleBuddy *b);
PurpleBuddy *twitter_buddy_new(PurpleAccount *account, const char *screenname, const char *alias);
void twitter_buddy_set_user_data(PurpleAccount *account, TwitterUserData *u, gboolean add_missing_buddy);
void twitter_buddy_update_icon_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message);
void twitter_buddy_update_icon(PurpleBuddy *buddy);

typedef struct
{
	guint get_replies_timer;
	guint get_friends_timer;
	long long last_reply_id;
	long long last_home_timeline_id;
	long long failed_get_replies_count;

	/* a table of TwitterEndpointChat
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

#endif /* UTIL_H_ */
