"""Verify the cloud firewall update preserves rules and requires an ETag."""
import contextlib
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace as Obj
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('oracle_https', Path(__file__).parents[1] / 'oracle_https.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def plain(value):
    if isinstance(value, list):
        return [plain(item) for item in value]
    if isinstance(value, Obj):
        return {key: plain(item) for key, item in vars(value).items()}
    return value


class Firewall(unittest.TestCase):
    def execute(self, *, apply=False, etag='revision', allowed=False, public_ip='192.0.2.1'):
        rule = Obj(protocol='6', source='0.0.0.0/0', source_type='CIDR', is_stateless=False,
                   tcp_options=Obj(source_port_range=None, destination_port_range=Obj(min=443 if allowed else 22, max=443 if allowed else 22)))
        original = Obj(ingress_security_rules=[rule], egress_security_rules=[Obj(protocol='all', destination='0.0.0.0/0')])
        calls = []
        def update(target, details, **kwargs):
            calls.append((target, details, kwargs))
        def get_list(target):
            data = calls[-1][1] if calls else original
            return Obj(data=data, headers={'etag': etag} if etag else {})
        client = Obj(get_vnic=lambda _: Obj(data=Obj(public_ip=public_ip, subnet_id='subnet')),
                     get_subnet=lambda _: Obj(data=Obj(security_list_ids=['default'], compartment_id='ocid1.tenancy.test', vcn_id='vcn')),
                     get_vcn=lambda _: Obj(data=Obj(default_security_list_id='default')),
                     get_security_list=get_list, update_security_list=update)
        class ServiceError(Exception):
            pass
        models = Obj(IngressSecurityRule=Obj, TcpOptions=lambda **kw: Obj(source_port_range=None, **kw),
                     PortRange=Obj, UpdateSecurityListDetails=Obj)
        sdk = Obj(auth=Obj(signers=Obj(InstancePrincipalsSecurityTokenSigner=lambda: object())),
                  core=Obj(VirtualNetworkClient=lambda *a, **kw: client, models=models),
                  exceptions=Obj(ServiceError=ServiceError), util=Obj(to_dict=plain))
        argv = ['oracle_https', '--public-ip', '192.0.2.1'] + (['--apply'] if apply else [])
        error = None
        with patch.dict('sys.modules', {'oci': sdk}), patch('sys.argv', argv), \
                patch.object(module, 'metadata', return_value=[{'nicIndex': 0, 'vnicId': 'own-vnic'}]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            try:
                module.main()
            except SystemExit as caught:
                error = caught.code
        return original, calls, error

    def test_inspection_does_not_write(self):
        self.assertEqual(self.execute()[1], [])

    def test_apply_preserves_ssh_and_egress_with_concurrency_guard(self):
        original, calls, error = self.execute(apply=True)
        self.assertIsNone(error)
        self.assertEqual(len(calls), 1)
        target, details, kwargs = calls[0]
        self.assertEqual(kwargs, {'if_match': 'revision'})
        self.assertEqual(details.ingress_security_rules[:-1], original.ingress_security_rules)
        self.assertEqual(details.egress_security_rules, original.egress_security_rules)
        self.assertEqual(details.ingress_security_rules[-1].tcp_options.destination_port_range.min, 443)
        self.assertEqual(details.ingress_security_rules[-1].tcp_options.destination_port_range.max, 443)

    def test_existing_https_does_not_duplicate(self):
        self.assertEqual(self.execute(apply=True, allowed=True)[1], [])

    def test_missing_etag_or_wrong_vps_refuses_update(self):
        for kwargs in ({'etag': None}, {'public_ip': '192.0.2.2'}):
            with self.subTest(kwargs=kwargs):
                _, calls, error = self.execute(apply=True, **kwargs)
                self.assertEqual(calls, [])
                self.assertEqual(error, 1)
