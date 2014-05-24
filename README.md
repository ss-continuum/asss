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
1. Create src/system.mk using one of the example system.mk.*.dist files.
2. cd src && make 
3. Copy the dist folder your desired zone folder (e.g. ~/zone)
4. Copy or symlink bin into your zone folder (e.g. ~/zone/bin
5. Download the correct security.so file from https://bitbucket.org/jowie/asss/downloads into bin (e.g. ~/zone/bin.security.so)
6. (Optional) run "continuum.exe Z" and copy "scrty" and "scrty1" into the zone folder (e.g. ~/zone)
7. You can now run the zone by running "bin/asss" in your zone folder (e.g. cd ~/zone && bin/asss)


## Vagrant
You can automatically set up a virtual machine that runs your zone using vagrant:

1. Install VirtualBox https://www.virtualbox.org/ (or another provider that vagrant supports)
2. Install Vagrant: http://www.vagrantup.com/
3. Run "vagrant up" in this directory to set up the VM
4. Run "vagrant ssh" to login
5. "cd /zone" and "bin/asss" to run the server! 

Anytime you change the source and you would like to rebuild, run "vagrant provision"

## Documentation
There is more documention in the doc/ directory, however some sections are outdated.