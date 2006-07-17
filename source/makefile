# FreeDOS Editor Makefile
# By Joe Cosentino and Jeremy Davis (updated by Eric Auer 11/2003)
#
# For use with Turbo C 2.01, Turbo C++ 1.01, Turbo C/C++ 3.0
# and Borland C/C++ 3.1 - if you have another compiler, please
# send us your adjusted makefile to share it with the community.
#

all : edit.exe edit.hlp

#------------------------------------------------
# NOTE: Set DRIVE to match where you installed your compiler
#------------------------------------------------
DRIVE = c:\tc
#DRIVE = c:\tc201
#DRIVE = c:\tc101
#DRIVE = c:\tc30
#DRIVE = c:\borlandc

#-------------------------------------------------------------------
#  This macro builds the full Edit system with all options enabled.
#  Comment it out for a minimum system or selectively
#  comment out the #defines at the top of dflat.h.
#-------------------------------------------------------------------
FULL = -DBUILD_FULL_DFLAT

#-------------------------------------------------------------------
# Select a memory model here. L = LARGE recommended.
# T tiny, S small (default), M medium, C compact, L large, H huge.
# T: 1 ds/ss/cs, all near.
# S: 1 cs, 1 ds/ss, all near.
# M: 1 ds/ss, many CS, functions far.
# C: 1 ds/ss, 1 cs, heap (malloc'ed data) is far.
#   L: 1 ds/ss, many CS, functions and heap are far.
# H: 1 ss, many CS, many DS (static vars), everything far.
MODEL = l

#------------------------------------------------
# NOTE: Delete the DEBUG and LINKDEBUG macros to 
# build without debugging information in the .EXE
#------------------------------------------------
#DEBUG = -v -Od
#LINKDEBUG = /m /v
LINKDEBUG = /m

#------------------------------------------------
# NOTE: Temporary file space. Change to match
#       your computer. A RAM disk works best.
#------------------------------------------------
HEADERS=tcdef.sym

#------------------------------------------------
# Set to match your compiler
#------------------------------------------------
CC = tcc
#CC = bcc
LINKER = tlink
LIB = tlib

#------------------------------------------------
# Set any extra options here
#------------------------------------------------
# -1 use 186/286 -G for speed -K unsigned char -Z optimize register usage
# -O optimize jumps -M create link map -a word align (handle with care!)
# -f emulate FPU (default) -f- use no FPU -f87 use native FPU
# -r use reg vars -w enable warnings -N add stack checks
# -k use standard stack frame -p use pascal calls ...
# *** -w should only display "structure passed by value" errors!
# ###    Feel free to use -w-stv to mute those
# ###    and still see all other error messages.
# ### adding -K ("default char is unsigned"), as dflat has unsigned char!
CCEXTRA = -K -w -w-stv
# Calendar utility; you can disable (remove from menu) by defining NOCALENDAR
#CCEXTRA = $(CCEXTRA) -DNOCALENDAR

#------------------------------------------------
# -c is "compile only" (do not link), -d "merge duplicate strings"
COMPILE = $(CC) $(DEBUG) $(FULL) -c -d -m$(MODEL) $(CCEXTRA) -I$(DRIVE)\include -L$(DRIVE)\lib
LINK= $(LINKER) $(LINKDEBUG) $(DRIVE)\lib\c0$(MODEL)
LIBS= $(DRIVE)\lib\c$(MODEL)

#------------------------------------------------
# *** You should never have to modify this file below this line! ***

.c.obj:
    $(COMPILE) $*.c

# creates the main binary
edit.exe : edit.obj dialogs.obj menus.obj dflat.lib
    $(LINK) edit dialogs menus,edit.exe,edit,dflat $(LIBS)

# dflat.bld contents: For each of the listed .obj files, 1 line like
# "+windows.obj &". Do not put an & after the last line.
dflat.lib :   window.obj video.obj message.obj                         \
              mouse.obj console.obj textbox.obj listbox.obj            \
              normal.obj config.obj menu.obj menubar.obj popdown.obj   \
              rect.obj applicat.obj keys.obj sysmenu.obj editbox.obj   \
              dialbox.obj button.obj fileopen.obj msgbox.obj           \
              helpbox.obj log.obj lists.obj statbar.obj decomp.obj     \
              combobox.obj pictbox.obj clipbord.obj search.obj         \
              dfalloc.obj checkbox.obj text.obj radio.obj box.obj      \
              spinbutt.obj  watch.obj slidebox.obj direct.obj          \
              editor.obj calendar.obj asciitab.obj
        del dflat.lib
        $(LIB) dflat @dflat.bld

# help file helper tool
huffc.exe : huffc.obj htree.obj
    $(LINK) huffc htree,$*.exe,$*,$(LIBS)

# help file helper tool
fixhelp.exe : fixhelp.obj decomp.obj
    $(LINK) fixhelp decomp,$*.exe,$*,$(LIBS)

# runs huffc and fixhelp to compress and index the help file
edit.hlp : edit.txt huffc.exe fixhelp.exe
    huffc edit.txt edit.hlp
    fixhelp edit

# run this make target to compress EDIT with UPX (http://upx.sf.net/)
# the --best option compresses much slower but only very slighly better.
upx : all
    upx --8086 --best edit.exe
