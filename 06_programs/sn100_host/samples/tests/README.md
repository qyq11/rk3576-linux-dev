# 样本预设专项测试

这里保存测试源码和固定输入。某个测试文件存在，只表示有这组验证代码；实际通过范围必须查对应日期、环境和源码版本的记录。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [sample_qc_workflow_test.cpp / sample_qc_workflow_test.pro](sample_qc_workflow_test.cpp) | 单孔质控样本预设流程专项测试源码。同名.pro定义该测试的构建目标。 |
| [sample_reset_workflow_test.cpp / sample_reset_workflow_test.pro](sample_reset_workflow_test.cpp) | 样本复位/复测草稿流程专项测试源码。同名.pro定义该测试的构建目标。 |
| [sample_task_reset_test.cpp / sample_task_reset_test.pro](sample_task_reset_test.cpp) | 样本任务状态转换专项测试源码。同名.pro定义该测试的构建目标。 |
| [samples_test.cpp / samples_test.pro](samples_test.cpp) | 样本架/任务预设专项测试源码。同名.pro定义该测试的构建目标。 |

## 运行与结果

从[SN100工程README](../../README.md)找到对应WSL/构建入口；测试对象、生成程序、日志和可变数据库留在根temp。测试应使用明确隔离的匿名目录，不导入参考生产库。电脑离线、Windows编译、ARM64编译、RK运行和真实仪器分别登记。
