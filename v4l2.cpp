#include "v4l2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include <linux/videodev2.h>

struct buffer {
    void *start;
    size_t length;
};

static int fd = -1;
static struct buffer *buffers;
static int buf_count = 0;


static int xioctl(int fh, int request, void *arg)
{
    int r;
    do {
        r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);
    return r;
}

int v4l2_open_device(const char* dev_name)
{
    struct v4l2_capability cap;
    struct v4l2_fmtdesc fmtdesc;

    fd = open(dev_name, O_RDWR, 0);
    if (-1 == fd) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno, strerror(errno));
        return -1;
    }

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n", dev_name);
            return -1;
        } else {
            fprintf(stderr, "VIDIOC_QUERYCAP error %d, %s\n", errno, strerror(errno));
            return -1;
        }
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s is no video capture device\n", dev_name);
        return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s does not support streaming i/o\n", dev_name);
        return -1;
    }

    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    printf("support format:\n");
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1) {
        printf("\t%d.%s\n", fmtdesc.index + 1, fmtdesc.description);
        fmtdesc.index++;


        struct v4l2_frmsizeenum  frmsize;
        frmsize.index = 0;
        frmsize.pixel_format = fmtdesc.pixelformat;
        while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) != -1) {
            printf("\t%s\t%4dx%4d(", fmtdesc.description, frmsize.discrete.width, frmsize.discrete.height);
            frmsize.index++;

            struct v4l2_frmivalenum  framival;
            framival.index = 0;
            framival.pixel_format = fmtdesc.pixelformat;
            framival.width = frmsize.discrete.width;
            framival.height = frmsize.discrete.height;
            while(ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &framival) != -1) {
                printf("%s%d", framival.index == 0 ? "" : "|", framival.discrete.denominator / framival.discrete.numerator);
                framival.index++;
            }
            printf(")\n");
        }
    }


    return 0;
}

void v4l2_close_device()
{
    if (fd >= 0) {
        close(fd);
    }
    fd = -1;
}

int v4l2_init_device(int width, int height, IMAGE_FORMAT_E format)
{
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_streamparm param;

    printf("set image: %dx%d\n", width, height);
    printf("set format: %s\n", (format == FMT_MJPEG) ? "MJPEG" : "YUYV");
    memset(&fmt, 0, sizeof(struct v4l2_format));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = (format == FMT_MJPEG) ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt)) {
        fprintf(stderr, "VIDIOC_S_FMT error %d, %s\n", errno, strerror(errno));
        return -1;
    }

    memset(&param, 0, sizeof(struct v4l2_streamparm));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    xioctl(fd, VIDIOC_G_PARM, &param);
    int fps = param.parm.capture.timeperframe.denominator / param.parm.capture.timeperframe.numerator;
    printf("get fps: %d\n", fps);
    if (fps > 30) {
        fps = 30;
        printf("set fps: %d\n", fps);
        param.parm.capture.timeperframe.denominator = param.parm.capture.timeperframe.numerator * fps;
        if (-1 == xioctl(fd, VIDIOC_S_PARM, &param)) {
        fprintf(stderr, "VIDIOC_S_FMT error %d, %s\n", errno, strerror(errno));
        }
    }

    memset(&req, 0, sizeof(struct v4l2_requestbuffers));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) {
            fprintf(stderr, "Device does not support memory mapping\n");
            return -1;
        } else {
            fprintf(stderr, "VIDIOC_REQBUFS error %d, %s\n", errno, strerror(errno));
            return -1;
        }
    }
    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory on device\n");
        return -1;
    }

    buffers = (buffer*)calloc(req.count, sizeof(*buffers));
    if (NULL == buffers) {
        fprintf(stderr, "Out of memory\n");
        return -1;
    }

    buf_count = req.count;
    for (int i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf)) {
            fprintf(stderr, "VIDIOC_QUERYBUF error %d, %s\n", errno, strerror(errno));
            goto err;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (MAP_FAILED == buffers[i].start) {
            fprintf(stderr, "mmap error %d, %s\n", errno, strerror(errno));
            goto err;
        }
    }

    return 0;

err:
    for (int i = 0; i < buf_count; i++) {
        if (NULL != buffers[i].start) {
            munmap(buffers[i].start, buffers[i].length);
        }
    }
    free(buffers);

    return -1;
}

void v4l2_uninit_device()
{
    for (int i = 0; i < buf_count; ++i) {
        if (-1 == munmap(buffers[i].start, buffers[i].length)) {
            fprintf(stderr, "munmap error %d, %s\n", errno, strerror(errno));
        }                    
    }
    free(buffers);
}

int v4l2_start_capturing()
{
    struct v4l2_buffer buf;
    enum v4l2_buf_type type;

    for (int i = 0; i < buf_count; i++) {
        memset(&buf, 0, sizeof(struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) {
            fprintf(stderr, "VIDIOC_QBUF error %d, %s\n", errno, strerror(errno));
            return -1;
        }
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fd, VIDIOC_STREAMON, &type)) {
        fprintf(stderr, "VIDIOC_STREAMON error %d, %s\n", errno, strerror(errno));
        return -1;
    }

    return 0;
}

void v4l2_stop_capturing()
{
    enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type)) {
        fprintf(stderr, "VIDIOC_STREAMOFF error %d, %s\n", errno, strerror(errno));
    }
}

int v4l2_get_image(IMAGE_S* image)
{
    struct v4l2_buffer* buf = new struct v4l2_buffer;

    memset(buf, 0, sizeof(struct v4l2_buffer));
    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf->memory = V4L2_MEMORY_MMAP;
    if (-1 == xioctl(fd, VIDIOC_DQBUF, buf)) {
        fprintf(stderr, "VIDIOC_DQBUF error %d, %s\n", errno, strerror(errno));
        delete buf;
        return -1;
    }
    
    if (buf->bytesused < 1024) {
        if (-1 == xioctl(fd, VIDIOC_QBUF, buf)) {
            fprintf(stderr, "VIDIOC_QBUF error %d, %s\n", errno, strerror(errno));
        }
        printf("============skip\n");
        delete buf;
        return -1;
    }

    image->buffer = (char*)(buffers[buf->index].start);
    image->length = buf->bytesused;
    image->p = buf;
    return 0;
}

void v4l2_release_image(IMAGE_S* image)
{
    struct v4l2_buffer* buf = (struct v4l2_buffer*)(image->p);
    if (-1 == xioctl(fd, VIDIOC_QBUF, buf)) {
        fprintf(stderr, "VIDIOC_QBUF error %d, %s\n", errno, strerror(errno));
    }
    delete buf;
}

static void process_image(const void *p, int size)
{
    static int i = 0;
    static time_t t0 = time(0);
    static int fps = 0;
    static FILE* fp = NULL;
    fps++;
    if (t0 != time(0))
    {
        printf("===============FPS: %d\n", fps);
        t0 = time(0);
        fps = 0;

        fp = fopen("1.jpg", "w");
        fwrite(p, size, 1, fp);
        fclose(fp);
    } 
}


int read_frame()
{
    struct v4l2_buffer buf;
    
    memset(&buf, 0, sizeof(struct v4l2_buffer));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
        fprintf(stderr, "VIDIOC_DQBUF error %d, %s\n", errno, strerror(errno));
        return -1;
    }

    process_image(buffers[buf.index].start, buf.bytesused);
    printf("=================buf.bytesused: %d\n", buf.bytesused);

    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) {
        fprintf(stderr, "VIDIOC_QBUF error %d, %s\n", errno, strerror(errno));
        return -1;
    }

    return 0;
}

int main1(int argc, char **argv)
{
    v4l2_open_device("/dev/video21");
    v4l2_init_device(1920, 1080, FMT_MJPEG);
    v4l2_start_capturing();

    while (1) {
        read_frame();
    }
    
    v4l2_stop_capturing();
    v4l2_uninit_device();
    v4l2_close_device();
    return 0;
}