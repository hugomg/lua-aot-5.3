#!/usr/bin/python3
import glob, os
import sys

sys.stdout = open("Makefile", "w")

highlight = "./syntax_highlight.sh"
indir  = "./examples"

names = []
lua_files = []
c_files = []
s_files = []
so_files = []

for lua_filename in sorted(os.listdir(indir)):
    name, ext = os.path.splitext(lua_filename)
    if ext != '.lua': continue
    lua_files.append( os.path.join(indir, name + '.lua') )
    c_files.append( os.path.join(indir, name + '.c') )
    s_files.append( os.path.join(indir, name + '.S') )
    so_files.append( os.path.join(indir, name + '.so') )

print('LUASRC=../src')
print('CC=gcc -std=gnu99')
print('CFLAGS=-Wall -Wextra -std=c99 -pedantic -fPIC -O2 -Wno-unused-label -g -I$(LUASRC)')
print()
print('C_FILES:=' + " ".join(c_files))
print('S_FILES:=' + " ".join(s_files))
print('SO_FILES:=' + " ".join(so_files))
print()
print('LUAOT:=$(LUASRC)/luaot')
print('INCLUDES:=$(LUASRC)/luaot-generated-header.c $(LUASRC)/luaot-generated-footer.c')
print()
print(".PHONY: all clean")
print()

print("all: $(SO_FILES)")
print()

print("clean:")
print("\t" + "rm -rf -- " + " ".join(c_files + s_files + so_files))
print()

for lua_file, c_file in zip(lua_files, c_files):
    print(c_file + ':' + ' ' + lua_file + ' ' + '$(LUAOT)' )
    print('\t' + '$(LUAOT)' + ' ' + lua_file + ' -o ' + c_file)
    print( )

for c_file, s_file in zip(c_files, s_files):
    print(s_file + ':' + ' ' + c_file + ' ' + '$(INCLUDES)' )
    print('\t' + '$(CC) $(CFLAGS) -S ' + ' ' + c_file + ' -o ' + s_file)
    print( )

for s_file, so_file in zip(s_files, so_files):
    print(so_file + ':' + ' ' + s_file )
    print('\t' + '$(CC) -shared ' + ' ' + s_file + ' -o ' + so_file)
    print( )

