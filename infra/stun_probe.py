"""Manually verify STUN discovery and refusal of TURN relay allocation."""
import argparse
import os
import socket
import struct

COOKIE = 0x2112A442


def request(sock, host, port, kind, attributes=b'', timeout=2):
    transaction = os.urandom(12)
    packet = struct.pack('!HHI', kind, len(attributes), COOKIE) + transaction + attributes
    sock.sendto(packet, (host, port))
    sock.settimeout(timeout)
    response, _ = sock.recvfrom(4096)
    if len(response) < 20 or response[8:20] != transaction:
        raise RuntimeError('STUN transaction mismatch')
    response_kind, length, cookie = struct.unpack('!HHI', response[:8])
    if cookie != COOKIE or length + 20 != len(response):
        raise RuntimeError('Malformed STUN response')
    return response_kind, response[20:]


def probe(host, port):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        kind, attributes = request(sock, host, port, 1)
        if kind != 0x101:
            raise RuntimeError('STUN binding was not accepted')
        found = False
        while len(attributes) >= 4:
            attribute, length = struct.unpack('!HH', attributes[:4])
            value = attributes[4:4 + length]
            if attribute == 0x20 and len(value) >= 8 and value[1] == 1:
                mapped_port = struct.unpack('!H', value[2:4])[0] ^ (COOKIE >> 16)
                found = mapped_port > 0
            attributes = attributes[4 + ((length + 3) & ~3):]
        if not found:
            raise RuntimeError('STUN did not return an IPv4 XOR-MAPPED-ADDRESS')
        print('STUN binding passed; mapped address omitted')
        try:
            # REQUESTED-TRANSPORT UDP, as a TURN allocation request.
            kind, _ = request(sock, host, port, 3, struct.pack('!HHBBBB', 0x19, 4, 17, 0, 0, 0), timeout=1)
            if kind == 0x103:
                raise RuntimeError('Unexpected TURN allocation: STUN service must not relay media')
        except socket.timeout:
            pass
        print('Unauthenticated TURN relay allocation unavailable')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--port', type=int, default=3478)
    args = parser.parse_args()
    probe(args.host, args.port)
