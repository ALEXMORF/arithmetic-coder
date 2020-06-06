#include "common.h"
#include "arithmetic_coder.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

int main()
{
    //char *InputFilename = "../data/san-miguel-low-poly.obj";
    char *InputFilename = "../data/raw-pic.data";
    
    FILE *File = fopen(InputFilename, "rb");
    ASSERT(File);
    
    fseek(File, 0, SEEK_END);
    int DataSize = ftell(File);
    rewind(File);
    u8 *Data = (u8 *)calloc(DataSize, 1);
    fread(Data, 1, DataSize, File);
    fclose(File);
    
    encoder Encoder = {};
    
    clock_t BeginTick = clock();
    Encoder.Encode((u8 *)Data, DataSize);
    clock_t EndTick = clock();
    
    f32 CompressionTime = f32(EndTick - BeginTick) / f32(CLOCKS_PER_SEC);
    f32 CompressionRatio = f32(DataSize)/f32(Encoder.OutputSize);
    printf("compression time: %.2fs, compression ratio: %.5f\n", CompressionTime, CompressionRatio);
    
    u8 *DecodedData = Decode(Encoder.OutputStream, Encoder.OutputSize);
    
    getchar();
    return 0;
}