import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'bootstrap-ip.sh'


class TemporaryIPCertificate(unittest.TestCase):
    def test_certificate_matches_ip_is_private_and_reused(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            command = ['bash', str(SCRIPT), '127.0.0.1']
            first = subprocess.run(command, cwd=root, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            key = root / 'infra/certs/signal/privkey.pem'
            cert = root / 'infra/certs/signal/fullchain.pem'
            self.assertEqual(key.stat().st_mode & 0o777, 0o600)
            subprocess.run(['openssl', 'x509', '-in', str(cert), '-noout', '-checkip', '127.0.0.1'], check=True, stdout=subprocess.DEVNULL)
            original = hashlib.sha256(cert.read_bytes()).digest()
            second = subprocess.run(command, cwd=root, capture_output=True, text=True)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(original, hashlib.sha256(cert.read_bytes()).digest())
            different = subprocess.run(['bash', str(SCRIPT), '127.0.0.2'], cwd=root, capture_output=True, text=True)
            self.assertNotEqual(different.returncode, 0)
            self.assertEqual(original, hashlib.sha256(cert.read_bytes()).digest())
            self.assertNotIn('PRIVATE KEY', first.stdout + first.stderr)


if __name__ == '__main__':
    unittest.main()
