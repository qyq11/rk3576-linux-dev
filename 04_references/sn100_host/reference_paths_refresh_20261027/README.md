# 历史清单中的参考路径更新

用户于2026-10-27明确要求同步历史清单中的参考工程路径。本次要求优先于旧说明中“历史清单保留当时路径”的约定；测试日期和已验范围保持原样，压缩包内部的归档原件保持原样。

参考工程当前在`06_programs/sn100_host_reference/Seerain450_PC/`。本次更新58份说明或来源清单的498处路径，并同步这些清单自身的上级校验值，共涉及120份原有文本文件。这里的校验值是用来确认文件内容是否一致的一串字符；只更新指向本次修改清单的校验值，原业务源码、数据库和压缩包的校验值保持原样。

核对了518处参考位置：517处能找到当前文件、文件夹或明确的文件名前缀；剩下1处是历史记录中已删除的旧`SeeRain450.pro.user`位置，保留删除事实。另核对本次JSON里能明确识别的211处临时文件指向，当前都存在，没有补造临时文件。

本次120份文本的最终文件指纹一致，94份JSON可以解析，143处上级清单引用与修改后文件的实际指纹一致。为保留旧文件的Windows换行，差异检查使用`cr-at-eol`选项，通过且退出码为0。这些都是文档和清单核对，没有重新编译、运行产品或操作RK，也不增加任何旧业务测试的通过范围。

| 文件 | 用途 |
| --- | --- |
| [verification.json](verification.json) | 本次最终核对范围和结果 |
| [changes.json](changes.json) | 120份文本的修改前指纹、最终指纹与上级校验更新位置 |
| [path_changes.json](path_changes.json) | 第一步只替换路径时的58份清单及中间指纹；最终指纹以changes.json为准 |
| [path_target_checks.json](path_target_checks.json) | Windows、WSL及相对位置统一解释后的存在性核对 |
| [historical_missing_targets.json](historical_missing_targets.json) | 旧临时文件指向核对；本轮缺失数为0 |
| [preserved_other_source_hashes.json](preserved_other_source_hashes.json) | 与修改清单碰巧有相同旧指纹、但指向其他冻结源码的1198处记录，全部保留 |
| [SHA256SUMS](SHA256SUMS) | 本证据目录自身的文件指纹，便于以后核对 |
