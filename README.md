# visca2uvc
Bridges VISCA over TCP and UVC

## Instructions

Install on your OS:

* `git`
* C++ compiler (`gcc`)
* `pkg-config`
* `libusb`
* `cmake`

```
$ git clone --recursive https://github.com/vtrebicky/visca2uvc.git
$ cd visca2uvc
$ mkdir build
$ cd build
$ cmake ..
$ cmake --build . --target visca2uvc
$ ./visca2uvc get_zoom_abs
```
