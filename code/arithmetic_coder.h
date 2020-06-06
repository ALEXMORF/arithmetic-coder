#pragma once

#include <stdlib.h>

/*NOTE(chen):

precision boundary calculation:

High = Low + (Range * IntervalMax) / Scale - 1;
        Low = Low + (Range * IntervalMin) / Scale;
        
Range bits = [CodeBits-2, CodeBits] (no closer than 1/4 invariant)

High and Low must never cross, that means the following must be true:

Range/Scale >= 1, given that min diff between Interval MinMax is 1.

Worst case, Range has bit count of CodeBits-2, 
Scale must not be greather than that value. Therefore:

Scale Bits <= CodeBits - 2

*/

#define CODE_BIT_COUNT 17
#define SCALE_BIT_COUNT 15

#pragma pack(push, 1)
struct header
{
    u32 CumProb[256];
    size_t EncodedByteCount;
};
#pragma pack(pop)

struct encoder
{
    u32 CumProb[256];
    
    u8 *OutputStream;
    size_t OutputCap;
    size_t OutputSize;
    u8 StagingByte;
    int BitsFilled;
    
    void Model(u8 *Data, size_t DataSize);
    __forceinline void OutputBit(u8 Bit);
    u8 *Encode(u8 *Data, size_t DataSize);
};

void
encoder::Model(u8 *Data, size_t DataSize)
{
    u32 Scale = (1 << SCALE_BIT_COUNT) - 1;
    
    size_t FreqTable[256] = {};
    for (size_t ByteI = 0; ByteI < DataSize; ++ByteI)
    {
        FreqTable[Data[ByteI]] += 1;
    }
    
    size_t CumTable[256] = {};
    for (int SymbolI = 0; SymbolI < 256; ++SymbolI)
    {
        size_t Cum = SymbolI > 0? CumTable[SymbolI-1]: 0;
        CumTable[SymbolI] = FreqTable[SymbolI] + Cum;
    }
    
    u32 Prob[256] = {};
    for (int SymbolI = 0; SymbolI < 256; ++SymbolI)
    {
        Prob[SymbolI] = u32((FreqTable[SymbolI] * Scale) / DataSize);
        
        if (FreqTable[SymbolI] != 0 && Prob[SymbolI] == 0)
        {
            Prob[SymbolI] = 1;
        }
    }
    
    CumProb[256] = {};
    for (int SymbolI = 0; SymbolI < 256; ++SymbolI)
    {
        u32 Cum = SymbolI == 0? 0: CumProb[SymbolI-1];
        CumProb[SymbolI] = Cum + Prob[SymbolI];
    }
}

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

u8 *
encoder::Encode(u8 *Data, size_t DataSize)
{
    Model(Data, DataSize);
    
    header Header = {};
    for (int I = 0; I < 256; ++I)
    {
        Header.CumProb[I] = CumProb[I];
    }
    Header.EncodedByteCount = DataSize;
    
    OutputCap = sizeof(Header);
    OutputStream = (u8 *)calloc(OutputCap, 1);
    *((header *)OutputStream) = Header;
    OutputSize = OutputCap;
    
    u32 Scale = (1 << SCALE_BIT_COUNT) - 1;
    u32 CodeBitMask = (1 << CODE_BIT_COUNT) - 1;
    u32 MsbBitMask = (1 << (CODE_BIT_COUNT-1));
    u32 SecondMsbBitMask = (1 << (CODE_BIT_COUNT-2));
    u32 Half = CodeBitMask >> 1;
    
    u32 Low = 0;
    u32 High = CodeBitMask;
    size_t BitsPending = 0;
    for (size_t ByteI = 0; ByteI < DataSize; ++ByteI)
    {
        u8 Symbol = Data[ByteI];
        u32 IntervalMin = Symbol == 0? 0: CumProb[Symbol-1];
        u32 IntervalMax = CumProb[Symbol];
        
        ASSERT(Low < High);
        u32 Range = High - Low;
        High = Low + (Range * IntervalMax) / Scale;
        Low = Low + (Range * IntervalMin) / Scale;
        ASSERT(Low < High);
        
        for (;;)
        {
            if (Low > Half || High <= Half) // same MSB
            {
                u32 MSB = High & MsbBitMask;
                Low = (Low << 1) & CodeBitMask;
                High = ((High << 1) | 1) & CodeBitMask;
                
                u8 FirstBit = MSB? 1: 0;
                u8 PendingBit = FirstBit? 0: 1;
                OutputBit(FirstBit);
                for (size_t I = 0; I < BitsPending; ++I)
                {
                    OutputBit(PendingBit);
                }
                BitsPending = 0;
            }
            else if ((Low & SecondMsbBitMask) && ((~High) & SecondMsbBitMask)) // same second MSB
            {
                BitsPending += 1;
                
                Low <<= 1;
                Low &= ~MsbBitMask;
                High = (High << 1) | 1;
                High |= MsbBitMask;
                
                Low &= CodeBitMask;
                High &= CodeBitMask;
            }
            else
            {
                break;
            }
        }
    }
    
    //TODO(chen): push out remaining bytes ?????
    
    return OutputStream;
}

u8 *Decode(u8 *Bits, size_t EncodedSize)
{
    u8 *Output = 0;
    
    header *Header = (header *)Bits;
    Bits += sizeof(header);
    
    
    
    return Output;
}