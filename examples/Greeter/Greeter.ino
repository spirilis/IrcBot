/* BasicResponse - IRC bot sketch which responds to some basic commands. */
#include <IrcBot.h>
#include <Ethernet.h>
#include <EthernetClient.h>

byte ourMac[] = { 0x52, 0x54, 0xFF, 0xFF, 0xFF, 0x01 };

IrcBot irc;

// Add nicknames to this list in order to authorize them to run the "nick" command.
const char *authnicks[] = {
  "Spirilis",
  NULL
};

void setup() {
  int i;
  IPAddress ip;

  Serial.setBufferSize(2048, 64);
  Serial.begin(115200);
  for (i=0; i < 3; i++) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println(" BEGIN!");

  Serial.println("Performing DHCP:");
  if (!Ethernet.begin(ourMac)) {
    Serial.println("Failed to configure Ethernet using DHCP.  Halting.");
    while(1) delay(1000);
  }
  Serial.print("Done; IP = ");
  ip = Ethernet.localIP();
  ip.printTo(Serial);
  Serial.println();

  Serial.println("Initializing IRC bot:");
  Serial.print("adding channel #energia as idx = "); Serial.println(irc.addChannel("#energia"));

  irc.attachOnCommand("hi", HandleHi, NULL);
  irc.attachOnCommand("die", KillBot, NULL);
  irc.attachOnCommand("roll", RollOver, NULL);
  irc.attachOnCommand("remember", RememberMe, NULL);
  irc.attachOnCommand("forget", ForgetMe, NULL);
  irc.attachOnCommand("nick", authnicks, ChangeNick, NULL);
  irc.attachOnUserJoin("#energia", "Spirilis", MeetAndGreet, "My Master");

  Serial.println("Issuing irc.begin():");
  irc.begin();
}

void loop() {
  static uint32_t lastmillis;

  if (millis()-lastmillis > 1000) {
    Serial.print("botState = "); Serial.println(irc.getStateStrerror());
    lastmillis = millis();
  }
  irc.loop();  // Main worker for IRC bot; runs the whole state machine
}

void HandleHi(void *userobj, const char *chan, const char *nick, const char *message)
{
  char tmpbuf[IRC_NICKUSER_MAXLEN + 32];

  Serial.println(">> Executing HandleHi callback handler function");
  strcpy(tmpbuf, "Hi there, ");
  strcat(tmpbuf, nick);
  strcat(tmpbuf, "!");
  irc.sendPrivmsg(chan, nick, tmpbuf);
}

void KillBot(void *userobj, const char *chan, const char *nick, const char *message)
{
  Serial.println(">> Executing KillBot callback handler function");
  irc.sendPrivmsg(chan, NULL, "Bye for now!");
  irc.end();
}

void MeetAndGreet(void *userobj, const char *chan, const char *nick)  // OnUserJoin doesn't take a message
{
  char *override = (char *)userobj, tmpbuf[IRC_NICKUSER_MAXLEN+32];

  Serial.print(">> Greeting "); Serial.print(nick); Serial.print(" to "); Serial.println(chan);

  strcpy(tmpbuf, "Nice to see you again,");
  if (override != NULL) {
    if (*override != ',' && *override != ':' && *override != ';')
      strcat(tmpbuf, " ");
    strcat(tmpbuf, override);
  } else {
    strcat(tmpbuf, " ");
    strcat(tmpbuf, nick);
  }
  strcat(tmpbuf, "!");
  irc.sendPrivmsg(chan, nick, tmpbuf);
}

void RollOver(void *userobj, const char *chan, const char *nick, const char *message)
{
  Serial.println(">> Issuing CTCP ACTION rolls over on his belly.");
  irc.sendPrivmsgCtcp(chan, "ACTION", "rolls over on his belly.");
}

void RememberMe(void *userobj, const char *chan, const char *nick, const char *message)
{
  if (strstr(message, "me") == NULL)
    return;
  
  if (!irc.attachOnUserJoin(chan, nick, MeetAndGreet, NULL)) {
    irc.sendPrivmsg(chan, nick, "Sorry, my loyalties have been spread thin, and I simply won't ever remember your name!");
  }
}

void ForgetMe(void *userobj, const char *chan, const char *nick, const char *message)
{
  if (strstr(message, "me") == NULL)
    return;
  
  irc.detachOnUserJoin(chan, nick);
}

void ChangeNick(void *userobj, const char *chan, const char *nick, const char *message)
{
  char *tmp1;

  tmp1 = strstr(message, " ");
  if (tmp1 != NULL)
    *tmp1 = '\0';
  Serial.print(">> Changing NICK to "); Serial.println(message);
  irc.setNick(message);
}
