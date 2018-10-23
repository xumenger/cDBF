/********************************************************************************* 
 * Copyright(C), xumenger
 * FileName     : cDBF.h
 * Author       : xumenger
 * Version      : V1.0.0 
 * Date         : 2018-09-30
 * Description  : 
     1.cDBF.h对外方法接口实现
     2.注意内存的管理：申请和释放的一一对应；防止内存泄漏；防止野指针等
     3.注意字符串的操作安全性
     4.通过文件锁保证多进程/多线程读写文件的安全性
     5.读写文件的技巧，需要考虑磁盘、缓存的细节
     6.编程规范：充分判断函数调用的各种返回值
     7.如何根据FieldName快速定位到cDBF->Fields中的序号，需要实现一个排序
     8.需要显式将字符串最后一位设置为NULL
     9.字符串和整型/浮点型的转换、浮点型的精度需要注意
    10.注意数组、字符串的处理，防止出现数组越界的严重问题
    11.文件锁参考[http://blog.csdn.net/dragon_li_chen/article/details/17147911]
**********************************************************************************/  
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "cDBF.h"
#include "cHash.h"

int ReadHead(CDBF *cDBF);
int WriteHead(CDBF *cDBF);
int ReadFields(CDBF *cDBF);
int LockRow(CDBF *cDBF, int rowNo);
int UnLockRow(CDBF *cDBF, int rowNo);
int GetIndexByName(CDBF *cDBF, char *FieldName);

/*******************************************************************************
* Function   : OpenDBF
* Description: 打开DBF文件; 供外部调用的Public方法
* Input      :
    * filePath, DBF文件目录  
* Output     :
* Return     : 该DBF文件对应的CDBF文件指针; 返回NULL表示打开失败
* Others     : 
*******************************************************************************/  
CDBF *OpenDBF(char *filePath)
{
    //为cDBF申请内存
    CDBF *cDBF = malloc(sizeof(CDBF));
    if (NULL == cDBF){
        return NULL;
    }
    memset(cDBF, '\0', sizeof(CDBF));
    cDBF->status = dsBrowse;
    //读写二进制文件方式打开DBF文件
    cDBF->FHandle = fopen(filePath, "rb+");
    if (NULL == cDBF->FHandle){
        CloseDBF(cDBF);
        return NULL;
    }
    //申请内存保存DBF文件的目录
    cDBF->Path = malloc(strlen(filePath) + 1);
    if(NULL == cDBF->Path){
        CloseDBF(cDBF);
        return NULL;
    }
    strcpy(cDBF->Path, filePath);
    //申请存储文件头的内存
    cDBF->Head = malloc(sizeof(DBFHead));
    if (NULL == cDBF->Head){
        CloseDBF(cDBF);
        return NULL;
    }
    //读取文件头
    if (DBF_FAIL == ReadHead(cDBF)){
        CloseDBF(cDBF);
        return NULL;
    }
    //判断文件列个数
    cDBF->FieldCount = (cDBF->Head->DataOffset - sizeof(DBFHead)) / sizeof(DBFField);
    if ((cDBF->FieldCount < MIN_FIELD_COUNT) || (cDBF->FieldCount > MAX_FIELD_COUNT)){
        CloseDBF(cDBF);
        return NULL;
    }
    //申请存储列信息的内存
    cDBF->Fields = malloc(sizeof(DBFField) * cDBF->FieldCount);
    if (NULL == cDBF->Fields){
        CloseDBF(cDBF);
        return NULL;
    }
    //申请行数据缓存
    cDBF->ValueBuf = malloc(cDBF->Head->RecSize);
    if (NULL == cDBF->ValueBuf){
        CloseDBF(cDBF);
        return NULL;
    }
    cDBF->deleted = ' ';
    memset(cDBF->ValueBuf, '\0', cDBF->Head->RecSize);
    //读列信息
    if (DBF_FAIL == ReadFields(cDBF)){
        CloseDBF(cDBF);
        return NULL;
    }
	//申请列值信息的存储空间
	cDBF->Values = malloc(sizeof(DBFValue) * cDBF->FieldCount);
	if (NULL == cDBF->Values){
        CloseDBF(cDBF);
        return NULL;
    }
    //定位到第一行
    cDBF->RecNo = 0;
    if (cDBF->Head->RecCount > 0){
        //不考虑删除的情况，所以不需要考虑读的时候有一行，Go的时候被删除的情况！
        if (DBF_SUCCESS != Go(cDBF, 1)){
            CloseDBF(cDBF);
            return NULL;
        }
    }
    return cDBF;
}


/******************************************************************************* 
* Function   : CloseDBF
* Description: 关闭DBF文件; 供外部调用的Public方法
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     : 是否关闭成功, -1:关闭成功; 1:关闭失败
* Others     : 
*******************************************************************************/ 
int CloseDBF(CDBF *cDBF)
{
    if (NULL != cDBF){
        //OpenDBF中逐层申请内存，在Close中逐层释放内存、释放文件句柄
        if(NULL != cDBF->Path){
            free(cDBF->Path);
        }
        if(NULL != cDBF->FHandle){
            fclose(cDBF->FHandle);
        }
        if(NULL != cDBF->Head){
            free(cDBF->Head);
        }
        if(NULL != cDBF->Fields){
            free(cDBF->Fields);
        }
        if(NULL != cDBF->ValueBuf){
            free(cDBF->ValueBuf);
        }
        if(NULL != cDBF->Values){
            free(cDBF->Values);
        }
        free(cDBF);
        cDBF = NULL;
        return DBF_SUCCESS;
    }
    return DBF_FAIL;
}


/******************************************************************************* 
* Function   : First
* Description: 切换到DBF文件的第一条记录
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     : 当前指向记录的序号, -1:切换失败; 0:DBF中没有记录，无法切换到第一条; 1:第一条记录的序号 
* Others     : 
*******************************************************************************/ 
int First(CDBF *cDBF)
{
    if(cDBF->Head->RecCount <= 0){
        return DBF_NONE;
    }
    return Go(cDBF, 1);
}


/******************************************************************************* 
* Function   : Last
* Description: 切换到DBF文件的最后一条记录
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     : 当前指向记录的序号, -1:切换失败; 0:DBF中没有记录，无法切换到第一条; >0:最后一条记录的序号 
* Others     : 
*******************************************************************************/ 
int Last(CDBF *cDBF)
{
    if(cDBF->Head->RecCount <= 0){
        return DBF_EOF;
    }
	return Go(cDBF, cDBF->Head->RecCount);
}


/******************************************************************************* 
* Function   : Next
* Description: 切换到DBF文件的下一条记录
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     : 当前指向记录的序号, -1:切换失败; 0:下一条是Eof，无法切换到第一条; >0:切换成功，返回切换后的序号
* Others     : 
*******************************************************************************/
int Next(CDBF *cDBF)
{
    if(cDBF->RecNo > cDBF->Head->RecCount){
        return DBF_EOF;
    }
	return Go(cDBF, cDBF->RecNo + 1);
}


/******************************************************************************* 
* Function   : Prior
* Description: 切换到DBF文件的上一条记录
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     : 当前指向记录的序号, -1:切换失败; 0:当前已是第一条，无法向上; >0:切换成功，返回切换后的序号
* Others     : 
*******************************************************************************/
int Prior(CDBF *cDBF)
{
    if((cDBF->Head->RecCount <= 0) || (cDBF->RecNo <= 1)){
        return DBF_NONE;
    }
    return Go(cDBF, cDBF->RecNo - 1);
}


/******************************************************************************* 
* Function   : Go
* Description: 切换到DBF文件的第rowNo条记录
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * rowNo, 将要切换的记录的行号
* Output     :
* Return     : 当前指向记录的序号, -1:切换失败; >0:切换成功，返回切换后的序号
* Others     : 
*******************************************************************************/
int Go(CDBF *cDBF, int rowNo)
{
    if((rowNo <=0) || (rowNo > cDBF->Head->RecCount)){
        return DBF_FAIL;
    }
    //加锁
    if(DBF_SUCCESS != LockRow(cDBF, rowNo)){
        #ifdef DEBUG
        printf("Debug Go LockRow Error, rowNo = %d\n", rowNo);
        #endif
        return DBF_FAIL;
    }
    //偏移：文件头偏移 + 该行前面的数据偏移
    int Offset = cDBF->Head->DataOffset + (cDBF->Head->RecSize * (rowNo - 1));
    //修改文件指针到对应的记录位置。1.文件指针，2.指针的偏移量，3.指针偏移起始位置
    if(0 != fseek(cDBF->FHandle, Offset, SEEK_SET)){
        #ifdef DEBUG
        printf("Debug Go fseek Error, rowNo = %d\n", rowNo);
        #endif
        return DBF_FAIL;
    }
    //先读删除标记
    int readCount = 0;
    readCount = fread(&cDBF->deleted, 1, 1, cDBF->FHandle);
    if (1 != readCount){
        #ifdef DEBUG
        printf("Debug Go fread Error, readCount = %d\n", readCount);
        #endif
        return DBF_FAIL;
    }
    //然后将DBF文件中该列的数据读到内存中
    int i=0;
    int Width = 0;
    for(i=0; i<cDBF->FieldCount; i++){
        //将列值和列信息建立关系
        cDBF->Values[i].Field = &cDBF->Fields[i];
        //将磁盘中的各列读到对应内存列值中
        Width = cDBF->Values[i].Field->Width;
        readCount = fread(cDBF->Values[i].ValueBuf, Width, 1, cDBF->FHandle);
        if (1 != readCount){
            #ifdef DEBUG
            printf("Debug Go fread Error, readCount = %d\n", readCount);
            #endif
            return DBF_FAIL;
        }
        //将字符串最后一位设置为NULL
        cDBF->Values[i].ValueBuf[Width] = '\0';
    }
    //解锁
    if(DBF_SUCCESS != UnLockRow(cDBF, rowNo)){
        #ifdef DEBUG
        printf("Debug Go UnLockRow Error, rowNo = %d\n", rowNo);
        #endif
        return DBF_FAIL;
    }
    //更新cDBF的记录信息
    cDBF->RecNo = rowNo;
    return cDBF->RecNo;
}


/******************************************************************************* 
* Function   : Edit
* Description: 编译当前CDBF所指向的行
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     : 是否允许编辑, -1:不允许编辑; 1:允许编辑
* Others     :  编辑成功后需调用Post方法从内存更新到磁盘
*******************************************************************************/
int Edit(CDBF *cDBF)
{
    //直接返回，接下来在内存中编辑，然后调用Post才能更新到磁盘
    cDBF->status = dsEdit;
    return DBF_SUCCESS;
}


/******************************************************************************* 
* Function   : Append
* Description: 新增一行
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     : 新增记录后的行号, -1:新增失败; >0:新增记录后DBF文件记录数
* Others     : 新增成功后需调用Post方法从内存更新到磁盘
*******************************************************************************/
int Append(CDBF *cDBF)
{
    cDBF->status = dsAppend;
    //先将列的内存值清为空格
    cDBF->deleted = ' ';
    int i = 0;
    for(i=0; i<cDBF->FieldCount; i++){
        memset(cDBF->Values[i].ValueBuf, ' ', sizeof(cDBF->Values[i].ValueBuf));
    }
    return DBF_SUCCESS;
}


/******************************************************************************* 
* Function   : Delete
* Description: 将当前行设置为Deleted
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     : 是否删除成功, -1:删除失败; 1:删除成功
* Others     : 删除成功后需调用Post方法从内存更新到磁盘
*******************************************************************************/
int Delete(CDBF *cDBF)
{
    cDBF->status = dsEdit;
    cDBF->deleted = '*';
    return DBF_SUCCESS;
}


/******************************************************************************* 
* Function   : Post
* Description: 将修改更新到磁盘
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     : 是否更新成功, -1:更新失败; 1:更新成功
* Others     : 
*******************************************************************************/
int Post(CDBF *cDBF)
{
    //列数据先写入内存缓冲区
    int i = 0;
    void *RowData = cDBF->ValueBuf;
    memcpy(RowData, &cDBF->deleted, 1);
    RowData = RowData + 1;
    for(i=0; i<cDBF->FieldCount; i++){
        memcpy(RowData, cDBF->Values[i].ValueBuf, cDBF->Fields[i].Width);
        RowData = RowData + cDBF->Fields[i].Width;
    }
    //编辑结果保存到磁盘
    int Offset = 0;
    if(dsEdit == cDBF->status){
        Offset = cDBF->Head->DataOffset + (cDBF->Head->RecSize * (cDBF->RecNo - 1));
    }
    else if(dsAppend == cDBF->status){
        Offset = cDBF->Head->DataOffset + (cDBF->Head->RecSize * (cDBF->Head->RecCount));
        cDBF->Head->RecCount ++;
    }
    if(0 != fseek(cDBF->FHandle, Offset, SEEK_SET)){
        #ifdef DEBUG
        printf("Debug Post fseek Error\n");
        #endif
        return DBF_FAIL;
    }
    int writeCount = fwrite(cDBF->ValueBuf, cDBF->Head->RecSize, 1, cDBF->FHandle);
    if(1 != writeCount){
        #ifdef DEBUG
        printf("Debug Post fwrite Error\n");
        #endif
        return DBF_FAIL;
    }
    //更新文件头中记录数信息
    if(DBF_FAIL == WriteHead(cDBF)){
        return DBF_FAIL;
    }
    //修改DBF文件编辑状态
    cDBF->status = dsBrowse;
    return DBF_SUCCESS;
}


/******************************************************************************* 
* Function   : Zap
* Description: 清空DBF中的数据
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     : 是否清空成功, -1:清空失败; 1:清空成功
* Others     :
    * OpenDBF成功后，就可以直接调用Zap清空DBF文件了 
*******************************************************************************/
int Zap(CDBF *cDBF)
{
    //首先清空文件
    //fileno通过fopen的文件描述符得到对应open的文件描述符
    int fd = fileno(cDBF->FHandle);
    if(0 != ftruncate(fd, cDBF->Head->DataOffset)){
        return DBF_FAIL;
    }
    //更新文件头中记录数信息
    cDBF->Head->RecCount = 0;
    if(DBF_FAIL == WriteHead(cDBF)){
        return DBF_FAIL;
    }
    return DBF_SUCCESS;
}


/******************************************************************************* 
* Function   : Fresh
* Description: 将DBF信息从磁盘更新到内存，刷新记录数等信息
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     : 是否刷新成功, -1:刷新失败; 1:刷新成功
* Others     :
*******************************************************************************/
int Fresh(CDBF *cDBF)
{
    return ReadHead(cDBF);
}


/******************************************************************************* 
* Function   : GetFieldAsBoolean
* Description: 获取cDBF指向的当前行的fieldName列的值，并作为布尔值返回
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * fieldName, 列名
* Output     :
* Return     : 布尔值, 0-False; 1-True
* Others     :
*******************************************************************************/
unsigned char GetFieldAsBoolean(CDBF *cDBF, char *fieldName)
{
    int index = GetIndexByName(cDBF, fieldName);
    if(DBF_FAIL == index){
        return DBF_FALSE;
    }
    if(('L' == cDBF->Fields[index].FieldType) && ('T' == cDBF->Values[index].ValueBuf[0])){
        return DBF_TRUE;
    }
    else{
        return DBF_FALSE;
    }
}


/******************************************************************************* 
* Function   : GetFieldAsInteger
* Description: 获取cDBF指向的当前行的fieldName列的值，并作为整型值返回
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * fieldName, 列名
* Output     :
* Return     : 返回的整型值
* Others     :
*******************************************************************************/
int GetFieldAsInteger(CDBF *cDBF, char *fieldName)
{
    char *str = GetFieldAsString(cDBF, fieldName);
    return atoi(str);
}


/******************************************************************************* 
* Function   : GetFieldAsInteger
* Description: 获取cDBF指向的当前行的fieldName列的值，并作为浮点值返回
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * fieldName, 列名
* Output     :
* Return     : 返回的浮点值
* Others     :
*******************************************************************************/
double GetFieldAsFloat(CDBF *cDBF, char *fieldName)
{
    char *str = GetFieldAsString(cDBF, fieldName);
    
    //注意，该函数需要包含stdlib.h
    //不包含stdlib.h也能编译通过，但是总是返回0.0
    //所以在编译时一定要加-Wall选项
    return atof(str);
}


/******************************************************************************* 
* Function   : GetFieldAsString
* Description: 获取cDBF指向的当前行的fieldName列的值，并作为字符串返回
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * fieldName, 列名
* Output     :
* Return     : 返回的字符串。
    * 正常返回字符串，否则返回空字符串
    * 返回字符串数组指针，所以若调用者需长久使用字符串，要申请字符数组进行保存
* Others     :
*******************************************************************************/
char *GetFieldAsString(CDBF *cDBF, char *fieldName)
{
    int index = GetIndexByName(cDBF, fieldName);
    if(DBF_FAIL == index){
        return "";
    }
    
    //字符串类型后面会用空格补齐，需要去除空格
    //int、float在前面补空格，可以不去除这种空格，不影响atoi、atof的转换
    int i = 0;
    for(i=cDBF->Fields[index].Width-1; i>=0; i--){
        if(' ' != cDBF->Values[index].ValueBuf[i]){
            break;
        }
    }
    cDBF->Values[index].ValueBuf[i+1] = '\0';
    return cDBF->Values[index].ValueBuf;
}


/******************************************************************************* 
* Function   : SetFieldAsBoolean
* Description: 设置cDBF指向的当前行的fieldName列的值
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * fieldName, 列名
    * value, 设置的值
* Output     :
* Return     : -1-设置失败; 1-设置成功
* Others     :
*******************************************************************************/
int SetFieldAsBoolean(CDBF *cDBF, char *fieldName, unsigned char value)
{
    int index = GetIndexByName(cDBF, fieldName);
    if(DBF_FAIL == index){
        return DBF_FAIL;
    }
    char boolValue;
    if(DBF_TRUE == value){
        boolValue = 'T';
    }
    else{
        boolValue = 'F';
    }
    cDBF->Values[index].ValueBuf[0] = boolValue;
    return DBF_SUCCESS;
}


/******************************************************************************* 
* Function   : SetFieldAsInteger
* Description: 设置cDBF指向的当前行的fieldName列的值
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * fieldName, 列名
    * default, 设置的值
* Output     :
* Return     : -1, 设置失败; 1-设置成功
* Others     :
*******************************************************************************/
int SetFieldAsInteger(CDBF *cDBF, char *fieldName, int value)
{
    int index = GetIndexByName(cDBF, fieldName);
    if(DBF_FAIL == index){
        return DBF_FAIL;
    }
    //int转成string，按DBF格式要求前面不足的位补空格
    //sprintf(s, "%*d", 2, 100);并不会截位，还是100，所以需要考虑设置值超长的问题！
    //这里不考虑截位，ValueBuf是256位，int转成string后，不会超过256位
    //后续将Values的ValueBuf拷贝到行缓存中，会按照Width拷贝，会自动截位！
    sprintf(cDBF->Values[index].ValueBuf, "%*d", cDBF->Fields[index].Width, value);
    return DBF_SUCCESS;
}


/******************************************************************************* 
* Function   : SetFieldAsFloat
* Description: 设置cDBF指向的当前行的fieldName列的值
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * fieldName, 列名
    * default, 设置的值
* Output     :
* Return     : -1, 设置失败; 1-设置成功
* Others     :
*******************************************************************************/
int SetFieldAsFloat(CDBF *cDBF, char *fieldName, double value)
{
    int index = GetIndexByName(cDBF, fieldName);
    if(DBF_FAIL == index){
        return DBF_FAIL;
    }
    //float转成string，按DBF格式要求前面不足的位补空格
    //这里不考虑截位，ValueBuf是256位，int转成string后，不会超过256位
    //后续将Values的ValueBuf拷贝到行缓存中，会按照Width拷贝，会自动截位！
    sprintf(cDBF->Values[index].ValueBuf, "%*.*f", cDBF->Fields[index].Width, cDBF->Fields[index].Scale, value);
    return DBF_SUCCESS;
}


/******************************************************************************* 
* Function   : SetFieldAsString
* Description: 设置cDBF指向的当前行的fieldName列的值
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * fieldName, 列名
    * default, 设置的值
* Output     :
* Return     : -1, 设置失败; 1-设置成功
* Others     :
*******************************************************************************/
int SetFieldAsString(CDBF *cDBF, char *fieldName, char *value)
{
    int index = GetIndexByName(cDBF, fieldName);
    if(DBF_FAIL == index){
        return DBF_FAIL;
    }
    //string类型写到DBF中要求后面补空格，且不用'\0'结尾
    //比如5位，写入"123"，不应该是'1','2','3','\0'，而应该是'1','2','3',' ',' '
    if(strlen(value) >= cDBF->Fields[index].Width){
        //如果value超长，会在这里自动截位
        memcpy(cDBF->Values[index].ValueBuf, value, cDBF->Fields[index].Width);
    }
    else{
        memcpy(cDBF->Values[index].ValueBuf, value, strlen(value));
        int i = 0;
        //字符串不足的，后面补空格
        for(i=strlen(value); i<cDBF->Fields[index].Width; i++){
            cDBF->Values[index].ValueBuf[i] = ' ';
        }
    }
    return DBF_SUCCESS;
}


/*----------------------------------------------------------------------------
* Function   : ReadHead
* Description: 读DBF文件的文件头，OpenDBF、Fresh时调用
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     :
    * 是否读取成功, -1:读取失败; 1:读取成功
* Others     :
----------------------------------------------------------------------------*/
int ReadHead(CDBF *cDBF)
{
    /*先实现功能，这里需要加锁，后续实现！*/
    
    //先定位到文件头
    if(0 != fseek(cDBF->FHandle, 0, SEEK_SET)){
        #ifdef DEBUG
        printf("Debug ReadHead fseek Error\n");
        #endif
        return DBF_FAIL;
    }
    //fread从cDBF->FHandle读1个sizeof(DBFHead)字节的数据放到cDBF->Head中，fread会自动移动文件指针
    int readCount = fread(cDBF->Head, sizeof(DBFHead), 1, cDBF->FHandle);
    if (1 != readCount){
        #ifdef DEBUG
        printf("Debug ReadHead fread Error, readCount = %d\n", readCount);
        #endif
        return DBF_FAIL;
    }
    #ifdef DEBUG
    printf("Debug ReadHead RecCount = %d\n", cDBF->Head->RecCount);
    #endif
    return DBF_SUCCESS;
}

/*----------------------------------------------------------------------------
* Function   : WriteHead
* Description: 将内存中的DBF头信息更新到磁盘
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     :
    * 是否更新成功, -1:更新失败; 1:更新成功
* Others     :
----------------------------------------------------------------------------*/
int WriteHead(CDBF *cDBF)
{
    //先定位到文件头
    if(0 != fseek(cDBF->FHandle, 0, SEEK_SET)){
        #ifdef DEBUG
        printf("Debug WriteHead fseek Error\n");
        #endif
        return DBF_FAIL;
    }
    //获取年月日信息
    time_t timep;
    struct tm *p;
    time(&timep);
    p = gmtime(&timep);
    //这里有int到unsigned char的转换，因为这些值不会超过unsigned char的范围，所以不会有问题
    cDBF->Head->Year = (unsigned char)p->tm_year;  //当前年-1990
    cDBF->Head->Month = (unsigned char)p->tm_mon;  //0~11
    cDBF->Head->Day = (unsigned char)p->tm_mday;   //1-31
    //头数据写到磁盘中
    int writeCount = fwrite(cDBF->Head, sizeof(DBFHead), 1, cDBF->FHandle);
    if(1 != writeCount){
        #ifdef DEBUG
        printf("Debug WriteHead fwrite Error");
        #endif
        return DBF_FAIL;
    }
    return DBF_SUCCESS; 
}


/*----------------------------------------------------------------------------
* Function   : ReadFields
* Description: 读DBF文件的列信息
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针  
* Output     :
* Return     :
    * 是否读取成功, -1:读取失败; 1:读取成功
* Others     :
----------------------------------------------------------------------------*/
int ReadFields(CDBF *cDBF)
{
    //先定位到列信息偏移地址
    if(0 != fseek(cDBF->FHandle, sizeof(DBFHead), SEEK_SET)){
        #ifdef DEBUG
        printf("Debug ReadFields fseek Error\n");
        #endif
        return DBF_FAIL;
    }
    //将列信息从磁盘读取到内存
    int readCount = fread(cDBF->Fields, sizeof(DBFField), cDBF->FieldCount, cDBF->FHandle);
    if (readCount != cDBF->FieldCount){
        #ifdef DEBUG
        printf("Debug ReadFields fread Error, readCount = %d, FieldCount = %d\n", readCount, cDBF->FieldCount);
        #endif
        return DBF_FAIL;
    }

    #ifdef DEBUG
    printf("Debug ReadFields FieldCount = %d\n", cDBF->FieldCount);
    printf("Debug ReadFields print FieldName\n");
    int i = 0;
    for (i=0; i<cDBF->FieldCount; i++){
        printf("Debug ReadFields %s\n", cDBF->Fields[i].FieldName);
    }
    #endif
    return DBF_SUCCESS;
}


/*----------------------------------------------------------------------------
* Function   : Lock
* Description: 
    * 通过文件锁将cDBF当前行锁住，保证在多进程/多线程并发情况下的数据一致性
    * 该方法是cDBF的私有方法，不提供接口给外部调用
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * rowNo, 加锁的行号
* Output     :
* Return     :
    * 是否锁定成功, -1:锁定失败; 1:锁定成功
* Others     :
----------------------------------------------------------------------------*/
int LockRow(CDBF *cDBF, int rowNo)
{
    return DBF_SUCCESS;
}


/*----------------------------------------------------------------------------
* Function   : UnLock
* Description: 
    * 给cDBF当前指向的行解文件锁
    * 该方法是cDBF的私有方法，不提供接口给外部调用
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * rowNo, 解锁的行号
* Output     :
* Return     :
    * 是否解锁成功, -1:解锁失败; 1:解锁成功
* Others     :
----------------------------------------------------------------------------*/
int UnLockRow(CDBF *cDBF, int rowNo)
{
    return DBF_SUCCESS;
}

/*----------------------------------------------------------------------------
* Function   : GetIndexByName
* Description: 
    * 根据列名找到该列对应的序号，用于根据列名获取某行记录的列值
    * 该方法是cDBF的私有方法，不提供接口给外部调用
* Input      :
    * cDBF, OpenDBF返回的CDBF结构体指针
    * FieldName, 列名
* Output     :
* Return     :
    * 列的序号, -1:没找到对应列失败; >=0:列的序号
* Others     :
----------------------------------------------------------------------------*/
int GetIndexByName(CDBF *cDBF, char *FieldName)
{
    int i = 0;
    //现在实现为逐列比较，DBF列一般不多，所以虽然时间复杂度为O(N)，但影响不大
    //后续可以考虑实现为Hash的方式在O(1)的时间复杂度内快速定位列序号
    for(i=0; i<cDBF->FieldCount; i++){
        //不区分大小写的比较字符串
        if(0 == strcasecmp(FieldName, cDBF->Fields[i].FieldName)){
            return i;
        }
    }
    return DBF_FAIL;    
}
