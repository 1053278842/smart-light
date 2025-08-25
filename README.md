| Supported Targets | ESP32 |
# 前言

你好，这是一个基于ESP-IDF框架实现的手动WIFI装配的程序，可运行在ESP32芯片中。

## 功能点

本用例采用AP/Station WIFI + HTTP + NVS + GPIO等 初步实现Wifi配置。

- WIFI: 同时开启AP（本机热点）+Station(正常WIFI)，AP用于手动连接配网，配网成功后自动关闭。
- HTTP: 通过Web程序，可以和esp设备连接到同一局域网中，通过192.168.4.1/config?ssid=xx&password=xx接口传输凭证
- NVS: 启用NVS存储WIFI凭证
- GPIO灯: 即板载灯，本项目GPIO为2，可自行根据GPIO定义修改。


## 事件描述
本用例中存在多个组件的事件定义，统一由`app_main.c`程序调度。
- `WIFI_MANAGER_CONNECTED_SUCCESS`
    即WIFI连接成功、已获取到IP地址，代表配网成功
- `WIFI_MANAGER_CONNECTED_FAIL`
    即WIFI配网失败，如WIFI凭证错误导致的连接失败
- `HTTP_RECIVE_SSID`
    即收到配置接口的请求

## 配置流程
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