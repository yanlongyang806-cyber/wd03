# libs 目录缺失

## 问题

构建失败是因为 `libs` 目录缺失。项目需要以下库项目：

- `libs/AILib/AILib.vcxproj`
- `libs/ContentLib/ContentLib.vcxproj`
- `libs/HttpLib/HttpLib.vcxproj`
- `libs/PatchClientLib/PatchClientLib.vcxproj`
- `libs/ServerLib/ServerLib.vcxproj`
- `libs/StructParserStub/StructParserStub.vcxproj`
- `libs/UtilitiesLib/UtilitiesLib.vcxproj`
- `libs/WorldLib/WorldLib.vcxproj`

## 解决方案

### 方法 1: 从原始项目位置复制

如果 `libs` 目录在其他位置（例如 `I:\wd1\libs` 或 `I:\wd1\Night\libs`），请复制到仓库：

```powershell
cd I:\wd1\wd02

# 如果 libs 在父目录
Copy-Item -Path "..\libs" -Destination "libs" -Recurse

# 或者如果在其他位置
Copy-Item -Path "I:\wd1\libs" -Destination "libs" -Recurse

# 然后提交
git add libs
git commit -m "Add libs directory"
git push
```

### 方法 2: 从其他仓库获取

如果 `libs` 是独立的 Git 仓库，可以作为子模块添加：

```bash
git submodule add <libs-repository-url> libs
git commit -m "Add libs as submodule"
git push
```

### 方法 3: 手动创建最小结构

如果无法获取完整的 `libs`，至少需要创建项目文件结构：

```powershell
# 创建目录结构
New-Item -ItemType Directory -Path "libs\AILib" -Force
New-Item -ItemType Directory -Path "libs\ContentLib" -Force
# ... 其他库目录

# 创建最小 .vcxproj 文件（至少包含项目定义）
# 或者从其他项目复制并修改
```

## 验证

添加 `libs` 后，运行：

```powershell
.\quick_diagnosis.ps1
```

应该显示所有项目引用都已找到。

## 当前状态

- ✅ CrossRoads - 已在仓库内
- ✅ Core - 已在仓库内  
- ❌ libs - **缺失**（需要添加）

添加 `libs` 后，构建应该能够成功。

