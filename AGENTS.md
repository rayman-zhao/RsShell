# AGENTS.md — RsShell

Windows Shell 集成层，VC++（MSVC）COM DLL 宿主。服务于 Ruslan（及后续 Rs 系应用），
当前唯一产物：`Explorer/RsShell.Explorer.dll`——资源管理器右键菜单 verb（IExplorerCommand）。

## 定位与边界

- 本仓是**薄翻译层**：Shell 表面（Explorer 菜单/缩略图/预览）与主应用之间的翻译。
  自身无业务逻辑；语义路由全部在主应用侧完成（Ruslan 的激活参数解析）。
- 注册不属于本仓：COM 类与 verb 声明在**消费方**的 AppxManifest（Ruslan `MSIX/AppxManifest.xml`），
  本仓只出 DLL。CLSID 固定通用——MSIX 的 COM 注册按包隔离，多包并存不冲突，消费方照抄即可。
- **语言边界**：与 Swift 侧经 **C ABI** 互操作（不用完整 C++ interop）。缩略图/预览的
  解码在 RsSlide（Swift）——由独立 Swift shim DLL 暴露 `@_cdecl` 出图函数，本仓 DLL
  动态加载（LoadLibrary）调用，shim 缺失时优雅降级默认图标（2026-11-03 定向；不借道
  主应用出图，Java rjslide 仅作批量预生成备选）。Swift 运行时随消费方包分发。

## 构建与约定

- CMake + MSVC x64：`cmake -S Explorer -B build && cmake --build build --config Release`。
  产物路径是 Ruslan `build-msix.ps1` 的对接契约（`build/Release/RsShell.Explorer.dll`）。
- **换应用的调整面**（全部集中在 `Explorer/Resource.rc`，除此之外零改动）：
  两张语言表的菜单文案（产品名可按语言不同）与 AppId（两表同值，= 消费方清单
  `<Application Id>`，`Invoke` 经 LoadString 运行时取用）、`IDI_APP` 的图标文件。
  **LoadString 无跨语言回退**（zh 线程命中 zh 表后同区块缺 ID 即失败），语言无关
  的字符串必须每张表都放。
- DLL 版本号唯一来源是 `Explorer/Version.h`（VERSIONINFO 资源），独立于消费方
  应用版本，发布变更时递增。
- 开发坑：Explorer 对 title/icon/state 缓存凶，迭代改资源后需重启 explorer 验证；
  此类扩展只能装上 MSIX 才生效，无松散测试捷径。
