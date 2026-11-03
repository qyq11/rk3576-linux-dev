# v2实际检查结果

- 最终只编`candidate/shell/main.cpp`，WSL Ubuntu-24.04/qyq，沿用Qt5.15.13 ARM64头与C++14 Release参数；退出0，无C++错误。未重复v1八对象、未链接产品或运行程序。
- 最终`main.o`为ELF64/AArch64可重定位对象，SHA `49f96b43ea16916c69d3ecf19321790fb39194d9e7abc345549e4493807d2ac4`。
- 最终main源码SHA `94fbde27ab3da3577e9d03a3e2e22d736b347d6b0532a63192e4bf12a599d61c`。159个项目头捕获到私有`compile_support/`，编译时不再读实时正式项目头。候选11文件加159头，共170项前后相同；两份输入清单SHA同为`54ba11c3ac6b5eb604b94940e21edaf6e9ccb88ea962655c7cb5ffcbbefac2ea`。
- 两次已实际发生的main草稿编译也退出0，分别留在`revisions/pre_wording_fix/`和`revisions/pre_anchor_fix/`；它们不代表最终源码。收齐“关闭时不说正在重读”及“不覆盖公共tooltip”两项后，仅重新编最终main一次，结果如上。没有删除或伪改这些真实记录。
- 相对v1只有main的55行新增/7行替换；`channel_notifications_delta.patch`和累计`channel_switch.patch`分别在私有v1副本/原base上只读apply检查通过。没有尝试改05正式，实际串行变基和全量链接由总窗口负责。
- `v1_preservation_check.txt`核v1固定交接清单16项未变。v2的core/page补丁SHA与v1完全相同，无新正式目录、资源或moc输入。

实现后只读重新核原Windows完成框和保存顺序、Linux状态信号实际消费者、CusMessageBox模态性及ReagentsPage已有QToolTip用法，依据指纹见`source_review_inputs.sha256`。这是源码核对，非Windows实际运行；没有Windows重建、板卡/主控/真实库操作、模拟服务或脱机测试。

源码已具备原状态区定位的非模态可见路径；是否实际显示、显示位置/遮挡/时长、操作并行影响须在RK核对。失焦时仅保留页面属性/tooltip，没有自动显示或重新激活补弹；不能把属性保存说成已显示。原设备灯/主状态文字/公共tooltip未被本片覆盖。Windows/RK/仪器/视觉/响应资源通过结论均未新增。
