# AGENTS.md — RsShell

Windows Shell 集成层，VC++（MSVC）COM DLL 宿主。服务于 Ruslan（及后续 Rs 系应用），
当前唯一产物：`Explorer/RsShell.Explorer.dll`——资源管理器右键菜单 verb（IExplorerCommand）。

## 定位与边界

- 本仓是**薄翻译层**：Shell 表面（Explorer 菜单/缩略图/预览）与主应用之间的翻译。
  自身无业务逻辑；语义路由全部在主应用侧完成（Ruslan 的激活参数解析）。
- 注册不属于本仓：COM 类与 verb 声明在**消费方**的 AppxManifest（Ruslan `MSIX/AppxManifest.xml`），
  本仓只出 DLL。CLSID 变更须同步消费方清单。
- **语言边界**：本仓不链任何 Swift 产物。将来缩略图 handler 的解码在 RsSlide（Swift），
  DLL 内不可直接调用——届时走子进程出图/主应用预生成缓存，策略另行决策。

## 构建与约定

- CMake + MSVC x64：`cmake -S Explorer -B build && cmake --build build --config Release`。
  产物路径是 Ruslan `build-msix.ps1` 的对接契约（`build/Release/RsShell.Explorer.dll`）。
- 菜单文案/图标走 DLL 资源（Resource.rc 双语 STRINGTABLE + Explorer.ico）；
  应用显示名变更须同步 `Resource.rc` 与消费方清单。DLL 版本号唯一来源是
  `Explorer/Version.h`（VERSIONINFO 资源），独立于消费方应用版本，发布变更时递增。
- 开发坑：Explorer 对 title/icon/state 缓存凶，迭代改资源后需重启 explorer 验证；
  此类扩展只能装上 MSIX 才生效，无松散测试捷径。
- AUMID 的 AppId 段（`!Ruslan`）硬编码于 OpenInAppCommand.cpp，与 Ruslan 清单
  `<Application Id>` 对应，改名须两处同步。
