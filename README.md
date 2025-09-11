# FBTerm True-color

This is an enhanced version of the **`fbterm`** project, adding support for **xterm-style 256 colors** and **24-bit true color display**.

![256 Color display](doc/1.png)

![True-color display](doc/2.png)

## Project Overview

This project is based on the classic framebuffer terminal **FbTerm v1.7**, originally developed by **dragchan**.

The original `fbterm` is a highly efficient and lightweight Linux framebuffer terminal. However, it has several outdated bugs and lacks modern features such as extended color support and Unicode rendering (especially Emoji).

The goal of this project is to fix and enhance the source code so that `fbterm` can remain a **lightweight, efficient, and colorful terminal solution** for embedded Linux devices (such as Luckfox Pico) or systems without a desktop environment—even in the 21st century.

## Improvements

Compared to the original `fbterm-1.7`, this version provides:

* **Full 256-Color Support**

  * Fixed a bug where bright background colors (ANSI codes 100–107) were not rendered.
  * Corrected parsing of xterm 256-color escape sequences (`\x1b[38;5;...m`, `\x1b[48;5;...m`) so they can be properly recognized and displayed.

* **24-bit True Color Support**

  * Implemented parsing for true color escape sequences `\x1b[38;2;R;G;Bm` (foreground) and `\x1b[48;2;R;G;Bm` (background).
  * Added a robust “on-the-fly palette reprogramming” mechanism to efficiently support true color without breaking the original rendering architecture.

* **Rendering Logic Bug Fixes**

  * Fixed a critical flaw in `VTerm::expose`: the `CharAttr::operator!=` comparison failed to detect true-color attributes, causing adjacent true-color blocks to be incorrectly merged (“color stuck” bug).
  * Fixed incomplete state resets in functions like `VTerm::erase_line` that could cause unintended “color bleeding.”

## Compilation

It is recommended to cross-compile this project in a Linux environment for use on embedded targets.

#### Dependencies

Before compiling, ensure the following libraries are cross-compiled and installed into your target platform’s **sysroot** (or **staging**) directory:

* `zlib`
* `libpng` (optional, but recommended)
* `freetype`
* `libiconv`
* `expat`
* `fontconfig`

#### Build Command

Once dependencies are installed and your cross-compiler toolchain is set up, simply run:

```bash
make
```

## 🚀 Usage & Configuration

#### 1. Font Configuration (Essential)

This terminal uses **fontconfig** for font management, supporting intelligent fallback for CJK characters and Emoji.

* **Step 1:** Copy your required font files (`.ttf`, `.otf`) into `/usr/share/fonts/` on the target device. At minimum, include a high-quality monospaced font and a CJK font.
* **Step 2:** Create `~/.config/fontconfig/fonts.conf` with the following example config:

```xml
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <match target="pattern">
    <test qual="any" name="family">
      <string>monospace</string>
    </test>
    <edit name="family" mode="prepend" binding="strong">
      <string>DejaVu Sans Mono</string>
      <string>Noto Sans CJK SC</string>
    </edit>
  </match>
</fontconfig>
```

This configuration prefers **DejaVu Sans Mono** for English text, and falls back to **Noto Sans CJK SC** for Chinese characters.

* **Step 3:** Run `fc-cache -fv` on the target device to refresh the font cache.

#### 2. FbTerm Configuration

Create `~/.fbtermrc` in your home directory:

```
# Request a generic "monospace" font; actual font selection is handled by fontconfig
font-family=monospace
font-size=16
```

#### 3. Auto Start on Boot

On embedded systems using SysVinit or BusyBox init, you can make `fbterm` start automatically by editing `/etc/inittab`:

```
# Start system TTY port
tty1::respawn:/sbin/getty ...
```

Modify an init script such as `/etc/init.d/S99fbterm`:

```bash
# Release the framebuffer from the default kernel console
if [ -e /sys/class/vtconsole/vtcon1/bind ]; then
    echo 0 > /sys/class/vtconsole/vtcon1/bind
fi

# Launch fbterm in a new session, detached from init,
# with its I/O explicitly redirected to the physical console.
setsid sh -c 'exec fbterm </dev/tty1 >/dev/tty1 2>&1' &
```

To run startup tasks inside `fbterm`, add commands to `/etc/profile.d/<your_profile>`:

```bash
echo "Hello, world!" > /dev/tty1
```

This will display the message directly in the `fbterm` terminal at startup.

## License

This project inherits from the original **FbTerm** project and is licensed under **GNU General Public License v2.0 or later (GPLv2+)**. See the `COPYING` file for details.

---

# FBTerm True-color
这是一个基于`fbterm`项目的加强版，在原有项目的基础上，增加了`xterm`格式的256色和24位真彩色的显示支持。

![256 Color display](doc/1.png)

![True-color display](doc/2.png)

## 项目简介
本项目基于 **dragchan** 开发的经典帧缓冲终端 **FbTerm v1.7** 版本。

原版的 `fbterm` 是一款极其优秀的、轻量级的Linux帧缓冲终端，但在现代化的色彩和Unicode字符（特别是Emoji）支持上存在一些年代久远的Bug和功能缺失。

本项目的目标，是通过深入源代码进行修复和增强，使其在21世纪的今天，依然能成为嵌入式Linux设备（如 Luckfox Pico）或其他无桌面环境中一个**轻量、高效、且色彩绚丽**的终端解决方案。

## 功能改进
相对于原版 `fbterm-1.7`，本版本主要有以下改进：

* **完整的256色支持 (Full 256-Color Support)**
    * 修复了原版代码中高亮背景色（ANSI codes 100-107）无法渲染的Bug。
    * 修正了对标准 xterm 256色序列 (`\x1b[38;5;...m` 和 `\x1b[48;5;...m`) 的解析逻辑，使其能够被正确识别和渲染。

* **24位真彩色支持 (24-bit True Color Support)**
    * 全新实现了对 `\x1b[38;2;R;G;Bm` (前景) 和 `\x1b[48;2;R;G;Bm` (背景) 真彩色序列的解析。
    * 通过一套稳健的、无状态的“即时调色板重编程”方案，在不破坏原渲染架构的基础上，高效地实现了真彩色显示。

* **渲染逻辑Bug修复 (Rendering Logic Bug Fixes)**
    * 修正了 `VTerm::expose` 渲染优化函数中的核心缺陷——其 `CharAttr::operator!=` 比较运算符无法识别真彩色属性，导致连续的真彩色块无法被正确分段渲染（“颜色卡住”Bug）。
    * 修复了 `VTerm::erase_line` 等清除函数可能因状态未被完全重置而导致的“颜色蔓延”Bug。
  

## 如何编译

本项目推荐在Linux环境下进行交叉编译，以便在嵌入式目标设备上运行。

#### 依赖库

在编译本项目前，你需要预先为你的目标平台交叉编译好以下依赖库：
* `zlib`
* `libpng` (可选，但推荐)
* `freetype`
* `libiconv`
* `expat`
* `fontconfig`

#### 编译命令

在所有依赖都已编译并安装到你的`sysroot`（或`staging`）目录后，配置好指向你的交叉编译工具链的环境变量，然后直接运行：
```bash
make
```
## 🚀 如何使用与配置

#### 1. 字体配置 (核心)

本终端使用 `fontconfig` 进行字体管理，可以实现中英文、Emoji的智能后备显示。

* **第一步：** 将你需要的字体文件（`.ttf`, `.otf`）拷贝到目标设备的 `/usr/share/fonts/` 目录下。推荐至少包含一个高质量的等宽英文字体和一个包含中文的CJK字体。
* **第二步：** 在你的用户主目录下，创建 `~/.config/fontconfig/fonts.conf` 文件。这是一个示例配置，它会优先使用 `DejaVu Sans Mono` 显示英文，当遇到中文字符时，会自动后备到 `Noto Sans CJK SC`：

```xml
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <match target="pattern">
    <test qual="any" name="family">
      <string>monospace</string>
    </test>
    <edit name="family" mode="prepend" binding="strong">
      <string>DejaVu Sans Mono</string>
      <string>Noto Sans CJK SC</string>
    </edit>
  </match>
</fontconfig>
```

* **第三步：** 在目标设备上运行 `fc-cache -fv` 来刷新字体缓存。

#### 2. FbTerm 配置

在你的用户主目录下，创建 `~/.fbtermrc` 文件：
```
# 请求一个通用的“等宽字体”，具体的实现交给上面的fontconfig
font-family=monospace
font-size=16
```

#### 3. 开机自启

在基于SysVinit或BusyBox init的嵌入式系统上，可以通过修改 `/etc/inittab` 文件来实现开机自动运行。将`tty1`的默认`getty`替换为你的程序：
```
# Start system TTY port
tty1::respawn:/sbin/getty ...

```
修改`/etc/init.d/`下的启动脚本如`S99fbterm`：
```bash
# Release the framebuffer from the default kernel console
if [ -e /sys/class/vtconsole/vtcon1/bind ]; then
    echo 0 > /sys/class/vtconsole/vtcon1/bind
fi

# Launch fbterm in a new session, detached from init,
# with its I/O explicitly redirected to the physical console.
setsid sh -c 'exec fbterm </dev/tty1 >/dev/tty1 2>&1' &

```
即可在开机时让`fbterm`自动接管系统的tty终端。如果需要进行在`fbterm`上的启动任务，可以在`/etc/profile.d/<your_profile>`中：
```bash
echo "Hello, world!" > /dev/tty1
```
即可在`fbterm`的终端屏幕上实现任务。

## License

本项目继承自原始的FbTerm项目，同样使用 **GNU General Public License v2.0 or later (GPLv2+)** 许可证。详情请见 `COPYING` 文件

---

Original `README` document:

```
FbTerm - a fast FrameBuffer based TERMinal emulator for linux


DESCRIPTION

  FbTerm is a fast terminal emulator for linux with frame buffer device or VESA video card. Features include:
  
  * mostly as fast as terminal of linux kernel while accelerated scrolling is enabled
  * select font with fontconfig and draw text with freetype2, same as Qt/Gtk+ based GUI apps
  * dynamicly create/destroy up to 10 windows initially running default shell
  * record scrollback history for every window
  * auto-detect current locale and convert text encoding, support double width scripts like Chinese, Japanese etc
  * switch between configurable additional text encodings with hot keys on the fly
  * copy/past selected text between windows with mouse when gpm server is running
  * change the orientation of screen display, a.k.a. screen rotation
  * lightweight input method framework with client-server architecture
  * background image for eye candy


  read man page doc/fbterm.1 for usage help.


INPUT METHOD

  Instead of adding input method directly in FbTerm, a client-server based input method framework is designed to do 
this work. FbTerm acts as a client, standalone IM program as a server, and they run in seperated processes.

  As a normal IM user, please visit FbTerm's homepage (http://code.google.com/p/fbterm/) to check current available IM 
programs. If you want to develope a new IM program for FbTerm, there is a IM example in im/ directory, which help you to 
understand IM architecture and provide some base sources to simplify the development.

```
