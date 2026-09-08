# 参与 ubs-mem 贡献

感谢您参与 ubs-mem 项目贡献。

## 开发环境

推荐使用 openEuler 24.03 LTS SP3 或更高版本。请安装 [README.md](README.md) 中列出的构建依赖，也可以使用
`.devcontainer/` 下的配置启动开发容器。

克隆仓库后初始化测试依赖：

```shell
git submodule update --init --recursive
```

安装 `pre-commit` 并注册 Git hook：

```shell
dnf install -y python3-pip
pip3 install "pre-commit>=4.0.0,<5"
pre-commit --version
pre-commit install
```

项目要求 `pre-commit >= 4.0.0, < 5`。首次运行时会联网下载 `.pre-commit-config.yaml` 中配置的 hook，
请确保能够访问其中的 GitCode 仓库。如果始终手动执行 `pre-commit run --all-files`，可以不安装 Git hook。

## 构建与测试

提交前至少执行一次项目构建：

```shell
sh build.sh -t release
```

修改源码或测试时应运行单元测试：

```shell
cd test
sh run_dt.sh
```

覆盖率统计为可选功能，需要预先安装 `lcov` 和 `genhtml`：

```shell
cd test
sh run_dt.sh --coverage
```

提交前运行适用的 pre-commit 检查：

```shell
pre-commit run --all-files
pre-commit run --hook-stage manual run-build
pre-commit run --hook-stage manual run-build-test
```

## 变更要求

- 保持变更范围聚焦，行为变更应补充测试。
- 修改用户文档时同步更新 `README.md` 和 `README_EN.md`。
- 不要提交构建产物、覆盖率数据、私钥、凭据或环境相关文件。
- 遵循项目现有的木兰宽松许可证 v2 文件头和格式配置。

## 提交变更

影响行为的变更应创建或关联 Issue。PR 中应说明问题、解决方案、兼容性影响和验证结果，并确认提交内容符合
项目许可证与贡献要求。
