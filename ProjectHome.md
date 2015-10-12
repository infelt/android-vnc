Framebuffer VNC server specifically for the emerging Android platform.  Forked from an old version of fbvncserver originally released for the Zaurus and iPAQ devices.

This project is largely a hack to facilitate testing of the Android user experience on real handsets with full screen VNC clients running.  The current test framework is ideally suited for Windows Mobile based handsets running [.NET VNC Viewer](http://dotnetvnc.sourceforge.net).

The subversion repository contains instructions for advanced users to build the package from source, but I admit it is a pain.  You will need to custom build an Android kernel, the libvncserver library, etc.  For your convenience, I have supplied a statically linked fbvncserver binary to install onto the emulator as well as the kernel you will need to boot.  For the brave, check out the subversion sources and read the README file for complete instructions.

### Quick Start ###

Download the kernel image and fbvncserver binary and invoke the following:

```
emulator -kernel zImage -noskin
```

Wait some time for the emulator to start and then install the binary:

```
adb push fbvncserver /data
adb shell /data/fbvncserver
```

Then redirect the port so the local machine can access the server:

```
telnet localhost 5554
redir add tcp:5900:5901
```

And finally:

```
xvncviewer -noauto localhost
```

Further tunneling is required to connect from a Smartphone over the Internet or LAN, but it is pretty trivial.  The redirection added through Android's telnet interface only listens on the local interface.


### Notes about .NET VNC Viewer ###

I used the [.NET VNC Viewer](http://dotnetvnc.sourceforge.net) in most of my tests from my Windows Mobile smartphone.  It seems to work OK, except that it assumes you are connecting to a normal system with mouse and optimizes for low-quality.  I suggest adjusting the display defaults to "Full Screen" and "Force 16-bit" (or "Server Decides", should be all the same).  Read the documentation for this project to figure out how to use it effectively.

I do recommend adjusting the "Development Settings" from within Android to disable animations.  This seems to improve performance quite a bit.

In order to make full use of the keyboard, you must press "`*`" on your Smartphone and then the directional pad and other keys will work as expected.  Also, soft keys 1 and 2 are mapped to Android's menu and back buttons, respectively (but only when you use "`*`" first!).  See the .NET VNC Viewer documentation for more information.