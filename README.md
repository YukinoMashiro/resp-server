# 介绍
1. `resp-server`是支持`redis`的`RESP2`协议的一个服务端，只需简单利用接口，便可以直接与redis客户端交互，完成命令行解析和回复。
2. `resp-server`是基于`redis-6.0.9`版本代码提炼完成的，并非从零开发。因此绝大部分代码来源于redis。此项目旨在是为想开发k-v数据库的同学提供一个工具去解决命令行的解析与回复的问题，将更多的精力放在数据库的开发上。
# 环境配置
```bash
cmake # 最低版本3.16
make  # 任意版本
gcc   # 任意版本
Linux # 当前仅支持Linux
```
# 快速开始
## 如何启动resp-server
```shell
cd resp-server
cmake . # cmake生成make文件
make    # 编译源码
./resp-server #启动resp-server，监听默认端口2233 
./redis-cli -p 2233 # redis-cli连接，redis-cli可以从redis官网或者源码编译获得
```

## 如何引用此项目
- `main.c`中举例了接口使用，可参考此文件。
- 首先调用`respInitOptions`指定端口号、日志文件、命令列表。
- 最后调用`respListenEvent`循环监听事件触发。

## 注意事项
1. 因此项目只使用了一个线程，`respListenEvent`中监听事件是死循环，因此如果要增加业务，请在此函数调用前增加
2. 因redis客户端连接时，一定会自动发送`command`命令，因此自定义命令列表时，务必加入`command`

