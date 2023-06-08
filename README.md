<h1 align="center">CDN Uring</h1>
<div align="center">

![CMake](https://github.com/lanstat/cdn-uring/actions/workflows/cmake.yml/badge.svg)
![gem](https://img.shields.io/badge/platform-linux--x64%20%7C%20linux--x32%20%7C%20linux--arm-orange)

  <strong>CDN/HLS powered by liburing</strong>
</div>
<div align="center">
  Http multiplexer connection
</div>

<br />


## Table of Contents
- [Build](#build)
- [Usage](#usage)
- [TODO](#todo)

## Build

### Prerequisites

Kernel 5.6+

#### Ubuntu
```sh
sudo apt-get install -y libssl-dev liburing-dev
```

#### Archlinux
```sh
sudo pacman -S liburing openssl
```

#### Make
```sh
cd build
cmake ../
make
```

## Usage 

### Server
```sh
./cdn [arguments]
./cdn -ssl -server-port=8001 &
./cdn -server-port=8000 -dns-port=50060 &
```
### Nginx
```nginx
http {
  upstream http_stream {
    server 127.0.0.1:8000;
  }
  
  upstream https_stream {
    server 127.0.0.1:8001;
  }

  server {
    listen 80;
    server_name _;
    location / {
      proxy_pass http://http_stream;
    }
  }
  
  server {
    listen 443;
    server_name _;
    location / {
      proxy_pass http://https_stream;
    }
  }
}
```
### Client
```sh
wget https://localhost/upload.wikimedia.org/wikipedia/commons/3/35/Tux.svg
wget http://localhost/upload.wikimedia.org/wikipedia/commons/3/35/Tux.svg
```
### Arguments
- __-ssl__ Enable https support
- __-debug__ Print debug logs
- __-hls__ Enable hls mode
- __-silent__ Disable all logs
- __-server-port=###__ Define the listener port, default 8000
- __-dns-port=###__ Define the dns listener port, default 50053
- __-buffer-size=###__ Buffer I/O size
- __-no-cache__ Disable the cache storage
- __-ipv6__ Listen ipv6
- __-proxy=###__ Proxy to mask the request, ex -proxy=google.com
- __-host-file=###__ File with static host

## TODO
- Add perfomance test
- Add error handling on http external request
- Add admin manager
- Add Log to file
