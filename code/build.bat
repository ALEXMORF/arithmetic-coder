@echo off

IF NOT EXIST ..\build mkdir ..\build
pushd ..\build

ctime -begin arithmetic_coder.ctm
cl -nologo -FC -Z7 -O2 -WX -W4 -wd4996 -wd4100 -wd4505 -wd4189 ..\code\main.cpp 
ctime -end arithmetic_coder.ctm

popd