# 试剂专项测试

这里保存测试源码和固定输入。某个测试文件存在，只表示有这组验证代码；实际通过范围必须查对应日期、环境和源码版本的记录。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [reagent_batch_test.cpp / reagent_batch_test.pro](reagent_batch_test.cpp) | 试剂批组专项测试源码。同名.pro定义该测试的构建目标。 |
| [test_reagents.cpp](test_reagents.cpp) | 试剂本地存取专项测试源码。 |
| [tests.pro](tests.pro) | 试剂独立测试的qmake工程定义。 |

## 运行与结果

从[SN100工程README](../../README.md)找到对应WSL/构建入口；测试对象、生成程序、日志和可变数据库留在根temp。测试应使用明确隔离的匿名目录，不导入参考生产库。电脑离线、Windows编译、ARM64编译、RK运行和真实仪器分别登记。
