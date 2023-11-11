#pragma once

typedef enum _IMAGE_FORMAT {
    FMT_MJPEG,
    FMT_YUYV
} IMAGE_FORMAT_E;

typedef struct _IMAGE_S {
    char* buffer;
    int length;
    void* p;
} IMAGE_S;

int v4l2_open_device(const char* dev_name);
void v4l2_close_device();
int v4l2_init_device(int width, int height, IMAGE_FORMAT_E format);
void v4l2_uninit_device();
int v4l2_start_capturing();
void v4l2_stop_capturing();

int v4l2_get_image(IMAGE_S* image);
void v4l2_release_image(IMAGE_S* image);
