| Supported Targets | ESP32 |
# 前言

你好，这是一个基于ESP-IDF框架实现的mqtt远程亮灯程序，可运行在ESP32芯片中。

使用此项目您需要:
- *硬件设备：ESP32或相关型号芯片，其他芯片没钱没接触过
- 编译环境：vscode + esp-idf框架
- 服务器：云服务器，且部署EMQX或其他MQTT服务器
- *额外条件：长得帅

## 如何使用

- 新增`components/mqtt_ssl/mqtt_ssl_config.h`配置文件(可参考同级文件.._example.h)，并配置好mqtt请求服务器地址和CA密文，目前项目使用自签模式。
- vscode:构建 + 烧录 + 监视。
- 接电，通过指示灯或日志进行wifi配网
- 使用MQTTX或其他脚本工具调用API

## 功能点

本用例采用AP/Station WIFI + HTTP + NVS + GPIO等 初步实现Wifi配置。基于MQTT SSL 实现远程控制。

- WIFI: 同时开启AP（本机热点）+Station(正常WIFI)，AP用于手动连接配网，配网成功后自动关闭。
- HTTP: 通过Web程序，可以和esp设备连接到同一局域网中，通过192.168.4.1/config?ssid=xx&password=xx接口传输凭证
- NVS: 启用NVS存储WIFI凭证
- GPIO灯: 即板载灯，本项目GPIO为2，可自行根据GPIO定义修改。
- MQTT: 通过自定义配置，连接上MQTT Borker,可通过相关订阅地址控制亮灯。

## MQTT API

| 序号 | 地址名 (Topic)                                     | 描述                 | 请求格式 (Payload) |
|------|---------------------------------------------------|--------------------|------------------|
| 1    | /ll/washroom/light/light001/down/control          | 控制洗手间灯开关     | JSON，例如：{"status":true} |
| 2    | /ll/washroom/light/light001/down/blink            | 控制板载灯闪烁     | JSON，例如：{"status":true} |

> ps:任意请求JSON格式内容中，如果包含`"freelog":true`,还会打印设备内存使用情况，方便监控内存是否存在异常泄漏

## 配网流程
1. 启动后设备自动读取凭证并连接WIFI
2. 连接失败时，启动HTTP和AP热点，同时板载灯`常亮`
    1. 通过手机、电脑等设备连接上设备的热点，名称形如`SoftAp_xxx`
    2. 通过外设，手动访问地址(192.168.4.1/config?ssid=xx&password=xx),将设备可连入的WIFI凭证传递
    3. 收到请求后，设备自动连接，连接期间板载灯`闪烁` 
    4. 连接失败，则板载灯`常亮`，等待重新进行HTTP请求操作
    5. 连接成功，板载灯关闭，AP热点关闭
3. 连接成功，存储凭证，同时设备仅仅开启Station WIFI网络，无其他占用。
##  板载灯提示含义
> - 板载灯`闪烁`时,不可进行HTTP请求，否则设备将重启，需要重新连接热点
> - 板载灯`常亮`时,表示等待HTTP请求配网中
> - 板载灯`熄灭`或`不亮`时,表示网络已连接，无需配网

## 主要事件描述
本用例中存在多个组件的事件定义，统一由`app_main.c`程序调度。
- `WIFI_MANAGER_CONNECTED_SUCCESS`
    即WIFI连接成功、已获取到IP地址，代表配网成功
- `WIFI_MANAGER_CONNECTED_FAIL`
    即WIFI配网失败，如WIFI凭证错误导致的连接失败
- `HTTP_RECIVE_SSID`
    即收到配置接口的请求
