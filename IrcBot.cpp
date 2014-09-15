/* IrcBot - IRC client class for managing an IRC connection using the Ethernet
 * library.  Intended for running an IRC bot under the hood with a Tiva-C Connected
 * LaunchPad.
 *
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

#include <IrcBot.h>


const uint32_t IrcBot::version = 0x00000100;
const char *IrcBot::versionString = "v1.0";

IrcBot::IrcBot(Stream *debugStream, const char *server, const char *nick, const char *user, const char *desc)
{
	Dbg = debugStream;
	strncpy(_ircnick, nick, IRC_NICKUSER_MAXLEN-1);
	strncpy(_ircuser, user, IRC_NICKUSER_MAXLEN-1);
	strncpy(_ircdescription, desc, IRC_DESCRIPTION_MAXLEN-1);
	strncpy(_ircserver, server, IRC_SERVERNAME_MAXLEN-1);
	_ircport = 6667;
	InitVariables();
}

IrcBot::IrcBot(void)
{
	Dbg = &Serial;
	strncpy(_ircnick, "MyTivaLP", IRC_NICKUSER_MAXLEN-1);
	strncpy(_ircuser, "tm4c129", IRC_NICKUSER_MAXLEN-1);
	strncpy(_ircdescription, "Energia Warrior", IRC_DESCRIPTION_MAXLEN-1);
	strncpy(_ircserver, "chat.freenode.net", IRC_SERVERNAME_MAXLEN-1);
	_ircport = 6667;
	InitVariables();
}

void IrcBot::InitVariables(void)
{
	int i;

	// Initialize all variables to defaults
	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		_ircchannels[i][0] = '\0';
		chanState[i] = IRC_CHAN_NOTJOINED;
		channelJoinCallbacks[i].callback = NULL;
		channelJoinCallbacks[i].userobj = NULL;
		channelPartCallbacks[i].callback = NULL;
		channelPartCallbacks[i].userobj = NULL;
	}
	ringbuf_start = 0;
	ringbuf_end = 0;
	_enabled = true;
	botState = IRC_DISCONNECTED;

	connectCallback = disconnectCallback = NULL;
	connectCallbackUserobj = disconnectCallbackUserobj = NULL;
	for (i=0; i < IRC_CALLBACK_MAX_CHANNELNICK; i++) {
		channelUserJoinCallbacks[i].chanidx = -1;
		channelUserJoinCallbacks[i].callback = NULL;
		channelUserJoinCallbacks[i].userobj = NULL;
		channelUserJoinCallbacks[i].nick[0] = '\0';

		channelUserPartCallbacks[i].chanidx = -1;
		channelUserPartCallbacks[i].callback = NULL;
		channelUserPartCallbacks[i].userobj = NULL;
		channelUserPartCallbacks[i].nick[0] = '\0';
	}

	for (i=0; i < IRC_COMMAND_REGISTRY_MAX; i++) {
		commandCallbackRegistry[i].cmd = NULL;
		commandCallbackRegistry[i].callback = NULL;
		commandCallbackRegistry[i].userobj = NULL;
		commandCallbackRegistry[i].authnicks = NULL;
	}
}

void IrcBot::writebuf(const uint8_t *buf)
{
	conn.write(buf, strlen((const char *)buf));
}

/* Main loop where all the processing happens */
void IrcBot::loop(void)
{
	int i = 0, j = 0;

	if (!_enabled)
		return;

	/* Handle bot-state matters first */
	if (botState > IRC_CONNECTING) {
		if (!conn.connected()) {
			Dbg->println("Found TCP connection closed; setting to IRC_DISCONNECTED");
			botState = IRC_DISCONNECTED;
			// If registered, run OnDisconnect callback
			executeOnDisconnectCallback();
		} else {
			if (conn.available() || ringBufferLen() > 0) {
				Dbg->println("processInboundData()");
				processInboundData();
				if (!_enabled)
					return;
			}
		}
	}

	switch (botState) {
		case IRC_DISCONNECTED:
			if (conn.connected()) {
				Dbg->println("Network shows us connected");
				botState++;
			} else {
				Dbg->println("Attempting to connect-");
				Dbg->print("conn.connect(\""); Dbg->print(_ircserver);
				Dbg->print("\", "); Dbg->print(_ircport); Dbg->println(");");
				i = conn.connect(_ircserver, _ircport);
				Dbg->print("conn.connect() return status = "); Dbg->println(i);
				if (i == 1) {  // Connect() successful
					for (i=0; i < IRC_CHANNEL_MAX; i++)
						chanState[i] = IRC_CHAN_NOTJOINED;
					ringbuf_start = ringbuf_end = 0;
					_hasmotd = false;
					botState++;
				} else {
					Dbg->println("Connection attempt unsuccessful; trying again");
				}
			}
			return;

		case IRC_CONNECTING:
			if (conn.connected()) {
				Dbg->println("Network shows us connected");
				botState++;
				// If registered, run the "Connect" callback.
				executeOnConnectCallback();
			}
			return;

		// In between these two is IRC_CONNECTED; processInboundData will get us past that once the server responds.

		case IRC_SERVERINIT:  // Server has responded with something (anything); proceed to register nickname
			char abuf[IRC_NICKUSER_MAXLEN+7];
			strcpy(abuf, "NICK ");
			strcat(abuf, _ircnick);
			strcat(abuf, "\r\n");
			writebuf(abuf);
			botState++;
			Dbg->print(">> Registering nick ("); Dbg->print(_ircnick); Dbg->println(")-");
			nick_user_millis = millis();
			return;

		case IRC_REGISTERING_NICK:
			if (millis()-nick_user_millis > 500)
				botState++;
			return;

		case IRC_NICK_REGISTERED:  // Nick is confirmed registered; submit USER
			char bbuf[IRC_NICKUSER_MAXLEN*2+IRC_SERVERNAME_MAXLEN+IRC_DESCRIPTION_MAXLEN+11];
			strcpy(bbuf, "USER ");
			strcat(bbuf, _ircuser);
			strcat(bbuf, " 0 * :");
			strcat(bbuf, _ircdescription);
			strcat(bbuf, "\r\n");
			writebuf(bbuf);
			botState++;
			Dbg->println(">> Registering user-");
			return;

		/* In between these is IRC_REGISTERING_USER and IRC_USER_REGISTERED; processInboundData will get us past this
		 * once we confirm our user information has been received & processed.
		 */

		case IRC_MOTD_FINISHED:
			// Init done, proceed to the rest of this loop.
			break;

		default:
			// If init isn't done, return so we don't try processing main loop runtime code.
			return;
	}  /* switch(botState) */


	/* Bot is connected and operating (presumably) normally; process data & rejoin channels if needed */
	// Check for disconnected channels
	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (chanState[i] == IRC_CHAN_NOTJOINED) {
			if (_ircchannels[i][0] != '\0') {
				// Channel defined; attempt to join!
				char buf[IRC_CHANNEL_MAXLEN+7];
				strcpy(buf, "JOIN ");
				strcat(buf, _ircchannels[i]);
				strcat(buf, "\r\n");
				writebuf(buf);
				chanState[i]++;
			} /* IRC channel defined or not */
		} /* IRC_CHAN_NOTJOINED */
	}
}

void IrcBot::begin(void)
{
	_enabled = true;
	botState = IRC_DISCONNECTED;
	loop();
}

void IrcBot::end(void)
{
	if (conn.connected()) {
		writebuf("QUIT :Bot quitting via end()\r\n");
		delay(250);
		conn.stop();
		executeOnDisconnectCallback();
	}
	botState = IRC_DISCONNECTED;
	_enabled = false;
}

int IrcBot::getState(void)
{
	return botState;
}

const char *ircServerStateDescriptions[] = {
	"Not connected",
	"Attempting to connect",
	"Connected; waiting for server response",
	"Connected; server has shown signs of life",
	"Registering Nick",
	"Nick registered",
	"Registering User & Description",
	"User & Description Registered; waiting for end of MOTD",
	"IRC connection healthy"
};

const char *IrcBot::getStateStrerror(void)
{
	if (!_enabled)
		return "Bot Disabled";
	return ircServerStateDescriptions[botState];
}

void IrcBot::setServer(const char *server)
{
	if (botState > IRC_DISCONNECTED && strncmp(server, _ircserver, IRC_SERVERNAME_MAXLEN-1) != 0) {
		// Force re-connect if we're changing servers
		end();
		strncpy(_ircserver, server, IRC_SERVERNAME_MAXLEN-1);
		begin();
	} else {
		strncpy(_ircserver, server, IRC_SERVERNAME_MAXLEN-1);
	}
}

void IrcBot::setPort(uint16_t ircPort)
{
	if (botState > IRC_DISCONNECTED && _ircport != ircPort) {
		// Force re-connect if we're changing ports on the fly
		end();
		_ircport = ircPort;
		begin();
	} else {
		_ircport = ircPort;
	}
}

void IrcBot::setNick(const char *nick)
{
	strncpy(_ircnick, nick, IRC_NICKUSER_MAXLEN-1);
	if (botState > IRC_NICK_REGISTERED) {
		char buf[IRC_NICKUSER_MAXLEN+7];
		strcpy(buf, "NICK ");
		strcat(buf, _ircnick);
		strcat(buf, "\r\n");
		writebuf(buf);
	}
}

void IrcBot::setUsername(const char *user)
{
	strncpy(_ircuser, user, IRC_NICKUSER_MAXLEN-1);
}

void IrcBot::setDescription(const char *desc)
{
	strncpy(_ircdescription, desc, IRC_DESCRIPTION_MAXLEN-1);
}

void IrcBot::setDebug(Stream *debugStream)
{
	if (debugStream != NULL)
		Dbg = debugStream;
}

int IrcBot::addChannel(const char *chan)
{
	int i, j = 0;

	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (_ircchannels[i][0] == '\0') {
			strncpy(_ircchannels[i], chan, IRC_CHANNEL_MAXLEN-1);
			chanState[i] = IRC_CHAN_NOTJOINED;
			return i;
		}
	}
	return -1;
}

int IrcBot::removeChannel(const int chanidx)
{
	if (chanidx < 0 || chanidx >= IRC_CHANNEL_MAX)
		return -1;  // Invalid channel index
	
	if (chanState[chanidx] == IRC_CHAN_JOINED && conn.connected()) {
		// Part channel first
		char buf[IRC_CHANNEL_MAXLEN+7];
		strcpy(buf, "PART ");
		strcat(buf, _ircchannels[chanidx]);
		strcat(buf, "\r\n");
		writebuf(buf);
	}

	// Flush callback entries related to this channel
	flushUserJoinOrPartByChanIdx(chanidx);

	if (channelJoinCallbacks[chanidx].callback != NULL) {
		channelJoinCallbacks[chanidx].callback = NULL;
		channelJoinCallbacks[chanidx].userobj = NULL;
	}
	if (channelPartCallbacks[chanidx].callback != NULL) {
		channelPartCallbacks[chanidx].callback = NULL;
		channelPartCallbacks[chanidx].userobj = NULL;
	}

	// Deactivate channel slot
	chanState[chanidx] = IRC_CHAN_NOTJOINED;
	_ircchannels[chanidx][0] = '\0';
	return chanidx;
}

// Search for channel by name and part/remove it
int IrcBot::removeChannel(const char *chan)
{
	int i;

	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (strncmp(chan, _ircchannels[i], IRC_CHANNEL_MAXLEN-1) == 0) {
			return removeChannel(i);
		}
	}
	return -1;  // Channel not found
}

boolean IrcBot::isConnected(void)
{
	if (botState > IRC_CONNECTING && conn.connected()) {
		return true;
	}
	return false;
}

boolean IrcBot::sendPrivmsg(const char *chan, const char *tonick, const char *message)
{
	int i;

	if (botState != IRC_MOTD_FINISHED) {
		Dbg->println(">> sendPrivmsg: botState != IRC_MOTD_FINISHED");
		return false;
	}
	
	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (!strncmp(chan, _ircchannels[i], IRC_CHANNEL_MAXLEN))
			break;
	}
	if (i == IRC_CHANNEL_MAX) {
		Dbg->print(">> sendPrivmsg: Cannot find channel "); Dbg->print(chan);
		Dbg->println(" in bot registry.");
		return false;  // Channel not found in bot registry
	}
	if (chanState[i] != IRC_CHAN_JOINED) {
		Dbg->print(">> sendPrivmsg: Channel "); Dbg->print(chan);
		Dbg->print(" currently not listed as joined; status=");
		Dbg->println(chanState[i]);
		return false;  // We haven't yet joined this channel!
	}
	
	// All set; send message
	Dbg->print(">> sendPrivmsg - Sending message PRIVMSG "); Dbg->print(_ircchannels[i]); Dbg->print(" :");
	writebuf("PRIVMSG ");
	writebuf(_ircchannels[i]);
	writebuf(" :");
	if (tonick != NULL) {
		Dbg->print(tonick); Dbg->print(": ");
		writebuf(tonick);
		writebuf(": ");
	}
	Dbg->println(message);
	writebuf(message);
	writebuf("\r\n");
	return true;
}

boolean IrcBot::sendPrivmsgCtcp(const char *chan, const char *ctcpcmd, const char *message)
{
	int i;

	if (botState != IRC_MOTD_FINISHED) {
		Dbg->println(">> sendPrivmsg: botState != IRC_MOTD_FINISHED");
		return false;
	}
	
	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (!strncmp(chan, _ircchannels[i], IRC_CHANNEL_MAXLEN))
			break;
	}
	if (i == IRC_CHANNEL_MAX) {
		Dbg->print(">> sendPrivmsg: Cannot find channel "); Dbg->print(chan);
		Dbg->println(" in bot registry.");
		return false;  // Channel not found in bot registry
	}
	if (chanState[i] != IRC_CHAN_JOINED) {
		Dbg->print(">> sendPrivmsg: Channel "); Dbg->print(chan);
		Dbg->print(" currently not listed as joined; status=");
		Dbg->println(chanState[i]);
		return false;  // We haven't yet joined this channel!
	}
	
	// All set; send message
	Dbg->print(">> sendPrivmsgCtcp - Sending CTCP message "); Dbg->print(_ircchannels[i]); Dbg->print(" :");
	writebuf("PRIVMSG ");
	writebuf(_ircchannels[i]);
	writebuf(" :\001");
	writebuf(ctcpcmd);
	writebuf(' ');
	Dbg->print(ctcpcmd); Dbg->print(' ');
	Dbg->println(message);
	writebuf(message);
	writebuf('\001');
	writebuf("\r\n");
	return true;
}

inline unsigned int IrcBot::ringBufferLen(void)
{
	if (ringbuf_start > ringbuf_end)
		return (IRC_INGRESS_RINGBUF_LEN-ringbuf_start)+ringbuf_end;
	return ringbuf_end - ringbuf_start;
}

int IrcBot::ringBufferSearch(const uint8_t search)
{
	int i;
	unsigned int end;
	uint8_t c;

	if (ringbuf_end < ringbuf_start)
		end = IRC_INGRESS_RINGBUF_LEN + ringbuf_end;
	else
		end = ringbuf_end;

	for (i = ringbuf_start; i < end; i++) {
		c = ringbuf[i % IRC_INGRESS_RINGBUF_LEN];
		if (c == search) {
			return i - ringbuf_start;
		}
	}
	return -1;  // Not found
}

unsigned int IrcBot::ringBufferSearchConsume(void *buf, const uint8_t search)
{
	int i, origin = ringbuf_start;
	unsigned int end, count = 0;
	uint8_t c, *cbuf = (uint8_t *)buf;

	if (ringbuf_end < ringbuf_start)
		end = IRC_INGRESS_RINGBUF_LEN + ringbuf_end;
	else
		end = ringbuf_end;

	for (i = ringbuf_start; i < end; i++) {
		c = ringbuf[i % IRC_INGRESS_RINGBUF_LEN];
		if (c != search) {
			cbuf[i-origin] = c;
			ringbuf_start++;
			count++;
		} else {
			break;
		}
	}
	ringbuf_start = ringbuf_start % IRC_INGRESS_RINGBUF_LEN;
	Dbg->println();
	return count;
}

unsigned int IrcBot::ringBufferSearchFlush(const uint8_t search)
{
	int i;
	unsigned int end, count = 0;
	uint8_t c;

	if (ringbuf_end < ringbuf_start)
		end = IRC_INGRESS_RINGBUF_LEN + ringbuf_end;
	else
		end = ringbuf_end;

	for (i = ringbuf_start; i < end; i++) {
		c = ringbuf[i % IRC_INGRESS_RINGBUF_LEN];
		if (c != search) {
			ringbuf_start++;
			count++;
		} else {
			break;
		}
	}
	ringbuf_start = ringbuf_start % IRC_INGRESS_RINGBUF_LEN;
	return count;
}

unsigned int IrcBot::ringBufferConsume(void *buf, const unsigned int maxlen)
{
	int i, origin = ringbuf_start;
	unsigned int end, count = 0;
	uint8_t c, *cbuf = (uint8_t *)buf;

	if (ringbuf_end < ringbuf_start)
		end = IRC_INGRESS_RINGBUF_LEN + ringbuf_end;
	else
		end = ringbuf_end;
	if ( (end - ringbuf_start) > maxlen )
		end = ringbuf_start + maxlen;

	for (i = ringbuf_start; i < end; i++) {
		c = ringbuf[i % IRC_INGRESS_RINGBUF_LEN];
		cbuf[i-origin] = c;
		ringbuf_start++;
		count++;
	}
	ringbuf_start = ringbuf_start % IRC_INGRESS_RINGBUF_LEN;
	return count;
}

unsigned int IrcBot::ringBufferFlush(const unsigned int count)
{
	int i;
	unsigned int end, ttl;
	uint8_t c;

	if (ringbuf_end < ringbuf_start)
		end = IRC_INGRESS_RINGBUF_LEN + ringbuf_end;
	else
		end = ringbuf_end;
	
	if ( (end - ringbuf_start) > count )
		end = ringbuf_start + count;
	ttl = end - ringbuf_start;

	ringbuf_start = end % IRC_INGRESS_RINGBUF_LEN;
	return ttl;
}

void IrcBot::processInboundData(void)
{
	// Implement ring buffer for incoming data
	int len;
	int i, j, cmdtoken, chanidx;
	char packet[IRC_INGRESS_RINGBUF_LEN], *arg1, *arg2, *argstart;
	char tmpbuf[64];
	char from_nick[IRC_NICKUSER_MAXLEN], from_user[IRC_NICKUSER_MAXLEN], from_host[IRC_SERVERNAME_MAXLEN];
	char *tochan, *tonick = NULL, *tmp1 = NULL, *tmp2 = NULL, *msgstart = NULL;
	boolean is_from_user, found_cmd, found_authnick;

	Dbg->print("issuing read-");
	len = conn.read(tcpbuf, IRC_INGRESS_BUFFER_LEN);
	if (len > 0) {
		Dbg->print("Stuffing "); Dbg->print(len); Dbg->println(" bytes into ring buffer-");
		// Add data to ring buffer
		for (i=0; i < len; i++) {
			ringbuf[ringbuf_end++] = tcpbuf[i];
			ringbuf_end = ringbuf_end % IRC_INGRESS_RINGBUF_LEN;
		}
	}
	if (ringBufferLen() > 0 && _enabled && botState > IRC_DISCONNECTED) {
		Dbg->print("Ring buffer has "); Dbg->print(ringBufferLen()); Dbg->println(" bytes; processing:");
		// Process incoming message
		while (ringBufferSearch('\r') >= 0) {  // A full message is available.
			len = ringBufferSearchConsume(packet, '\r');
			//Dbg->print("Read "); Dbg->print(len); Dbg->print(" bytes... ");
			// Flush \r and \n
			ringBufferFlush(2);
			//Dbg->println("Flushed \\r\\n");

			packet[len] = '\0';
			Dbg->print("RECV: "); Dbg->println(packet);
			// Packet contains our line; process!
			arg1 = strstr(packet, " ");
			if (arg1 == NULL) {
				// Malformed line, discard.
				continue;
			}
			*arg1 = '\0';
			arg1++;
			if (packet[0] == ':') {  // Message from a user or from the server; command or code is 2nd arg
				arg2 = strstr(arg1, " ");
				if (arg2 != NULL) {
					*arg2 = '\0';
					arg2++;
				}
				argstart = arg2;
				cmdtoken = ircProtocolCommandToken(arg1);
				Dbg->print(">> Line started with : - command token is "); Dbg->print(ircReplyCodeStrerror(cmdtoken)); Dbg->print("; argstart = ");
				if (argstart == NULL)
					Dbg->println("(null)");
				else
					Dbg->println(argstart);

				if (parseUserHostString(packet, from_nick, from_user, from_host))
					is_from_user = true;
				else
					is_from_user = false;

				if (is_from_user) {
					Dbg->print(">> Parsed \"From\": ");
					Dbg->print("nick="); Dbg->print(from_nick);
					Dbg->print(", user="); Dbg->print(from_user);
					Dbg->print(", host="); Dbg->println(from_host);
				} else {
					Dbg->print("(unable to parse: "); Dbg->print(packet); Dbg->println(")");
				}
			} else {  // Command is all alone with nothing in front indicating "who" or "where" it came from
				is_from_user = false;
				argstart = arg1;
				cmdtoken = ircProtocolCommandToken(packet);
				Dbg->print(">> Line started with cmd - command token is "); Dbg->print(ircReplyCodeStrerror(cmdtoken)); Dbg->print("; argstart = ");
				Dbg->println(argstart);
			}

			if (cmdtoken > 0) {
				// Valid command or reply; process!

				switch (cmdtoken) {
					case IRC_CMDTOKEN_PING:  // Received ping, send PONG
						if (argstart != NULL && argstart[0] == ':') {
							strncpy(tmpbuf, argstart+1, 63);
							strcpy(packet, "PONG ");
							//strcat(packet, _ircuser);
							//strcat(packet, " ");
							strcat(packet, tmpbuf);
						} else {
							strcpy(packet, "PONG ");
							strcat(packet, _ircuser);
						}
						strcat(packet, "\r\n");
						Dbg->print(">> Responding with: "); Dbg->println(packet);
						writebuf((char *)packet);
						break;

					case IRC_CMDTOKEN_PONG:  // Received PONG from a prior PING
						Dbg->print(">> Received PONG: ");
						if (argstart != NULL)
							Dbg->println(argstart);
						else
							Dbg->println(" (no data)");
						break;

					case IRC_CMDTOKEN_RPL_ENDOFMOTD:
					case IRC_CMDTOKEN_ERR_NOMOTD:
						if (botState > IRC_REGISTERING_USER)
							botState = IRC_MOTD_FINISHED;
						_hasmotd = true;
						break;

					case IRC_CMDTOKEN_JOIN:
					case IRC_CMDTOKEN_PART:
						// What's the channel?
						arg2 = strstr(argstart, " ");
						if (arg2 != NULL)
							*arg2 = '\0';
						if (argstart[0] == ':') {  // Some IRC servers do that; the channel is prepended with a : for some odd reason...
							argstart++;
						}
						for (chanidx = 0; chanidx < IRC_CHANNEL_MAX; chanidx++) {
							if (!strncmp(argstart, _ircchannels[chanidx], IRC_CHANNEL_MAXLEN))
								break;
						}
						if (chanidx != IRC_CHANNEL_MAX) {
							// Is this in relation to us?
							if (is_from_user) {
								if (strcmp(from_nick, _ircnick) == 0) {
									if (cmdtoken == IRC_CMDTOKEN_JOIN) {
										if (chanState[chanidx] == IRC_CHAN_JOINING) {
											chanState[chanidx] = IRC_CHAN_JOINED;
											Dbg->print(">> Confirmed JOIN for channel "); Dbg->println(_ircchannels[chanidx]);
											// Execute channel JOIN callback if registered
											executeOnChannelJoinCallback(chanidx);
										}
									} else {  // IRC_CMDTOKEN_PART
										chanState[chanidx] = IRC_CHAN_NOTJOINED;
										Dbg->print(">> We have PARTed channel "); Dbg->println(_ircchannels[chanidx]);
										// Execute channel PART callback if registered
										executeOnChannelPartCallback(chanidx);
									} /* if(cmdtoken == IRC_CMDTOKEN_JOIN or not) */
								} else {
									// No, this is notifying us of someone else joining/parting a channel
									// See if an appropriate callback has been registered for this one.
									if (cmdtoken == IRC_CMDTOKEN_JOIN) {
										for (i=0; i < IRC_CALLBACK_MAX_CHANNELNICK; i++) {
											if (channelUserJoinCallbacks[i].chanidx == chanidx &&
												channelUserJoinCallbacks[i].callback != NULL &&
												!strncmp(channelUserJoinCallbacks[i].nick, from_nick, IRC_NICKUSER_MAXLEN)) {

												Dbg->print(">> Executing OnChannelUserJoin callback for channel ");
												Dbg->print(_ircchannels[chanidx]); Dbg->print(" and nick=");
												Dbg->println(channelUserJoinCallbacks[i].nick);
												channelUserJoinCallbacks[i].callback(channelUserJoinCallbacks[i].userobj,
																					 _ircchannels[chanidx],
																					 channelUserJoinCallbacks[i].nick);
											}
										}
									} else {  // IRC_CMDTOKEN_PART
										for (i=0; i < IRC_CALLBACK_MAX_CHANNELNICK; i++) {
											if (channelUserPartCallbacks[i].chanidx == chanidx &&
												channelUserPartCallbacks[i].callback != NULL &&
												!strncmp(channelUserPartCallbacks[i].nick, from_nick, IRC_NICKUSER_MAXLEN)) {

												Dbg->print(">> Executing OnChannelUserPart callback for channel ");
												Dbg->print(_ircchannels[chanidx]); Dbg->print(" and nick=");
												Dbg->println(channelUserJoinCallbacks[i].nick);
												channelUserPartCallbacks[i].callback(channelUserPartCallbacks[i].userobj,
																					 _ircchannels[chanidx],
																					 channelUserPartCallbacks[i].nick);
											}
										}
									} /* if(cmdtoken == IRC_CMDTOKEN_JOIN or not) */
								}
							}
						} else {
							// A message from an invalid channel?  Odd...
							if (cmdtoken == IRC_CMDTOKEN_JOIN)
								Dbg->print(">> Received a JOIN message for a channel we don't have in our registry! (");
							else
								Dbg->print(">> Received a PART message for a channel we don't have in our registry! (");
							Dbg->print(argstart);
							Dbg->println(")");
						}
						break;


					case IRC_CMDTOKEN_PRIVMSG:
						if (is_from_user) {
							Dbg->print(">> Privmsg params: From nick="); Dbg->print(from_nick);
							Dbg->print(" (user="); Dbg->print(from_user);
							Dbg->print("); argstart="); Dbg->println(argstart);
						} else {
							Dbg->print(">> Privmsg params: argstart="); Dbg->println(argstart);
						}
						tochan = argstart;
						tonick = strstr(tochan, " ");
						if (tonick == NULL) {
							Dbg->println(">> Malformed PRIVMSG; No space between channel and message : delimiter");
							break;  // Malformed PRIVMSG line?
						}
						*tonick = '\0';
						tonick++;
						if (*tonick != ':') {
							Dbg->println(">> Malformed PRIVMSG; No : indicating start-of-message");
							break;  // Malformed PRIVMSG line, missing the : for the remaining message?
						}
						tonick++;
						tmp1 = strstr(tonick, ":");
						if (tmp1 != NULL) {
							*tmp1 = '\0';
							if (strstr(tonick, " ") != NULL) {
								*tmp1 = ' ';  // restore the space; this isn't a nick-targeted message.
							} else {
								do {
									tmp1++;
								} while (*tmp1 == ' ');
								msgstart = tmp1;
							}
						} else {
							msgstart = tonick;
							tonick = NULL;
						}

						/* At this point, chan = NUL-terminated channel name, tonick = NUL-terminated target nickname if present or NULL if not,
						 * and msgstart points to the real message.
						 */
						Dbg->print(">> Privmsg CHAN="); Dbg->print(tochan); Dbg->print(", ToNick=");
						if (tonick != NULL)
							Dbg->print(tonick);
						else
							Dbg->print("(none applicable)");
						Dbg->print(", Message=");
						Dbg->println(msgstart);

						if (tonick != NULL && strcmp(tonick, _ircnick) == 0) {
							Dbg->println(">> Message directed to us; running command processing subsystem");
							// Message directed to us; search command registry and send to callback!
							tmp1 = strstr(msgstart, " ");
							if (tmp1 != NULL) {
								*tmp1 = '\0';
								tmp1++;
							}
							found_cmd = false;
							for (i=0; i < IRC_COMMAND_REGISTRY_MAX; i++) {
								if (commandCallbackRegistry[i].cmd != NULL &&
									commandCallbackRegistry[i].callback != NULL &&
									!strcmp(msgstart, commandCallbackRegistry[i].cmd)) {  // Found a match; execute!
									found_cmd = true;
									// Handle authnicks authentication
									if (commandCallbackRegistry[i].authnicks == NULL) {
										Dbg->println(">> Executing callback");
										commandCallbackRegistry[i].callback(commandCallbackRegistry[i].userobj,
																			tochan,
																			from_nick,
																			tmp1);
									} else {
										// Source nick is in from_nick
										j = 0;
										tmp2 = commandCallbackRegistry[i].authnicks[j];
										found_authnick = false;
										while (tmp2 != NULL && tmp2[0] != '\0') {
											if (!strncmp(from_nick, tmp2, IRC_NICKUSER_MAXLEN)) {
												// Found from_nick in authnicks; proceed to execute callback
												found_authnick = true;
												Dbg->println(">> Nick authorized; executing callback");
												commandCallbackRegistry[i].callback(commandCallbackRegistry[i].userobj,
																					tochan,
																					from_nick,
																					tmp1);
												break;
											}
											j++;
											tmp2 = commandCallbackRegistry[i].authnicks[j];
										}
										if (!found_authnick) {
											Dbg->println(">> Nick not found in authnicks list.");
											if (commandCallbackRegistry[i].unauth_callback != NULL) {
												Dbg->println(">> Executing unauthorized-attempt callback for this command");
												commandCallbackRegistry[i].unauth_callback(commandCallbackRegistry[i].userobj,
																						   tochan,
																						   from_nick,
																						   tmp1);
											}
										}
									} /* if (authnicks == NULL) */
								} /* if (found a matching callback for this command) */
							} /* for (each item in command callback registry) */
							if (!found_cmd && unknownCommandCallback != NULL) {
								Dbg->println(">> Executing unknown-command callback routine");
								unknownCommandCallback(unknownCommandCallbackUserobj, tochan, from_nick, tmp1);
							}
						}
						break;

					case IRC_CMDTOKEN_ERR_ERRONEOUSNICKNAME:
					case IRC_CMDTOKEN_ERR_NICKNAMEINUSE:
					case IRC_CMDTOKEN_ERR_NICKCOLLISION:
						Dbg->println(">> Server reported nickname in use or invalid!");
						strcat(_ircnick, "_");
						botState = IRC_SERVERINIT;
						return;

					case IRC_CMDTOKEN_RPL_WELCOME:
					case IRC_CMDTOKEN_ERR_ALREADYREGISTERED:
						if (botState == IRC_REGISTERING_USER) {
							botState++;
							if (_hasmotd)
								botState++;  // Advance past user registration completely
						}
						break;

					case IRC_CMDTOKEN_ERR_YOUREBANNEDCREEP:
						Dbg->println(">> Server reported that we're banned; disabling bot.");
						end();
						return;

					default:
						if (botState == IRC_CONNECTED)
							botState++;
						break;

				}
			}
		}
	}
}

/* Callback handler maintenance - Commands */
boolean IrcBot::attachOnCommand( const char *cmd, IRC_CALLBACK_TYPE_COMMAND callback, const void *userobj )
{
	return attachOnCommand(cmd, NULL, callback, userobj);
}

boolean IrcBot::attachOnCommand( const char *cmd, const char **authnicks, IRC_CALLBACK_TYPE_COMMAND callback, const void *userobj )
{
	int i;

	for (i=0; i < IRC_COMMAND_REGISTRY_MAX; i++) {
		if (commandCallbackRegistry[i].cmd != NULL && !strcmp(commandCallbackRegistry[i].cmd, cmd)) {
			return false;  // Command already registered!
		}

		if (commandCallbackRegistry[i].cmd == NULL || commandCallbackRegistry[i].callback == NULL) {
			commandCallbackRegistry[i].cmd = (char *)cmd;
			commandCallbackRegistry[i].callback = callback;
			commandCallbackRegistry[i].unauth_callback = NULL;  // This can be initialized with attachOnCommandUnauthorized
			commandCallbackRegistry[i].userobj = (void *)userobj;
			commandCallbackRegistry[i].authnicks = (char **)authnicks;
			return true;
		}
	}
	return false;  // Out of command registry entries
}

boolean IrcBot::detachOnCommand(const char *cmd)
{
	int i;

	for (i=0; i < IRC_COMMAND_REGISTRY_MAX; i++) {
		if (commandCallbackRegistry[i].cmd != NULL && !strcmp(commandCallbackRegistry[i].cmd, cmd)) {
			commandCallbackRegistry[i].cmd = NULL;
			commandCallbackRegistry[i].callback = NULL;
			commandCallbackRegistry[i].unauth_callback = NULL;
			commandCallbackRegistry[i].userobj = NULL;
			commandCallbackRegistry[i].authnicks = NULL;
			return true;
		}
	}
	return false;  // Command not found in the command callback registry
}

boolean IrcBot::attachOnUnknownCommand( IRC_CALLBACK_TYPE_COMMAND callback, const void *userobj )
{
	if (unknownCommandCallback != NULL)
		return false;
	
	unknownCommandCallback = callback;
	unknownCommandCallbackUserobj = (void *)userobj;
	return true;
}

boolean IrcBot::detachOnUnknownCommand(void)
{
	if (unknownCommandCallback == NULL)
		return false;
	
	unknownCommandCallback = NULL;
	unknownCommandCallbackUserobj = NULL;
	return true;
}

boolean IrcBot::attachOnCommandUnauthorized( const char *cmd, IRC_CALLBACK_TYPE_COMMAND callback )
{
	int i;

	for (i=0; i < IRC_COMMAND_REGISTRY_MAX; i++) {
		if (commandCallbackRegistry[i].cmd != NULL && !strcmp(commandCallbackRegistry[i].cmd, cmd)) {
			commandCallbackRegistry[i].unauth_callback = callback;
			return true;
		}
	}

	return false;  // Command not found in registry
}

boolean IrcBot::detachOnCommandUnauthorized( const char *cmd )
{
	int i;

	for (i=0; i < IRC_COMMAND_REGISTRY_MAX; i++) {
		if (commandCallbackRegistry[i].cmd != NULL && !strcmp(commandCallbackRegistry[i].cmd, cmd)) {
			commandCallbackRegistry[i].unauth_callback = NULL;
			return true;
		}
	}

	return false;  // Command not found in registry
}

/* Callback handler maintenance - Connect/Disconnect */
boolean IrcBot::attachOnConnect(IRC_CALLBACK_TYPE_CONNECT callback, const void *userobj)
{
	if (connectCallback != NULL)
		return false;  // Already registered!

	connectCallback = callback;
	connectCallbackUserobj = (void *)userobj;
	return true;
}

boolean IrcBot::detachOnConnect(void)
{
	if (connectCallback == NULL)
		return false;  // Not registered in the first place!

	connectCallback = NULL;
	connectCallbackUserobj = NULL;
	return true;
}

void IrcBot::executeOnConnectCallback(void)
{
	if (connectCallback != NULL) {
		Dbg->println(">> Executing OnConnect callback");
		connectCallback(connectCallbackUserobj);
	}
}

boolean IrcBot::attachOnDisconnect(IRC_CALLBACK_TYPE_CONNECT callback, const void *userobj)
{
	if (disconnectCallback != NULL)
		return false;  // Already registered!

	disconnectCallback = callback;
	disconnectCallbackUserobj = (void *)userobj;
	return true;
}

boolean IrcBot::detachOnDisconnect(void)
{
	if (disconnectCallback == NULL)
		return false;  // Not registered in the first place!

	disconnectCallback = NULL;
	disconnectCallbackUserobj = NULL;
	return true;
}

void IrcBot::executeOnDisconnectCallback(void)
{
	if (disconnectCallback != NULL) {
		Dbg->println(">> Executing OnDisconnect callback");
		disconnectCallback(disconnectCallbackUserobj);
	}
}

/* Callback handler maintenance - Channel Join/Part (Us only) */
boolean IrcBot::attachOnJoin(const char *channel, IRC_CALLBACK_TYPE_CHANNEL callback, const void *userobj)
{
	int i;

	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (_ircchannels[i][0] != '\0' && !strncmp(channel, _ircchannels[i], IRC_CHANNEL_MAXLEN)) {
			if (channelJoinCallbacks[i].callback == NULL) {
				channelJoinCallbacks[i].callback = callback;
				channelJoinCallbacks[i].userobj = (void *)userobj;
				return true;
			} else {
				return false;  // Channel found, but, a handler is already registered!
			}
		}
	}
	return false;  // Channel not found in current bot configuration
}

boolean IrcBot::detachOnJoin(const char *channel)
{
	int i;

	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (_ircchannels[i][0] != '\0' && !strncmp(channel, _ircchannels[i], IRC_CHANNEL_MAXLEN)) {
			if (channelJoinCallbacks[i].callback != NULL) {
				channelJoinCallbacks[i].callback = NULL;
				channelJoinCallbacks[i].userobj = NULL;
				return true;
			} else {
				return false;  // Channel found, but, no handler was registered.
			}
		}
	}
	return false;  // Channel not found in current bot configuration
}

void IrcBot::executeOnChannelJoinCallback(const int chanidx)
{
	if (channelJoinCallbacks[chanidx].callback != NULL) {
		Dbg->print(">> Executing OnChannelJoin callback for channel "); Dbg->println(_ircchannels[chanidx]);
		channelJoinCallbacks[chanidx].callback(channelJoinCallbacks[chanidx].userobj, _ircchannels[chanidx]);
	}
}

boolean IrcBot::attachOnPart(const char *channel, IRC_CALLBACK_TYPE_CHANNEL callback, const void *userobj)
{
	int i;

	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (_ircchannels[i][0] != '\0' && !strncmp(channel, _ircchannels[i], IRC_CHANNEL_MAXLEN)) {
			if (channelPartCallbacks[i].callback == NULL) {
				channelPartCallbacks[i].callback = callback;
				channelPartCallbacks[i].userobj = (void *)userobj;
				return true;
			} else {
				return false;  // Channel found, but, a handler is already registered!
			}
		}
	}
	return false;  // Channel not found in current bot configuration
}

boolean IrcBot::detachOnPart(const char *channel)
{
	int i;

	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (_ircchannels[i][0] != '\0' && !strncmp(channel, _ircchannels[i], IRC_CHANNEL_MAXLEN)) {
			if (channelPartCallbacks[i].callback != NULL) {
				channelPartCallbacks[i].callback = NULL;
				channelPartCallbacks[i].userobj = NULL;
				return true;
			} else {
				return false;  // Channel found, but, no handler was registered.
			}
		}
	}
	return false;  // Channel not found in current bot configuration
}

void IrcBot::executeOnChannelPartCallback(const int chanidx)
{
	if (channelPartCallbacks[chanidx].callback != NULL) {
		Dbg->print(">> Executing OnChannelPart callback for channel "); Dbg->println(_ircchannels[chanidx]);
		channelPartCallbacks[chanidx].callback(channelPartCallbacks[chanidx].userobj, _ircchannels[chanidx]);
	}
}


/* Callback handler maintenance - Channel Join/Part (Other arbitrary nicks) */
boolean IrcBot::attachOnUserJoin(const char *channel, const char *nick, IRC_CALLBACK_TYPE_CHANNEL_USER callback, const void *userobj)
{
	int i, j, regidx;

	// Find a slot in the ChanUserCallbackRegistry
	for (regidx = 0; regidx < IRC_CALLBACK_MAX_CHANNELNICK; regidx++) {
		if (channelUserJoinCallbacks[regidx].callback == NULL)
			break;
	}
	if (regidx == IRC_CALLBACK_MAX_CHANNELNICK)
		return false;  // No more channel+nick callback registry slots!
	
	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (_ircchannels[i][0] != '\0' && !strncmp(channel, _ircchannels[i], IRC_CHANNEL_MAXLEN)) {
			// We found a matching channel; re-search the registry to make sure this isn't a duplicate.
			for (j=0; j < IRC_CALLBACK_MAX_CHANNELNICK; j++) {
				if (channelUserJoinCallbacks[j].callback != NULL &&
					channelUserJoinCallbacks[j].chanidx == i &&
					channelUserJoinCallbacks[j].nick[0] != '\0' &&
					!strncmp(channelUserJoinCallbacks[j].nick, nick, IRC_NICKUSER_MAXLEN))
					return false;  // This channel+nick combination has already been registered!
			}

			// All clear; go ahead and register.
			channelUserJoinCallbacks[regidx].callback = callback;
			channelUserJoinCallbacks[regidx].chanidx = i;
			strncpy(channelUserJoinCallbacks[regidx].nick, nick, IRC_NICKUSER_MAXLEN-1);
			channelUserJoinCallbacks[regidx].userobj = (void *)userobj;
			return true;
		}
	}
	return false;  // Channel not found in current bot configuration
}

boolean IrcBot::detachOnUserJoin(const char *channel, const char *nick)
{
	int i, j;

	// Find the channel in the bot's channel registry
	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (_ircchannels[i][0] != '\0' && !strncmp(_ircchannels[i], channel, IRC_CHANNEL_MAXLEN))
			break;
	}
	if (i == IRC_CHANNEL_MAX)
		return false;  // Channel not found in current bot configuration
	
	for (j=0; j < IRC_CALLBACK_MAX_CHANNELNICK; j++) {
		if (channelUserJoinCallbacks[j].callback != NULL &&
			channelUserJoinCallbacks[j].chanidx == i &&
			!strncmp(channelUserJoinCallbacks[j].nick, nick, IRC_NICKUSER_MAXLEN)) {
			channelUserJoinCallbacks[j].callback = NULL;
			channelUserJoinCallbacks[j].chanidx = -1;
			channelUserJoinCallbacks[j].userobj = NULL;
			channelUserJoinCallbacks[j].nick[0] = '\0';
			return true;
		}
	}
	return false;  // Channel+Nick combination not found in registry
}

boolean IrcBot::attachOnUserPart(const char *channel, const char *nick, IRC_CALLBACK_TYPE_CHANNEL_USER callback, const void *userobj)
{
	int i, j, regidx;

	// Find a slot in the ChanUserCallbackRegistry
	for (regidx = 0; regidx < IRC_CALLBACK_MAX_CHANNELNICK; regidx++) {
		if (channelUserPartCallbacks[regidx].callback == NULL)
			break;
	}
	if (regidx == IRC_CALLBACK_MAX_CHANNELNICK)
		return false;  // No more channel+nick callback registry slots!
	
	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (_ircchannels[i][0] != '\0' && !strncmp(channel, _ircchannels[i], IRC_CHANNEL_MAXLEN)) {
			// We found a matching channel; re-search the registry to make sure this isn't a duplicate.
			for (j=0; j < IRC_CALLBACK_MAX_CHANNELNICK; j++) {
				if (channelUserPartCallbacks[j].callback != NULL &&
					channelUserPartCallbacks[j].chanidx == i &&
					channelUserPartCallbacks[j].nick[0] != '\0' &&
					!strncmp(channelUserPartCallbacks[j].nick, nick, IRC_NICKUSER_MAXLEN))
					return false;  // This channel+nick combination has already been registered!
			}

			// All clear; go ahead and register.
			channelUserPartCallbacks[regidx].callback = callback;
			channelUserPartCallbacks[regidx].chanidx = i;
			strncpy(channelUserPartCallbacks[regidx].nick, nick, IRC_NICKUSER_MAXLEN-1);
			channelUserPartCallbacks[regidx].userobj = (void *)userobj;
			return true;
		}
	}
	return false;  // Channel not found in current bot configuration
}

boolean IrcBot::detachOnUserPart(const char *channel, const char *nick)
{
	int i, j;

	// Find the channel in the bot's channel registry
	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (_ircchannels[i][0] != '\0' && !strncmp(_ircchannels[i], channel, IRC_CHANNEL_MAXLEN))
			break;
	}
	if (i == IRC_CHANNEL_MAX)
		return false;  // Channel not found in current bot configuration
	
	for (j=0; j < IRC_CALLBACK_MAX_CHANNELNICK; j++) {
		if (channelUserPartCallbacks[j].callback != NULL &&
			channelUserPartCallbacks[j].chanidx == i &&
			!strncmp(channelUserPartCallbacks[j].nick, nick, IRC_NICKUSER_MAXLEN)) {
			channelUserPartCallbacks[j].callback = NULL;
			channelUserPartCallbacks[j].chanidx = -1;
			channelUserPartCallbacks[j].userobj = NULL;
			channelUserPartCallbacks[j].nick[0] = '\0';
			return true;
		}
	}
	return false;  // Channel+Nick combination not found in registry
}


boolean IrcBot::flushUserJoinOrPartByChanIdx(const int chanidx)
{
	int j;

	if (chanidx < 0 || chanidx >= IRC_CHANNEL_MAX)
		return false;

	for (j=0; j < IRC_CALLBACK_MAX_CHANNELNICK; j++) {
		if (channelUserPartCallbacks[j].callback != NULL &&
			channelUserPartCallbacks[j].chanidx == chanidx) {

			channelUserPartCallbacks[j].callback = NULL;
			channelUserPartCallbacks[j].chanidx = -1;
			channelUserPartCallbacks[j].userobj = NULL;
			channelUserPartCallbacks[j].nick[0] = '\0';
		}
		if (channelUserJoinCallbacks[j].callback != NULL &&
			channelUserJoinCallbacks[j].chanidx == chanidx) {

			channelUserJoinCallbacks[j].callback = NULL;
			channelUserJoinCallbacks[j].chanidx = -1;
			channelUserJoinCallbacks[j].userobj = NULL;
			channelUserJoinCallbacks[j].nick[0] = '\0';
		}
	}

	return true;
}

boolean IrcBot::flushUserJoinOrPart(const char *channel)
{
	int i, j;

	// Find the channel in the bot's channel registry
	for (i=0; i < IRC_CHANNEL_MAX; i++) {
		if (_ircchannels[i][0] != '\0' && !strncmp(_ircchannels[i], channel, IRC_CHANNEL_MAXLEN))
			break;
	}
	if (i == IRC_CHANNEL_MAX)
		return false;  // Channel not found in current bot configuration

	return flushUserJoinOrPartByChanIdx(i);
}



// Parse IRC User specification e.g. Nick!~Username@Hostname into their disparate components.
boolean IrcBot::parseUserHostString(const void *str, char *nick, char *user, char *host)
{
	const char *cstr = (const char *)str;
	char *arg0, *arg1, *arg2, *arg3;

	if (str == NULL)
		return false;

	arg0 = (char *)cstr;
	if (*arg0 == ':')
		arg0++;
	// Process nick
	arg1 = strstr(arg0, "!");
	if (arg1 == NULL)
		return false;
	*arg1 = '\0';
	arg1++;
	if (nick != NULL)
		strcpy(nick, arg0);

	// Process user
	if (*arg1 == '~')
		arg1++;
	arg2 = strstr(arg1, "@");
	if (arg2 == NULL)
		return false;
	*arg2 = '\0';
	arg2++;
	if (user != NULL)
		strcpy(user, arg1);

	// Process hostname
	if (host != NULL)
		strcpy(host, arg2);

	return true;
}


int IrcBot::ircProtocolCommandToken(const char *cmd)
{
	if (cmd[0] >= '0' && cmd[0] <= '9') {  // Numeric reply code
		return atoi(cmd);
	} else {
		if (!strcmp(cmd, "PRIVMSG")) return IRC_CMDTOKEN_PRIVMSG;
		if (!strcmp(cmd, "NOTICE")) return IRC_CMDTOKEN_NOTICE;
		if (!strcmp(cmd, "PASS")) return IRC_CMDTOKEN_PASS;
		if (!strcmp(cmd, "NICK")) return IRC_CMDTOKEN_NICK;
		if (!strcmp(cmd, "USER")) return IRC_CMDTOKEN_USER;
		if (!strcmp(cmd, "OPER")) return IRC_CMDTOKEN_OPER;
		if (!strcmp(cmd, "MODE")) return IRC_CMDTOKEN_MODE;
		if (!strcmp(cmd, "SERVICE")) return IRC_CMDTOKEN_SERVICE;
		if (!strcmp(cmd, "QUIT")) return IRC_CMDTOKEN_QUIT;
		if (!strcmp(cmd, "JOIN")) return IRC_CMDTOKEN_JOIN;
		if (!strcmp(cmd, "PART")) return IRC_CMDTOKEN_PART;
		if (!strcmp(cmd, "TOPIC")) return IRC_CMDTOKEN_TOPIC;
		if (!strcmp(cmd, "INVITE")) return IRC_CMDTOKEN_INVITE;
		if (!strcmp(cmd, "KICK")) return IRC_CMDTOKEN_KICK;
		if (!strcmp(cmd, "PING")) return IRC_CMDTOKEN_PING;
		if (!strcmp(cmd, "PONG")) return IRC_CMDTOKEN_PONG;
	}
	return -1;
}

void IrcBot::argToken(char *buffer, CmdTok *ts)
{
	char c;

	ts->argc = 1;
	ts->buffer = buffer;
	ts->argv[0] = buffer;
	while ( (c = *buffer) != '\0' ) {
		if (c == ' ' || c == '\t') {
			*buffer = '\0';
			do {
				buffer++;
			} while (*buffer == ' ' || *buffer == '\t');
			ts->argc++;
			ts->argv[ts->argc-1] = buffer;
			if (ts->argc == IRC_CMDTOK_MAX)
				return;
		} else {
			buffer++;
		}
	}

	return;
}

const IrcReplyCode ircReplyCodeDatabase[] = {
	{901, "PRIVMSG"},
	{902, "NOTICE"},
	{903, "PASS"},
	{904, "NICK"},
	{905, "USER"},
	{906, "OPER"},
	{907, "MODE"},
	{908, "SERVICE"},
	{909, "QUIT"},
	{910, "JOIN"},
	{911, "PART"},
	{912, "TOPIC"},
	{913, "INVITE"},
	{914, "KICK"},
	{915, "PING"},
	{916, "PONG"},
	{1, "RPL_WELCOME"},
	{2, "RPL_YOURHOST"},
	{3, "RPL_CREATED"},
	{4, "RPL_MYINFO"},
	{5, "RPL_BOUNCE"},
	{302, "RPL_USERHOST"},
	{303, "RPL_ISON"},
	{301, "RPL_AWAY"},
	{305, "RPL_UNAWAY"},
	{306, "RPL_NOWAWAY"},
	{311, "RPL_WHOISUSER"},
	{312, "RPL_WHOISSERVER"},
	{313, "RPL_WHOISOPERATOR"},
	{317, "RPL_WHOISIDLE"},
	{318, "RPL_ENDOFWHOIS"},
	{319, "RPL_WHOISCHANNELS"},
	{314, "RPL_WHOWASUSER"},
	{369, "RPL_ENDOFWHOWAS"},
	{322, "RPL_LIST"},
	{323, "RPL_LISTEND"},
	{325, "RPL_UNIQOPIS"},
	{324, "RPL_CHANNELMODEIS"},
	{331, "RPL_NOTOPIC"},
	{332, "RPL_TOPIC"},
	{341, "RPL_INVITING"},
	{346, "RPL_INVITELIST"},
	{347, "RPL_ENDOFINVITELIST"},
	{348, "RPL_EXCEPTLIST"},
	{349, "RPL_ENDOFEXCEPTLIST"},
	{351, "RPL_VERSION"},
	{352, "RPL_WHOREPLY"},
	{315, "RPL_ENDOFWHO"},
	{353, "RPL_NAMREPLY"},
	{366, "RPL_ENDOFNAMES"},
	{367, "RPL_BANLIST"},
	{368, "RPL_ENDOFBANLIST"},
	{371, "RPL_INFO"},
	{374, "RPL_ENDOFINFO"},
	{375, "RPL_MOTDSTART"},
	{372, "RPL_MOTD"},
	{376, "RPL_ENDOFMOTD"},
	{381, "RPL_YOUREOPER"},
	{391, "RPL_TIME"},
	{221, "RPL_UMODEIS"},
	{263, "RPL_TRYAGAIN"},
	{401, "ERR_NOSUCHNICK"},
	{402, "ERR_NOSUCHSERVER"},
	{403, "ERR_NOSUCHCHANNEL"},
	{404, "ERR_CANNOTSENDTOCHAN"},
	{405, "ERR_TOOMANYCHANNELS"},
	{406, "ERR_WASNOSUCHNICK"},
	{407, "ERR_TOOMANYTARGETS"},
	{408, "ERR_NOSUCHSERVICE"},
	{409, "ERR_NOORIGIN"},
	{411, "ERR_NORECIPIENT"},
	{412, "ERR_NOTEXTTOSEND"},
	{413, "ERR_NOTOPLEVEL"},
	{414, "ERR_WILDTOPLEVEL"},
	{415, "ERR_BADMASK"},
	{421, "ERR_UNKNOWNCOMMAND"},
	{422, "ERR_NOMOTD"},
	{431, "ERR_NONICKNAMEGIVEN"},
	{432, "ERR_ERRONEOUSNICKNAME"},
	{433, "ERR_NICKNAMEINUSE"},
	{436, "ERR_NICKCOLLISION"},
	{437, "ERR_UNAVAILRESOURCE"},
	{441, "ERR_USERNOTINCHANNEL"},
	{442, "ERR_NOTONCHANNEL"},
	{443, "ERR_USERONCHANNEL"},
	{451, "ERR_NOTREGISTERED"},
	{461, "ERR_NEEDMOREPARAMS"},
	{462, "ERR_ALREADYREGISTERED"},
	{463, "ERR_NOPERMFORHOST"},
	{464, "ERR_PASSWDMISMATCH"},
	{465, "ERR_YOUREBANNEDCREEP"},
	{466, "ERR_YOUWILLBEBANNED"},
	{467, "ERR_KEYSET"},
	{471, "ERR_CHANNELISFULL"},
	{472, "ERR_UNKNOWNMODE"},
	{473, "ERR_INVITEONLYCHAN"},
	{474, "ERR_BANNEDFROMCHAN"},
	{475, "ERR_BADCHANNELKEY"},
	{476, "ERR_BADCHANMASK"},
	{477, "ERR_NOCHANMODES"},
	{478, "ERR_BANLISTFULL"},
	{481, "ERR_NOPRIVILEGES"},
	{482, "ERR_CHANOPRIVSNEEDED"},
	{484, "ERR_RESTRICTED"},
	{485, "ERR_UNIQOPPRIVSNEEDED"},
	{501, "ERR_UMODEUNKNOWNFLAG"},
	{502, "ERR_USERSDONTMATCH"},
	{0, NULL}
};

const char *IrcBot::ircReplyCodeStrerror(unsigned int cmdtoken)
{
	int i = 0;

	while (ircReplyCodeDatabase[i].description != NULL) {
		if (cmdtoken == ircReplyCodeDatabase[i].code)
			return ircReplyCodeDatabase[i].description;
		i++;
	}
	return "(not found)";
}
