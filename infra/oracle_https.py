"""Manual, instance-principal-only HTTPS access for this VPS's attached subnet."""
import argparse
import json
import sys
import urllib.request


def metadata(path):
    request = urllib.request.Request(
        'http://169.254.169.254/opc/v2/' + path,
        headers={'Authorization': 'Bearer Oracle'},
    )
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request, timeout=5) as response:
        return json.load(response)


def permits_https(rule):
    """Recognize public IPv4 access, including wider TCP port ranges."""
    if rule.source != '0.0.0.0/0' or rule.source_type != 'CIDR' or rule.is_stateless:
        return False
    if rule.protocol == 'all':
        return True
    if rule.protocol != '6':
        return False
    options = rule.tcp_options
    if options is None:
        return True
    if options.source_port_range is not None:
        return False
    ports = options.destination_port_range
    return ports is None or ports.min <= 443 <= ports.max


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public-ip', required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    import oci

    operation = 'authenticate'
    try:
        vnics = metadata('vnics/')
        primary = [item for item in vnics if item.get('nicIndex', 0) == 0]
        if len(primary) != 1:
            raise RuntimeError('Cannot identify a unique primary VNIC; no changes made')
        signer = oci.auth.signers.InstancePrincipalsSecurityTokenSigner()
        client = oci.core.VirtualNetworkClient({}, signer=signer, timeout=(10, 30))
        operation = 'GetVnic'
        vnic = client.get_vnic(primary[0]['vnicId']).data
        if vnic.public_ip != args.public_ip:
            raise RuntimeError('Primary VNIC public IP does not match the configured server; no changes made')
        operation = 'GetSubnet'
        subnet = client.get_subnet(vnic.subnet_id).data
        operation = 'GetSecurityList'
        lists = {identifier: client.get_security_list(identifier)
                 for identifier in subnet.security_list_ids}
        print('Instance principal authorized: attached VNIC, subnet and security lists readable')
        print('Network compartment is root:', subnet.compartment_id.startswith('ocid1.tenancy.'))
        print('Attached security lists:', len(lists))
        allowed = any(permits_https(rule) for response in lists.values()
                      for rule in response.data.ingress_security_rules)
        print('Existing public stateful TCP 443 rule:', allowed)
        if allowed or not args.apply:
            return
        operation = 'GetVcn'
        vcn = client.get_vcn(subnet.vcn_id).data
        target = vcn.default_security_list_id
        if target not in lists:
            raise RuntimeError('Default security list is not attached; select a target explicitly before changing rules')
        response = lists[target]
        original = response.data
        etag = response.headers.get('etag')
        if not etag:
            raise RuntimeError('Security list ETag absent; refusing an unprotected update')
        models = oci.core.models
        new_rule = models.IngressSecurityRule(
            protocol='6', source='0.0.0.0/0', source_type='CIDR', is_stateless=False,
            description='Lazarus Share HTTPS signaling',
            tcp_options=models.TcpOptions(destination_port_range=models.PortRange(min=443, max=443)),
        )
        details = models.UpdateSecurityListDetails(
            ingress_security_rules=[*original.ingress_security_rules, new_rule],
            egress_security_rules=original.egress_security_rules,
        )
        operation = 'UpdateSecurityList'
        client.update_security_list(target, details, if_match=etag)
        operation = 'VerifySecurityList'
        updated = client.get_security_list(target).data
        old_ingress = oci.util.to_dict(original.ingress_security_rules)
        old_egress = oci.util.to_dict(original.egress_security_rules)
        expected = [*old_ingress, oci.util.to_dict(new_rule)]
        if (oci.util.to_dict(updated.ingress_security_rules) != expected
                or oci.util.to_dict(updated.egress_security_rules) != old_egress):
            raise RuntimeError('Post-update rules differ from the expected preserved rules; inspect concurrent changes')
        print('Added only stateful public TCP 443; existing ingress and egress rules verified unchanged')
    except oci.exceptions.ServiceError as error:
        print(f'Oracle API denied/failed: operation={operation} status={error.status} code={error.code}; credentials and identifiers omitted', file=sys.stderr)
        raise SystemExit(1)
    except Exception as error:
        # Authentication errors can contain request details; never print their body.
        if isinstance(error, RuntimeError):
            print(str(error), file=sys.stderr)
        else:
            print(f'Oracle check failed: {type(error).__name__}; sensitive request details omitted', file=sys.stderr)
        raise SystemExit(1)


if __name__ == '__main__':
    main()
