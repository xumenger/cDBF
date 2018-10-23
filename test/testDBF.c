#include <stdio.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include "../src/cDBF.h"

#define ONE_SECOND 1000000

int main()
{
    int i = 0;
    printf("[test Start]\n");

    printf("\n[start OpenDBF]\n");
    CDBF *cDBF = OpenDBF("./testDbf-dBaseIII.dbf");
    if (NULL == cDBF){
        printf("OpenDBF Error\n");
        return -1;
    }

    printf("\n[get DBFHead Message]\n");
	printf("cDBF's path:%s\n", cDBF->Path);
	printf("cDBF's reccount=%d\n", cDBF->Head->RecCount);

    printf("\n[get DBFField Message]\n");
	for(i=0; i<cDBF->FieldCount; i++){
        printf("filed = %d, name = %s\n", i, cDBF->Fields[i].FieldName);
    }

    printf("\n[get Data Message]\n");
    Go(cDBF, 1);
    for(i=0; i<cDBF->FieldCount; i++){
        printf("filed = %d, name = %s, value = %s\n", i, cDBF->Values[i].Field->FieldName, cDBF->Values[i].ValueBuf);
    }
    
    printf("\n[test Go and FieldByName]\n");
    Go(cDBF, 1);
    printf("Record 1\n");
    printf("name = %s, str values = %s\n", "name", GetFieldAsString(cDBF, "nAmE"));
    printf("name = %s, int values = %d\n", "age", GetFieldAsInteger(cDBF, "agE"));
    printf("name = %s, float values = %f\n", "age", GetFieldAsFloat(cDBF, "agE"));
    printf("name = %s, float values = %f\n", "float", GetFieldAsFloat(cDBF, "float"));
    printf("name = %s, str values = %s\n", "birthday", GetFieldAsString(cDBF, "birtHday"));
    printf("name = %s, str values = %s\n", "job", GetFieldAsString(cDBF, "job"));
    if(DBF_TRUE == GetFieldAsBoolean(cDBF, "bool")){
        printf("bool 为 True\n");
    }
    else{
        printf("bool 为 False\n");
    }
    
    Go(cDBF, 2);
    printf("Record 2\n");
    printf("name = %s, str values = %s\n", "name", GetFieldAsString(cDBF, "nAmE"));
    printf("name = %s, int values = %d\n", "age", GetFieldAsInteger(cDBF, "agE"));
    printf("name = %s, float values = %f\n", "age", GetFieldAsFloat(cDBF, "agE"));
    printf("name = %s, float values = %f\n", "float", GetFieldAsFloat(cDBF, "float"));
    printf("name = %s, str values = %s\n", "birthday", GetFieldAsString(cDBF, "birtHday"));
    printf("name = %s, str values = %s\n", "job", GetFieldAsString(cDBF, "job"));
    if(DBF_TRUE == GetFieldAsBoolean(cDBF, "bool")){
        printf("bool 为 True\n");
    }
    else{
        printf("bool 为 False\n");
    }
    
    printf("\n[test Go and FieldByName Error]\n");
    printf("name = %s, float values = %f\n", "job", GetFieldAsFloat(cDBF, "job"));
    printf("name = %s, int values = %d\n", "job", GetFieldAsInteger(cDBF, "job"));

    printf("\n[test Post]\n");
    Append(cDBF);
    SetFieldAsString(cDBF, "name", "post");
    SetFieldAsInteger(cDBF, "age", 777);
    SetFieldAsFloat(cDBF, "float", 77.7);
    SetFieldAsString(cDBF, "birthday", "20170707");
    SetFieldAsBoolean(cDBF, "bool", DBF_TRUE);
    Post(cDBF);
    
    printf("\n[test Edit]\n");
    Go(cDBF, 1);
    Edit(cDBF);
    SetFieldAsString(cDBF, "name", "editeditediteditediteditediteditediteditediteediteditediteditditeditedit");
    SetFieldAsInteger(cDBF, "age", 777);
    SetFieldAsFloat(cDBF, "float", 77.7);
    SetFieldAsString(cDBF, "birthday", "201707072017");
    SetFieldAsBoolean(cDBF, "bool", DBF_TRUE);
    Post(cDBF);

    printf("\n[test Zap]\n");
    Zap(cDBF);

    printf("\n[test Speed]\n");
    struct timeval tvStart, tvEnd;
    gettimeofday(&tvStart, NULL);
    for(i=0; i<1000; i++){
        Append(cDBF);
        SetFieldAsString(cDBF, "name", "post");
        SetFieldAsInteger(cDBF, "age", 777);
        SetFieldAsFloat(cDBF, "float", 77.7);
        SetFieldAsString(cDBF, "birthday", "20170707");
        SetFieldAsBoolean(cDBF, "bool", DBF_TRUE);
        Post(cDBF);
    }
    gettimeofday(&tvEnd, NULL);
    int useTime = tvEnd.tv_sec * ONE_SECOND + tvEnd.tv_usec - (tvStart.tv_sec * ONE_SECOND + tvStart.tv_usec); 
    printf("Append 1000 use %d us\n", useTime);   

    printf("\n[start CloseDBF]\n");
    CloseDBF(cDBF);

    printf("\n[test Finish]\n\n");
    
    return 0;
}
