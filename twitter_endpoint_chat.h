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

#include "twitter_util.h"

typedef enum
{
	TWITTER_CHAT_SEARCH = 0,
	TWITTER_CHAT_TIMELINE = 1
} TwitterChatType;

typedef struct _TwitterEndpointChatSettings TwitterEndpointChatSettings;
typedef struct _TwitterEndpointChat TwitterEndpointChat;
typedef void (*TwitterChatLeaveFunc)(TwitterEndpointChat *ctx);
typedef gint (*TwitterChatSendMessageFunc)(TwitterEndpointChat *ctx, const char *message);

struct _TwitterEndpointChatSettings
{
	TwitterChatType type;
	int (*send_message)(TwitterEndpointChat *endpoint_chat, const gchar *message);
	void (*endpoint_data_free)(gpointer endpoint_data);
	gint (*get_default_interval)(PurpleAccount *account);
	gchar *(*get_name)(GHashTable *components);
	gchar *(*verify_components)(GHashTable *components);
	gboolean (*interval_timeout)(TwitterEndpointChat *endpoint_chat);
	gboolean (*on_start)(TwitterEndpointChat *endpoint_chat);
	gpointer (*create_endpoint_data)(GHashTable *components);
};

struct _TwitterEndpointChat
{
	TwitterChatType type;
	PurpleAccount *account;
	guint timer_handle;
	gchar *chat_name;
	TwitterEndpointChatSettings *settings;
	gpointer endpoint_data;
};

TwitterEndpointChat *twitter_endpoint_chat_new(
	TwitterEndpointChatSettings *settings,
	TwitterChatType type, PurpleAccount *account, const gchar *chat_name,
	GHashTable *components);

void twitter_endpoint_chat_free(TwitterEndpointChat *ctx);

