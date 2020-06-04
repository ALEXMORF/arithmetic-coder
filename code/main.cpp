#include "common.h"
#include "arithmetic_coder.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main()
{
#if 0
    FILE *File = fopen("../data/san-miguel-low-poly.obj", "rb");
    ASSERT(File);
    
    fseek(File, 0, SEEK_END);
    int DataSize = ftell(File);
    rewind(File);
    u8 *Data = (u8 *)calloc(DataSize, 1);
    fread(Data, 1, DataSize, File);
    fclose(File);
    
    size_t EncodedSize = 0;
    void *EncodedData = Encode64(Data, DataSize, &EncodedSize);
#else
    
    char *Data = "Hello Man";
    size_t DataSize = strlen(Data);
    
    range Model[256] = {};
    f64 EncodedData = Encode64((u8 *)Data, DataSize, Model);
    
    size_t DecodedSize = 0;
    u8 *DecodedData = Decode64(EncodedData, DataSize, Model);
    
#endif
    
    return 0;
}