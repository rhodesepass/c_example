# spi_test

这个示例展示了如何使用SPI接口，读取ADXL345加速度计的数据。

## 接线：

接口定义请见：[接口定义](https://ep.iccmc.cc/guide/develop/extension.html#%E6%8E%A5%E5%8F%A3%E5%AE%9A%E4%B9%89)

```
3.3V -> 模块VCC
GND -> 模块GND
MOSI -> 模块SDI
MISO -> 模块SDO
SCK -> 模块SCL
CS -> 模块CS
```

:warning: 请注意，请先在srgn_config中打开spi1的接口，否则会报错。

## 开源感谢

https://github.com/libdriver/adxl345 提供的ADXL345驱动代码。