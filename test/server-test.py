from http.server import BaseHTTPRequestHandler
import socketserver
import sys

hName = "localhost"
Port = 18080

def value_or_default(data, key, default):
    if key in data:
        return data[key]
    return default

def parse_params(query):
    query = query[2:]
    data = {}
    for param in query.split('&'):
        parts = param.split('=')
        data[parts[0]] = parts[1]

    data['status-code'] = int(value_or_default(data, 'status-code', '200'))
    data['content-length'] = value_or_default(data, 'content-length', '10240')
    return data

class Webserver(BaseHTTPRequestHandler):

    def do_GET(self):
        query = parse_params(self.requestline.split(' ')[1])

        self.send_response(query['status-code'])
        self.send_header("Content-type", "text/html")
        self.send_header("Content-Length", query['content-length'])
        self.end_headers()
        self.wfile.write(
            bytes("<html><head><title>https://pythonbasics.org</title></head>",
                  "utf-8"))
        self.wfile.write(bytes("<p>Request: %s</p>" % self.path, "utf-8"))
        self.wfile.write(bytes("<body>", "utf-8"))
        self.wfile.write(bytes("<p>Web server</p>", "utf-8"))
        self.wfile.write(bytes("</body></html>", "utf-8"))

    def do_POST(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(bytes("<html><body>Error</body></html>", "utf-8"))


with socketserver.TCPServer(("", Port), Webserver,
                            bind_and_activate=False) as httpd:
    print("Starting at port", Port)
    try:
        httpd.allow_reuse_address = True

        httpd.server_bind()
        httpd.server_activate()

        httpd.serve_forever()
    except KeyboardInterrupt:
        print('Closing server')
        httpd.shutdown()
        sys.exit(0)
