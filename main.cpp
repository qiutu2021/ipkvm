#include "httplib.h"
#include "v4l2.h"

int main()
{
    v4l2_open_device("/dev/video21");
    v4l2_init_device(1920, 1080, FMT_MJPEG);
    v4l2_start_capturing();

    // HTTP
    httplib::Server svr;
    svr.Get("/mjpeg", [=](const httplib::Request& req, httplib::Response& resp) {
        resp.set_chunked_content_provider("multipart/x-mixed-replace; boundary=ipkvmstreamer", [](size_t offset, httplib::DataSink& sink) {
            IMAGE_S image;
            int ret = v4l2_get_image(&image);
            if (0 != ret)
                return true;

            char buffer[1024] = {0};
            sprintf(buffer, "--ipkvmstreamer\r\n" \
                "Content-Type: image/jpeg\r\n" \
                "Content-Length: %d\r\n" \
                "\r\n", image.length);
            sink.write(buffer, strlen(buffer));

            sink.write(image.buffer, image.length);
            printf("========send %d\n", image.length);

            memset(buffer, 0, 1024);
            sprintf(buffer, "--ipkvmstreamer\r\n");
            sink.write(buffer, strlen(buffer));

            v4l2_release_image(&image);
            return true;
        });
    });

    svr.listen("0.0.0.0", 8080);

    v4l2_stop_capturing();
    v4l2_uninit_device();
    v4l2_close_device();

    return 0;
}