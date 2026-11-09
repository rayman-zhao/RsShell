#pragma once

// dllmain.cpp 定义的模块锁（DllCanUnloadNow 依据），各 COM 类在引用计数跨 0/1 时调用
void ModuleLockAdd() noexcept;
void ModuleLockRelease() noexcept;
