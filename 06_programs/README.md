# 正式程序源码入口

这里保存自己维护的程序源码、工程文件和构建脚本。可执行文件、对象文件和临时运行数据放根temp目录。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [rk.sh](rk.sh) | 通用WSL→ARM64编译、上传、校验和运行入口；build只本地编译，deploy/all会访问RK。 |
| [.gitignore](.gitignore) | 排除误生成的构建目录、对象、Qt Creator个人配置及只读参考包。 |
| [workflow_demo/](workflow_demo/README.md) | 最小C示例，验证自动构建/部署/执行链路。 |
| [sn100_host/](sn100_host/README.md) | 正式SN100移植工程；完整界面入口在shell目录。 |
| [sn100_host_reference/](sn100_host_reference) | 老板提供的Windows原工程与压缩原件，只读参考、本地保留；不在此修改、构建或用旧配置连接仪器。 |

原工程已去掉重复目录层级：主工程直接位于[Seerain450_PC/SeeRain450.pro](sn100_host_reference/Seerain450_PC/SeeRain450.pro)，数据库和配置位于同层的[bin/](sn100_host_reference/Seerain450_PC/bin/)。[移动后引用核对](../02_records/20261022_sn100_host_migration.md#reference-layout-20261027)记录新旧路径对应关系。

## 从哪里开始

先读对应程序README，再执行其构建/运行流程。新正式程序提供`build.sh <绝对输出文件路径>`；通用工作流详见[实际记录](../02_records/20261021_ai_workflow.md)。SN100独立页与统一主程序有不同入口，不能互相替代。
