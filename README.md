# 电子通行证 C语言二次开发示例

本repo中存放电子通行证的C语言基础库及开发示例。

* [lib](lib) 存放电子通行证的C语言基础库。
* [examples/epniccc](examples/epniccc) ST-NICCC移植，主要展示双缓冲画图、按键读取的写法。
* [examples/libgpio_test](examples/libgpio_test) 展示libgpio的使用。(c语言读写GPIO)

## 编译

```bash
mkdir build
cd build
cmake ..
make -j
```

## 编译产物路径

* ep_niccc: build/examples/epniccc/epniccc
* libgpio_test: build/examples/libgpio_test/libgpio_test