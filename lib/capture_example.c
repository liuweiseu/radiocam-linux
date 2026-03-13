#include <stdio.h>
#include "camera.h"

int main() {

    // initialize camera
    if (camera_init() < 0) {
        printf("Camera init failed\n");
        return -1;
    }

    camera_set_vblank(2000); // equivalently: camera_set_control(V4L2_CID_VBLANK, 2000);

    // capture one frame
    if (capture_frame("frame.raw") < 0) {
        printf("Frame capture failed\n");
        camera_close();
        return -1;
    }

    // close camera
    camera_close();

    printf("Frame saved to frame.raw\n");

    return 0;
}