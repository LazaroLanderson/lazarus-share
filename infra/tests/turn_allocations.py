"""Authenticated TCP allocations against an isolated Coturn fixture; no raw logs."""
import base64
import hashlib
import hmac
import secrets
import socket
import struct
import time

MAGIC = 0x2112A442

def attribute(kind, value):
    return struct.pack('!HH', kind, len(value)) + value + b'\0' * (-len(value) % 4)

def exact(connection, length):
    result = b''
    while len(result) < length:
        chunk = connection.recv(length - len(result))
        if not chunk:
            raise AssertionError('TURN connection closed during allocation')
        result += chunk
    return result

def exchange(connection, body, key=None):
    transaction = secrets.token_bytes(12)
    header = struct.pack('!HHI12s', 3, len(body) + (24 if key else 0), MAGIC, transaction)
    if key:
        body += attribute(8, hmac.new(key, header + body, hashlib.sha1).digest())
    connection.sendall(header + body)
    response = exact(connection, 20)
    kind, length, magic, received = struct.unpack('!HHI12s', response)
    assert magic == MAGIC and received == transaction, 'Unexpected TURN response'
    payload = exact(connection, length)
    attributes = {}
    offset = 0
    while offset + 4 <= length:
        tag, size = struct.unpack('!HH', payload[offset:offset + 4])
        attributes[tag] = payload[offset + 4:offset + 4 + size]
        offset += 4 + size + (-size % 4)
    return kind, attributes

def verify_allocations(host, port, secret, count=8):
    username = f'{int(time.time()) + 600}:{secrets.token_hex(8)}'.encode()
    password = base64.b64encode(hmac.new(secret.encode(), username, hashlib.sha1).digest())
    connections = []
    try:
        for index in range(count):
            connection = socket.create_connection((host, port), timeout=3)
            connections.append(connection)
            requested = attribute(0x19, b'\x11\0\0\0')
            kind, challenge = exchange(connection, requested)
            assert kind == 0x113 and 0x14 in challenge and 0x15 in challenge, 'Expected TURN authentication challenge'
            realm, nonce = challenge[0x14], challenge[0x15]
            key = hashlib.md5(username + b':' + realm + b':' + password).digest()
            body = requested + attribute(6, username) + attribute(0x14, realm) + attribute(0x15, nonce)
            kind, response = exchange(connection, body, key)
            error = response.get(9, b'\0\0\0\0')
            code = (error[2] & 7) * 100 + error[3]
            assert kind == 0x103, f'TURN allocation {index + 1}/{count} rejected with code {code}'
    finally:
        for connection in connections:
            connection.close()
