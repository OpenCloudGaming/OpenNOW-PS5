import base64
import concurrent.futures
import hashlib
import socket
import subprocess
import sys
import time


def frame(opcode, body):
    return bytes((opcode, len(body))) + body


def receive_exact(connection, length):
    data = b""
    while len(data) < length:
        chunk = connection.recv(length - len(data))
        assert chunk, "Client closed before sending the frame"
        data += chunk
    return data


def receive_frame(connection):
    opcode, size = receive_exact(connection, 2)
    assert size & 0x80, "Client frame must be masked"
    size &= 0x7f
    assert size < 126
    mask = receive_exact(connection, 4)
    payload = receive_exact(connection, size)
    return opcode & 0x0f, bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))


def serve(listener, failure):
    with listener.accept()[0] as connection:
        connection.settimeout(5)
        request = b""
        while b"\r\n\r\n" not in request:
            request += connection.recv(1024)
            assert len(request) < 8192
        lines = request.decode().split("\r\n")
        assert lines[0] == "GET /nvst/sign_in?pairing_id=fixture-session HTTP/1.1"
        headers = dict(line.split(": ", 1) for line in lines[1:] if line)
        assert headers["Origin"] == "https://play.geforcenow.com"
        assert headers["Sec-WebSocket-Protocol"] == "x-nv-sessionid.fixture-session"
        key = headers["Sec-WebSocket-Key"]
        assert len(base64.b64decode(key, validate=True)) == 16
        accept = base64.b64encode(hashlib.sha1(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        if failure == "http":
            response = b"HTTP/1.1 403 secret-session\r\nContent-Length: 12\r\n\r\nsecret-token"
        else:
            if failure == "challenge":
                accept = "secret-incorrect-challenge"
            response = ("HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\nConnection: keep-alive, Upgrade\r\n"
                        f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode()
        connection.sendall(response[:17])
        time.sleep(0.01)
        if failure:
            connection.sendall(response[17:])
            return
        connection.sendall(response[17:] + frame(0x81, b'{"text":1}') +
                           frame(0x02, b'{"binary":') + frame(0x89, b'ping') +
                           frame(0x80, b'1}'))
        seen = []
        while True:
            received = receive_frame(connection)
            seen.append(received)
            if received[0] == 8:
                break
        assert (1, b'{"client":1}') in seen
        assert (10, b'ping') in seen


for failure, expected in (("http", "WebSocket upgrade rejected (HTTP 403)"),
                          ("challenge", "Invalid WebSocket accept challenge"),
                          (None, "success")):
    with socket.socket() as listener, concurrent.futures.ThreadPoolExecutor() as executor:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(10)
        future = executor.submit(serve, listener, failure)
        port = listener.getsockname()[1]
        result = subprocess.run([sys.argv[1],
                                 f"ws://127.0.0.1:{port}/nvst/sign_in?pairing_id=fixture-session",
                                 expected], timeout=8)
        future.result(timeout=8)
        assert result.returncode == 0, f"WebSocket fixture failed: {failure or 'frames'}"
print("Real WebSocket upgrade, challenge, masking, ping and fragmented signaling passed")
