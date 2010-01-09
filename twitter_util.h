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


gchar *xmlnode_get_child_data(const xmlnode *node, const char *name);

#ifndef g_slice_new0
#define g_slice_new0(a) g_new0(a, 1)
#define g_slice_free(a, b) g_free(b)
#endif

#ifndef g_strcmp0
#define g_strcmp0(a, b) (a == NULL && b == NULL ? 0 : a == NULL ? -1 : b == NULL ? 1 : strcmp(a, b))
#endif

//TODO: move these 
const char *twitter_linkify(PurpleAccount *account, const char *message);
char *twitter_format_tweet(PurpleAccount *account, const char *src_user, const char *message, long long id);
void twitter_chat_add_tweet(PurpleConvChat *chat, const char *who, const char *message, long long id, time_t time);

#endif /* UTIL_H_ */
