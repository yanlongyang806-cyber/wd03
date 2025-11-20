# GameServer - PVP Game Server

这是 GameServer 项目的独立仓库，专注于游戏服务器的 PVP 功能。

## 项目结构

```
wd02/
├── NNOGameServer.vcxproj    # 项目文件
├── NNOGameServer.sln        # 解决方案文件
├── NNOLogging.c             # 日志实现
├── AutoGen/                 # 自动生成的代码
├── Common/                  # 共享代码
│   ├── NNOCommon.c
│   └── NNOCommon.h
└── PropertySheets/          # 项目属性表
    ├── GeneralSettings.props
    ├── CrypticApplication.props
    └── LinkerOptimizations.props
```

## 编译要求

### 必需组件

1. **Visual Studio 2010 或更高版本**
   - 项目使用 ToolsVersion="4.0"
   - 需要 C++ 编译工具链

2. **依赖目录**（需要添加到仓库或通过符号链接）
   - `../../CrossRoads/` - 游戏服务器库（包含 PVP 功能）
   - `../../core/` - 核心系统
   - `../../libs/` - 各种库项目
   - `../../utilities/bin/structparser.exe` - 预构建工具

## 本地编译

### 方法 1: 使用 Visual Studio

1. 打开 `NNOGameServer.sln`
2. 选择配置（Debug/Full Debug）
3. 生成解决方案

### 方法 2: 使用 MSBuild

```powershell
msbuild NNOGameServer.vcxproj /p:Configuration=Debug /p:Platform=Win32
```

## GitHub Actions 自动构建

项目配置了 GitHub Actions 工作流，会在以下情况自动构建：

- 推送到 main 或 master 分支
- 创建 Pull Request
- 手动触发（workflow_dispatch）

查看构建状态：https://github.com/yanlongyang806-cyber/wd02/actions

## PVP 功能

GameServer 包含完整的 PVP 功能实现：

- **PVP 游戏模式**：
  - Deathmatch（死亡竞赛）
  - Domination（统治模式）
  - Capture The Flag（夺旗）
  - Tower Defense（塔防）
  - Last Man Standing（最后一人）
  - Custom（自定义）

- **PVP 功能**：
  - 决斗系统（Duel）
  - 团队决斗（Team Duel）
  - PVP 感染模式
  - 匹配系统（MMR）

## 依赖说明

### 必需的依赖

- **CrossRoads**: 包含 PVP 实现的核心库
  - `Common/pvp_common.c` - PVP 基础功能
  - `Common/PvPGameCommon.c` - PVP 游戏模式
  - `GameServerLib/gslPVP.c` - PVP 服务器实现
  - `GameServerLib/gslPvPGame.c` - PVP 游戏逻辑

- **Core**: 核心系统代码

### 可选的依赖

- **libs**: 各种库项目（WorldLib, ServerLib, AILib 等）
- **PropertySheets**: 项目属性表（已包含在仓库中）
- **utilities**: 预构建工具（structparser.exe）

## 许可证

[添加许可证信息]

## 贡献

欢迎提交 Issue 和 Pull Request！

