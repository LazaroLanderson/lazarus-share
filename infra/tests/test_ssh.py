import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('prepare_ssh', Path(__file__).resolve().parents[1] / 'prepare_ssh.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DeployKey(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.original = Path(self.tmp.name) / 'original'
        self.output = Path(self.tmp.name) / 'output'
        subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', str(self.original)], check=True)
        self.value = self.original.read_text()

    def test_valid_key_and_windows_or_escaped_line_breaks(self):
        for value in (self.value, self.value.replace('\n', '\r\n'), self.value.replace('\n', '\\n')):
            module.prepare_key(value, self.output)
            self.assertEqual(self.output.stat().st_mode & 0o777, 0o600)
            self.assertEqual(self.output.read_text(), self.value)

    def test_reject_public_key(self):
        with self.assertRaisesRegex(ValueError, 'public key'):
            module.prepare_key(self.original.with_suffix('.pub').read_text(), self.output)
        self.assertFalse(self.output.exists())

    def test_reject_truncated_private_key(self):
        with self.assertRaisesRegex(ValueError, 'complete private key'):
            module.prepare_key(self.value[:len(self.value)//2], self.output)
        self.assertFalse(self.output.exists())

    def test_reject_password_protected_key(self):
        protected = Path(self.tmp.name) / 'protected'
        subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', 'test-only', '-f', str(protected)], check=True)
        with self.assertRaisesRegex(ValueError, 'password-protected'):
            module.prepare_key(protected.read_text(), self.output)
        self.assertFalse(self.output.exists())
