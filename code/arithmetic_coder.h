#pragma once

#include "ch_buf.h"

internal u8 *
Encode(u8 *In, size_t InSize, size_t *OutSize_Out)
{
    u8 *Out = 0;
    *OutSize_Out = 0;
    
    return Out;
}

internal u8 *
Decode(u8 *In, size_t InSize, size_t *OutSize_Out)
{
    u8 *Out = 0;
    *OutSize_Out = 0;
    
    return Out;
}

//
//
// toy impl using f64

struct range
{
    f64 Min;
    f64 Max;
};

internal f64
Encode64(u8 *In, size_t InSize, range *Model)
{
    //
    //
    // modelling
    
    size_t TotalFreq = 0;
    size_t FreqTable[256] = {};
    for (size_t ByteI = 0; ByteI < InSize; ++ByteI)
    {
        TotalFreq += 1;
        FreqTable[In[ByteI]] += 1;
    }
    
    size_t CumTable[256] = {};
    size_t CumSoFar = 0;
    for (size_t SymbolI = 0; SymbolI < 256; ++SymbolI)
    {
        CumSoFar += FreqTable[SymbolI];
        CumTable[SymbolI] = CumSoFar;
    }
    
    for (size_t SymbolI = 0; SymbolI < 256; ++SymbolI)
    {
        f64 Begin = SymbolI == 0? 0.0: Model[SymbolI-1].Max;
        f64 End = f64(CumTable[SymbolI]) / f64(TotalFreq);
        Model[SymbolI] = {Begin, End};
    }
    
    //
    //
    // coding
    
    f64 Min = 0.0;
    f64 Max = 1.0;
    
    for (size_t ByteI = 0; ByteI < InSize; ++ByteI)
    {
        f64 Range = Max - Min;
        
        range Interval = Model[In[ByteI]];
        Max = Min + Range * Interval.Max;
        Min = Min + Range * Interval.Min;
    }
    
    f64 EncodedValue = (Min + Max) / 2.0;
    return EncodedValue;
}

internal u8 *
Decode64(f64 EncodedValue, size_t DataSize, range *Model)
{
    f64 Min = 0.0;
    f64 Max = 1.0;
    
    u8 *Out = 0;
    for (size_t ByteI = 0; ByteI < DataSize; ++ByteI)
    {
        f64 Percent = (EncodedValue - Min) / (Max - Min);
        
        u8 DecodedSymbol = 0;
        range ChosenInterval = {};
        for (u8 SymbolI = 0; SymbolI < 256; ++SymbolI)
        {
            range Interval = Model[SymbolI];
            if (Percent >= Interval.Min && Percent < Interval.Max)
            {
                DecodedSymbol = SymbolI;
                ChosenInterval = Interval;
                break;
            }
        }
        
        BufPush(Out, DecodedSymbol);
        
        f64 Range = Max - Min;
        Max = Min + ChosenInterval.Max * Range;
        Min = Min + ChosenInterval.Min * Range;
    }
    
    return Out;
}