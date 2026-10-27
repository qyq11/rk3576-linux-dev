# 项目专项测试

这里保存测试源码和固定输入。某个测试文件存在，只表示有这组验证代码；实际通过范围必须查对应日期、环境和源码版本的记录。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [project_advanced_run.sh](project_advanced_run.sh) | 项目高级方案的专项运行入口。 |
| [project_advanced_test.cpp / project_advanced_test.pro](project_advanced_test.cpp) | 项目高级方案专项测试源码。同名.pro定义该测试的构建目标。 |
| [projects_test.cpp / projects_test.pro](projects_test.cpp) | 项目本地存取专项测试源码。同名.pro定义该测试的构建目标。 |
| [project_config_store_rk_driver.cpp](project_config_store_rk_driver.cpp) | 在RK新匿名空库核对新增/导入、单位、引用保护、写失败与只读权限恢复；只有固定项目保护使用单独标记的旧匿名夹具。 |
| [project_config_page_rk_driver.cpp](project_config_page_rk_driver.cpp) | 在RK显示环境向正式菜单和按钮发送鼠标事件，核对表单、保存/取消、稀释、参数、复制/删除、文件导入和写失败后重试。截图是Qt页面绘制，不代表物理HDMI视觉已验。 |
| [project_config_rk_build.sh](project_config_rk_build.sh) | 仅ARM64交叉编译，`SN100_CONFIG_RK_STAGE=store`或`page`选目标，接一个绝对输出文件路径，最多两个编译进程；对应.pro保留qmake构建清单。 |

## 运行与结果

历史入口保留原范围。后续在WSL交叉编译后上传RK执行，不在Windows/WSL运行测试；测试对象、程序、日志和可变数据库留在根temp。两个新驱动都接`--data-dir <尚不存在的RK绝对目录>`，禁止使用现场数据根；保存重开在驱动内部使用同一目录，不重播种。页面驱动须由统一验证窗口分配真实显示环境，不能与主程序争用显示器。

2026-10-27实际范围、失败/修正、产物指纹、RK输出和资源见[同一移植记录](../../../../02_records/20261022_sn100_host_migration.md#project-config-dispatch-20261027)。后台通过不代替按钮通过，按钮事件也不代替物理键鼠、原版视觉、仪器和长时间运行。
