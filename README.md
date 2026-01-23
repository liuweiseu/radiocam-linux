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
TODO  

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

