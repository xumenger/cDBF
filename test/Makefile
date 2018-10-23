#直接执行make命令进行构建
#http://www.xumenger.com/make-makefile/

#目标 : 依赖
#<tab键>命令
#一定要是Tab绝对不能是空格

#最后执行的编译命令要放在最前面！

#链接.o生成可执行文件
testDBF : cDBF.o cHash.o testDBF.o
	gcc -Wall testDBF.o cDBF.o cHash.o -o testDBF
#编译(不链接).c生成.o文件，通过-DDEBUG开启DEBUG编译选项
cDBF.o : ../src/cDBF.c ../src/cDBF.h ../src/cDBFStruct.h
	gcc -Wall -DDEBUG -c ../src/cDBF.c -o cDBF.o
cHash.o : ../src/cHash.c ../src/cHash.h
	gcc -Wall -DDEBUG -c ../src/cHash.c -o cHash.o
testDBF.o : testDBF.c
	gcc -Wall -c testDBF.c -o testDBF.o
#删除.o文件
.PHONY : clean
clean:
	rm -f *.o