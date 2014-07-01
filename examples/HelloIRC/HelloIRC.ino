/* HelloIRC - Basic IRC bot application which merely connects. */
#include <IrcBot.h>
#include <Ethernet.h>
#include <EthernetClient.h>

byte ourMac[] = { 0x52, 0x54, 0xFF, 0xFF, 0xFF, 0x01 };

IrcBot irc;

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
