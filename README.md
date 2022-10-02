FtpdminMod
==========

A **modified** version of [Ftpdmin](https://www.sentex.ca/~mwandel/ftpdmin/) (0.96): a minimal and portable Windows FTP server, created by Matthias Wandel.

It does not require an "install". It is intended to be run temporarily, on an as-needed basis to do file transfers between Windows computers without going thru the trouble of trying to configure windows file sharing or an FTP server.

It is not intended as an internet FTP server, or even intended to be left running. Rather, it is started from the command line so that another computer can connect to it via FTP, typically over a LAN. When the file transfers are done, stop it. No "installation" is necessary to use it.

It has no notion of security or even passwords. Rather, once it is running, everyone can connect to it. The only resemblance of security is the -g option (get only, disallows put), and the ability to specify a directory that is considered the root directory (by default it is the current working directory).

### My modifications ###
* The root directory is by default the current working directory;
* Some fixes to be able to compile with GCC in Windows, and run properly (see "\_compile.bat" for compiler details);
* Executable size is 31 kB.

### Original source ###
See [original Ftpdmin site](https://www.sentex.ca/~mwandel/ftpdmin/) or file "\_orig_ftpdmin-src_orig.zip"
