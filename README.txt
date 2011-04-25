This folder contains all Aquaria sources and necessary build scripts.
However, it does *not* contain any graphical file nor sound. If you
want to play the game, you first need to buy the original
full-featured version (http://www.bit-blot.com/aquaria/) and install
it. Once you have done that, you need to build the files in this
folder (see below for how to do that) and copy the resulting files to
the place where you installed the original full-featured version.

BUILDING
--------

Follow these steps to build Aquaria. 

1- Install required dependencies first. This includes a C++ compiler
  and a handful of libraries. Here is a list of the Debian names for
  some of these dependencies:

build-essential cmake liblua5.1-0-dev libogg-dev libvorbis-dev
libopenal-dev libsdl1.2-dev

2- Create a sub-directory 'build' and move into it

$ mkdir build
$ cd build

3- run cmake

$ cmake ..

4- If you miss some dependencies, install them and run cmake again.

5- run make

$ make

6- Copy necessary files to where you installed the original
  full-featured version of Aquaria (e.g., ~/aquaria which is the
  default)

$ cp aquaria ~/aquaria/
$ cp -r ../games_scripts/* ~/aquaria

You should *not* remove any file from the aquaria installation, just
replace some of them with the versions included in this folder.

MODS
----

If you plan to use any of the Aquaria mods, you'll also need to update
the copies in your personal data directory:

cp -a ~/aquaria/_mods ~/.Aquaria/
