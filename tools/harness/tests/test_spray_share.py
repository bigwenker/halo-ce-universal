"""Production optional transfer protocol, including three separate UDP peers."""
import hashlib
import re
import socket
import subprocess
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, run, mutated

def executable(control=None):
    header = read('port/linux/include/spray_share.h')
    crypto_header = read('port/third_party/monocypher/monocypher.h')
    crypto = read('port/third_party/monocypher/monocypher.c').replace('#include "monocypher.h"', '')
    source = re.sub(r'^#include "[^\n]+\n', '', read('port/linux/src/spray_share.c'), flags=re.M)
    if control:
        before, after, _ = CONTROLS[control]
        source = mutated(source, before, after)
    return build('spray_share', (('api.inc', header + crypto_header), ('under_test.inc', crypto + source)))

CONTROLS = {
    'bitmap': ('unsigned char bits[RECEIVE_BITMAP_BYTES];', 'unsigned char bits[256];', 'limit'),
    'size': ('size > SPRAY_SHARE_LIMIT ||', 'size > SPRAY_SHARE_LIMIT+1 ||', 'limit'),
    'digest': ('if (!memcmp(hash, r->image.hash, 32))', 'if (1)', 'corruption'),
    'token': ('if (!q->token || get64(p + 16) != q->token)', 'if (!q->token)', 'wrong-token'),
}

@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    status, output = run(executable(control), CONTROLS[control][2])
    assert status == 1, output

@pytest.mark.parametrize('case', ['limit', 'loss', 'corruption', 'disconnect', 'wrong-token', 'invalid-placement', 'cache', 'cache-other-owner', 'cache-reconnect', 'upstream', 'upstream-host', 'host-restart'])
def test_transfer(case):
    status, output = run(executable(), case)
    assert status == 0, output

@pytest.mark.parametrize('kind', ['png', 'jpg', 'gif'])
def test_peer_to_peer_images(tmp_path, kind):
    # A real PNG goes from client A through the host to client B, using real
    # UDP sockets, separate processes, and deliberate 1-in-7 datagram loss.
    import struct
    import zlib
    width, height = 1024, 1200
    raw = b''.join(b'\x00' + hashlib.shake_256(str(y).encode()).digest(width * 4) for y in range(height))
    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    assert 2 * 1024 * 1024 < len(png) <= 5 * 1024 * 1024
    if kind != 'png':
        png = (Path(__file__).resolve().parents[1] / 'fixtures' / ('spray.' + kind)).read_bytes()
    source = tmp_path / ('spray.' + kind)
    source.write_bytes(png)
    # Pick consecutive unused ports, rather than assume a fixed test port.
    for base in range(32000, 60000, 4):
        guards = []
        try:
            for i in range(4):
                guard = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                guards.append(guard)
                guard.bind(('127.0.0.1', base + i))
            break
        except OSError:
            pass
        finally:
            for guard in guards:
                guard.close()
    processes = []
    try:
        for peer in [0, 2, 1]:
            destination = tmp_path / f'peer{peer}.{kind}'
            processes.append(subprocess.Popen([str(executable()), 'socket', str(peer), str(base), str(source), str(destination), '24000' if kind == 'png' else '3000'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
        for process in processes:
            stdout, stderr = process.communicate(timeout=45)
            assert process.returncode == 0, stdout + stderr
        for peer in [0, 1, 2]:
            assert (tmp_path / f'peer{peer}.{kind}').read_bytes() == png
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait()
