# P1 core-journey wire fixture

Versioned lab fixture that speaks the accepted product contract revisions (login 0.13, envelope 0.10, inbox 0.9, delivery 0.12, profile 0.9). This is not a device lab (`LAB-02` … `LAB-12`) and does not replace those fixtures.

Default port: **8777**.

Host tests: `python3 -m unittest scripts.tests.test_lab_p1_host`

Run server:

```bash
python3 server/labs/lab-p1-core-journey-wire/server.py --run-id local-dev --port 8777
```

Desk Bearer and PIN values live in `fixtures/accounts.json`. They are redacted from server logs and must not appear in test output.
