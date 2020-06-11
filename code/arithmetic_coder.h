#pragma once

#include <stdlib.h>
#include <stdio.h>

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

#define CODE_BIT_COUNT 16
#define SCALE_BIT_COUNT 14
#define MODEL_ORDER 16

#pragma pack(push, 1)
struct header
{
    size_t EncodedByteCount;
};
#pragma pack(pop)

struct interval
{
    u8 Symbol;
    
    u32 Min;
    u32 Max;
};

struct model
{
    u32 CumProb[1<<(1*MODEL_ORDER)][2];
    int Context;
    
    __forceinline void Init();
    __forceinline void Update(u8 Symbol);
    __forceinline interval GetInterval(u32 Prob);
    
    __forceinline size_t GetContextSize();
};

struct encoder
{
    u8 *OutputStream;
    size_t OutputCap;
    size_t OutputSize;
    u8 StagingByte;
    int BitsFilled;
    
    __forceinline void OutputBit(u8 Bit);
    u8 *Encode(u8 *Data, size_t DataSize);
};

struct decoder
{
    u8 *InputStream;
    u8 StagingByte;
    size_t BytesRead;
    int BitsLeft;
    u8 BitMask;
    
    __forceinline u8 InputBit();
    u8 *Decode(u8 *Data, size_t DataSize);
};

__forceinline void 
encoder::OutputBit(u8 Bit)
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
    for (int ContextI = 0; ContextI < GetContextSize(); ++ContextI)
    {
        for (int I = 0; I < 2; ++I)
        {
            u32 Sum = I == 0? 0: CumProb[ContextI][I-1];
            CumProb[ContextI][I] = Sum + 1;
        }
    }
    
    Context = 0;
}

__forceinline void
model::Update(u8 Symbol)
{
    for (int I = Symbol; I < 2; ++I)
    {
        CumProb[Context][I] += 1;
    }
    
    u32 Scale = (1 << SCALE_BIT_COUNT) - 1;
    
    //NOTE(chen): if bits exceed, rescale by 1/2
    if (CumProb[Context][1] >= Scale)
    {
        u32 Prob[2] = {};
        for (int I = 0; I < 2; ++I)
        {
            u32 PrevCum = I == 0? 0: CumProb[Context][I-1];
            Prob[I] = CumProb[Context][I] - PrevCum;
            
            bool IsNonZero = Prob[I] != 0;
            Prob[I] /= 2;
            if (IsNonZero && Prob[I] == 0)
            {
                Prob[I] = 1;
            }
        }
        
        for (int I = 0; I < 2; ++I)
        {
            u32 PrevCum = I == 0? 0: CumProb[Context][I-1];
            CumProb[Context][I] = PrevCum + Prob[I];
        }
    }
    ASSERT(CumProb[Context][1] < Scale);
    
    Context = ((Context << 1) | Symbol) % GetContextSize();
}

__forceinline interval 
model::GetInterval(u32 Prob)
{
    interval Result = {};
    
    for (int I = 0; I < 2; ++I)
    {
        u32 Min = I == 0? 0: CumProb[Context][I-1];
        u32 Max = CumProb[Context][I];
        if (Prob >= Min && Prob < Max)
        {
            Result.Min = Min;
            Result.Max = Max;
            Result.Symbol = u8(I);
            return Result;
        }
    }
    
    ASSERT(!"TODO(chen): unreachable");
    return Result;
}

__forceinline size_t
model::GetContextSize()
{
    return 1<<(1*MODEL_ORDER);
}

u8 *
encoder::Encode(u8 *Data, size_t DataSize)
{
    header Header = {};
    Header.EncodedByteCount = DataSize;
    
    model *Model = (model *)calloc(1, sizeof(model));
    Model->Init();
    
    OutputCap = sizeof(Header);
    OutputStream = (u8 *)calloc(OutputCap, 1);
    *((header *)OutputStream) = Header;
    OutputSize = OutputCap;
    
    u32 CodeBitMask = (1 << CODE_BIT_COUNT) - 1;
    u32 MsbBitMask = (1 << (CODE_BIT_COUNT-1));
    u32 SecondMsbBitMask = (1 << (CODE_BIT_COUNT-2));
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
            
            u32 IntervalMin = Symbol == 0? 0: Model->CumProb[Model->Context][Symbol-1];
            u32 IntervalMax = Model->CumProb[Model->Context][Symbol];
            ASSERT(IntervalMin < IntervalMax);
            
            ASSERT(Low < High);
            u32 Range = High - Low + 1;
            u32 Scale = Model->CumProb[Model->Context][1];
            High = Low + (Range * IntervalMax) / Scale - 1;
            Low = Low + (Range * IntervalMin) / Scale;
            ASSERT(Low <= High);
            
            for (;;)
            {
                if (Low >= Half || High < Half) // same MSB
                {
                    u8 FirstBit = (High & MsbBitMask)? 1: 0;
                    OutputBit(FirstBit);
                    
                    u8 PendingBit = !FirstBit;
                    for (size_t I = 0; I < BitsPending; ++I)
                    {
                        OutputBit(PendingBit);
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
            
            Model->Update(Symbol);
        }
    }
    
    BitsPending += 1;
    if (Low < OneFourth)
    {
        OutputBit(0);
        for (size_t I = 0; I < BitsPending; ++I)
        {
            OutputBit(1);
        }
    }
    else
    {
        OutputBit(1);
        for (size_t I = 0; I < BitsPending; ++I)
        {
            OutputBit(0);
        }
    }
    BitsPending = 0;
    
    //NOTE(chen): make sure our last byte flushes
    if (BitsFilled != 0)
    {
        int PadBits = 8 - BitsFilled;
        for (int BitI = 0; BitI < PadBits; ++BitI)
        {
            OutputBit(0);
        }
        ASSERT(BitsFilled == 0);
    }
    
    free(Model);
    
    return OutputStream;
}

__forceinline u8
decoder::InputBit()
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

u8 *
decoder::Decode(u8 *Bits, size_t EncodedSize)
{
    header *Header = (header *)Bits;
    Bits += sizeof(header);
    InputStream = Bits;
    
    model *Model = (model *)calloc(1, sizeof(model));
    Model->Init();
    
    u8 *Output = (u8 *)calloc(Header->EncodedByteCount, 1);
    
    u32 CodeBitMask = (1 << CODE_BIT_COUNT) - 1;
    u32 MsbBitMask = (1 << (CODE_BIT_COUNT-1));
    u32 SecondMsbBitMask = (1 << (CODE_BIT_COUNT-2));
    u32 Half = MsbBitMask;
    u32 OneFourth = Half >> 1;
    u32 ThreeFourths = OneFourth * 3;
    
    u32 Low = 0;
    u32 High = CodeBitMask;
    
    u32 EncodedValue = 0;
    for (int BitI = 0; BitI < CODE_BIT_COUNT; ++BitI)
    {
        EncodedValue = (EncodedValue << 1) | InputBit();
    }
    
    for (size_t ByteI = 0; ByteI < Header->EncodedByteCount; ++ByteI)
    {
        u8 OutputByte = 0;
        
        for (int BitI = 0; BitI < 8; ++BitI)
        {
            u32 Range = High - Low + 1;
            u32 Scale = Model->CumProb[Model->Context][1];
            //TODO(chen): wtf?
            u32 Prob = ((EncodedValue - Low + 1) * Scale - 1) / Range;
            
            interval Interval = Model->GetInterval(Prob);
            
            ASSERT(Low < High);
            High = Low + (Range * Interval.Max) / Scale - 1;
            Low = Low + (Range * Interval.Min) / Scale;
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
                EncodedValue = (EncodedValue << 1) + InputBit();
            }
            ASSERT(EncodedValue <= High);
            
            u8 DecodedSymbol = Interval.Symbol;
            OutputByte |= DecodedSymbol << (7-BitI);
            
            Model->Update(DecodedSymbol);
        }
        
        Output[ByteI] = OutputByte;
    }
    
    free(Model);
    
    return Output;
}

