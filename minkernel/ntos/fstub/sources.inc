MAJORCOMP=ntos
MINORCOMP=fstub

TARGETNAME=fstub
TARGETTYPE=LIBRARY
TARGETPATH=obj

BUILD_PRODUCES=ntosfstub$(NT_UP)

MIPS_OPTIONS=-nodwalign
GPSIZE=32

INCLUDES=..\..\inc;$(BASE_INC_PATH)

MSC_WARNING_LEVEL=/W4 /WX

C_DEFINES=$(C_DEFINES) -D_NTSYSTEM_

SOURCES=..\halfnc.c \
        ..\drivesup.c

NTTEST=
OPTIONAL_NTTEST=tio

SOURCES_USED=..\sources.inc