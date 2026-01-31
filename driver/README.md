# Load driver
There are two ways to load the drivers:
1. load the driver during the system boot time;
2. load the driver during the running time.  
To use the V4L2 framework, it would be great to use option 1. 

## Load the driver during the system boot time
1. copy the driver to the system driver directory
    ```
    cd /lib/modules/$(uname -r)/kernel/drivers/

    # create the sdr dir, if it doesn't exist
    sudo mkdir sdr

    sudo cp ~/radiocam.ko sdr/
    ```
2. let the system scan all the driver directories
    ```
    sudo depmod -a
    ```
3. add a `conf` file to let the system load the driver
    ```
    sudo vim /etc/modules-load.d/radiocam.conf

    # add radiocam to this file
    ```
4. reboot the SBC

## Load the driver during runtime  
1. run the command directly
    ```
    sudo insmod radiocam.ko
    ```

# Unload the driver
1. make sure the driver is loaded
    ```
    lsmod

    Module                  Size  Used by
    ...
    radiocam               20480  0
    ...
    ```
2. unload the driver
    ```
    sudo rmmod radiocam
    ```