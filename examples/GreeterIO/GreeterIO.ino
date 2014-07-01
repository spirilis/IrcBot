/* BasicResponse - IRC bot sketch which responds to some basic commands. */
#include <IrcBot.h>
#include <Ethernet.h>
#include <EthernetClient.h>

byte ourMac[] = { 0x52, 0x54, 0xFF, 0xFF, 0xFF, 0x01 };

IrcBot irc;

// Add nicknames to this list in order to authorize them to run the "nick" and "io" commands.
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
  irc.attachOnCommand("io", authnicks, HandleMemoryIO, NULL);
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

void *parseTextToPtr(const char *txt)
{
  int len = strlen(txt), i = len - 1;
  long j;
  char c;
  uint32_t ptr = 0;

  if (strncmp(txt, "0x", 2) != 0) {
    // Probably a decimal address value
    ptr = atol(txt);
  } else {
    if (len > 10)
      return NULL;

    while (i > 1) {
      c = txt[i];
      if (c >= '0' && c <= '9')
        j = c-'0';
      else if (c >= 'A' && c <= 'F')
        j = c-'A'+10;
      else if (c >= 'a' && c <= 'f')
        j = c-'a'+10;
      else
        return NULL;

      ptr |= j << ((len-i-1)*4);
      i--;
    }
  }
  return (void *)ptr;
}

const char hexdigits[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

void HandleMemoryIO(void *userobj, const char *chan, const char *nick, const char *message)
{
  CmdTok args;
  void *ptr;
  int i, j;
  char outbuf[448], *cur = &outbuf[0];
  uint8_t a, b;

  irc.argToken((char *)message, &args);
  if (args.argc < 3) {
    // Invalid command
    irc.sendPrivmsg(chan, nick, "Invalid syntax; insufficient arguments");
    return;
  }

  if (!strcmp(args.argv[0], "rd")) {
    ptr = parseTextToPtr(args.argv[1]);
    j = atoi(args.argv[2]);
    if (j) {
      for (i=0; i < j && i < 148; i++) {
        *cur++ = hexdigits[ *((uint8_t *)ptr+i) >> 4 ];
        *cur++ = hexdigits[ *((uint8_t *)ptr+i) & 0x0F ];
        *cur++ = ' ';
      }
      *cur = '\0';
      irc.sendPrivmsg(chan, nick, outbuf);
    }
  } else if (!strcmp(args.argv[0], "rda")) {
    ptr = parseTextToPtr(args.argv[1]);
    j = atoi(args.argv[2]);
    if (j) {
      for (i=0; i < j && i < 148; i++) {
        *cur++ = *((char *)ptr+i);
        *cur++ = ' ';
      }
      *cur = '\0';
      irc.sendPrivmsg(chan, nick, outbuf);
    }
  } else if (!strcmp(args.argv[0], "wr")) {
    ptr = parseTextToPtr(args.argv[1]);

    // Validate input
    for (i = 0; i < args.argc-2; i++) {
      Serial.print(">> Validating input byte specifier: "); Serial.println(args.argv[i+2]);
      if (strlen(args.argv[i+2]) > 2) {
        irc.sendPrivmsg(chan, nick, "Invalid byte found in write stream; aborting");
        return;
      }
      cur = args.argv[i+2];
      while (*cur != '\0') {
        if (*cur < '0' || (*cur > '9' && *cur < 'A') || (*cur > 'F' && *cur < 'a') || (*cur > 'f')) {
          irc.sendPrivmsg(chan, nick, "Invalid byte found in write stream; aborting");
          return;
        }
      }
    }

    // Input validated; commit writes
    for (i = 0; i < args.argc-2; i++) {
      cur = args.argv[i+2];
      b = 0;
      for (j=strlen(cur)-1; j >= 0; j--) {
        if (cur[j] >= '0' && cur[j] <= '9')
          a = cur[j] - '0';
        else if (cur[j] >= 'A' && cur[j] <= 'F')
          a = cur[j] - 'A' + 10;
        else if (cur[j] >= 'a' && cur[j] <= 'f')
          a = cur[j] - 'a' + 10;
        b |= a << ((1-j)*4);
      }
      Serial.print(">> Writing hex value "); Serial.print(b, HEX); Serial.print(" to address "); Serial.println((uint32_t)ptr, HEX);
      *((uint8_t *)ptr+i) = b;
    }
    irc.sendPrivmsg(chan, nick, "Write committed.");
  }
}
