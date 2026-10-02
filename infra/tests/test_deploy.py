"""Exercise deployment state and rollback with a real temporary Git repository."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'deploy.sh'


class Deployment(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.repo = root / 'checkout'
        self.origin = root / 'origin.git'
        self.repo.mkdir()
        self.git('init', '-b', 'main')
        self.git('config', 'user.email', 'test@example.invalid')
        self.git('config', 'user.name', 'Deploy Test')
        (self.repo / 'infra').mkdir()
        (self.repo / '.gitignore').write_text('.deploy/\ninfra/.env\n')
        (self.repo / 'infra/compose.yml').write_text('services: {}\n')
        (self.repo / 'infra/.env').write_text('SIGNAL_HOST=example.invalid\n')
        self.git('add', '.')
        self.git('commit', '-m', 'previous deployment')
        self.old = self.git('rev-parse', 'HEAD')
        (self.repo / 'change').write_text('new server\n')
        self.git('add', 'change')
        self.git('commit', '-m', 'new deployment')
        self.new = self.git('rev-parse', 'HEAD')
        subprocess.run(['git', 'clone', '--bare', str(self.repo), str(self.origin)], check=True, capture_output=True)
        self.git('remote', 'add', 'origin', str(self.origin))
        self.git('checkout', '--detach', self.old)
        tools = root / 'bin'
        tools.mkdir()
        fake = '''#!/usr/bin/env python3
import json,os,sys,subprocess
from pathlib import Path
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
name=Path(sys.argv[0]).name
with Path('.deploy/events').open('a') as f: f.write(json.dumps([name,head,sys.argv[1:]])+'\\n')
phase=os.environ.get('FAIL_PHASE','')
fail=head==os.environ['TARGET'] and ((name=='curl' and phase=='health') or (name=='docker' and phase in ('build','up') and phase in sys.argv))
sys.exit(1 if fail else 0)
'''
        for name in ('docker', 'curl'):
            file = tools / name
            file.write_text(fake)
            file.chmod(0o755)
        self.env = os.environ | {'PATH': str(tools) + os.pathsep + os.environ['PATH'], 'TARGET': self.new}

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.repo, text=True, stderr=subprocess.DEVNULL).strip()

    def run_deploy(self, phase=''):
        return subprocess.run(['bash', str(SCRIPT), self.new, str(self.repo), 'full'], env=self.env | {'FAIL_PHASE': phase}, capture_output=True, text=True)

    def test_variables_create_private_env_and_preserve_turn_secret(self):
        env_file = self.repo / 'infra/.env'
        env_file.unlink()
        command = ['bash', str(SCRIPT), self.new, str(self.repo), 'signaling', 'salas.example.invalid', '0.0.0.0']
        result = subprocess.run(command, env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('SIGNAL_HOST=salas.example.invalid', env_file.read_text())
        self.assertEqual(env_file.stat().st_mode & 0o777, 0o600)
        env_file.write_text(env_file.read_text() + 'TURN_SECRET=preserve-private-value\n')
        result = subprocess.run(command, env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('TURN_SECRET=preserve-private-value', env_file.read_text())
        self.assertNotIn('preserve-private-value', result.stdout + result.stderr)

    def test_invalid_variable_does_not_change_env_or_containers(self):
        env_file = self.repo / 'infra/.env'
        original = env_file.read_text()
        result = subprocess.run(['bash', str(SCRIPT), self.new, str(self.repo), 'signaling', 'bad;host', '0.0.0.0'], env=self.env, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(env_file.read_text(), original)
        self.assertFalse((self.repo / '.deploy/events').exists())

    def test_pinned_tls_uses_certificate_and_keeps_ip_hostname_verification(self):
        cert = self.repo / 'infra/certs/signal/fullchain.pem'
        cert.parent.mkdir(parents=True)
        cert.write_text('test CA: curl is replaced by the isolated test harness\n')
        command = ['bash', str(SCRIPT), self.new, str(self.repo), 'signaling', '127.0.0.1', '0.0.0.0', 'pinned']
        result = subprocess.run(command, env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        events = [json.loads(line) for line in (self.repo / '.deploy/events').read_text().splitlines()]
        args = next(args for name, _, args in events if name == 'curl')
        self.assertIn('--cacert', args)
        self.assertIn('infra/certs/signal/fullchain.pem', args)
        self.assertIn('https://127.0.0.1/health', args)
        self.assertNotIn('--insecure', args)
        self.assertNotIn('-k', args)

    def test_success_records_revision_and_checks_tls(self):
        result = self.run_deploy()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.git('rev-parse', 'HEAD'), self.new)
        self.assertEqual((self.repo / '.deploy/current').read_text().strip(), self.new)
        events = [json.loads(line) for line in (self.repo / '.deploy/events').read_text().splitlines()]
        self.assertTrue(any(name == 'curl' and 'https://example.invalid/health' in args for name, _, args in events))

    def test_build_failure_does_not_replace_running_containers(self):
        result = self.run_deploy('build')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.git('rev-parse', 'HEAD'), self.old)
        self.assertNotIn('"up"', (self.repo / '.deploy/events').read_text())

    def test_container_or_health_failure_restores_previous_revision(self):
        for phase in ('up', 'health'):
            with self.subTest(phase=phase):
                result = self.run_deploy(phase)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(self.git('rev-parse', 'HEAD'), self.old)
                events = [json.loads(line) for line in (self.repo / '.deploy/events').read_text().splitlines()]
                self.assertTrue(any(name == 'docker' and head == self.old and 'up' in args for name, head, args in events))
                self.assertFalse((self.repo / '.deploy/current').exists())

    def test_missing_domain_fails_before_changing_containers(self):
        (self.repo / 'infra/.env').write_text('SIGNAL_IP=203.0.113.1\n')
        result = self.run_deploy()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.git('rev-parse', 'HEAD'), self.old)
        self.assertFalse((self.repo / '.deploy/events').exists())


if __name__ == '__main__':
    unittest.main()
