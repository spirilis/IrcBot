/* IrcBot - IRC client class for managing an IRC connection using the Ethernet or WiFi
 * library.  Intended for running an IRC bot under the hood with a Tiva-C Connected
 * LaunchPad, or CC3200 SimpleLinkWiFi LaunchPad.
 *
 * Copyright (c) 2014, Eric Brundick <spirilis@linux.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose
 * with or without fee is hereby granted, provided that the above copyright notice
 * and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT,
 * OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef IRCBOT_H
#define IRCBOT_H

#include <Energia.h>
#include <inttypes.h>
#include <string.h>


//#define IRC_NETWORK_CLIENT_CLASS WiFiClient
//#include <WiFi.h>
//#include <WiFiClient.h>

#define IRC_NETWORK_CLIENT_CLASS EthernetClient
#include <Ethernet.h>
#include <EthernetClient.h>



#define IRC_CHANNEL_MAX 4
#define IRC_CHANNEL_MAXLEN 32
#define IRC_CALLBACK_MAX_CHANNELNICK 64
#define IRC_COMMAND_REGISTRY_MAX 32
#define IRC_SERVERNAME_MAXLEN 64
#define IRC_NICKUSER_MAXLEN 32
#define IRC_DESCRIPTION_MAXLEN 128
#define IRC_INGRESS_BUFFER_LEN 512
#define IRC_INGRESS_RINGBUF_LEN 1024
#define IRC_CMDTOK_MAX 16

typedef void(*IRC_CALLBACK_TYPE_CONNECT)(void *userobj);
typedef void(*IRC_CALLBACK_TYPE_CHANNEL)(void *userobj, const char *channel);
typedef void(*IRC_CALLBACK_TYPE_CHANNEL_USER)(void *userobj, const char *channel, const char *nick);
typedef void(*IRC_CALLBACK_TYPE_COMMAND)(void *userobj, const char *channel, const char *fromnick, const char *message);

typedef struct {
	char *cmd;
	IRC_CALLBACK_TYPE_COMMAND callback;
	IRC_CALLBACK_TYPE_COMMAND unauth_callback;
	void *userobj;
	char **authnicks;
} CmdRegistry;

typedef struct {
	IRC_CALLBACK_TYPE_CHANNEL callback;
	void *userobj;
} ChanCallbackRegistry;

typedef struct {
	int chanidx;
	char nick[IRC_NICKUSER_MAXLEN];
	IRC_CALLBACK_TYPE_CHANNEL_USER callback;
	void *userobj;
} ChanUserCallbackRegistry;

typedef struct {
	int argc;
	char *buffer;
	char *argv[IRC_CMDTOK_MAX];
} CmdTok;


enum {
	IRC_DISCONNECTED = 0,
	IRC_CONNECTING,
	IRC_CONNECTED,
	IRC_SERVERINIT,
	IRC_REGISTERING_NICK,
	IRC_NICK_REGISTERED,
	IRC_REGISTERING_USER,
	IRC_USER_REGISTERED,
	IRC_MOTD_FINISHED
};

enum {
	IRC_CHAN_NOTJOINED = 0,
	IRC_CHAN_JOINING,
	IRC_CHAN_JOINED,
	IRC_CHAN_NEEDAUTH
};



class IrcBot {
	private:
		IRC_NETWORK_CLIENT_CLASS conn;
		Stream *Dbg;
		int botState;
		char _ircnick[IRC_NICKUSER_MAXLEN], _ircuser[IRC_NICKUSER_MAXLEN], _ircdescription[IRC_DESCRIPTION_MAXLEN];
		char _ircserver[IRC_SERVERNAME_MAXLEN];
		char _ircchannels[IRC_CHANNEL_MAX][IRC_CHANNEL_MAXLEN];
		int chanState[IRC_CHANNEL_MAX];
		uint16_t _ircport;
		uint8_t tcpbuf[IRC_INGRESS_BUFFER_LEN];
		uint8_t ringbuf[IRC_INGRESS_RINGBUF_LEN];
		unsigned int ringbuf_start, ringbuf_end;
		uint32_t nick_user_millis;
		boolean _enabled;
		boolean _hasmotd;
		
		void InitVariables(void);
		void processInboundData(void);  // RX state machine for TCP connection
		int ircProtocolCommandToken(const char *cmd);
		const char *ircReplyCodeStrerror(unsigned int cmdtoken);
		inline unsigned int ringBufferLen(void);
		int ringBufferSearch(const uint8_t search);
		unsigned int ringBufferSearchConsume(void *buf, const uint8_t search);
		unsigned int ringBufferSearchFlush(const uint8_t search);
		unsigned int ringBufferConsume(void *buf, const unsigned int maxlen);
		unsigned int ringBufferFlush(const unsigned int count);
		void writebuf(const uint8_t *buf);
		void writebuf(const char *buf) { writebuf((const uint8_t *)buf); };
		void writebuf(const char c) { conn.write((uint8_t)c); };
		void writebuf(const uint8_t c) { conn.write(c); };

		/* Callback handling */
		// Connect & disconnect (only 1 allowed)
		IRC_CALLBACK_TYPE_CONNECT connectCallback;
		void *connectCallbackUserobj;
		void executeOnConnectCallback(void);

		IRC_CALLBACK_TYPE_CONNECT disconnectCallback;
		void *disconnectCallbackUserobj;
		void executeOnDisconnectCallback(void);

		// Channel join & part (only 1 allowed per channel)
		ChanCallbackRegistry channelJoinCallbacks[IRC_CHANNEL_MAX];
		void executeOnChannelJoinCallback(const int);
		ChanCallbackRegistry channelPartCallbacks[IRC_CHANNEL_MAX];
		void executeOnChannelPartCallback(const int);

		// Trap on other users joining & parting certain channels (IRC_CALLBACK_MAX_CHANNELNICK allowed)
		ChanUserCallbackRegistry channelUserJoinCallbacks[IRC_CALLBACK_MAX_CHANNELNICK];
		ChanUserCallbackRegistry channelUserPartCallbacks[IRC_CALLBACK_MAX_CHANNELNICK];

		// Registry of commands directed at this bot
		CmdRegistry commandCallbackRegistry[IRC_COMMAND_REGISTRY_MAX];
		IRC_CALLBACK_TYPE_COMMAND unknownCommandCallback;
		void *unknownCommandCallbackUserobj;


	public:
		static const uint32_t version;
		static const char *versionString;

		IrcBot();
		IrcBot(Stream *debugStream, const char *server, const char *nick, const char *user, const char *desc);

		void setServer(const char *server);
		void setPort(uint16_t ircPort);
		void setNick(const char *nick);
		void setUsername(const char *user);
		void setDescription(const char *desc);
		int addChannel(const char *chan);
		int removeChannel(const int chanidx);
		int removeChannel(const char *chan);
		void begin(void);
		void end(void);
		void loop(void);  // Run a loop of the IRC Bot's state machine
		boolean sendPrivmsg(const char *chan, const char *tonick, const char *message);
		boolean sendPrivmsgCtcp(const char *chan, const char *ctcpcmd, const char *message);
		int getState(void);  // Get the master state of the bot in enum value
		const char *getStateStrerror(void);
		boolean parseUserHostString(const void *str, char *nick, char *user, char *host);
		void setDebug(Stream *debugStream);
		void argToken(char *buffer, CmdTok *ts);

		boolean isConnected();

		boolean attachOnConnect( IRC_CALLBACK_TYPE_CONNECT, const void *userobj );
		boolean attachOnDisconnect( IRC_CALLBACK_TYPE_CONNECT, const void *userobj );
		boolean attachOnJoin( const char *channel, IRC_CALLBACK_TYPE_CHANNEL, const void *userobj );
		boolean attachOnPart( const char *channel, IRC_CALLBACK_TYPE_CHANNEL, const void *userobj );
		boolean attachOnUserJoin( const char *channel, const char *nick, IRC_CALLBACK_TYPE_CHANNEL_USER, const void *userobj );
		boolean attachOnUserPart( const char *channel, const char *nick, IRC_CALLBACK_TYPE_CHANNEL_USER, const void *userobj );
		boolean attachOnCommand( const char *cmd, IRC_CALLBACK_TYPE_COMMAND, const void *userobj );
		boolean attachOnCommand( const char *cmd, const char **authnicks, IRC_CALLBACK_TYPE_COMMAND, const void *userobj );
		boolean attachOnUnknownCommand( IRC_CALLBACK_TYPE_COMMAND, const void *userobj );
		boolean attachOnCommandUnauthorized( const char *cmd, IRC_CALLBACK_TYPE_COMMAND );

		boolean detachOnConnect(void);
		boolean detachOnDisconnect(void);
		boolean detachOnJoin( const char *channel );
		boolean detachOnPart( const char *channel );
		boolean detachOnUserJoin( const char *channel, const char *nick );
		boolean detachOnUserPart( const char *channel, const char *nick );
		boolean flushUserJoinOrPart( const char *channel );
		boolean flushUserJoinOrPartByChanIdx( const int chanidx );
		boolean detachOnCommand( const char *cmd );
		boolean detachOnUnknownCommand(void);
		boolean detachOnCommandUnauthorized( const char *cmd );
};

// IRC protocol commands & tokens
#define IRC_CMDTOKEN_PRIVMSG   901    // Reply codes 900+ don't exist, this is just being used to
#define IRC_CMDTOKEN_NOTICE    902    // allow contiguous use of both numeric reply codes and text commands.
#define IRC_CMDTOKEN_PASS      903
#define IRC_CMDTOKEN_NICK      904
#define IRC_CMDTOKEN_USER      905
#define IRC_CMDTOKEN_OPER      906
#define IRC_CMDTOKEN_MODE      907
#define IRC_CMDTOKEN_SERVICE   908
#define IRC_CMDTOKEN_QUIT      909
#define IRC_CMDTOKEN_JOIN      910
#define IRC_CMDTOKEN_PART      911
#define IRC_CMDTOKEN_TOPIC     912
#define IRC_CMDTOKEN_INVITE    913
#define IRC_CMDTOKEN_KICK      914
#define IRC_CMDTOKEN_PING      915
#define IRC_CMDTOKEN_PONG      916
#define IRC_CMDTOKEN_RPL_WELCOME			001
#define IRC_CMDTOKEN_RPL_YOURHOST			002
#define IRC_CMDTOKEN_RPL_CREATED			003
#define IRC_CMDTOKEN_RPL_MYINFO				004
#define IRC_CMDTOKEN_RPL_BOUNCE				005
#define IRC_CMDTOKEN_RPL_USERHOST			302
#define IRC_CMDTOKEN_RPL_ISON				303
#define IRC_CMDTOKEN_RPL_AWAY				301
#define IRC_CMDTOKEN_RPL_UNAWAY				305
#define IRC_CMDTOKEN_RPL_NOWAWAY			306
#define IRC_CMDTOKEN_RPL_WHOISUSER			311
#define IRC_CMDTOKEN_RPL_WHOISSERVER		312
#define IRC_CMDTOKEN_RPL_WHOISOPERATOR		313
#define IRC_CMDTOKEN_RPL_WHOISIDLE			317
#define IRC_CMDTOKEN_RPL_ENDOFWHOIS			318
#define IRC_CMDTOKEN_RPL_WHOISCHANNELS		319
#define IRC_CMDTOKEN_RPL_WHOWASUSER			314
#define IRC_CMDTOKEN_RPL_ENDOFWHOWAS		369
#define IRC_CMDTOKEN_RPL_LIST				322
#define IRC_CMDTOKEN_RPL_LISTEND			323
#define IRC_CMDTOKEN_RPL_UNIQOPIS			325
#define IRC_CMDTOKEN_RPL_CHANNELMODEIS		324
#define IRC_CMDTOKEN_RPL_NOTOPIC			331
#define IRC_CMDTOKEN_RPL_TOPIC				332
#define IRC_CMDTOKEN_RPL_INVITING			341
#define IRC_CMDTOKEN_RPL_INVITELIST			346
#define IRC_CMDTOKEN_RPL_ENDOFINVITELIST	347
#define IRC_CMDTOKEN_RPL_EXCEPTLIST			348
#define IRC_CMDTOKEN_RPL_ENDOFEXCEPTLIST	349
#define IRC_CMDTOKEN_RPL_VERSION			351
#define IRC_CMDTOKEN_RPL_WHOREPLY			352
#define IRC_CMDTOKEN_RPL_ENDOFWHO			315
#define IRC_CMDTOKEN_RPL_NAMREPLY			353
#define IRC_CMDTOKEN_RPL_ENDOFNAMES			366
#define IRC_CMDTOKEN_RPL_BANLIST			367
#define IRC_CMDTOKEN_RPL_ENDOFBANLIST		368
#define IRC_CMDTOKEN_RPL_INFO				371
#define IRC_CMDTOKEN_RPL_ENDOFINFO			374
#define IRC_CMDTOKEN_RPL_MOTDSTART			375
#define IRC_CMDTOKEN_RPL_MOTD				372
#define IRC_CMDTOKEN_RPL_ENDOFMOTD			376
#define IRC_CMDTOKEN_RPL_YOUREOPER			381
#define IRC_CMDTOKEN_RPL_TIME				391
#define IRC_CMDTOKEN_RPL_UMODEIS			221
#define IRC_CMDTOKEN_RPL_TRYAGAIN			263
#define IRC_CMDTOKEN_ERR_NOSUCHNICK			401
#define IRC_CMDTOKEN_ERR_NOSUCHSERVER		402
#define IRC_CMDTOKEN_ERR_NOSUCHCHANNEL		403
#define IRC_CMDTOKEN_ERR_CANNOTSENDTOCHAN	404
#define IRC_CMDTOKEN_ERR_TOOMANYCHANNELS	405
#define IRC_CMDTOKEN_ERR_WASNOSUCHNICK		406
#define IRC_CMDTOKEN_ERR_TOOMANYTARGETS		407
#define IRC_CMDTOKEN_ERR_NOSUCHSERVICE		408
#define IRC_CMDTOKEN_ERR_NOORIGIN			409
#define IRC_CMDTOKEN_ERR_NORECIPIENT		411
#define IRC_CMDTOKEN_ERR_NOTEXTTOSEND		412
#define IRC_CMDTOKEN_ERR_NOTOPLEVEL			413
#define IRC_CMDTOKEN_ERR_WILDTOPLEVEL		414
#define IRC_CMDTOKEN_ERR_BADMASK			415
#define IRC_CMDTOKEN_ERR_UNKNOWNCOMMAND		421
#define IRC_CMDTOKEN_ERR_NOMOTD				422
#define IRC_CMDTOKEN_ERR_NONICKNAMEGIVEN	431
#define IRC_CMDTOKEN_ERR_ERRONEOUSNICKNAME	432
#define IRC_CMDTOKEN_ERR_NICKNAMEINUSE		433
#define IRC_CMDTOKEN_ERR_NICKCOLLISION		436
#define IRC_CMDTOKEN_ERR_UNAVAILRESOURCE	437
#define IRC_CMDTOKEN_ERR_USERNOTINCHANNEL	441
#define IRC_CMDTOKEN_ERR_NOTONCHANNEL		442
#define IRC_CMDTOKEN_ERR_USERONCHANNEL		443
#define IRC_CMDTOKEN_ERR_NOTREGISTERED		451
#define IRC_CMDTOKEN_ERR_NEEDMOREPARAMS		461
#define IRC_CMDTOKEN_ERR_ALREADYREGISTERED	462
#define IRC_CMDTOKEN_ERR_NOPERMFORHOST		463
#define IRC_CMDTOKEN_ERR_PASSWDMISMATCH		464
#define IRC_CMDTOKEN_ERR_YOUREBANNEDCREEP	465
#define IRC_CMDTOKEN_ERR_YOUWILLBEBANNED	466
#define IRC_CMDTOKEN_ERR_KEYSET				467
#define IRC_CMDTOKEN_ERR_CHANNELISFULL		471
#define IRC_CMDTOKEN_ERR_UNKNOWNMODE		472
#define IRC_CMDTOKEN_ERR_INVITEONLYCHAN		473
#define IRC_CMDTOKEN_ERR_BANNEDFROMCHAN		474
#define IRC_CMDTOKEN_ERR_BADCHANNELKEY		475
#define IRC_CMDTOKEN_ERR_BADCHANMASK		476
#define IRC_CMDTOKEN_ERR_NOCHANMODES		477
#define IRC_CMDTOKEN_ERR_BANLISTFULL		478
#define IRC_CMDTOKEN_ERR_NOPRIVILEGES		481
#define IRC_CMDTOKEN_ERR_CHANOPRIVSNEEDED	482
#define IRC_CMDTOKEN_ERR_RESTRICTED			484
#define IRC_CMDTOKEN_ERR_UNIQOPPRIVSNEEDED	485
#define IRC_CMDTOKEN_ERR_UMODEUNKNOWNFLAG	501
#define IRC_CMDTOKEN_ERR_USERSDONTMATCH		502

/* Textual version of all the numbered replies; for debugging use */
typedef struct {
	unsigned int code;
	const char *description;
} IrcReplyCode;


#endif
