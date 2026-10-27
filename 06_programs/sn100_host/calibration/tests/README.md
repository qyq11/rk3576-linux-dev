# 校准专项测试

这里保存测试源码和固定输入。某个测试文件存在，只表示有这组验证代码；实际通过范围必须查对应日期、环境和源码版本的记录。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [calibration_create_catalog.py](calibration_create_catalog.py) | 构造校准测试需要的匿名项目/试剂目录。 |
| [calibration_固定测试数据.h](calibration_fixture.h) | 校准测试的固定数据构造辅助。 |
| [calibration_observe.sh](calibration_observe.sh) | 校准专项运行/资源观测入口。 |
| [calibration_page_test.cpp / calibration_page_test.pro](calibration_page_test.cpp) | 校准页面专项测试源码。同名.pro定义该测试的构建目标。 |
| [calibration_store_test.cpp / calibration_store_test.pro](calibration_store_test.cpp) | 校准存储专项测试源码。同名.pro定义该测试的构建目标。 |
| [calibration_verify.py](calibration_verify.py) | 核对校准专项的实际保存结果。 |

## 运行与结果

从[SN100工程README](../../README.md)找到对应WSL/构建入口；测试对象、生成程序、日志和可变数据库留在根temp。测试应使用明确隔离的匿名目录，不导入参考生产库。电脑离线、Windows编译、ARM64编译、RK运行和真实仪器分别登记。
