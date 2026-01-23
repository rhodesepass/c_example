# uart_test

这个示例展示了如何使用UART接口，链接ESP-01S模块，然后通过ESP-01S请求“一言”api，并显示在屏幕上。

## 接线：

接口定义请见：[接口定义](https://ep.iccmc.cc/guide/develop/extension.html#%E6%8E%A5%E5%8F%A3%E5%AE%9A%E4%B9%89)

```
3.3V -> 模块VCC
GND -> 模块GND
UART2RX -> 模块TX
UART2TX -> 模块RX
```

:warning: 请注意，请先在srgn_config中打开uart2的接口，否则会报错。

PA端口好像有bug，先试试uart2把。

一言api地址：http://v1.hitokoto.cn/?encode=text

请先配置wifi ssid和密码，然后把字体(SourceHanSansSC-Regular.ttf)放在和编译产物同级目录下，然后执行。