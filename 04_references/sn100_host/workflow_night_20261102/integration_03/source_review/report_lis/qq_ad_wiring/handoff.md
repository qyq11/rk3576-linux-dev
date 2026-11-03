# Qq真实AD展示：两片组合交接

当前组合是**下一批候选**，没有改正在构建integration_02的正式输入。第一接口片`../qq_ad_display/`保持固定；本目录第二片只给主入口窄补丁，补齐真实调用。AD指主控给出的原始光电采样读数，本片不计算检测结果。

## 最终采用的文件和顺序

1. 先合入`../qq_ad_display/qq_ad_display.patch`，仅修改`instrument/instrument_page.cpp/.h`，建立只读通道表、Qq接收及原型号重载接口。
2. 再合入本目录`qq_ad_wiring.patch`，仅修改`shell/main.cpp`，初始化真实通道快照并接实际Qq/Qj信号。

补丁目标均在`06_programs/sn100_host/`。完整候选和base供比对，不以整文件复制覆盖后来公共修改；若底稿变化，由总窗口按窄补丁合并。两片均已检查能在各自当前正式底稿应用，正式文件仍与base逐字节一致。

| 文件 | 改前SHA-256 | 候选SHA-256 |
| --- | --- | --- |
| `instrument/instrument_page.cpp` | `e9dcdd69f6956ff931dcb84e0a56a42fe4eedec79ec17be0f8f304ec8f684f57` | `f0f888855f0d0407c8d89d89c520fff16e5bc036dd40c853eac9d60d936347f6` |
| `instrument/instrument_page.h` | `0f011bfe9f81ebbf01ed64025204a83e9492a04bb45602f3567fbd8b5403c177` | `2112f32629edbbf54e34033e7198b163ffbef7096ee0d224607a0cca19749ae5` |
| `shell/main.cpp` | `924d5fb5fa6544210b509b99e9adc0ddb7b3dce96ccb58464521aa4768869964` | `f200237e5f6b9ce0a38238cd912d004944f95ed7bfabf4013b4bb1ccfdce6687` |

第一片及本目录`candidate_manifest.json`、组合清单`combination_manifest.json`保存具体身份。没有修改`.ui`、协议、构建脚本或其他模块源码。

## 为什么没有新增后台读取

最初按总窗口要求准备让MaintenanceStore在线程内窄读原通道；随后核到新正式main已经在`withOriginalProjects && withDeviceControl`分支，于连接前成功`loadSystemProfile(originalDbRoot,&originalAlarmProfile,...)`。其中`channels`已经只读加载原`db/systemInfo/channel.db`的test/warm/cache三表及指纹。

现直接复制这份初始化值快照给页面，避免重复读取，也没有在GUI报文回调里做SQL。负责后台的子Agent在收到更正时尚未创建或修改任何候选文件；**本组合没有MaintenanceStore、原system_profile、元类型注册或新数据库读取改动**。原库但不开启真实控制、或不使用原库的分支不消费默认空profile，仍保持通道展示未就绪。

## 实际入口和消息流

- 原依据：`CommandReceiver::pcQueryAD`→`signalPcQueryAD`→`frmDriverSet::slotPcQueryAD`；原`refreshChannelTable`从通道原库建行，精确字符串匹配，四项原文字写第2～5列，详细字段/权限/短项及颜色依据见第一片handoff。
- 新初始化：在`main`创建并挂接InstrumentPage后，使用已加载的`originalAlarmProfile.channels`和本次实际生效主题的通道数，调用`configureOriginalAdChannels`。主题显式覆盖时用`themeConfiguration`，否则用原运行配置。配置失败真实返回错误，不生成占位通道。
- 新真实入口：连接`DeviceControl::payloadReceived`到同GUI线程页面，显式DirectConnection。回调开头取当前包的连接代次，Qq立即交`receiveOriginalAd(payload,generation,...)`，没有异步队列后把旧包重新标代次。页面继续验证实际连接、代次和原完整解析；成功计入`originalAdMessageCount`，错误输出`ORIGINAL_AD_REJECTED`。
- 新型号重载：DeviceControl先完整校验本次Qj并安装快照，随后回调读主字段20/21（从0计数）中的试剂位/测试通道数。旧协议缺项时按原源码明确的20/18缺省。只有试剂位数量相对当前值改变，才调用`reloadOriginalAdChannels`，重载450nm表头和通道行。没有每个Qj重建或仅按SN500名称判断。

原版只在试剂位数量变更时更新通道数并发型号重载；即使回包中的测试通道数单独变化而试剂位数不变，本片也保留原条件，不擅自扩大重载。24通道最后一列表头仍为空格。

## 输出、错误及恢复

通道开关显示原库`is_open`并保持不可修改，不将其说成设备已经确认。Qq不创建行、不改开关、不发命令，读数按原顺序和文字进入已有表格。`Base`按当前角色4/5显示；角色切换重建可见行。连接代次改变、断线/状态失效后清旧AD，收到实际新包后才更新。

原短回复未提供的后列保留旧文字，提示和`Qt::UserRole=false`明确它不是本次读数；合法零数量清为`-`，格式错误不按零数量处理。重复通道首值、完整字符串身份、保留空项及已有严格字段校验不改。Qq缺独立序号和单独超时规则，停止Qq但Qj持续有效时保留最后接收值及时间，不能称持续测量已经通过。

通道初始化失败记录`ORIGINAL_AD_CHANNELS_INVALID`，真实Qq错误记录`ORIGINAL_AD_REJECTED`，重载错误记录`ORIGINAL_AD_RELOAD_REJECTED`，均不更改数据库或重发设备动作。具体TX/RX继续由原实际连接日志记录，没有新的模拟来源。

## 实际验证到哪一步

- 第一片最终页面与Qt元对象已ARM64编译退出0，两个对象均AArch64，输入指纹为上表f0f88885/2112f326；详见第一片最终`compile_slice_arm64.log`。
- 本片`compile_slice_arm64.sh`使用第一片固定的新页面头文件和当前正式其余头文件，C++14、Qt5.15.13匹配头文件、Release参数编译主入口对象，实际退出0、ELF64/AArch64。`main.o`为`2a626bb3bc3635f751059872c072d0f65acebd0aa68ef0c26e8daf8a0257814d`，本目录最终编译日志包含输入SHA。
- `git apply --check`通过；主入口保留原CRLF换行，按`core.whitespace=cr-at-eol`核新增内容无空白错误。首次直接把CR当空白导致检查报错，已确认并非新增行尾空格，没有为消除误报全文件改换行。
- 独立子Agentreview确认main当前与base均924d5fb5，复用profile的分支、有效主题、控制器创建/连接时机、同线程代次、Qj索引/缺省和原重载条件未见确定bug。原运输层也会拒绝旧连接批次。没有运行产品或制造消息作review依据。
- 没有两片合入后的完整主程序链接、正式发布或RK联机结果；这些由总窗口在下一批统一完成，不把当前integration_02结果自动归给本组合。

## Windows实际核对

已按交出的导航时段实际查看既有Windows程序。20:17:09显示SN450/等待连接、管理员会话，仪器状态表有1～18及Base、405/570/660/800nm表头、1～18蓝色开启/Base白色关闭、所有AD为`-`。没有点击开关/连接/设备控制，没有改配置或生产资料。见`windows_review.md`及本聊天原始截图；导航已交回总窗口并由其交QC聊天。

这只证明未连接状态下的原版表格可见，不证明真实AD更新、24通道、450型号重载、低角色权限或断线恢复实际运行通过。原Windows既有程序与当前RK SN500机型不同，剩余分支继续列待核。

## 总窗口下一批验收条件

确认两片同时纳入新构建快照，再做全量ARM64链接与上传指纹核对。确认板B身份、实际生效INI及正在运行的原主控；本片只读，不需新增机械动作或通道写入授权。

1. 核`ORIGINAL_AD_CHANNELS_READY`来自原通道快照；当前实际Qq的count、通道完整名称、四段值逐个对应页面原行和原列，配原收发日志及`originalAdMessageCount`核包到展示，不只看是否出现数字。
2. 核当前实际24通道、实际试剂位数量变化时的重载；未达到变更条件时不重建清值。相同数量连续Qj不反复清AD。
3. 实际断开、重连及新代次包必须清旧值再更新；缺通道/零数量/重复/短字段仅在真实出现时核，未出现不造包替代通过。
4. 核管理员/工程师与低角色Base显示、正常进出页面及关闭重开；开关只读。当前没有按同负载测CPU、内存、响应或长稳，仍待验。

本片停止在固定交接点，不修改正式源码、不部署、不占显示；LIS、Uc/Tc、耗材、长清洗或其他机械动作不随本片展开。
