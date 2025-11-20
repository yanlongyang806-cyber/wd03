# Git Submodules 使用指南

## 🔍 问题诊断

如果你的构建失败并显示 "Missing dependencies: CrossRoads, Core, libs"，很可能是因为 **Git Submodules 未正确初始化**。

## ✅ 正确的 Git 克隆流程

### 方法 A：克隆时同时初始化子模块（推荐）

```bash
git clone --recurse-submodules https://github.com/yanlongyang806-cyber/wd02.git
cd wd02
```

### 方法 B：如果已克隆，手动初始化子模块

```bash
# 如果已经克隆了仓库
cd wd02

# 初始化并更新子模块
git submodule init
git submodule update --recursive

# 或者一条命令完成
git submodule update --init --recursive
```

## 🔍 验证子模块状态

### 检查子模块状态

```bash
git submodule status
```

**输出解读**：
- ` abc123... CrossRoads (v1.0.0)` - ✅ 已初始化并检出
- `-abc123... CrossRoads` - ❌ 未初始化（需要运行 `git submodule update --init`）
- `+abc123... CrossRoads` - ⚠️ 有未提交的更改

### 检查关键目录

```bash
# 检查依赖目录是否存在
ls -la CrossRoads/
ls -la Core/
ls -la libs/
```

**应该看到**：
```
CrossRoads/
├── GameServerLib/
│   └── GameServerLib.vcxproj
├── Common/
│   └── pvp_common.c
└── ...

Core/
├── Core.vcxproj
└── ...

libs/
├── AILib/
│   └── AILib.vcxproj
├── ContentLib/
└── ...
```

如果这些目录是**空的**或**不存在** → 子模块未正确拉取。

## 📋 检查 .gitmodules 文件

项目根目录应该有 `.gitmodules` 文件，内容类似：

```ini
[submodule "CrossRoads"]
    path = CrossRoads
    url = https://github.com/.../CrossRoads.git

[submodule "Core"]
    path = Core
    url = https://github.com/.../Core.git

[submodule "libs"]
    path = libs
    url = https://github.com/.../libs.git
```

**如果没有这个文件**：
- 说明项目可能不是用标准子模块管理依赖
- 需要手动放置依赖（参考 `ADD_DEPENDENCIES.md`）

## 🔧 GitHub Actions 配置

### ✅ 正确的配置

GitHub Actions 工作流已配置为自动拉取子模块：

```yaml
- name: Checkout code
  uses: actions/checkout@v4
  with:
    fetch-depth: 0
    submodules: recursive  # 👈 关键！
    token: ${{ secrets.GITHUB_TOKEN }}
```

### ❌ 错误配置

```yaml
# ❌ 缺少 submodules 参数
- name: Checkout code
  uses: actions/checkout@v4
  with:
    fetch-depth: 0
```

这会导致子模块不被拉取，出现 "Missing dependencies" 错误。

## 🛠️ 故障排除

### 问题 1: 子模块 URL 需要认证

如果子模块是私有仓库，需要配置认证：

```bash
# 使用 SSH
git config submodule.CrossRoads.url git@github.com:user/CrossRoads.git

# 或使用 Personal Access Token
git config submodule.CrossRoads.url https://token@github.com/user/CrossRoads.git
```

### 问题 2: 子模块更新失败

```bash
# 强制更新
git submodule update --init --recursive --force

# 或删除后重新初始化
rm -rf CrossRoads
git submodule update --init --recursive
```

### 问题 3: 子模块指向错误的提交

```bash
# 更新到最新版本
git submodule update --remote --recursive
```

## 📊 本地测试构建

在正确初始化子模块后，测试构建：

```bash
# 在 Visual Studio 开发者命令行中
cd wd02
msbuild NNOGameServer.vcxproj /p:Configuration=Debug /p:Platform=Win32
```

观察具体哪一步失败。

## ✅ 检查清单

| 步骤 | 命令 | 预期结果 |
|------|------|----------|
| 1. 检查 .gitmodules | `cat .gitmodules` | 应该显示子模块配置 |
| 2. 检查子模块状态 | `git submodule status` | 所有子模块应该已初始化（不以 `-` 开头） |
| 3. 检查目录 | `ls CrossRoads/ Core/ libs/` | 目录应该存在且包含文件 |
| 4. 验证项目引用 | `ls CrossRoads/GameServerLib/GameServerLib.vcxproj` | 文件应该存在 |

## 🎯 快速修复命令

如果遇到 "Missing dependencies" 错误，运行：

```bash
cd /i/wd1/wd02

# 1. 检查子模块状态
git submodule status

# 2. 初始化子模块
git submodule update --init --recursive

# 3. 验证
ls CrossRoads/ Core/ libs/

# 4. 重新构建
msbuild NNOGameServer.vcxproj /p:Configuration=Debug /p:Platform=Win32
```

## 📝 注意事项

1. **嵌套子模块**：如果子模块本身包含子模块，必须使用 `--recursive` 参数

2. **子模块更新**：当主项目更新后，子模块不会自动更新，需要手动运行：
   ```bash
   git submodule update --remote --recursive
   ```

3. **子模块提交**：如果修改了子模块内容，需要在子模块目录中提交：
   ```bash
   cd CrossRoads
   git add .
   git commit -m "Update"
   cd ..
   git add CrossRoads
   git commit -m "Update submodule"
   ```

## 🔗 相关文档

- `ADD_DEPENDENCIES.md` - 手动添加依赖的指南
- `DIAGNOSIS_AND_FIX.md` - 完整的诊断与修复指南

