// stdafx.h : 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 特定于项目的包含文件
//

#pragma once

//解决 Please define _WIN32_WINNT or _WIN32_WINDOWS appropriately. For example:
// error: main.cpp
// Please define _WIN32_WINNT or _WIN32_WINDOWS appropriately. For example:
// - add -D_WIN32_WINNT=0x0601 to the compiler command line; or
// - add _WIN32_WINNT=0x0601 to your project's Preprocessor Definitions.
// Assuming _WIN32_WINNT=0x0601 (i.e. Windows 7 target).

#define WIN32_LEAN_AND_MEAN //  从 Windows 头文件中排除极少使用的信息

#include <windows.h>