# 如何添加依赖到仓库

## ⚠️ 重要：必须在正确的目录下执行

**必须在 `wd02` 目录下执行 Git 命令，不是在 `wd1` 目录！**

## 正确的操作步骤

### 1. 切换到 wd02 目录

```bash
cd /i/wd1/wd02
# 或者
cd I:\wd1\wd02
```

### 2. 验证是 Git 仓库

```bash
git status
```

如果显示 "not a git repository"，说明在错误的目录。

### 3. 安装 Git LFS（如果还没安装）

```bash
git lfs install
```

### 4. 配置跟踪大文件

```bash
git lfs track "*.dll"
git lfs track "*.lib"
git lfs track "*.pdb"
git lfs track "*.exe"
git lfs track "*.obj"
```

### 5. 添加 .gitattributes

```bash
git add .gitattributes
```

### 6. 添加依赖

```bash
# 添加 CrossRoads
git add ../Cryptic/CrossRoads

# 添加 Core
git add ../Cryptic/Core

# 添加 libs（如果存在）
git add ../libs
```

### 7. 提交

```bash
git commit -m "Add dependencies with Git LFS"
```

### 8. 推送

```bash
git push
```

## 完整命令序列（复制粘贴）

```bash
# 切换到正确的目录
cd /i/wd1/wd02

# 验证是 Git 仓库
git status

# 安装 Git LFS
git lfs install

# 配置跟踪
git lfs track "*.dll"
git lfs track "*.lib"
git lfs track "*.pdb"
git lfs track "*.exe"

# 添加配置
git add .gitattributes

# 添加依赖
git add ../Cryptic/CrossRoads
git add ../Cryptic/Core
git add ../libs

# 提交
git commit -m "Add dependencies with Git LFS"

# 推送
git push
```

## 常见错误

### 错误 1: "not a git repository"

**原因**：在错误的目录下执行命令

**解决**：确保在 `wd02` 目录下

```bash
cd /i/wd1/wd02
git status  # 验证
```

### 错误 2: 文件太大

**原因**：单个文件超过 100MB

**解决**：使用 Git LFS（如上）

### 错误 3: 找不到依赖目录

**原因**：依赖不在预期位置

**解决**：检查依赖位置

```bash
ls -la /i/wd1/Cryptic/CrossRoads
ls -la /i/wd1/Cryptic/Core
ls -la /i/wd1/libs
```

## 验证

添加依赖后：

1. **检查 Git 状态**：
   ```bash
   git status
   ```

2. **查看文件大小**：
   ```bash
   git lfs ls-files
   ```

3. **触发构建**：
   - 访问：https://github.com/yanlongyang806-cyber/wd02/actions
   - 点击 "Run workflow"

## 注意事项

- ✅ 必须在 `wd02` 目录下执行
- ✅ 使用 Git LFS 处理大文件
- ✅ 检查依赖是否存在
- ❌ 不要在 `wd1` 目录下执行
- ❌ 不要忘记添加 .gitattributes

