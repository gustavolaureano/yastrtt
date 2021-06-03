YASTRTT: Yet Another ST RTT

A tool to interact with the Segger RTT library through ST-Link debugger interfaces and using the the open source version of the STMicroelectronics STLINK Tools.
(https://github.com/stlink-org/stlink)

Main targets:
 - Have a single and portable binary with all the necessary libraries statically linked, removing the requirement of installing any other software but the ST-Link driver
 - Be a simple command line tool, just execute it and it will open as a terminal for you, letting you interact with the target over RTT
 - Be self-recoverable, if the connection to the target or to the ST-Link is lost the application automatically restart the search 
  
 
The official Segger RTT software supports multiple data channels in both directions, the RTT Viewer only works with channel 0 but it is able to divide the incomming data into up to 16 different virtual terminals (the terminal is selected by a special sequence of characters sent by the target), also supporting different text colors.
This application has no intention of supporting more than one channel nor more than one "terminal", every data sent by the target over channel 0 will be directly printed to the console, and every character typed by the user will be written into the rx channel 0 on the target, if the target send special commands to change the terminal or text color these commands will be interpreted as text and printed to the console too.

This code is based on (and use parts of):
 - Segger's RTT target library (https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/)
 - the rtt_stlink project (https://github.com/trlsmax/rtt_stlink)
 - the "Build Your Own Text Editor" booklet (https://viewsourcecode.org/snaptoken/kilo/index.html)


Possible improvements:
 - Integrate libstlink and libusb as submodules and automatic compilation
 - Add command line options to allow custom CB identifiers and customizable up/down channels 
 - Move to C++?