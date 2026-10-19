"""LAB-03 private build overlay and compiled-input identity (never run identity)."""
import hashlib
import json
import os
from pathlib import Path
import socket

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'client/labs/lab-03-media-playback'
FIXTURES = ROOT / 'server/labs/lab-03-media/fixtures'

def client_address():
    override = os.environ.get('LAB03_CLIENT_ADDRESS')
    if override:
        import ipaddress
        ipaddress.IPv4Address(override)
        return override
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(('192.0.2.1', 9))
        return sock.getsockname()[0]
    finally:
        sock.close()

def source_hash():
    digest = hashlib.sha256()
    sources = [PROJECT/'CMakeLists.txt', PROJECT/'sdkconfig.defaults', PROJECT/'sdkconfig.defaults.scripted']
    sources += sorted((PROJECT/'main').glob('*'))
    foundation = ROOT/'client/labs/lab-02-bounded-inbox/components/lab02'
    sources += [foundation/'inbox_model.c', foundation/'include/lab02/inbox_model.h', foundation/'include/lab02/layout.h']
    for path in sources:
        if path.is_file():
            digest.update(path.relative_to(ROOT).as_posix().encode()+b'\0'+path.read_bytes())
    return digest.hexdigest()

def prepare(host, config, spec, variant, build_dir, kit='', target=None):
    profile = None
    if target is not None or kit:
        deployment = target.deployment if target else host.load_active_deployment(config)
        resolved = target.kit if target else host.kit_for(deployment, kit)
        profile = deployment.secrets_profile_for_kit(resolved)
    address = client_address()
    values = {'LAB03_SERVER_URL': f'http://{address}:8766',
              'LAB03_WIFI_SSID': profile.wifi_ssid if profile else '',
              'LAB03_WIFI_PASSWORD': profile.wifi_password if profile else ''}
    identity = {'source': source_hash(), 'target': spec.target, 'variant': variant.name,
                'compiled_private_sha256': hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest(),
                'fixture_sha256': hashlib.sha256((FIXTURES/'manifest.json').read_bytes()).hexdigest()}
    fingerprint = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    build_dir.mkdir(parents=True, exist_ok=True)
    header=build_dir/'lab03_build_id.h'
    content='#pragma once\n#define LAB03_BUILD_ID '+json.dumps(fingerprint[:24])+'\n'
    if not header.exists() or header.read_text()!=content:
        header.write_text(content)
    # Survives the shared host's removal of an unconfigured build directory.
    build_dir.with_name(build_dir.name+'.lab03.build-id.h').write_text(content)
    overlay = build_dir.parent / (build_dir.name+'.lab03.defaults')
    overlay.parent.mkdir(parents=True, exist_ok=True)
    flags = os.O_CREAT | os.O_WRONLY | os.O_TRUNC
    fd = os.open(overlay, flags, 0o600)
    os.chmod(overlay, 0o600)
    with os.fdopen(fd, 'w') as out:
        for key, value in values.items():
            out.write(f'CONFIG_{key}={json.dumps(value)}\n')
    return overlay, fingerprint, values


def restore_header(build_dir):
    """Restore the frozen, non-secret ID after host build-directory preparation."""
    sidecar=build_dir.with_name(build_dir.name+'.lab03.build-id.h')
    content=sidecar.read_text()
    if not content.startswith('#pragma once\n#define LAB03_BUILD_ID "'):
        raise ValueError('invalid LAB-03 build identity header')
    build_dir.mkdir(parents=True,exist_ok=True)
    header=build_dir/'lab03_build_id.h'
    if not header.exists() or header.read_text()!=content:
        header.write_text(content)
