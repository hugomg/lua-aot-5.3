#!/usr/bin/python3
import glob, os
import sys

sys.stdout = open("Makefile", "w")

highlight = "./syntax_highlight.sh"
indir  = "./examples"

names = []
lua_files = []
c_files = []
so_files = []
#intermediate files
tmp_files = []

for lua_filename in sorted(os.listdir(indir)):
    name, ext = os.path.splitext(lua_filename)
    if ext != '.lua': continue
    lua_files.append( os.path.join(indir, name + '.lua') )
    c_files.append( os.path.join(indir, name + '.c') )
    so_files.append( os.path.join(indir, name + '.so') )
    tmp_files.append( os.path.join(indir, name + '.i') )
    tmp_files.append( os.path.join(indir, name + '.o') )
    tmp_files.append( os.path.join(indir, name + '.s') )

print('LUASRC=../src')
print('CC=gcc -std=gnu99')
print('CFLAGS=-Wall -Wextra -pedantic -fPIC -save-temps=obj -O2 -Wno-unused-label -g -I$(LUASRC)')
print()
print('C_FILES:=' + " ".join(c_files))
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
print("\t" + "rm -rf -- " + " ".join(c_files + so_files + tmp_files))
print()

for lua_file, c_file in zip(lua_files, c_files):
    print(c_file + ':' + ' ' + lua_file + ' ' + '$(LUAOT)' )
    print('\t' + '$(LUAOT)' + ' ' + lua_file + ' -o ' + c_file)
    print( )

for c_file, so_file in zip(c_files, so_files):
    print(so_file + ':' + ' ' + c_file + ' ' + '$(INCLUDES)' )
    print('\t' + '$(CC) $(CFLAGS) -shared ' + ' ' + c_file + ' -o ' + so_file)
    print( )
