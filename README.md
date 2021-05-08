# 滑动窗口协议 SR协议
## 功能
- 数据帧无需捎带ACK时可自动缩减帧长度
- 可变数据帧数据长度，最大256字节
- 快速NAK
- 高程序健壮性
- 详细的debug信息（使用参数d7）
## 使用
- 使用Visual Studio编译
- 使用mingw-w64 GCC编译器，执行mingw32-make
- What ever you prefer.