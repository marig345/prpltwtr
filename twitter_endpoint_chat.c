#include "twitter_endpoint_chat.h"

void twitter_endpoint_chat_free(TwitterEndpointChat *ctx)
{
	PurpleConnection *gc;
	gc = purple_account_get_connection(ctx->account);

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
	g_slice_free(TwitterEndpointChat, ctx);
}

TwitterEndpointChat *twitter_endpoint_chat_new(
	TwitterEndpointChatSettings *settings,
	TwitterChatType type, PurpleAccount *account, const gchar *chat_name,
	gpointer endpoint_data)
{
	TwitterEndpointChat *ctx = g_slice_new0(TwitterEndpointChat);
	ctx->settings = settings;
	ctx->type = type;
	ctx->account = account;
	ctx->chat_name = g_strdup(chat_name);
	ctx->endpoint_data = endpoint_data;

	return ctx;
}
