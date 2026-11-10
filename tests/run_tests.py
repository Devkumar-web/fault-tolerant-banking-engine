#!/usr/bin/env python3
"""All mutable state is isolated; binaries are located before changing cwd."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import socket
import time
import signal

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()
def snapshots():
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (root / 'fixtures').iterdir() if p.is_file()}
# Compile every public header on its own, outside the original source directory.
import os
import shlex
for header in (root / 'include').glob('*.h'):
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
        ['-std=c11', '-D_XOPEN_SOURCE=700', '-D_DEFAULT_SOURCE', '-Wall', '-Werror',
         '-I' + str(root / 'include'), '-x', 'c', '-fsyntax-only', '-'],
        input=f'#include <{header.name}>\n', text=True, check=True)
print('PASS standalone public headers', flush=True)
before = snapshots()
try:
    with tempfile.TemporaryDirectory(prefix='bank-phase1-') as directory:
        subprocess.run([str(build / 'test_core')], cwd=directory, check=True, timeout=40)
    with tempfile.TemporaryDirectory(prefix='bank-phase1-fixtures-') as directory:
        for p in (root / 'fixtures').iterdir():
            if p.is_file(): shutil.copy2(p, directory)
        for command in ('customers', 'employees', 'loans', 'journal'):
            subprocess.run([str(build / 'database'), command], cwd=directory,
                           check=True, stdout=subprocess.DEVNULL, timeout=10)
        proc = subprocess.run([str(build / 'legacy_server'), 'invalid'], cwd=directory,
                              capture_output=True, text=True, timeout=10)
        assert proc.returncode != 0 and 'Invalid port' in proc.stderr
        print('PASS database inspection and server argument validation')
        # Use a temporary loopback port; process group cleanup includes workers.
        try:
            probe = socket.socket()
        except PermissionError:
            print('SKIP TCP smoke: environment denies network socket creation')
        else:
            with probe:
                probe.bind(('127.0.0.1', 0))
                port = probe.getsockname()[1]
            with tempfile.TemporaryFile() as log:
                server = subprocess.Popen([str(build / 'legacy_server'), str(port)], cwd=directory,
                                          stdout=log, stderr=log, start_new_session=True)
                try:
                    deadline = time.monotonic() + 5
                    while True:
                        if server.poll() is not None:
                            log.seek(0)
                            raise AssertionError(log.read().decode(errors='replace'))
                        try:
                            client = socket.create_connection(('127.0.0.1', port), timeout=1)
                            break
                        except ConnectionRefusedError:
                            if time.monotonic() > deadline: raise
                            time.sleep(0.02)
                    with client:
                        packet = b''
                        while len(packet) < 4096:
                            chunk = client.recv(4096-len(packet))
                            assert chunk, 'Server disconnected before menu'
                            packet += chunk
                        assert b'Select Login Type' in packet
                        client.sendall(b'6' + bytes(4095))
                    print('PASS TCP server startup, menu framing, and exit request')
                finally:
                    try: os.killpg(server.pid, signal.SIGTERM)
                    except ProcessLookupError: pass
                    server.wait(timeout=5)

finally:
    assert snapshots() == before, 'Fixtures changed during tests'
print('PASS fixtures unchanged; all tests completed')
