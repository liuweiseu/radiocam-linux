# Device Tree Overlay
There are two ways to load the device tree overlay (dtbo):
1. load it during the system boot time;
2. load it during the running time.
**Note:** All of the operations metioned here are on the SBC.

## Load dtbo during boot time
1. copy the `radiocam.dtbo` to `/boot/dtb/rockchip/overlay`, and rename it to `rk3588-radiocam.dtbo`
    ```
    sudo cp radiocam.dtbo /boot/dtb/rockchip/overlay/rk3588-radiocam.dtbo
    ```
2. enable the dtbo
    ```
    sudo orangepi-config
        --> System
            --> Hardware
                --> radiocam
    ```
3. reboot the SBC

## Load dtbo during runtime
1. copy `radiocam.dtbo` to `scripts` directory
    ```
    cp radiocam.dtbo scripts
    ```
2. copy the `scripts` directory to the SBC
    ```
    scp -r scripts xxx@xx.xx.xx.xx
    ```
3. ssh to the SBC, and run `load_dtbo.sh`
    ```
    ssh xx@xx.xx.xx.xx
    cd scripts
    sudo ./load_dtbo.sh
    ```
**Note:** If you load the dtbo in this way, you will see the warning messages like this in `dmesg`:  
    ```
    overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/mipi_in_ucam2 
    ```