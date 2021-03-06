#pragma once

#include <stdlib.h>
#include <stdio.h>

// for parallel encoder
#include <thread>
#include <atomic>

/*NOTE(chen):

precision boundary calculation:

High = Low + (Range * IntervalMax) / Scale -1;
        Low = Low + (Range * IntervalMin) / Scale;
        
Range bits = [CodeBits-2, CodeBits] (no closer than 1/4 invariant)

High and Low must never cross, that means the following must be true:

Range/Scale >= 1, given that min diff between Interval MinMax is 1.

Worst case, Range has bit count of CodeBits-2, 
Scale must not be greather than that value. Therefore:

Scale Bits <= CodeBits - 2

*/

#define ARITH_CODE_BIT_COUNT 16
#define ARITH_SCALE_BIT_COUNT 14
#define ARITH_MODEL_ORDER 16

#define KB(Value) (1024ULL*(Value))
#define MB(Value) (1024ULL*KB(Value))

#pragma pack(push, 1)
struct header
{
    size_t EncodedByteCount;
};
#pragma pack(pop)

struct memory
{
    u8 *Data;
    size_t Size;
};

struct interval
{
    u8 Symbol;
    
    u32 Min;
    u32 Max;
};

struct model
{
    u32 Prob[1<<(1*ARITH_MODEL_ORDER)];
    int Context;
    
    __forceinline void Init();
    __forceinline void UpdateOne();
    __forceinline void UpdateZero();
    
    __forceinline size_t GetContextSize();
};

struct encoder_state
{
    u8 *OutputStream;
    size_t OutputCap;
    size_t OutputSize;
    u8 StagingByte;
    int BitsFilled;
    
    __forceinline void OutputBit(u8 Bit);
    __forceinline void Init(header Header);
};

struct decoder_state
{
    u8 *InputStream;
    size_t OutputSize;
    u8 *Output;
    u8 StagingByte;
    size_t BytesRead;
    int BitsLeft;
    u8 BitMask;
    header *Header;
    
    __forceinline u8 InputBit();
    __forceinline void Init(u8 *Bits);
};

__forceinline void 
encoder_state::OutputBit(u8 Bit)
{
    StagingByte = (StagingByte << 1) | Bit;
    BitsFilled += 1;
    if (BitsFilled == 8)
    {
        if (OutputSize == OutputCap)
        {
            OutputCap *= 2;
            OutputStream = (u8 *)realloc(OutputStream, OutputCap);
        }
        OutputStream[OutputSize++] = StagingByte;
        
        StagingByte = 0;
        BitsFilled = 0;
    }
}

__forceinline void
model::Init()
{
    u32 Scale = 1 << ARITH_SCALE_BIT_COUNT;
    for (int ContextI = 0; ContextI < GetContextSize(); ++ContextI)
    {
        Prob[ContextI] = Scale >> 1;
    }
    
    Context = 0;
}

__forceinline 
void model::UpdateOne()
{
    u32 Scale = 1 << ARITH_SCALE_BIT_COUNT;
    Prob[Context] -= Prob[Context] >> 6;
    Context = ((Context << 1) + 1) % GetContextSize();
}

__forceinline 
void model::UpdateZero()
{
    u32 Scale = 1 << ARITH_SCALE_BIT_COUNT;
    Prob[Context] += (Scale - Prob[Context]) >> 6;
    Context = (Context << 1) % GetContextSize();
}

__forceinline size_t
model::GetContextSize()
{
    return 1<<(1*ARITH_MODEL_ORDER);
}

__forceinline void 
encoder_state::Init(header Header)
{
    OutputCap = sizeof(Header);
    OutputStream = (u8 *)calloc(OutputCap, 1);
    *((header *)OutputStream) = Header;
    OutputSize = OutputCap;
}

memory Encode(u8 *Data, size_t DataSize)
{
    model *Model = (model *)calloc(1, sizeof(model));
    Model->Init();
    
    encoder_state State = {};
    
    header Header = {};
    Header.EncodedByteCount = DataSize;
    
    State.Init(Header);
    
    u32 Scale = 1 << ARITH_SCALE_BIT_COUNT;
    u32 CodeBitMask = (1 << ARITH_CODE_BIT_COUNT) - 1;
    u32 MsbBitMask = (1 << (ARITH_CODE_BIT_COUNT-1));
    u32 SecondMsbBitMask = (1 << (ARITH_CODE_BIT_COUNT-2));
    u32 Half = MsbBitMask;
    u32 OneFourth = Half >> 1;
    u32 ThreeFourths = OneFourth * 3;
    
    u32 Low = 0;
    u32 High = CodeBitMask;
    size_t BitsPending = 0;
    for (size_t ByteI = 0; ByteI < DataSize; ++ByteI)
    {
        u8 Byte = Data[ByteI];
        u8 BitMask = 1 << 7;
        for (int BitI = 0; BitI < 8; ++BitI)
        {
            u8 Symbol = (Byte & BitMask)? 1: 0;
            BitMask >>= 1;
            
            u32 IntervalMin, IntervalMax;
            if (Symbol)
            {
                IntervalMin = Model->Prob[Model->Context];
                IntervalMax = Scale;
                Model->UpdateOne();
            }
            else
            {
                IntervalMin = 0;
                IntervalMax = Model->Prob[Model->Context];
                Model->UpdateZero();
            }
            
            ASSERT(Low < High);
            u32 Range = High - Low + 1;
            High = Low + ((Range * IntervalMax) >> ARITH_SCALE_BIT_COUNT) - 1;
            Low = Low + ((Range * IntervalMin) >> ARITH_SCALE_BIT_COUNT);
            ASSERT(Low <= High);
            
            for (;;)
            {
                if (Low >= Half || High < Half) // same MSB
                {
                    u8 FirstBit = (High & MsbBitMask)? 1: 0;
                    State.OutputBit(FirstBit);
                    
                    u8 PendingBit = !FirstBit;
                    for (size_t I = 0; I < BitsPending; ++I)
                    {
                        State.OutputBit(PendingBit);
                    }
                    
                    BitsPending = 0;
                }
                else if (Low >= OneFourth && High < ThreeFourths) // near-convergence
                {
                    BitsPending += 1;
                    Low -= OneFourth;
                    High -= OneFourth;
                }
                else
                {
                    break;
                }
                
                Low = (Low << 1) & CodeBitMask;
                High = ((High << 1) + 1) & CodeBitMask;
            }
        }
    }
    
    BitsPending += 1;
    if (Low < OneFourth)
    {
        State.OutputBit(0);
        for (size_t I = 0; I < BitsPending; ++I)
        {
            State.OutputBit(1);
        }
    }
    else
    {
        State.OutputBit(1);
        for (size_t I = 0; I < BitsPending; ++I)
        {
            State.OutputBit(0);
        }
    }
    BitsPending = 0;
    
    //NOTE(chen): make sure our last byte flushes
    if (State.BitsFilled != 0)
    {
        int PadBits = 8 - State.BitsFilled;
        for (int BitI = 0; BitI < PadBits; ++BitI)
        {
            State.OutputBit(0);
        }
        ASSERT(State.BitsFilled == 0);
    }
    
    free(Model);
    
    return {State.OutputStream, State.OutputSize};
}

__forceinline u8
decoder_state::InputBit()
{
    if (BitsLeft == 0)
    {
        StagingByte = InputStream[BytesRead++];
        BitsLeft = 8;
        BitMask = 1 << 7;
    }
    
    ASSERT(BitMask != 0);
    u8 Bit = (StagingByte & BitMask)? 1: 0;
    BitMask >>= 1;
    BitsLeft -= 1;
    return Bit;
}

__forceinline void
decoder_state::Init(u8 *Bits)
{
    Header = (header *)Bits;
    Bits += sizeof(header);
    InputStream = Bits;
    
    OutputSize = Header->EncodedByteCount;
    Output = (u8 *)calloc(Header->EncodedByteCount, 1);
}

memory Decode(u8 *Bits, size_t EncodedSize)
{
    model *Model = (model *)calloc(1, sizeof(model));
    Model->Init();
    
    decoder_state State = {};
    State.Init(Bits);
    
    u32 Scale = 1 << ARITH_SCALE_BIT_COUNT;
    u32 CodeBitMask = (1 << ARITH_CODE_BIT_COUNT) - 1;
    u32 MsbBitMask = (1 << (ARITH_CODE_BIT_COUNT-1));
    u32 SecondMsbBitMask = (1 << (ARITH_CODE_BIT_COUNT-2));
    u32 Half = MsbBitMask;
    u32 OneFourth = Half >> 1;
    u32 ThreeFourths = OneFourth * 3;
    
    u32 Low = 0;
    u32 High = CodeBitMask;
    
    u32 EncodedValue = 0;
    for (int BitI = 0; BitI < ARITH_CODE_BIT_COUNT; ++BitI)
    {
        EncodedValue = (EncodedValue << 1) | State.InputBit();
    }
    
    for (size_t ByteI = 0; ByteI < State.Header->EncodedByteCount; ++ByteI)
    {
        u8 OutputByte = 0;
        
        for (int BitI = 0; BitI < 8; ++BitI)
        {
            u32 Range = High - Low + 1;
            u32 ArithMid = Low + ((Range * Model->Prob[Model->Context]) >> ARITH_SCALE_BIT_COUNT) - 1;
            
            ASSERT(Low < High);
            u8 DecodedSymbol;
            if (EncodedValue <= ArithMid)
            {
                High = ArithMid;
                DecodedSymbol = 0;
                Model->UpdateZero();
            }
            else
            {
                Low = Low + ((Range * Model->Prob[Model->Context]) >> ARITH_SCALE_BIT_COUNT);
                DecodedSymbol = 1;
                Model->UpdateOne();
            }
            ASSERT(Low <= High);
            
            for (;;)
            {
                if (High < Half) // same MSB
                {
                    // need shifting
                }
                else if (Low >= Half) // same MSB
                {
                    Low -= Half;
                    High -= Half;
                    EncodedValue -= Half;
                }
                else if (Low >= OneFourth && High < ThreeFourths) // near convergence
                {
                    Low -= OneFourth;
                    High -= OneFourth;
                    EncodedValue -= OneFourth;
                }
                else
                {
                    break;
                }
                
                Low <<= 1;
                High = (High << 1) + 1;
                EncodedValue = (EncodedValue << 1) + State.InputBit();
            }
            ASSERT(EncodedValue <= High);
            
            OutputByte |= DecodedSymbol << (7-BitI);
        }
        
        State.Output[ByteI] = OutputByte;
    }
    
    free(Model);
    
    return {State.Output, State.OutputSize};
}

struct job
{
    volatile memory Input;
    volatile memory Output;
};

size_t Min(size_t A, size_t B)
{
    return A < B? A: B;
}

memory EncodeParallel(u8 *Data, size_t DataSize, size_t BlockSize = MB(1))
{
    size_t JobCount = (DataSize - 1) / BlockSize + 1;
    job *Jobs = (job *)calloc(JobCount, sizeof(job));
    std::atomic<size_t> NextJobIndex = 0;
    
    // build jobs
    for (size_t JobI = 0; JobI < JobCount; ++JobI)
    {
        job *Job = Jobs + JobI;
        Job->Input.Data = Data + Min(JobI*BlockSize, DataSize);
        Job->Input.Size = Min(BlockSize, DataSize-JobI*BlockSize);
    }
    
    auto WorkerFunc = [&]() {
        size_t JobIndex = NextJobIndex.fetch_add(1);
        while (JobIndex < JobCount)
        {
            job *Job = Jobs + JobIndex;
            
            memory Encoded = Encode(Job->Input.Data, Job->Input.Size);
            Job->Output.Data = Encoded.Data;
            Job->Output.Size = Encoded.Size;
            
            JobIndex = NextJobIndex.fetch_add(1);
        }
    };
    
    // spin up workers
    int WorkerCount = std::thread::hardware_concurrency() - 1;
    std::thread *Workers = (std::thread *)calloc(WorkerCount, sizeof(std::thread));
    for (int WorkerI = 0; WorkerI < WorkerCount; ++WorkerI)
    {
        Workers[WorkerI] = std::thread(WorkerFunc);
    }
    WorkerFunc();
    
    // block until jobs are done
    for (int WorkerI = 0; WorkerI < WorkerCount; ++WorkerI)
    {
        Workers[WorkerI].join();
    }
    
    // composite compressed data
    size_t OutputSize = 0;
    u8 *Output = 0;
    {
        OutputSize = 0;
        OutputSize += sizeof(size_t) * (1 + JobCount); // header
        for (int JobI = 0; JobI < JobCount; ++JobI)
        {
            OutputSize += Jobs[JobI].Output.Size;
        }
        
        Output = (u8 *)calloc(OutputSize, 1);
        size_t Cursor = 0;
        
        *(size_t *)(Output+Cursor) = JobCount;
        Cursor += sizeof(size_t);
        size_t Offset = 0;
        for (int JobI = 0; JobI < JobCount; ++JobI)
        {
            size_t ChunkSize = Jobs[JobI].Output.Size;
            *(size_t *)(Output+Cursor) = ChunkSize;
            Offset += ChunkSize;
            Cursor += sizeof(size_t);
        }
        
        for (int JobI = 0; JobI < JobCount; ++JobI)
        {
            job *Job = Jobs + JobI;
            memcpy(Output+Cursor, Job->Output.Data, Job->Output.Size);
            free(Job->Output.Data);
            Cursor += Job->Output.Size;
        }
    }
    
    free(Jobs);
    
    return {Output, OutputSize};
}

memory DecodeParallel(u8 *Data, size_t DataSize)
{
    size_t *HeaderReader = (size_t *)Data;
    size_t ChunkCount = *HeaderReader++;
    Data += (1+ChunkCount) * sizeof(size_t);
    
    // build a job for each chunk
    job *Jobs = (job *)calloc(ChunkCount, sizeof(job));
    size_t Offset = 0;
    for (int ChunkI = 0; ChunkI < ChunkCount; ++ChunkI)
    {
        size_t ChunkSize = *HeaderReader++;
        
        job *Job = Jobs + ChunkI;
        Job->Input.Data = Data + Offset;
        Job->Input.Size = ChunkSize;
        Offset += ChunkSize;
    }
    
    size_t JobCount = ChunkCount;
    std::atomic<size_t> NextJobIndex = 0;
    
    auto WorkerFunc = [&]() {
        size_t JobIndex = NextJobIndex.fetch_add(1);
        while (JobIndex < JobCount)
        {
            job *Job = Jobs + JobIndex;
            
            memory Decoded = Decode(Job->Input.Data, Job->Input.Size);
            Job->Output.Data = Decoded.Data;
            Job->Output.Size = Decoded.Size;
            
            JobIndex = NextJobIndex.fetch_add(1);
        }
    };
    
    // spin up workers
    int WorkerCount = std::thread::hardware_concurrency() - 1;
    std::thread *Workers = (std::thread *)calloc(WorkerCount, sizeof(std::thread));
    for (int WorkerI = 0; WorkerI < WorkerCount; ++WorkerI)
    {
        Workers[WorkerI] = std::thread(WorkerFunc);
    }
    WorkerFunc();
    
    // block until jobs are done
    for (int WorkerI = 0; WorkerI < WorkerCount; ++WorkerI)
    {
        Workers[WorkerI].join();
    }
    
    // composite decompressed data
    size_t OutputSize = 0;
    u8 *Output = 0;
    {
        OutputSize = 0;
        for (int JobI = 0; JobI < JobCount; ++JobI)
        {
            OutputSize += Jobs[JobI].Output.Size;
        }
        
        Output = (u8 *)calloc(OutputSize, 1);
        size_t Cursor = 0;
        for (int JobI = 0; JobI < JobCount; ++JobI)
        {
            job *Job = Jobs + JobI;
            memcpy(Output+Cursor, Job->Output.Data, Job->Output.Size);
            free(Job->Output.Data);
            Cursor += Job->Output.Size;
        }
    }
    
    free(Jobs);
    
    return {Output, OutputSize};
}
