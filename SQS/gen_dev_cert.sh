#!/bin/sh
# Generates a local, self-signed dev TLS certificate + private key for SQS
# using the REAL system openssl CLI (not hand-rolled crypto -- see
# sqs_main.c's own comment on why this project always defers to a real,
# audited TLS/X.509 implementation). Only for local testing: the cert is
# for CN=localhost / IP 127.0.0.1, valid 10 years, and is NOT signed by any
# publicly trusted CA, so a real browser will show a trust warning unless
# you import it yourself.
#
# To trust it in a real Firefox profile (untested in this project's own
# sandboxed dev environment -- no Firefox profile exists there to verify
# against, see the project plan's own "known limitations" note):
#   certutil -A -n "SQS dev CA" -t "C,," -i SQS/dev_cert.pem \
#       -d sql:$HOME/.mozilla/firefox/<your-profile>
set -e
cd "$(dirname "$0")"
openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout dev_key.pem -out dev_cert.pem \
    -days 3650 \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
echo "wrote SQS/dev_cert.pem + SQS/dev_key.pem"
