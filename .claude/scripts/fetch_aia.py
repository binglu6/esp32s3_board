"""Fetch the server's intermediate CA via the leaf certificate's AIA extension.

The xiaozhi cloud edge currently serves only the leaf certificate, so mbedTLS
cannot build the chain to the bundled root.  The downloaded intermediate is
added to the custom certificate bundle so the handshake can be verified.
"""

import os
import socket
import ssl
from urllib.request import urlopen

from cryptography import x509
from cryptography.hazmat.primitives import serialization

HOST = "api.tenclass.net"


def main() -> None:
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    with socket.create_connection((HOST, 443), timeout=10) as sock:
        with ctx.wrap_socket(sock, server_hostname=HOST) as tls:
            der = tls.getpeercert(binary_form=True)

    leaf = x509.load_der_x509_certificate(der)
    print("leaf:", leaf.subject.rfc4514_string())
    print("leaf issuer:", leaf.issuer.rfc4514_string())

    oid = x509.ObjectIdentifier("1.3.6.1.5.5.7.1.1")
    aia = leaf.extensions.get_extension_for_oid(oid).value
    url = None
    for desc in aia:
        if desc.access_method == x509.oid.AuthorityInformationAccessOID.CA_ISSUERS:
            url = desc.access_location.value
    print("AIA:", url)
    if not url:
        raise RuntimeError("no CA Issuers URL in leaf certificate")

    data = urlopen(url, timeout=15).read()
    inter = x509.load_der_x509_certificate(data)
    print("intermediate:", inter.subject.rfc4514_string())
    print("intermediate issuer:", inter.issuer.rfc4514_string())

    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "main", "certs"))
    os.makedirs(root, exist_ok=True)
    path = os.path.join(root, "server_chain.pem")
    with open(path, "wb") as out:
        out.write(inter.public_bytes(serialization.Encoding.PEM))
    print("wrote", path)


if __name__ == "__main__":
    main()
