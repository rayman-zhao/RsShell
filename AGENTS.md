# AGENTS.md — RsShell

Windows Shell 集成层，VC++（MSVC）COM DLL 宿主。服务于 Ruslan（及后续 Rs 系应用），
当前产物：`Explorer/build/Release/RsShell.Explorer.dll`——资源管理器右键菜单 verb
（IExplorerCommand）与数字切片缩略图（IExtractImage）。

## 定位与边界

- 本仓是**薄翻译层**：Shell 表面（Explorer 菜单/缩略图/预览）与主应用之间的翻译。
  自身无业务逻辑；语义路由全部在主应用侧完成（Ruslan 的激活参数解析）。
- 注册不属于本仓：COM 类与 verb 声明在**消费方**的 AppxManifest（Ruslan `MSIX/AppxManifest.xml`），
  本仓只出 DLL。CLSID 固定通用——MSIX 的 COM 注册按包隔离，多包并存不冲突，消费方照抄即可。
- **语言边界**：与 Swift 侧经 **C ABI** 互操作（不用完整 C++ interop）。解码在
  RsSlide（Swift）——其动态产品 **RsSlideCABI.dll**（静态产品仍名 `RsSlide`，消费方
  零迁移）暴露 `@_cdecl` 导出，契约镜像于本仓 `Explorer/SlideShim.h`（改任一侧必须
  同步）。本仓**直接链接其 import 库**（CMake 取 `../RsSlide/.build/release/
  RsSlideCABI.lib`，缺失即构建失败并提示先构建 RsSlide），符号编译期解析；DLL 是
  **硬依赖**（静态导入表），打包进程的包图搜索在 surrogate 里解析导入链（实测可用；
  缺失则 RsShell.Explorer.dll 整体加载失败、右键菜单同失，build-msix.ps1 步骤 7.6
  按硬失败收录）：
  - `RsSlidePreviewBitmap`：宏图即**字面意义的 Windows 位图栅格**——32bpp BGRA
    （alpha 不透明）、自底向上、stride=w*4（恒对齐）、缓冲=stride*h、原生宏图分辨率。
    两条宏图路在 DLL 内收敛直写 CoTaskMem 缓冲（TIFF 像素直读转换 / 存储 JPEG 经
    RsPack `tjDecompress` TJPF_BGRA+BOTTOMUP 就地解压，alpha 保证 255，C++ 零后处
    理）；仅格式无宏图时失败；
  - `RsSlideThumbnail`：金字塔层缩略图 JPEG（需完整解析切片，~20s 慢路）；保留导出
    备将来使用，RsShell 当前不调用（同步调必超 surrogate 预算）。
  Extract 同步直调 previewBitmap→CreateDIBSection 返回（实测 <0.3s，首访即出图；
  不借道主应用出图）。Swift 运行时随消费方包分发（build-msix.ps1 步骤 7 平铺包根）。
  **注意**：依赖 RsPack 的 `TIFFReadRGBAStrip` 与 `tjDecompress`——推送顺序必须
  RsPack → RsSlide（resolve 更新）→ 消费方（三仓均已推送）。

## 缩略图架构（2026-11-09 真机验证，载重约束勿擅改）

- **必须走 IExtractImage 遗留链，不能实现 IThumbnailProvider/IInitializeWith\***：Shell 对
  out-of-proc 缩略图 handler 只用 IInitializeWithStream 初始化，而 `IStream::Stat` 只给
  裸文件名拿不到路径；GB 级切片与闭源厂商 DLL（csp/vmsdk）只能按路径打开。只暴露
  IPersistFile+IExtractImage2 时 Shell 回退 `IPersistFile::Load`（带完整路径），
  此路在打包 surrogate 里实测可用。一旦 QI 暴露 IThumbnailProvider，Shell 立即改走
  流式且不再回退。
- **同步直调即可，无需旁路进程/缓存**（2026-11-09 起）：宏图路径（像素直读/JPEG 就地
  解压）实测 15-266ms，远低于 surrogate ~20s 预算，Extract 同步返回后系统 thumbcache
  直接固化——首访即出图、无负缓存问题。历史上的 ThumbWorker 进程 + 旁路缓存是为
  金字塔路径 ~20s 超预算而设（2026-11-07），宏图路径取代金字塔成为主路后即删除。
  金字塔路径（RsSlideThumbnail）在 surrogate/worker 里异常慢（~20s，非 CPU 饱和、
  IO 计数不可见，卡点在 RsSlide fetch 内部未定位）——若将来需要启用必须恢复旁路
  进程方案或先解决该慢路。
- surrogate 内加载 RsSlideCABI.dll 实测可用（`LoadLibraryEx(DLL_LOAD_DIR|SYSTEM32)`
  + `LoadPackagedLibrary` 兜底链）。当年 "126 玄学" 的真因是 DLL 文件名拼错，加载
  失败先核对文件名。WindowsApps 内的 exe 只能被带包身份的进程（surrogate）启动，
  PowerShell 直跑会拒绝访问——测试用包根拷贝或 loose 注册。

## 构建与约定

- CMake + MSVC x64：`cmake -S Explorer -B build && cmake --build build --config Release`。
  产物路径是 Ruslan `build-msix.ps1` 的对接契约（`build/Release/RsShell.Explorer.dll`
  由步骤 7.5 收录；RsSlideCABI.dll 取自 RsSlide `.build/release/` 由步骤 7.6 收录）。
- **换应用的调整面**（全部集中在 `Explorer/Resource.rc`，除此之外零改动）：
  两张语言表的菜单文案（产品名可按语言不同）与 AppId（两表同值，= 消费方清单
  `<Application Id>`，`Invoke` 经 LoadString 运行时取用）、`IDI_APP` 的图标文件。
  **LoadString 无跨语言回退**（zh 线程命中 zh 表后同区块缺 ID 即失败），语言无关
  的字符串必须每张表都放。缩略图类无文案资源。
- DLL 版本号唯一来源是 `Explorer/Version.h`（VERSIONINFO 资源），独立于消费方
  应用版本，发布变更时递增。
- 开发坑：Explorer 对 title/icon/state 缓存凶，迭代改资源后需重启 explorer 验证；
  此类扩展只能装上 MSIX 才生效，无松散测试捷径。验证缩略图需清
  `%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db`（先停 explorer）；
  开发模式 loose 注册（`Add-AppxPackage -Register`）与签名安装均实测可用，但同版本
  重装会被阻（0x80073CFB），先卸载或递增版本。
- **缩略图逻辑日常检查用 `build/Release/RsShell.ThumbTest.exe`**（进程内测试台，不随包
  分发）：绕过 Explorer/系统缩略图缓存，按 Shell 调用顺序直接驱动 COM 类，逐步打印
  HRESULT/耗时并把位图存 PNG；需与 RsShell.Explorer.dll 同目录有 RsSlideCABI.dll
  （拷进消费方 package 目录或 `--dll` 指过去）。
