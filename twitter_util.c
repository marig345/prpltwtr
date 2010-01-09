/**
 * Copyright (C) 2009 Tan Miaoqing
 * Contact: Tan Miaoqing <rabbitrun84@gmail.com>
 */

#include <string.h>
#include <debug.h>
#include "twitter_util.h"
#include "twitter_prefs.h" //TODO move
#include "config.h" //TODO move

gchar *xmlnode_get_child_data(const xmlnode *node, const char *name)
{
    xmlnode *child = xmlnode_get_child(node, name);
    if (!child)
        return NULL;
    return xmlnode_get_data_unescaped(child);
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
