# Product desk wire server

Versioned fixture server for the v2 core journey (login 0.13, envelope 0.10, inbox 0.9, delivery 0.12, profile 0.9). This is **product** host code, not a device lab (`LAB-02` … `LAB-12`).

Default port: **8777**.

Host tests: `python3 -m unittest scripts.tests.test_lab_p1_host scripts.tests.test_product_server_hardening`

Run server:

```bash
make server
# or
python3 server/device/main/server.py --run-id local-dev --port 8777
# or use a private deployment/roster file
python3 server/device/main/server.py --run-id local-dev --deployment /secure/pilot.yaml
```

The process entrypoint loads the committed four-user pilot roster from
`fixtures/deployment.yaml` before it starts listening. `--deployment` and its
`--roster` alias select another YAML deployment using the shared deployment
schema and loader. The endpoint token binds desk authentication; the endpoint's
`hangout_id` supplies the hangout identity, and the four YAML users supply
account PINs and roster display names. The committed token and PINs are fixture
credentials only, not production secrets.

The deployment is read once at startup and is intentionally not hot-reloaded.
Editing the YAML has no effect on a running process; restart the server to
apply a roster change. Dynamic roster administration and HTTP mutation routes
are out of scope for the initial pilot. The JSON files under `fixtures/` remain
isolated host-test fixtures and are not the production entrypoint's source of
truth.

YAML deployment validation and runtime loading require the repository host
dependencies PyYAML and `jsonschema`. From the repository root, run `make
install` first and use `.venv/bin/python` for direct server or validation
commands. Startup fails closed if the YAML file is missing/invalid or those
dependencies are unavailable; the server does not fall back to the legacy JSON
fixtures.

Desk bearer and PIN values are redacted from server logs and must not appear in
test output.

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

## Liveness, readiness, and shutdown

`GET /health` is a listener liveness check. It returns HTTP 200 with
`status: "live"`; its `ready` field reports whether persisted delivery
restoration found a recovery issue, but a live response does not guarantee that
all device functionality is available. `GET /ready` is the functional
readiness check: it returns HTTP 200 with `status: "ready"` only when startup
state has restored without a recorded issue, and HTTP 503 with non-sensitive
`issues` when degraded. A valid empty hangout is still a successful response
from `/v2/hangout` and is distinct from an unavailable server.

Ctrl+C and SIGTERM close the listener and optional log before the entrypoint
exits. The server enables same-address reuse so an owned process can be
restarted immediately after shutdown. Tests may set `FAMILY_LINK_RUNTIME_DIR`
to a private directory; normal deployments should keep the default runtime
location and must not share it between independent server processes.

## Structured server telemetry

When `--log <path>` is supplied, each request produces one bounded JSONL event
with schema `family-link.server-telemetry/1`. Events include the normalized
route, status, outcome/status class, elapsed milliseconds, attempt number, run
ID, fixture context, and safe operation, send, user, range, or resource fields
where applicable. Routes use placeholders for message and client IDs so query
strings and arbitrary paths are not copied into diagnostics.

The log is redacted and bounded by the existing log sink. PINs, bearer/session
credentials, request bodies, boot identifiers, and media contents are never
logged. `status_class` distinguishes `success`, `refusal`, `resource`,
`storage`, and `retryable_failure` outcomes for deployment debugging.

## Server deployment/qualification limits

The following are server deployment and qualification limits for this fixture
server. They bound resource use in the P3 implementation; they do not change
the normative product contract.

- Total media per open upload is capped at 6,022,144 bytes: 5,760,000 bytes
  for the 180-second PCM audio limit plus 262,144 bytes for an optional sketch;
  this bounds each upload's durable media reservation.
- Parts are 16 KiB each; the corresponding caps are 352 audio parts and 16 sketch parts per upload. This keeps each request and part manifest bounded.
- At most 4 concurrent open uploads are admitted, with an aggregate open-upload
  byte budget of 24,088,576 bytes (four maximum-size uploads), bounding the
  server's simultaneous disk reservation.
- A sketch is capped at 32,768 `flsk2` points, or 262,144 bytes, bounding
  sketch validation and storage.
- Each serialized inbox snapshot is capped at 262,144 bytes. A send that would
  exceed that deployment budget is rejected before publication, keeping inbox
  responses within the qualification envelope.
- Accepted media is retained on the server until later culling functionality
  is available; this server does not perform media culling.

These limits are qualification and deployment guardrails only. They are not
new client-visible product behavior or a revision of the product contract.
