#!/bin/bash
set -e # 如果任何命令失败，立即停止

echo "--- [1/6] 准备 fbterm 独立编译环境 ---"

# 1. 设置你的核心路径
export FBTERM_DIR=~/luckfox_build/fbterm-1.7
export INSTALL_DIR=/home/miku/luckfox_build/fbterm_build
export TOOLCHAIN_PATH=/home/miku/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf
export TARGET_HOST=arm-rockchip830-linux-uclibcgnueabihf

# 2. 将交叉编译工具链添加到 PATH
export PATH="$TOOLCHAIN_PATH/bin:$PATH"

# 3. 明确告诉 'configure' 脚本使用哪个工具
export CC="${TARGET_HOST}-gcc"
export CXX="${TARGET_HOST}-g++"
export AR="${TARGET_HOST}-ar"
export RANLIB="${TARGET_HOST}-ranlib"
export LD="${TARGET_HOST}-ld"

# 4. 设置编译和链接标志

# 包含路径 (-I...)
export CPPFLAGS="-I${INSTALL_DIR}/usr/include -I${INSTALL_DIR}/include -I${INSTALL_DIR}/usr/include/freetype2"

# C++ 编译器标志
export CXXFLAGS="-Wno-narrowing -fpermissive -fno-exceptions -fno-rtti -std=gnu++98"

# 库路径 (-L...)
export LDFLAGS="-L${INSTALL_DIR}/usr/lib -L${INSTALL_DIR}/lib"

# 需要链接的库 (-l...)
export LIBS="-liconv -lexpat -lz -lfontconfig -lfreetype -lutil -lm"

# ====================================================================
# ==                      !!! 关键修复 !!!                         ==
# ====================================================================
# 告诉 pkg-config 在哪里可以找到 freetype2.pc 等 .pc 文件
export PKG_CONFIG_PATH="${INSTALL_DIR}/usr/lib/pkgconfig:${INSTALL_DIR}/lib/pkgconfig"
# ====================================================================


echo "--- [2/6] 环境变量设置完毕 ---"
echo "  CPPFLAGS: $CPPFLAGS"
echo "  CXXFLAGS: $CXXFLAGS"
echo "  LDFLAGS:  $LDFLAGS"
echo "  LIBS:     $LIBS"
echo "  PKG_CONFIG_PATH: $PKG_CONFIG_PATH"

# 5. 进入 fbterm 目录
echo "--- [3/6] 进入目录: $FBTERM_DIR ---"
cd $FBTERM_DIR

# 6. 执行构建
echo "--- [4/6] 运行 'make clean' ---"
make clean

echo "--- [5/6] 运行 'autoreconf -fiv' (重新生成构建系统) ---"
# 这是必须的，因为你修改了 Makefile.am
autoreconf -fiv

echo "--- [6/6] 运行 './configure' 和 'make' ---"
./configure --prefix=/usr --host="${TARGET_HOST}"

make -j$(nproc)

echo "--- 编译完成! ---"
echo "你的 fbterm 可执行文件位于: $FBTERM_DIR/src/fbterm"
