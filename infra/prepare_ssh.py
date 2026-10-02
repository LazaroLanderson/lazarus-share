"""Write a validated deploy key without printing private key material."""
import os
from pathlib import Path
import re
import subprocess
import sys


def prepare_key(value, target):
    value = value.replace('\r\n', '\n').strip()
    if '\n' not in value and '\\n' in value:
        value = value.replace('\\n', '\n').strip()
    if value.startswith(('ssh-', 'ecdsa-')):
        raise ValueError('VPS_SSH_KEY contains a public key. Supply the full PRIVATE key file instead.')
    match = re.match(r'^-----BEGIN ([A-Z0-9 ]*PRIVATE KEY)-----\n', value)
    if not match or not value.endswith('-----END ' + match[1] + '-----'):
        raise ValueError('VPS_SSH_KEY must contain the complete private key, including BEGIN/END lines and real line breaks.')
    target = Path(target)
    target.write_text(value + '\n')
    target.chmod(0o600)
    result = subprocess.run(['ssh-keygen', '-y', '-P', '', '-f', str(target)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if result.returncode:
        target.unlink(missing_ok=True)
        raise ValueError('VPS_SSH_KEY is invalid or password-protected. Use a complete deploy private key without a passphrase.')


if __name__ == '__main__':
    try:
        prepare_key(os.environ.get('SSH_PRIVATE_KEY', ''), sys.argv[1])
    except ValueError as error:
        raise SystemExit(str(error))
