# BK7259 机器人方案 文档源码（bk7259/）

本目录是 BK7259 机器人方案 Sphinx 文档的源码，结构如下：

```
bk7259/
├── conf_common.py / Doxyfile / Makefile / build_doc.py 等  # Sphinx + Doxygen 公共配置
├── _static/                                                # 静态资源（CSS / JS / 字体 / 图片）
├── en/                                                     # 英文文档
│   ├── index.rst
│   ├── intro/         # 方案简介
│   ├── get-started/   # 快速入门（含 env-manual / env-docker）
│   ├── hw-reference/  # 硬件参考
│   ├── developer-guide/   # 各 components 模块开发者指南
│   ├── projects/      # 参考工程（beken_robot / secureboot_ai / baf_example）
│   └── thirdparty/    # 第三方（仅 agora）
└── zh_CN/                                                  # 中文文档（结构同 en/）
```

## 本地构建

确保已安装 `sphinx` 与依赖（``requirements.txt`` / ``setuptools.requirements.txt``）：

```bash
pip3 install -r requirements.txt
```

构建中文文档：

```bash
cd zh_CN
make html
```

构建英文文档：

```bash
cd en
make html
```

输出路径在各自语言目录下的 ``_build/html/index.html``。

## 编辑约定

- 新增 / 修改 ``.rst`` 文件后，请运行一次 ``make html`` 确认无 WARNING / ERROR。
- 结构性变更（新增目录 / 顶层 toctree）需同时更新 ``en/`` 和 ``zh_CN/`` 两侧并保持同名同层级。

