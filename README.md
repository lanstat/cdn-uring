<h1 align="center">CDN Uring</h1>
<div align="center">
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
```sh
cd build
cmake
make
```

## Usage 
```sh
./cdn [arguments]
```
### Arguments
- __-ssl__ Enable https support
- __-debug__ Print debug logs
- __-hls__ Enable hls mode
- __-silent__ Disable all logs
- __-server-port=###__ Define the listener port
- __-buffer-size=###__ Buffer I/O size
- __-no-cache__ Diable the cache storage

## TODO
- Add nginx configuration
- Add buffer cleaner
- Add ipv4/ipv6 socket linker

