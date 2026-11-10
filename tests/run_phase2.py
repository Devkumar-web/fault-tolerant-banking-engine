#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
import tempfile
build = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='bank-phase2-') as directory:
    subprocess.run([str(build/'test_phase2')],cwd=directory,check=True,timeout=45)

# Exercise the real listener/supervisor and C client using local stream sockets.
# TCP socket creation may be restricted in the execution environment.
import os
import socket
import struct
import time
import hashlib
root = Path(__file__).resolve().parents[1]
before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'fixtures').iterdir()}
MAGIC = 0x42414e4b

def send(sock, data):
    while data:
        count = sock.sendmsg([data])
        assert count > 0
        data = data[count:]

def receive(sock, count):
    data = b''
    while len(data) < count:
        chunk, _, _, _ = sock.recvmsg(count-len(data))
        if not chunk: raise EOFError('connection closed')
        data += chunk
    return data

def command(sock, op, rid, payload=b''):
    send(sock, struct.pack('!IHHIIQ', MAGIC, 2, op, len(payload), 0, rid)+payload)
    magic, version, kind, size, flags, returned_id = struct.unpack('!IHHIIQ', receive(sock, 24))
    assert (magic,version,kind,size,flags,returned_id) == (MAGIC,2,op|0x8000,20,0,rid)
    return struct.unpack('!IQQ',receive(sock,20))

def connect(path):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(7)
    sock.connect(str(path))
    return sock

def login(sock):
    assert command(sock,1,1,struct.pack('!II',101,1)+b'testpass')[0] == 0

# Probe only the environment's ability to bind. Never skip application failures.
with tempfile.TemporaryDirectory(prefix='bank-phase2-bind-') as probe_dir:
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
            probe.bind(str(Path(probe_dir)/'probe.sock'))
    except PermissionError:
        print('SKIP real listener/client smoke: environment denies bind; supervisor tested with injected connected sockets')
        assert before == {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'fixtures').iterdir()}
        sys.exit(0)

with tempfile.TemporaryDirectory(prefix='bank-phase2-server-') as directory:
    path = Path(directory)/'bank.sock'
    subprocess.run([str(build/'test_phase2'),'--seed'],cwd=directory,check=True,timeout=10)
    log = tempfile.TemporaryFile()
    held = []
    def start():
        proc = subprocess.Popen([str(build/'server'),'--unix',str(path)],cwd=directory,stdout=log,stderr=log)
        deadline = time.monotonic()+5
        while not path.exists():
            if proc.poll() is not None:
                log.seek(0)
                raise AssertionError(log.read().decode(errors='replace'))
            if time.monotonic()>deadline: raise AssertionError('startup timed out')
            time.sleep(.02)
        return proc
    server = start()
    try:
        with connect(path) as sock:
            assert command(sock,3,200,struct.pack('!Q',25))[0] == 8
            login(sock)
            first = command(sock,3,200,struct.pack('!Q',25))
            assert first[0] == 0 and first[2] == 10025
        # A real client process replays the same persisted operation.
        cli = subprocess.run([str(build/'client'),'unix:'+str(path),'101','1','deposit','200','0.25'],
                             input='testpass\n',text=True,capture_output=True,timeout=10)
        assert cli.returncode == 0, cli.stderr
        assert f'transaction={first[1]}' in cli.stdout and 'balance_paise=10025' in cli.stdout
        # Restart server and verify the replay is independent of process memory.
        server.terminate(); assert server.wait(timeout=5) == 0
        assert not path.exists()
        server = start()
        with connect(path) as sock:
            login(sock)
            assert command(sock,3,200,struct.pack('!Q',25)) == first
            assert command(sock,3,200,struct.pack('!Q',26))[0] == 9
            assert command(sock,2,201)[2] == 10025
        # Singleton guard prevents recovery racing an active server.
        other = subprocess.run([str(build/'server'),'--unix',str(Path(directory)/'other.sock')],
                               cwd=directory,capture_output=True,timeout=5)
        assert other.returncode != 0 and b'already in use' in other.stderr
        print('PASS real server/client, authorization, restart replay, singleton data guard')
        time.sleep(.15)
        # Hold exactly the admission limit with authenticated, responsive clients.
        for _ in range(16):
            sock=connect(path);held.append(sock);login(sock)
        with connect(path) as excess:
            try:
                data,_,_,_=excess.recvmsg(1)
                assert data == b''
            except ConnectionResetError:
                pass
        print('PASS maximum 16 workers; excess connection rejected')
        # Shut down while workers are idle. Parent waits, terminates and reaps them.
        started=time.monotonic();server.terminate();assert server.wait(timeout=5)==0
        assert time.monotonic()-started<5 and not path.exists()
        for sock in held:
            try:
                data,_,_,_=sock.recvmsg(1);assert data==b''
            except ConnectionResetError:
                pass
            sock.close()
        held=[]
        print('PASS controlled shutdown closes all held sessions')
        server=start()
        with connect(path) as stalled:
            send(stalled,b'BAN')
            started=time.monotonic()
            data,_,_,_=stalled.recvmsg(1)
            assert data==b'' and 4 <= time.monotonic()-started < 7
        with connect(path) as good:
            login(good);assert command(good,2,300)[0]==0
        print('PASS incomplete frame expires; server continues serving')
    finally:
        for sock in held: sock.close()
        if server.poll() is None:
            server.terminate();server.wait(timeout=5)
        log.close()
assert before == {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'fixtures').iterdir()}
print('PASS phase 2 fixtures unchanged')
