aqvision
========

Utilities to use an aquacomputer VISION from linux. Builds 2 programs:

* visioncli - simple command line program to query VISION
* pwmd - program to manage fan curves using VISION temperature as input. The fan curve is hard-coded for now.

Requirements
------------

* [boost C++ libraries](http://www.boost.org/)
* systemd (for pwmd)

Build
-----
```bash
$ autoreconf -i
$ ./configure --with-systemdsystemunitdir=/lib/systemd/system 
[...]
$ make && sudo make install
```
