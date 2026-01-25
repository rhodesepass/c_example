# 电子通行证 C语言二次开发示例

本repo中存放电子通行证的C语言基础库及开发示例。

* [lib](lib) 存放电子通行证的C语言基础库。
* [examples/epniccc](examples/epniccc) ST-NICCC移植，主要展示双缓冲画图、按键读取的写法。
* [examples/libgpio_test](examples/libgpio_test) 展示libgpio的使用。(c语言读写GPIO)
* [examples/uart_test](examples/uart_test) 使用UART驱动和ESP-01S模块，连接一言API，并使用ttf绘制到屏幕上。
* [examples/i2c_test](examples/i2c_test) 使用I2C驱动和SSD1306屏幕，显示“Hello, World!”。
* [examples/spi_test](examples/spi_test) 使用SPI驱动和ADXL345加速度计，读取加速度数据。

## 编译

```bash
mkdir build
cd build
cmake ..
make -j
make install
```

编译产物在dist下。

## 开源代码感谢

* [iliapenev/ssd1306_i2c](https://github.com/iliapenev/ssd1306_i2c)
* [Digilent/linux-userspace-examples](https://github.com/Digilent/linux-userspace-examples)
* [nothings/stb](https://github.com/nothings/stb)