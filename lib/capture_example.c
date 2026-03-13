#include <stdio.h>
#include "camera.h"

int main() {

    if (camera_init() < 0) {
        printf("Camera init failed\n");
        return -1;
    }

    camera_set_vblank(2000);

    if (capture_frame("frame.raw") < 0) {
        printf("Frame capture failed\n");
        camera_close();
        return -1;
    }

    camera_close();

    printf("Frame saved to frame.raw\n");

    return 0;
}