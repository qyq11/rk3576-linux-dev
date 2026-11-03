# 原主题颜色的实际使用对照（2026-11-02）

这份表只回答“配置读到了之后，原程序实际把颜色用在哪里，当前程序是否接到了同一用途”。不改布局、不把相同颜色的硬编码当成配置已生效，也不按键名给其他控件上色。

本轮以用户提供的SN500主题为目标：`temp/sn100_host/board_b/20261102_original_files/working/theme_config.ini`，SHA-256为`029605f35924de646b5402f1e377c93cb26e01952e0daa5fda300fe47f7feed7`。这里只读取该非认证主题文件，没有读实际config.ini、账户或患者数据库。其颜色与原主题相同，机型及孔数按用户提供版本；下表以`R,G,B,A`表示红、绿、蓝及透明度四通道，配置原文仍用点号分隔。

两处读取入口都已包含这19键：`original_runtime_settings.cpp:159`和`theme_configuration.cpp:36`。当前主程序在指定`--theme-config`时，才于各页面挂入后调用`frmMain::applyThemeConfiguration`；只读入RuntimeSettings不等于这条界面调用已发生。下面“已接”只指源码有实际调用，不表示RK视觉验收通过。

| 键 | SN500颜色或原默认 | 原实际用途与依据 | 当前实际消费者及缺口 |
| --- | --- | --- | --- |
| `leftBaseColor` | 28,28,28,255 | 原CusMenuButton背景及主窗左侧底色；`main.cpp:111`、`cusmenubutton.cpp:8`、原主窗`:219/221` | `applyThemeConfiguration`给Logo区、menuArea及CusMenuButton，已有 |
| `leftBtnColor` | 232,51,35,200 | 左菜单悬停/选中及开始键；`main.cpp:112`、`cusmenubutton.cpp:9`、`frmmain.cpp:107` | 同一函数传`setOriginalColours`，已有，不重做 |
| `leftBtnFontColor` | 缺键，默认255,255,255,255 | 左菜单文字、图标下文字及开始文字；`main.cpp:113`、`cusmenubutton.cpp:13/29/43` | 同一函数传字体色，已有 |
| `rightBaseColor` | 28,28,28,255 | 各页右侧功能菜单底色，例如原报告`initReportForm:114/120`、校准初始化`:106` | 当前各页仍多用28,28,28常量；未接该键 |
| `rightBtnColor` | 127,127,127,255 | 右功能菜单的Material按钮底色，例如原报告`colors_`与`initReportForm` | 当前多用127,127,127常量；未接该键 |
| `rightBtnFontColor` | 缺键，默认255,255,255,255 | 同组右菜单按钮文字，原报告`:117`及其他页面初始化 | 当前多数写死white；未接该键 |
| `matchColor` | 232,51,35,255 | 原试剂优先/更换等主要按钮、各页页签和部分标题色；例如`frmreagentmaterial.cpp:73` | 当前对应红色多为常量；新耗材窗也传固定红色，不能算读取该键 |
| `matchFontColor` | 缺键，默认255,255,255,255 | 原试剂主要按钮文字；`frmreagentmaterial.cpp:75` | 当前主要按钮多写死white，未接该键 |
| `matchHoverColor` | 242,89,76,200 | 原试剂主要按钮悬停；`frmreagentmaterial.cpp:74` | 当前悬停红色仍为常量，未接该键 |
| `kBtnNormalColor` | 42,118,242,255 | 原通用按钮/单选选中色及暂停提示；`main.cpp:156/157`、原主窗`:862` | 暂停提示及InstrumentPage通道选中色已接；其他页面/弹窗通用按钮仍多硬编码，部分接入 |
| `kBtnHoverColor` | 42,118,242,170 | 原`buttonStyle::setStrSheet`生成通用按钮悬停色；`main.cpp:157` | 当前相关QSS仍硬编码，未接该键 |
| `kBtnPressedColor` | 135,206,250,255 | 同一原样式生成按钮按下色；`main.cpp:157` | 当前相关QSS仍硬编码，未接该键 |
| `kReagentHoleSelectedColor` | 42,118,242,255 | 原`reagentStyle`→试剂孔选中框；`main.cpp:158` | `applyThemeConfiguration`已调用现有`cusReagentHole::setSelectedColour`，已有，不重做 |
| `kCalibrationLineColor` | 42,118,242,255 | 原`calibrationStyle`→真正拟合线graphPoint，宽3；`main.cpp:159`、原`calibrationchart.cpp:66/69` | 当前图只显示未拟合原始点，无拟合线消费者；不能改原始点颜色或新增连线冒充接入，留到真实曲线移植 |
| `kSampleSetBtnChecked` | 42,118,242,200 | 原样本项目选中、组合按下和校准项目按钮；`main.cpp:155`、`buttonStyle`样本专用样式 | 当前`projectStyle()`写死42,118,242,255，透明度也与目标200不同；明确缺口，但本片先不混改项目按钮链 |
| `kSampleSetWidget` | 183,215,255,255 | 仅选中样本架的wgtRack背景；原`main.cpp:149`→`CusSampleRack::setChecked:138/147`，未选中保持白色 | `SampleRackView::setSelectedColour`已存在但全仓无调用，新建架也只用默认色；本轮最小候选拟接此键 |
| `kSampleSetRowSelected` | 240,227,227,255 | 原样本设置表选中行；`main.cpp:150`→`tableStyle::getStrSampleSetSheet` | `applyThemeConfiguration`给现有`tblSetting`选中项，已有；晚创建其他表/弹窗不能据此说全部覆盖 |
| `kListWidgetRowSelected` | 183,215,255,255 | 原普通表格、列表、组合框选中项；`main.cpp:151-153`→三组样式 | 当前Root给现有普通QTableWidget，未覆盖原全部QListWidget/QComboBox及晚创建弹窗，部分接入 |
| `kReportRowSelected` | 183,215,255,255 | 原报告横表/竖表及患者表专用选中项；`frm_report_material.cpp:250-256` | 当前Root只匹配名称含ResultReport，漏`tablePatientInfo`，使它误用普通列表色；本片补该明确遗漏，不改表格或选择逻辑 |

原`getConfig:149-159`先设置共享样式，再创建业务页面。当前通用样式多数在各页构造函数或弹窗打开时生成，因此不能只给启动时`findChildren`找到的控件加一层QSS，就宣称晚打开的编辑窗口也已接好。

本次19键均找到原用途，没有把某个仅有配置值的键强行认作新功能。`kCalibrationLineColor`在当前未拟合显示范围内没有可合法复用的线，属于后续真实曲线的缺口，不是给原始点重新配色的理由。上述覆盖是源码检查，不替代原发行版和RK实屏逐状态视觉核对。

## 第一份最小候选范围

准备两处有精确原控件依据的接线：`kSampleSetWidget`由Root传给SamplePage，页面更新当前架并保存颜色，在每次`refreshRackList`新建架时调用已有`SampleRackView::setSelectedColour`；同一个Root函数把`tablePatientInfo`纳入原报告专用选中色。复用现有绘制与表格样式，选中架用主题色、未选中架仍白色；不新建架、不改选择、草稿、保存、任务或数据。

预计仅3个现有文件的窄差异：`samples/sample_page.cpp/.h`和`shell/frmmain.cpp::applyThemeConfiguration`。不改main、构建、曲线、算法或04冻结输入。目标主题本来等于旧默认浅蓝色，所以本候选不会为了证明“看见变化”而换色；需要在获准RK条件下核实际消费者及新旧架显示。

原右侧菜单及部分容器从四通道配置只取RGB三个分量；不能为了统一接口而把所有消费者一律改成新的透明度效果。原报告选中项QSS写法也如实保留为依据，本次不借主题接线重做其全部样式。
