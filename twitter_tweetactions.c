#include "twitter_tweetactions.h"
#if _HAVE_PIDGIN_

#include "twitter_conn.h"
#include "twitter_util.h"

typedef struct
{
	PurpleConversation *conv;
	char *who;
	long long tweet_id;
} TwitterTweetAction;

typedef struct
{
	PurpleConversationType type;
	gchar *conv_name;
} TwitterConversationId;

static void twitter_send_rt_success_cb(TwitterRequestor *r,
		xmlnode *node,
		gpointer user_data)
{
	TwitterConversationId *conv_id = user_data;
	PurpleConversation *conv;
	g_return_if_fail(conv_id != NULL);

	conv = purple_find_conversation_with_account(conv_id->type, conv_id->conv_name, r->account);

	if (conv)
	{
		purple_conversation_write(conv, NULL, "Successfully retweeted", PURPLE_MESSAGE_SYSTEM, time(NULL));
	}

	g_free(conv_id->conv_name);
	g_free(conv_id);
}

static void twitter_send_rt_error_cb(TwitterRequestor *r,
		const TwitterRequestErrorData *error_data,
		gpointer user_data)
{
	TwitterConversationId *conv_id = user_data;
	PurpleConversation *conv;
	g_return_if_fail(conv_id != NULL);

	conv = purple_find_conversation_with_account(conv_id->type, conv_id->conv_name, r->account);

	if (conv)
	{
		purple_conversation_write(conv, NULL, "Retweet failed", PURPLE_MESSAGE_ERROR, time(NULL));
	}

	g_free(conv_id->conv_name);
	g_free(conv_id);
}


static void twitter_context_menu_retweet(GtkWidget *w, TwitterTweetAction *action_data)
{
	PurpleAccount *account;
	TwitterConversationId *conv_id;
	g_return_if_fail(action_data != NULL);

	account = purple_conversation_get_account(action_data->conv);

	conv_id = g_new0(TwitterConversationId, 1);
	conv_id->conv_name = g_strdup(purple_conversation_get_name(action_data->conv));
	conv_id->type = purple_conversation_get_type(action_data->conv);

	twitter_api_send_rt(purple_account_get_requestor(account),
			action_data->tweet_id,
			twitter_send_rt_success_cb,
			twitter_send_rt_error_cb,
			conv_id);
}

static void twitter_context_menu_reply(GtkWidget *w, TwitterTweetAction *action_data)
{
	//twitter_got_uri_action(url, TWITTER_URI_ACTION_REPLY);
}

static void twitter_context_menu_link(GtkWidget *w, TwitterTweetAction *action_data)
{
	//twitter_got_uri_action(url, TWITTER_URI_ACTION_LINK);
}


static void twitter_url_menu_actions(GtkWidget *menu, TwitterTweetAction *action_data)
{
	GtkWidget *img, *item;

	img = gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_MENU);
	item = gtk_image_menu_item_new_with_mnemonic(("Retweet"));
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), img);
	g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(twitter_context_menu_retweet), (gpointer)action_data);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	img = gtk_image_new_from_stock(GTK_STOCK_REDO, GTK_ICON_SIZE_MENU);
	item = gtk_image_menu_item_new_with_mnemonic(("Reply"));
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), img);
	g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(twitter_context_menu_reply), (gpointer)action_data);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	img = gtk_image_new_from_stock(GTK_STOCK_HOME, GTK_ICON_SIZE_MENU);
	item = gtk_image_menu_item_new_with_mnemonic(("Goto Site"));
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), img);
	g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(twitter_context_menu_link), (gpointer)action_data);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

//TODO: 3rd param should be tweet data
static gboolean twitter_tweet_action_icon_event(GtkWidget *w, GdkEvent *event, TwitterTweetAction *action)
{
	if (event->type == GDK_ENTER_NOTIFY)
	{
		GtkIMHtml *imhtml = GTK_IMHTML(PIDGIN_CONVERSATION(action->conv)->imhtml);
		gdk_window_set_cursor(w->window, imhtml->hand_cursor);
	} else if (event->type == GDK_BUTTON_RELEASE) {
		GtkWidget *menu = gtk_menu_new();
		twitter_url_menu_actions(menu, action);
		gtk_widget_show_all(menu);
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
				0, gtk_get_current_event_time());
	}
	return FALSE;
}

static void twitter_tweet_action_free(TwitterTweetAction *t)
{
	if (!t)
		return;
	g_free(t->who);
	g_free(t);
}

static TwitterTweetAction *twitter_tweet_action_new(PurpleConversation *conv, const char *who, long long tweet_id)
{
	TwitterTweetAction *t = g_new(TwitterTweetAction, 1);
	t->conv = conv;
	t->who = g_strdup(who);
	t->tweet_id = tweet_id;
	return t;
}

static void twitter_tweet_actions_displayed_chat_cb(PurpleAccount *account, const char *who, char *message,
		PurpleConversation *conv,
		PurpleMessageFlags flags,
		void *account_signal)
{
	GtkIMHtml *imhtml;
	GtkTextBuffer *text_buffer;
	GtkTextIter iter;
	GtkWidget *box, *img;
	TwitterTweetAction *action;

	if (account != account_signal)
		return;

	imhtml = GTK_IMHTML(PIDGIN_CONVERSATION(conv)->imhtml);
	text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(imhtml));

	gtk_text_buffer_get_end_iter(text_buffer, &iter);

	box = gtk_event_box_new();
	action = twitter_tweet_action_new(conv, who, twitter_get_last_formatted_tweet_id());
	g_object_set_data_full(G_OBJECT(box),
			TWITTER_PROTOCOL_ID "-action-data",
			action,
			(GDestroyNotify) twitter_tweet_action_free);
	//TODO: cache this?
	//TODO: text version
	//TODO: change this image to something relevant
	img = gtk_image_new_from_stock(GTK_STOCK_PROPERTIES, GTK_ICON_SIZE_MENU);

	gtk_container_add(GTK_CONTAINER(box), GTK_WIDGET(img));

	gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(imhtml), box,
			gtk_text_buffer_create_child_anchor(text_buffer, &iter));

	gtk_widget_show(box);
	gtk_widget_show(img);

	//TODO: need to pass tweet data here
	g_signal_connect(G_OBJECT(box), "event", G_CALLBACK(twitter_tweet_action_icon_event), action);

}

void twitter_tweet_actions_account_load(PurpleAccount *account)
{
	purple_signal_connect(pidgin_conversations_get_handle(),
			"displayed-chat-msg",
			account,
			PURPLE_CALLBACK(twitter_tweet_actions_displayed_chat_cb), account);
}

void twitter_tweet_actions_account_unload(PurpleAccount *account)
{
	//TODO
}
#endif
