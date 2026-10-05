#include "protocol.h"
#include <QCoreApplication>
#include <cstdlib>
#include <iostream>
static void check(bool ok) { if (!ok) { std::cerr << "Protocol check failed\n"; std::exit(1); } }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto key = Protocol::randomBytes(16);
    check(Protocol::secret(Protocol::token(key)) == key);
    check(Protocol::token(key).size() == 26);
    const auto link = Protocol::inviteLink(key);
    check(Protocol::inviteSecret(link) == key);
    check(Protocol::inviteSecret("lazarus-share://join#" + Protocol::token(key)) == key);
    for (const auto &bad : {Protocol::token(key), QString("https://evil.example/join#") + Protocol::token(key),
         QString("https://share.app.lazaruslabs.com.br/join?token=") + Protocol::token(key),
         QString("https://share.app.lazaruslabs.com.br:443/join#") + Protocol::token(key),
         QString("lazarus-share://join/path#") + Protocol::token(key),
         QString("https://user@share.app.lazaruslabs.com.br/join#") + Protocol::token(key),
         QString("https://share.app.lazaruslabs.com.br/join#INVALID")}) check(Protocol::inviteSecret(bad).isEmpty());
    check(Protocol::secret("INVALID").isEmpty());
    check(Protocol::room(key).size() == 64);
    Protocol::Channel host(key, "session", "host", "guest", "hostnonce", "guestnonce", 1);
    Protocol::Channel guest(key, "session", "guest", "host", "guestnonce", "hostnonce", 1);
    QJsonObject body;
    auto signedFrame = host.seal({{"kind", "offer"}, {"sdp", "fingerprint: original"}});
    auto tampered = signedFrame; tampered["payload"] = signedFrame["payload"].toString() + "A";
    check(!guest.open(tampered, body));
    check(guest.open(signedFrame, body) && body["kind"] == "offer");
    check(!guest.open(signedFrame, body));
    Protocol::Channel fresh(key, "session", "guest", "host", "freshnonce", "hostnonce", 1);
    check(!fresh.open(signedFrame, body));
    Protocol::Channel other(Protocol::randomBytes(16), "session", "guest", "host", "guestnonce", "hostnonce", 1);
    check(!other.open(signedFrame, body));
    check(host.open(guest.seal({{"kind", "answer"}}), body));
    Protocol::Channel a(key,"v2session","alice","bob","anonce","bnonce",7);
    Protocol::Channel b(key,"v2session","bob","alice","bnonce","anonce",7);
    auto v2=a.seal({{"kind","restart"}});
    check(v2["peer"]=="bob" && v2["revision"].toInteger()==7);
    check(b.open(v2,body)); check(!b.open(v2,body));
    Protocol::Channel oldRevision(key,"v2session","bob","alice","bnonce","anonce",6);
    Protocol::Channel wrongSender(key,"v2session","bob","carol","bnonce","anonce",7);
    Protocol::Channel wrongRecipient(key,"v2session","carol","alice","bnonce","anonce",7);
    Protocol::Channel newSession(key,"otherSession","bob","alice","bnonce","anonce",7);
    check(!oldRevision.open(v2,body)); check(!wrongSender.open(v2,body));
    check(!wrongRecipient.open(v2,body)); check(!newSession.open(v2,body));
    check(a.open(b.seal({{"kind","answer"}}),body));
    std::cout << "Protocol authentication, token, replay and challenge checks passed\n";
}
