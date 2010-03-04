#include "twitter_tweetactions.h"
#if _HAVE_PIDGIN_

#include "twitter_conn.h"

static void twitter_tweet_actions_displayed_chat_cb(PurpleAccount *account, const char *who, char *message,
		PurpleConversation *conv,
		PurpleMessageFlags flags,
		void *account_signal)
{
	GtkIMHtml *imhtml;
	GtkTextBuffer *text_buffer;
	GtkTextIter iter;

	if (account != account_signal)
		return;

	imhtml = GTK_IMHTML(PIDGIN_CONVERSATION(conv)->imhtml);
	text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(imhtml));

	gtk_text_buffer_get_end_iter(text_buffer, &iter);

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
