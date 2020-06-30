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

int main()
{
    //char *InputFilename = "../data/san-miguel.obj";
    //char *InputFilename = "../data/san-miguel-padded.obj";
    //char *InputFilename = "../data/san-miguel-low-poly.obj";
    //char *InputFilename = "../data/conference.obj";
    //char *InputFilename = "../data/light_probes.data";
    char *InputFilename = "../data/foliage.data";
    //char *InputFilename = "../data/raw-pic.data";
    
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
    
    encoder Encoder = {};
    
    BeginTick = clock();
    Encoder.Encode(Data, DataSize);
    EndTick = clock();
    
    f32 CompressionTime = GetTimeElapsed(BeginTick, EndTick);
    f32 CompressionRatio = f32(DataSize)/f32(Encoder.OutputSize);
    printf("compression time: %.2fs, compression ratio: %.5f\n", 
           CompressionTime, CompressionRatio);
    
    decoder Decoder = {};
    
    size_t DecodedSize = 0;
    BeginTick = clock();
    u8 *DecodedData = Decoder.Decode(Encoder.OutputStream, Encoder.OutputSize);
    EndTick = clock();
    f32 DecompressionTime = GetTimeElapsed(BeginTick, EndTick);
    printf("decompression time: %.2fs\n", DecompressionTime);
    
    size_t CorrectByteCount = 0;
    size_t FirstErrorOccuring = ~0ull;
    for (size_t ByteI = 0; ByteI < DataSize; ++ByteI)
    {
        if (DecodedData[ByteI] == Data[ByteI])
        {
            CorrectByteCount += 1;
        }
        else
        {
            if (FirstErrorOccuring == (~0ull))
            {
                FirstErrorOccuring = ByteI;
            }
        }
    }
    printf("accuracy = %zu/%zu\n", CorrectByteCount, DataSize);
    if (FirstErrorOccuring != (~0ull))
    {
        printf("First error occuring at byte %zu\n", FirstErrorOccuring);
    }
    
    u32 BytesPerMB = 1024*1024;
    printf("compression throughput: %.2fmb/s\n", f32(DataSize)/f32(BytesPerMB)/CompressionTime);
    printf("decompression throughput: %.2fmb/s\n", f32(DataSize)/f32(BytesPerMB)/DecompressionTime);
    
    getchar();
    return 0;
}