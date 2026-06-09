1. 单片机型号是CH32V005F6P6，数据手册是\\0-AI\_Files\\CH32V006-005DS0.PDF（注意里面是CH32V005/006整个系列的10余型单片机，除通用信息外必须看CH32V005F6P6这一型号的），开发参考手册是\\0-AI\_Files\\CH32V00XRM.PDF。
2. 对于非代码文件，必须存储于\\0-AI\_Files 文件夹内，包括但不限于生成的python调试文件等。
3. 不要编译构建工程。
4. DX-BT311蓝牙模组接线：与单片机USART2\_AF\_3（三号重映射引脚）：PD2(TX)、PD3(RX) ；RST、BRTS、CTS、LINK都悬空。
5. DX-GP21定位模组接线（是串口，不是SPI）：与单片机USART1：PD5(TX)、PD6(RX)；WAKE\_UP、RESET、ANT\_ON、SET都悬空；有纽扣VCC\_Backup供电。
6. SX1276模组接线：与单片机SPI：MISO-PC7, MOSI-PC6, CLK-PC5（与W25Q64 SPI FLASH共用CH32V005F6P6的唯一SPI硬件外设），SX1276的CS接单片机PC4（不是硬件SPI NSS外设，就是普通GPIO）；RESET、DIO0、3、4、5都悬空；DIO1-PA1，DIO2-PA2。
7. W25Q64 SPI FLASH存储芯片接线：与单片机的SPI线同上，W25Q64的CS接单片机PC3（不是硬件SPI NSS外设，就是普通GPIO）；WP#、HOLD#/RST#固定上拉，数据手册在 \\0-AI\_Files\\C179171\_NOR+FLASH\_W25Q64JVSSIQ\_规格书\_NOR+FLASH\_W25Q64JVSSIQ\_英文规格书.docx 。
8. 一律用简体中文回复（除了特殊地方，如英文术语或用户指定要求等）。
9. 对于SX1276模组，官网链接是： https://www.semtech.com/products/wireless-rf/lora-connect/sx1276 ，如果需要你可以联网查看和下载数据等；数据手册已经下载到了本地的\\0-AI\_Files\\DS\_SX1276-7-8-9\_W\_APP\_V7.pdf。

