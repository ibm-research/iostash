** To compile and run this unit test, you should install CUnit.

# on ubuntu, install package: libcunit1-dev

+ Download and install from http://sourceforge.net/projects/cunit/

    Download 'CUnit-2.1-2-src.tar.bz2' from the web site.

    # tar xvfj CUnit-2.1-2-src.tar.bz2
    # cd CUnit-2.1-2
    # ./configure
    # make
    # make install <--- with root permission

+ edit .vimrc to add following:

    export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

+ Or, on ubuntu, install package: libcunit1-dev



