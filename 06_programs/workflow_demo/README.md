# 最小构建部署示例

程序读取系统信息并计算两个整数的和，用于确认电脑编译、上传和RK执行这条链路。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [main.c](main.c) | 解析参数、打印主机/架构/工作目录，输出两个整数的和；非法参数返回错误。 |
| [build.sh](build.sh) | 使用aarch64-linux-gnu-gcc生成ARM64程序，参数为绝对输出文件路径。 |

## 使用

在WSL的`06_programs/`目录中执行：

```bash
bash rk.sh workflow_demo build
```

该命令只编译。完整上传与运行方式、历史`12 + 30 = 42`输出和故障验证见[工作流记录](../../02_records/20261021_ai_workflow.md)。本次只补README，没有重新部署或运行。
