# 原孔位有效期候选

本目录仅为下一切片准备，尚未应用正式源、构建或RK运行。不要将当前手输日期保存通过写成原日历通过。

候选业务文件为`reagents_page.cpp`：复用已在告警页面移植的CusCalendar及同一`cusmaskcalendar.ui`，在本CPP私有日历窗口中管理遮罩、动画关闭和父子释放，不增加Q_OBJECT或访问数据库。原孔位的有效期保持只读，原空值不补一年；日期按钮异步打开日历，按原格式yyyy/MM/dd解析、无效才用当天显示；选中显式标记孔位草稿并关闭日历，取消不改值。原窗口来源是`MaskDlgReagentSetting::refreshCurrentHole/on_btnCalendarSet_clicked/on_btnSave_clicked`及`CusMaskCalendar`。现有非原库模式仍保持旧入口，未把它顺带称为原版一致。

候选测试`original_main_task_chain_rk_test.cpp`把normal场景改为真实日期按钮、日历取消、按月箭头和绘制格鼠标选择。先取消日历确认无修改；选2030/01/02后取消孔页确认原库不变/无Us；重开选2032/02/03保存，核原日期格式、精确Us/Ur及编辑器重开。清孔场景另核空日期保留、取消日历仍为空。格子位置来自现有CusCalendar的固定绘制公式，Qt测试事件不等于物理键鼠。

新依赖与构建归属先交整合处理：

- 直接复用`alarm_history/calendar/cuscalendar.cpp/.h/.ui`和`alarm_history/cusmaskcalendar.ui`，不修改其告警业务、日期格绘制或原资源。
- 统一主程序`build.sh`已包含这些输入，业务CPP不需增加新的moc。确认现有calendar头目录进入include搜索路径。
- 独立`reagents/reagents.pri`及`reagents/build.sh`需补同一日历CPP、头及两表单、头搜索目录；与`alarm_history_page.pri`共同构建时只能编一次。可由整合将重复条目改为唯一添加或用共享pri，不在本候选越权改公共构建。
- 日历左右箭头引用`:/image/toleft.png`和`toright.png`，现由`quality_control/qc_history_resources.qrc`提供；关闭图标由共享account_resources提供。统一主程序已经包含，独立试剂目标还要按唯一资源接入规则补箭头，不能只链接CPP而漏图标。
- 独立试剂build.sh原来还需核原试剂适配和项目依赖的完整清单；不能只补日历便称独立构建已恢复。

应用前逐份核`base_sources.json`，保留并行修改。当前生产11文件冻结在resume_quality_01，正式测试只修OTHER模拟回复、失败初始化清理及基础xuhao至08f05da8，本候选也同步这些修复，不替换正式测试。用户2026-10-28要求当前任务收尾后再安排测试，本候选继续保留，禁止自动应用/编译/运行。后续获用户明确继续后再按新批来源编译并直接RK检查日期、关闭/权限变化恢复及实屏；现有资料没有构成通过结论。
