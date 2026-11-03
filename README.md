# RsShell

Windows Shell 集成层（VC++ COM DLL），供 Ruslan 等 Rs 系应用接入资源管理器。

当前产物：`Explorer/RsShell.Explorer.dll` —— 文件夹右键"用 Motic DSAssistant Evo 打开"
（IExplorerCommand，经 AUMID 激活主应用并传所选路径）。

```powershell
cmake -S Explorer -B build
cmake --build build --config Release
```

注册声明与打包在消费方（Ruslan `MSIX/`），见 [AGENTS.md](AGENTS.md)。
