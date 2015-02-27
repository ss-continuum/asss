# A Small Subspace Server
A server for the multiplayer game subspace. This is a fork that is running on SSCJ Devastation. This fork fixes numerous stability issues, and should make installation easier.

## Dependencies
* Python 2
* Berkeley DB 4+ libraries
* MySQL or MariaDB client libraries
* GNU make
* GNU Debugger

Ubuntu 10.04 Lucid Lynx:
```
sudo apt-get install build-essential python2.6 python2.6-dev python2.6-dbg libdb4.8-dev mysql-client libmysqlclient-dev gdb mercurial
```

Ubuntu 12.04 Precise Pangolin:
```
sudo apt-get install build-essential python2.7 python2.7-dev python2.7-dbg libdb5.1-dev mysql-client libmysqlclient-dev gdb mercurial
```

Ubuntu 14.04 Trusty Tahr:
```
sudo apt-get install build-essential python2.7 python2.7-dev python2.7-dbg libdb5.3-dev mysql-client libmysqlclient-dev gdb mercurial
```

CentOS 6:
```
sudo yum groupinstall "Development Tools"
sudo yum install python-libs python-devel python-debuginfo db4-devel mysql-libs mysql-devel gdb mercurial
```

## Installing on GNU/Linux
Each step has an example for Ubuntu 14.04 Trusty (64 bit)

1. Install the dependencies listed in the previous section  
   `sudo apt-get install build-essential python2.7 python2.7-dev python2.7-dbg libdb5.3-dev mysql-client libmysqlclient-dev gdb mercurial`
2. Clone/download this repository  
   `hg clone https://jowie@bitbucket.org/jowie/asss ~/asss-src`  
   `cd ~/asss-src`  
   `hg update jowie`
3. Create src/system.mk using one of the example system.mk.*.dist files  
   `cp ~/asss-src/src/system.mk.trusty.dist ~/asss-src/src/system.mk`
4. Run make in the src directory  
   `cd ~/asss-src/src && make`
5. Copy the dist folder to the location where your zone should live  
   `cp -R ~/asss-src/dist ~/zone`
6. Symlink (or copy) the bin directory into your zone folder  
   `ln -s ~/asss-src/bin ~/zone/bin`
7. Download the correct enc_cont.so file from the [downloads section](downloads) into bin  
   `cd ~/zone/bin`  
   `wget --output-document=enc_cont.so https://bitbucket.org/jowie/asss/downloads/enc_cont_8d454bb0c6e6_x86_64-ubuntu-glibc-4.8.2.so`
8. Run "continuum.exe Z" on windows/wine and copy "scrty" and "scrty1" into the zone folder, overwriting the existing files
   You will have to keep these files private, so make sure you close down the file permissions  
    _The path in the example will look like: `~/zone/scrty` and `~/zone/scrty1`_  
   `chmod 0600 ~/zone/scrty*`
9. You can now run the zone by running "bin/asss" in your zone folder  
    `cd ~/zone && bin/asss`
10. Optionally, you can run asss using the `run-asss` script that automatically restarts the zone if it crashes or if a
    sysop uses `?recyclezone` (this command will not work properly without this script)  
    `cp ~/asss-src/scripts/run-asss ~/zone`  
    `nano ~/zone/run-asss` and make sure the line `ASSSHOME=$HOME/zone` is correct  
    `cd ~/zone && ./run-asss`
11. It is also possible to run asss as a service, you can find an example ubuntu init file in the `scripts` directory


## Vagrant
You can automatically set up a virtual machine that runs your zone using vagrant:

1. Install VirtualBox https://www.virtualbox.org/ (or another provider that vagrant supports)
2. Install Vagrant: http://www.vagrantup.com/
3. Run "vagrant up" in this directory to set up the VM
4. Run "vagrant ssh" to login
5. Type "runzone" to run the server!

Anytime you change the source and you would like to rebuild, run "vagrant provision" on the host (not in the VM)

## Documentation
There is more documention in the doc/ directory, however most sections are outdated.
