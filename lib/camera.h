#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>

int camera_init();
int camera_close();

int capture_frame(const char *filename);

int camera_set_control(uint32_t id, int value);

int camera_set_vblank(int value);
int camera_set_exposure(int value);
int camera_set_gain(int value);
int camera_set_test_pattern(int mode);

#endif
