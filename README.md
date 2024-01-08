# android 连接器
android 连接器 app会常驻手机后台，你可以随时随地保持与Fay数字人（https://github.com/TheRamU/Fay）的沟通。

![image-20231225180134156](data/image-20231225180134156.png)





**使用说明**

1、工程使用android studio 开发，可以直接下载[apk]([Release 0.2 · xszyou/fay-android (github.com)](https://github.com/xszyou/fay-android/releases/tag/0.2))运行。

2、若需要让app在公网环境保持与Fay的互通，在Fay中配置ngrok.cc 的id，并在服务器地址栏填上分配域名及端口（无须填写协议）。



**更新说明**

2024.01.08

1、优化socket管理；

2、实测7*24不掉线。

2023.12.25:

1、增加重连机制；

2、优化麦克风及蓝牙sco管理机制；

3、增加麦克风开关及服务器地址管理。