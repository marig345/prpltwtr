#include "twitter_tweetactions.h"
#if _HAVE_PIDGIN_

#include "twitter_conn.h"

//TODO: 3rd param should be tweet data
static gboolean twitter_tweet_action_icon_event(GtkWidget *w, GdkEvent *event, GtkIMHtml *imhtml)
{
	if (event->type == GDK_ENTER_NOTIFY)
	{
		gdk_window_set_cursor(w->window, imhtml->hand_cursor);
	} else if (event->type == GDK_BUTTON_RELEASE) {
		//TODO: add actions
	}
	return FALSE;
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

	if (account != account_signal)
		return;

	imhtml = GTK_IMHTML(PIDGIN_CONVERSATION(conv)->imhtml);
	text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(imhtml));

	gtk_text_buffer_get_end_iter(text_buffer, &iter);

	box = gtk_event_box_new();
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
	g_signal_connect(G_OBJECT(box), "event", G_CALLBACK(twitter_tweet_action_icon_event), imhtml);

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
