# Networking

Status: design only.

Two interfaces, because network hardware comes in two shapes: devices that
run TCP/IP themselves (you ask them for connections) and devices that move
raw Ethernet frames (you need your own TCP/IP stack). Consumers above
(`beer.tcp`-style sockets, an HTTP client, the REPL over the network) only
see the socket interface; frame devices get there through a stack.

```
 apps ── socket interface ─┬─ offload devices: ESP32 (ESP-AT), W5500
                           └─ TCP/IP stack ── frame interface ─┬─ virtio-net (QEMU)
                                                               ├─ ESP32 (ESP-Hosted)
                                                               └─ on-board Ethernet MAC
```

## Socket interface (offload)

A network device map, like the block-device map:

```clojure
{:name "wifi0"
 :connect!   (fn [host port] conn)        ; TCP; throws ex-info on failure
 :send!      (fn [conn buf off len] n)    ; ByteBuffer
 :recv!      (fn [conn buf off len] n)    ; 0 = nothing yet, nil = closed
 :close!     (fn [conn])
 :listen!    (fn [port] listener)         ; optional
 :status     (fn [] {:up? true :ip "192.168.1.20" ...})}
```

Plus device-specific setup (`join-wifi!` with SSID/password) outside the map.

### ESP32 as a Wi-Fi co-processor (ESP-AT)

The first target for the ZX LISP Machine. Espressif's ESP-AT firmware
speaks AT commands over UART: `AT+CWJAP` joins a network,
`AT+CIPSTART` opens a TCP connection, `AT+CIPSEND` / `+IPD` move data.
The ESP32 runs Wi-Fi, WPA, DHCP, DNS and TCP; beeros only needs a command
parser in beerlang on a UART. ESP32-C3/C6 are RISC-V too.

Limits: a handful of simultaneous connections, throughput bounded by the
UART (a few Mbit/s at high baud), no raw packets.

### W5500 (wired, offload)

SPI Ethernet chip with a hardware TCP/IP stack (8 sockets). Same
socket-interface shape as ESP-AT; needs an SPI driver on the board.

## Frame interface (raw Ethernet)

```clojure
{:name "eth0" :mac [0x52 0x54 0 0x12 0x34 0x56] :mtu 1500
 :send-frame! (fn [buf off len])
 :recv-frame! (fn [buf off] len-or-nil)}  ; nil = no frame waiting
```

Backends: virtio-net under QEMU (same virtqueue code as virtio-blk and
virtio-input), ESP32 running ESP-Hosted over SPI/SDIO (the ESP32 becomes a
Wi-Fi NIC), on-board MACs on generic boards.

### TCP/IP stack

Needed only for the frame interface. Options: lwIP in C behind natives
(proven, ~40k lines), or a small teaching stack in beerlang (ARP, IPv4,
ICMP, UDP, DHCP, then TCP) — slower, but readable and hackable like the
rest, which suits the teaching-machine goal.

## Order

1. ESP-AT over UART (gets the machine online with the least code).
2. virtio-net + a beerlang stack (ARP/ICMP/UDP first) under QEMU.
3. TCP in that stack, then ESP-Hosted / on-board MACs on real hardware.
