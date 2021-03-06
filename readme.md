# 赛尔网络下一代互联网技术创新项目：基于IPv6的工业物联网多协议网关关键技术开发与验证

网关是IPv6在工业物联网部署的关键设备，而工业通信协议的兼容适配是网关在工业场景应用的关键技术问题。本项目主要研究内容包括了：
1） 研究工业物联网协议族（工业现场总线、工业无线、传统IPv4网络等），研究各类终端接入IPv6工业物联网的网关协议转换技术；
2） 针对工业现场应用的复杂信息建模需求，设计网关信息服务组件，以OPC UA（OPC Unified Architecture，OPC统一架构）基础在网关上实现工业异构网络融合信息建模；
3） 设计支持IPv6的工业物联网多协议网关软硬件架构，研发网关原型样机，搭建基于IPv6的工业物联网多协议接入场景。

# 配置

## 源码

文件中包含部分开源代码，new_duoprotocol_gateway中包含多协议网关代码

## 软硬件环境

1） Linux：open62541SDK
2） Windows：WinSCP、SecureCRT、UaExpert、NCS4000组态
3） 多协议网关（核心板MT7620）
4） modbus传感器节点

## 使用

1） 进入new_duoprotocol_gateway目录，开启终端，在执行make之前，需要命名一个Makefile的特殊文件（在 Makefile文件中描述了整个工程所有文件的编译顺序、编译规则），通过make命令生成FF1_SEND文件；
2） 通过WinSCP连接openwrt系统，将生成的文件拷贝进系统中；
3） 通过SecureCRT进入openwrt系统，并赋予文件权限，执行文件，网关成功开启服务器。

# 创建节点
新增节点需要在OPC UA Server中添加对象并自定义对象类型：
1） 创建目标网络的对象节点（自定义对象节点）；
2） 定义一个变量节点的属性，并将变量加入到信息模型中。
注意：变量节点的父节点Id是对象节点的Id，它们的关系是hasComponent.
