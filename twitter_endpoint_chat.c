#include "twitter_endpoint_chat.h"

void twitter_conv_chat_context_free(TwitterConvChatContext *ctx)
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
	g_slice_free(TwitterConvChatContext, ctx);
}

TwitterConvChatContext *twitter_conv_chat_context_new(
	TwitterChatType type, PurpleAccount *account, const gchar *chat_name,
	gpointer endpoint_data)
{
	TwitterConvChatContext *ctx = g_slice_new0(TwitterConvChatContext);
	ctx->type = type;
	ctx->account = account;
	ctx->chat_name = g_strdup(chat_name);
	ctx->endpoint_data = endpoint_data;

	return ctx;
}
