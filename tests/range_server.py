import http.server, json, os, sys, threading, time
path, port = sys.argv[1], int(sys.argv[2])
delay = float(sys.argv[3]) / 1000 if len(sys.argv) > 3 else 0
size = os.path.getsize(path)
fd = os.open(path, os.O_RDONLY)
lock = threading.Lock()
counts = {"requests": 0, "bytes": 0, "largest": 0}

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *args):
        pass
    def do_HEAD(self):
        self.send_response(200)
        self.send_header("Content-Length", str(size))
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
    def do_GET(self):
        if self.path == "/stats":
            with lock:
                body = json.dumps(counts).encode()
                counts.update(requests=0, bytes=0, largest=0)
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        start, end = 0, size - 1
        header = self.headers.get("Range")
        if header:
            first, last = header.split("=")[1].split("-")
            start = int(first)
            end = min(int(last), size - 1) if last else size - 1
        length = end - start + 1
        with lock:
            counts["requests"] += 1
            counts["bytes"] += length
            counts["largest"] = max(counts["largest"], length)
        if delay:
            time.sleep(delay)
        self.send_response(206 if header else 200)
        self.send_header("Content-Length", str(length))
        self.send_header("Accept-Ranges", "bytes")
        if header:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        sent = 0
        while sent < length:
            chunk = os.pread(fd, min(1 << 20, length - sent), start + sent)
            self.wfile.write(chunk)
            sent += len(chunk)

server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
server.daemon_threads = True
server.serve_forever()
