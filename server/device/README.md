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

For device bring-up, `make server` serves plain HTTP on the local network. Use only fixture credentials on an isolated development network. The v2 contract still requires authenticated HTTPS before product use. Ordinary HTTPS requires a server certificate and device trust of that server; it does not require a certificate on each device. Runtime receipts, delivered media, and last-viewed state are stored under `runtime/` and excluded from Git.
