# Product desk wire server

Versioned fixture server for the v2 core journey (login 0.13, envelope 0.10, inbox 0.9, delivery 0.12, profile 0.9). This is **product** host code, not a device lab (`LAB-02` … `LAB-12`).

Default port: **8777**.

Host tests: `python3 -m unittest scripts.tests.test_lab_p1_host scripts.tests.test_product_server_hardening`

Run server:

```bash
make server
# or
python3 server/device/main/server.py --run-id local-dev --port 8777
```

Desk bearer and PIN values live in `fixtures/accounts.json`. They are redacted from server logs and must not appear in test output.

For local device bring-up and host tests, `make server` serves plain HTTP. Use
only fixture credentials on an isolated development network; plain HTTP is not
production-ready and is not a supported product deployment mode.

Production serving is an explicit TLS opt-in. Supply a PEM certificate and
private key, and enable the production guard:

```bash
make server SERVER_TLS_CERT=/secure/server.crt \
  SERVER_TLS_KEY=/secure/server.key SERVER_PRODUCTION=1
# or
python3 server/device/main/server.py --run-id production \
  --bind 0.0.0.0 --port 8777 --production \
  --tls-cert /secure/server.crt --tls-key /secure/server.key
```

The entrypoint requires certificate and key together, uses TLS 1.2 or newer,
and refuses `--production` without TLS. The endpoint must trust the configured
server certificate; client certificates are not required by this server.
Runtime receipts, delivered media, last-viewed state, and retained PIN replay
records are stored under `runtime/` and excluded from Git.
