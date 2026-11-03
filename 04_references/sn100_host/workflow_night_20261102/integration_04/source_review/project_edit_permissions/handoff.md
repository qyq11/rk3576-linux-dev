# 原资料会话的项目编辑权限候选

本片让原资料会话中的工程师、管理员按原项目页面的角色规则编辑配置，去掉当前工程额外叠加的运行态限制。仍须已经认证，仍只允许角色4和5；不是把所有已登录用户都变成可编辑。旧独立/匿名资料会话的就绪条件保持原样。

只修改本目录`candidate/projects/projects_store.cpp`，正式源码和共享计划未动；上一片`project_default_calibration/`完整保持独立。候选只有两个局部差异：`ProjectsStore::canEdit()`区分是否有`originalPolicySource`原会话标记；拒绝时，原会话提示只说明实际要求的认证和角色，不再误写必须就绪。旧独立分支的拒绝提示仍保留原串。

## 原保存、复制和删除入口证据

以下原路径以`06_programs/sn100_host_reference/Seerain450_PC/`为根。

| 原入口 | 实际规则和结果 |
| --- | --- |
| `frmsystem/frmsystemmaterial.cpp::slotUserChange()`，809至827行 | 保存、取消、改变、复制和删除共用`permission >= Engineer`；角色枚举在`public/systemcommon.h`132至139行，Engineer为4、Administrator为5。六个正常角色只有这两项达到门槛。 |
| `on_btnSave_clicked()`，2471行 | 由原保存按钮进入，先备份、收集控件、逐表保存；无`actionReady/actionTesting`拒绝。没有新增任何临床计算授权。 |
| `on_btnCopy_clicked()`，2840行 | 原复制按钮和复制名称确认，复制项目资料及校准关联；不是只有管理员可操作。本片仍保留当前仓库尚未支持外部关联时的明确拒绝。 |
| `on_btnDelProj_clicked()`，2965行 | 原删除按钮的自动槽；固定项目先拒绝，用户确认后删除并处理原关联。取消确认不删除。本片不改当前仓库更严格的引用检查、固定保护或事务。 |

原登录、注销和启动会更新这套按钮角色：主窗口构造中的注销连接约526行，登录框身份变化约680行，`slotUserChanged()`约970至975行都会调用项目页的`slotUserChange()`。

“隐藏删除”的证据范围必须区分：当前只读原`.cpp`对`btnDelProj`的全部引用未见隐藏调用，原`.ui`按钮及父`systemFunMenu`也未设置不可见，按钮几何位置在父控件内部。已核的是存在真实删除槽和角色门槛；实际发行是否隐藏须看既有画面或后续RK对照，不能据措辞宣称已证明隐藏。本片不修改显示、添加快捷键或另开放删除入口。

## 父窗口和全局状态已核范围

原`frmmain/frmmain.cpp`创建分析项目按钮并绑定页面5；构造约454行将`menuButtonGroup::buttonClicked(int)`直接接页面切换，约460行连接项目重新读取。约563至585行的按钮状态处理针对开始/暂停，Qj处理约768至889行更新状态灯和全局`actionReady`，没有对项目导航、保存或它们的父控件设禁用。全局触控事件过滤只处理键盘。经项目对象全部引用和父控件使能链检索，未找到检测态额外阻止项目保存的源码路径。

这些是源码证据，不代表原发行运行已验证。总协调明确授权本片只移除项目原会话的额外运行态条件，设备动作、任务准备、同步和其他页面仍按各自守卫执行。

## 本片新旧判断

`originalPolicySource`来自账户后台已读取角色/权限的原工作根，定义见`data/account_store.h::AccountSession`；空值表示旧独立模式。候选不根据角色标签、账户名称或页面数据根猜它。

| 会话条件 | 候选预期判断（仅源码推导，未执行测试） |
| --- | --- |
| 未认证，任意角色和资料来源 | 拒绝 |
| 已认证，但角色不是4或5 | 拒绝，包括原会话、运行态允许和不允许两种情况 |
| 已认证，角色4或5，`originalPolicySource`非空 | 允许项目配置编辑，不再额外要求`actionReady` |
| 已认证，角色4或5，`originalPolicySource`为空 | 与旧代码相同，仍要求`actionReady` |

页面`ProjectPage::updateActions/canStartOperation/markDirty`、结果显示设置及后台`check(write=true)`已有共用`canEdit`，无需增加一个绕过后台的新入口。现有单位维护、导入/切原目录等共用消费者也继续按同一身份规则；数据源、草稿、busy、外部版本和引用检查保持。

`projects_store.cpp`中的引用登记/释放另有角色1至5和`actionReady`判断，本片未改。试剂批组等其他角色1至5入口、`SampleStore`、`DeviceControl`和任务/设备命令保护均未改；补丁不含这些文件。角色4/5以外没有通过此片获得项目配置写入权。

本片只更改一个普通静态函数和对应失败提示，不新增线程、数据字段、定时器、队列或计算。无运行期性能结论；后续需在获准RK原配置条件下核工程师/管理员、低角色、注销及运行态变化，并分别核原会话与旧独立分支。当前没有运行产品、数据库、模拟、网络或RK。

## 固定交付及验证范围

`project_edit_permissions.patch`只含一个目标的两个小段差异，1431字节，SHA-256为`205d136e7ddf47b1c8c5ca11c17cdc1afa81ba466b6a32f9f99131949d0b8258`。对当前正式源码的`git apply --check`退出0，未实际应用。底稿`projects_store.cpp`指纹为`498bc771da21e725901f8e83959cfd78ee25ca635725a82975f13678c1cd4ff2`，结束复核时正式文件仍相同；候选及对象指纹见`candidate_manifest.json`。

`compile_slice_arm64.sh`只编译候选`projects_store.o`，退出0，readelf核为AArch64小端对象。候选cpp、正式`projects_store.h`及`account_store.h`在编译前后指纹一致；日志及清单保存在本目录。告警来自未修改的`ProjectUnit`聚合初始化。没有运行权限函数用例、产品或数据库，不把预期判断表说成实际测试结果，也没有完整链接或RK/仪器通过结论。

本片与默认校准参数候选改动目标不同：本片仅`projects_store.cpp`，默认参数片只应合其三个窄目标，不复制其中用于编译的旧`projects_store.cpp`支持副本。后续04整合应分别应用窄补丁，固定完整输入并重编。
