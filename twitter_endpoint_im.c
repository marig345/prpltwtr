#include "twitter_endpoint_im.h"
#include "twitter_util.h"

TwitterEndpointIm *twitter_endpoint_im_new(PurpleAccount *account, TwitterEndpointImSettings *settings)
{
	TwitterEndpointIm *endpoint = g_new0(TwitterEndpointIm, 1);
	endpoint->account = account;
	endpoint->settings = settings;
	return endpoint;
}
void twitter_endpoint_im_free(TwitterEndpointIm *ctx)
{
	if (ctx->timer)
	{
		purple_timeout_remove(ctx->timer);
		ctx->timer = 0;
	}
	g_free(ctx);
}

static gboolean twitter_endpoint_im_error_cb(PurpleAccount *account,
		const TwitterRequestErrorData *error_data,
		gpointer user_data)
{
	TwitterEndpointIm *ctx = (TwitterEndpointIm *) user_data;
	if (ctx->settings->error_cb(account, error_data, NULL))
	{
		twitter_endpoint_im_start(ctx);
	}
	return FALSE;
}


static void twitter_endpoint_im_success_cb(PurpleAccount *account,
		GList *nodes,
		gpointer user_data)
{
	TwitterEndpointIm *ctx = (TwitterEndpointIm *) user_data;
	ctx->settings->success_cb(account, nodes, NULL);
	twitter_endpoint_im_start(ctx);
}

static gboolean twitter_im_timer_timeout(gpointer _ctx)
{
	TwitterEndpointIm *ctx = (TwitterEndpointIm *) _ctx;
	ctx->settings->get_im_func(ctx->account, ctx->since_id,
		twitter_endpoint_im_success_cb, twitter_endpoint_im_error_cb,
		ctx);
	ctx->timer = 0;
	return FALSE;
}

void twitter_endpoint_im_start(TwitterEndpointIm *ctx)
{
	if (ctx->timer)
	{
		purple_timeout_remove(ctx->timer);
	}
	ctx->timer = purple_timeout_add_seconds(
			60 * ctx->settings->timespan_func(ctx->account),
			twitter_im_timer_timeout, ctx);
}

long long twitter_endpoint_im_get_since_id(TwitterEndpointIm *ctx)
{
	return (ctx->since_id 
			? ctx->since_id 
			: twitter_endpoint_im_settings_load_since_id(ctx->account, ctx->settings));
}

void twitter_endpoint_im_set_since_id(TwitterEndpointIm *ctx, long long since_id)
{
	ctx->since_id = since_id;
	twitter_endpoint_im_settings_save_since_id(ctx->account, ctx->settings, since_id);
}

long long twitter_endpoint_im_settings_load_since_id(PurpleAccount *account, TwitterEndpointImSettings *settings)
{
	return purple_account_get_long_long(account, settings->since_id_setting_id, 0);
}

void twitter_endpoint_im_settings_save_since_id(PurpleAccount *account, TwitterEndpointImSettings *settings, long long since_id)
{
	purple_account_set_long_long(account, settings->since_id_setting_id, since_id);
}

