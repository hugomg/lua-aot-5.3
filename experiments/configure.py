#!/usr/bin/python3
import glob, os
import sys

sys.stdout = open("Makefile", "w")

highlight = "./syntax_highlight.sh"
indir  = "./examples"
outdir = "./generated"

names = []
lua_files = []
c_files = []
so_files = []

for lua_filename in sorted(os.listdir(indir)):
    name, _ = os.path.splitext(lua_filename)

    names.append(name)
    lua_files.append( os.path.join(indir, name + '.lua') )
    c_files.append( os.path.join(outdir, name + '.c') )
    so_files.append( os.path.join(outdir, name + '.so') )

print('LUASRC=../src')
print('CC=gcc -std=gnu99')
#print('CC=clang -std=gnu99')
print('CFLAGS=-Wall -Wextra -std=c99 -pedantic -fPIC -shared -O2 -Wno-unused-label -g -I$(LUASRC)')
print()
#print('C_FILES:=' + " ".join(c_files))
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
print("\t" + "rm -rf ./generated/*")
print()

for name, lua_file, c_file in zip(names, lua_files, c_files):
    print(c_file + ':' + ' ' + lua_file + ' ' + '$(LUAOT)' )
    print('\t' + '$(LUAOT)' + ' ' + name + ' ' + lua_file + ' > ' + c_file)
    print( )

for c_file, so_file in zip(c_files, so_files):
    print(so_file + ':' + ' ' + c_file + ' ' + '$(INCLUDES)' )
    print('\t' + '$(CC) $(CFLAGS)' + ' ' + c_file + ' -o ' + so_file)
    print( )




