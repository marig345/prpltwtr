/**
 * Copyright (C) 2009 Tan Miaoqing
 * Contact: Tan Miaoqing <rabbitrun84@gmail.com>
 */

#include <string.h>
#include <debug.h>
#include "twitter_util.h"
#include "twitter_prefs.h" //TODO move
#include "config.h" //TODO move

long long purple_account_get_long_long(PurpleAccount *account, const gchar *key, long long default_value)
{
	const char* tmp_str;

	tmp_str = purple_account_get_string(account, key, NULL);
	if(tmp_str)
		return strtoll(tmp_str, NULL, 10);
	else
		return 0;
}

void purple_account_set_long_long(PurpleAccount *account, const gchar *key, long long value)
{
	gchar* tmp_str;

	tmp_str = g_strdup_printf("%lld", value);
	purple_account_set_string(account, key, tmp_str);
	g_free(tmp_str);
}


//TODO: move those
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

//TODO: move those
const char *twitter_linkify(PurpleAccount *account, const char *message)
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

//TODO: move those
char *twitter_format_tweet(PurpleAccount *account, const char *src_user, const char *message, long long id)
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

//TODO: move those
void twitter_chat_add_tweet(PurpleConvChat *chat, const char *who, const char *message, long long id, time_t time)
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

//TODO: move me
void twitter_buddy_set_status_data(PurpleAccount *account, const char *src_user, TwitterStatusData *s)
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

TwitterBuddyData *twitter_buddy_get_buddy_data(PurpleBuddy *b)
{
	if (b->proto_data == NULL)
		b->proto_data = g_new0(TwitterBuddyData, 1);
	return b->proto_data;
}

PurpleBuddy *twitter_buddy_new(PurpleAccount *account, const char *screenname, const char *alias)
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

void twitter_buddy_set_user_data(PurpleAccount *account, TwitterUserData *u, gboolean add_missing_buddy)
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

void twitter_buddy_update_icon_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message)
{
	PurpleBuddy *buddy = user_data;
	TwitterBuddyData *buddy_data = buddy->proto_data;
	if (buddy_data != NULL && buddy_data->user != NULL)
	{
		purple_buddy_icons_set_for_user(buddy->account, buddy->name,
				g_memdup(url_text, len), len, buddy_data->user->profile_image_url);
	}
}

void twitter_buddy_update_icon(PurpleBuddy *buddy)
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
