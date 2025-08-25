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
