#include "config.h"

#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <glib.h>

#include <account.h>
#include <accountopt.h>
#include <blist.h>
#include <cmds.h>
#include <conversation.h>
#include <connection.h>
#include <core.h>
#include <debug.h>
#include <notify.h>
#include <privacy.h>
#include <prpl.h>
#include <roomlist.h>
#include <status.h>
#include <util.h>
#include <version.h>
#include <cipher.h>
#include <sslconn.h>
#include <request.h>

typedef enum
{
	TWITTER_CHAT_SEARCH = 0,
	TWITTER_CHAT_TIMELINE = 1
} TwitterChatType;

typedef struct _TwitterEndpointChat TwitterEndpointChat;
typedef void (*TwitterChatLeaveFunc)(TwitterEndpointChat *ctx);
typedef gint (*TwitterChatSendMessageFunc)(TwitterEndpointChat *ctx, const char *message);

/* When I have time, I'd like to make this event driven
 * Where there is an object with attached events when the timeout completes
 * Then depending on actions, events will be detached. If event count = 0
 * then the timer stops. That would be nice... */
struct _TwitterEndpointChat
{
	TwitterChatType type;
	PurpleAccount *account;
	guint timer_handle;
	gchar *chat_name;
	gpointer endpoint_data;
};

TwitterEndpointChat *twitter_endpoint_chat_new(
	TwitterChatType type, PurpleAccount *account, const gchar *chat_name,
	gpointer endpoint_data);

void twitter_endpoint_chat_free(TwitterEndpointChat *ctx);

