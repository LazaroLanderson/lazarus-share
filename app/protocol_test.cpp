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
    check(Protocol::secret("INVALID").isEmpty());
    check(Protocol::room(key).size() == 64);
    Protocol::Channel host(key, "session", "peer", "hostnonce", "guestnonce", true);
    Protocol::Channel guest(key, "session", "peer", "guestnonce", "hostnonce", false);
    QJsonObject body;
    auto signedFrame = host.seal({{"kind", "offer"}, {"sdp", "fingerprint: original"}});
    auto tampered = signedFrame; tampered["payload"] = signedFrame["payload"].toString() + "A";
    check(!guest.open(tampered, body));
    check(guest.open(signedFrame, body) && body["kind"] == "offer");
    check(!guest.open(signedFrame, body));
    Protocol::Channel fresh(key, "session", "peer", "freshnonce", "hostnonce", false);
    check(!fresh.open(signedFrame, body));
    Protocol::Channel other(Protocol::randomBytes(16), "session", "peer", "guestnonce", "hostnonce", false);
    check(!other.open(signedFrame, body));
    check(host.open(guest.seal({{"kind", "answer"}}), body));
    std::cout << "Protocol authentication, token, replay and challenge checks passed\n";
}
