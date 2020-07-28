#include "common.h"
#include "arithmetic_coder.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

float GetTimeElapsed(clock_t BeginTick, clock_t EndTick)
{
    return f32(EndTick - BeginTick) / f32(CLOCKS_PER_SEC);
}

memory ReadEntireFile(char *Filename)
{
    memory Result = {};
    
    FILE *File = fopen(Filename, "rb");
    if (File)
    {
        fseek(File, 0, SEEK_END);
        Result.Size = ftell(File);
        rewind(File);
        Result.Data = (u8 *)calloc(Result.Size, 1);
        fread(Result.Data, 1, Result.Size, File);
        fclose(File);
    }
    
    return Result;
}

void WriteEntireFile(char *Filename, void *Data, size_t Size)
{
    FILE *File = fopen(Filename, "wb");
    if (File)
    {
        fwrite(Data, 1, Size, File);
        fclose(File);
    }
}

void Benchmark()
{
    char *InputFilename = "../data/conference.obj";
    
    printf("testing on %s:\n", InputFilename);
    
    clock_t BeginTick = clock();
    
    FILE *File = fopen(InputFilename, "rb");
    ASSERT(File);
    
    fseek(File, 0, SEEK_END);
    size_t DataSize = ftell(File);
    rewind(File);
    u8 *Data = (u8 *)calloc(DataSize, 1);
    fread(Data, 1, DataSize, File);
    fclose(File);
    
    clock_t EndTick = clock();
    printf("file loading time: %.2fs\n", GetTimeElapsed(BeginTick, EndTick));
    
    // parallel compression test & benchmark
    {
        BeginTick = clock();
        memory EncodedData = EncodeParallel(Data, DataSize);
        EndTick = clock();
        
        f32 ParallelCompressionTime = GetTimeElapsed(BeginTick, EndTick);
        printf("parallel compression time: %.2fs, parallel compression ratio: %.5f\n", 
               ParallelCompressionTime, f32(DataSize)/f32(EncodedData.Size));
        
        BeginTick = clock();
        memory DecodedData = DecodeParallel(EncodedData.Data, EncodedData.Size);
        EndTick = clock();
        
        f32 ParallelDecompressionTime = GetTimeElapsed(BeginTick, EndTick);
        printf("parallel decompression time: %.2fs\n", ParallelDecompressionTime);
        
        size_t CorrectByteCount = 0;
        for (size_t ByteI = 0; ByteI < DataSize; ++ByteI)
        {
            if (DecodedData.Data[ByteI] == Data[ByteI])
            {
                CorrectByteCount += 1;
            }
        }
        printf("accuracy: %zu/%zu\n", CorrectByteCount, DataSize);
        
        printf("parallel compression speed: %.2fmb/s\n", f32(DataSize)/f32(1024*1024)/ParallelCompressionTime);
        printf("parallel decompression speed: %.2fmb/s\n", f32(DataSize)/f32(1024*1024)/ParallelDecompressionTime);
    }
}

bool StringEqual(char *A, char *B)
{
    return strcmp(A, B) == 0;
}

int main(int ArgCount, char **Args)
{
#if 1 
    Benchmark();
#else
    if (ArgCount == 4)
    {
        bool Encode = false;
        if (StringEqual(Args[1], "-encode"))
        {
            Encode = true;
        }
        else if (StringEqual(Args[1], "-decode"))
        {
            Encode = false;
        }
        else
        {
            printf("usage: arith_coder.exe [-encode/-decode] [input file] [output file]\n");
            return -1;
        }
        
        char *InFilename = Args[2];
        char *OutFilename = Args[3];
        
        memory Input = ReadEntireFile(InFilename);
        if (!Input.Data)
        {
            printf("couldn't read %s\n", InFilename);
            return -1;
        }
        
        memory Output = {};
        if (Encode)
        {
            Output = EncodeParallel(Input.Data, Input.Size);
        }
        else
        {
            Output = DecodeParallel(Input.Data, Input.Size);
        }
        
        WriteEntireFile(OutFilename, Output.Data, Output.Size);
    }
    else
    {
        printf("usage: arith_coder.exe [-encode/-decode] [input file] [output file]\n");
        return -1;
    }
#endif
    
    return 0;
}