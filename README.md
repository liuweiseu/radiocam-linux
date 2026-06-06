# RadioCAM-Linux
This repo contains dts/dtbo, driver and the application example code.
## Install necessary packages
    ```
    TODO
    ```
## Compile dtbo/driver/app
### Device Tree Overlay
1. compile dtbo
    ```
    cd dts
    make
    ```
2. copy the dtbo file to the board
    ```
    scp radiocam.dtbo xx@xx.xx.xx.xx:~
    ```
### Driver
1. compile the driver
    ```
    cd driver
    ```
2. modify the Makefile
    ```
    KDIR:=/home/wei/Projects/opi5/orangepi-build/kernel/orange-pi-6.1-rk35xx
    ```
    **Note:** Make sure the `KDIR` is the directory to the compiled kernel directory.
3. copy the driver to the board
    ```
    scp radiocam.ko xx@xx.xx.xx.xx:~
    ```
### App
Build the user-space UDP streaming tools:

```
make -C apps
```

The tools are:

- `apps/radiocam_udp_sender`: streams V4L2 multi-plane capture buffers to UDP.
- `apps/radiocam_udp_recv`: validates RadioCam UDP packets and reports loss/throughput.

## Test on the board
### Load/Unload dtbo
1. ssh to the board
2. load dtbo
    ```
    sudo su
    mkdir /sys/kernel/config/device-tree/overlays/radiocam
    cat radiocam.dtbo > /sys/kernel/config/device-tree/overlays/radiocam/dtbo
    ```
3. unload dtbo
    ```
    rmdir /sys/kernel/config/device-tree/overlays/radiocam
    ```
### Load/Unload driver
1. ssh to the board
2. load driver
    ```
    sudo insmod radiocam.ko
    ```
3. unload driver
    ```
    sudo rmmod radiocam
    ```

### Fast UDP streaming

Preflight the Ethernet link before running the camera stream:

```
iperf3 -s
iperf3 -c <receiver-ip> -u -b 900M -l 1400
```

For standard 1500-byte MTU, use the default 1456-byte UDP payload. For jumbo
frames, configure both peers and the switch path, then use a larger payload:

```
sudo ip link set dev eth0 mtu 9000
sudo sysctl -w net.core.wmem_max=134217728
sudo sysctl -w net.core.rmem_max=134217728
```

Run a receiver:

```
./apps/radiocam_udp_recv --bind 0.0.0.0 --port 50000 --rcvbuf 67108864
```

Run the sender on the board:

```
./apps/radiocam_udp_sender \
    --device /dev/video0 \
    --dest <receiver-ip> \
    --port 50000 \
    --buffers 8 \
    --payload-bytes 1456 \
    --sndbuf 67108864 \
    --stats-interval 1
```

For jumbo MTU testing:

```
./apps/radiocam_udp_sender --dest <receiver-ip> --payload-bytes 8960
```

The sender can mirror a sparse packet sample to a local Python analyzer without
changing the full-rate UDP path:

```
./apps/radiocam_udp_sender \
    --dest <receiver-ip> \
    --analysis-port 50001 \
    --analysis-every 1000
```

Hardware test checklist:

```
v4l2-ctl --stream-mmap=8 --stream-to=/dev/null
iperf3 -c <receiver-ip> -u -b 900M -l 1400
./apps/radiocam_udp_recv --port 50000
./apps/radiocam_udp_sender --dest <receiver-ip> --port 50000
```

Confirm that frame, packet, and byte counters increase monotonically and that
reported packet/frame gaps stay within the expected network drop rate.

