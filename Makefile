.RECIPEPREFIX = >
cc = /c/mingw/bin/g++
res = /c/mingw/bin/windres --output-format=coff --target=pe-i386
standard = -m32 -std=c++11 -ggdb

all : panui

panui : phoenix-resource.o phoenix.o panui.o
> echo $(cc) $(standard) -fwhole-program -L/c/mingw/sdl32/lib -Wall -Wextra -pedantic -mconsole -o panui panui.o phoenix.o phoenix-resource.o -lSDL2 -lm -ldinput8 -ldxguid -ldxerr8 -lwinmm -limm32 -loleaut32 -lversion -luuid -static-libgcc -lkernel32 -luser32 -lgdi32 -ladvapi32 -lcomctl32 -lcomdlg32 -luxtheme -lmsimg32 -lshell32 -lole32 -lshlwapi -static | bash

phoenix-resource.o : include/phoenix/windows/phoenix.rc
> $(res) include/phoenix/windows/phoenix.rc phoenix-resource.o
phoenix.o : include/phoenix/phoenix.cpp
> echo $(cc) $(standard) -Iinclude -g -c include/phoenix/phoenix.cpp -DPHOENIX_WINDOWS | bash
panui.o : panui.cpp
> echo $(cc) $(standard) -I/c/mingw/include -Iinclude -g -c panui.cpp | bash
clean:
> rm -rf *.o
> $(MAKE) all
