#pragma once
#include <stdint.h>
#include <stdlib.h>

struct buf_hdr
{
    uint64_t Cap;
    uint64_t Count;
};

#define BufHdr(Array) ((buf_hdr *)Array - 1)
#define BufCount(Array) BufHdr(Array)->Count
#define BufLast(Array) (Array)[BufCount(Array)-1]
#define BufPush(Array, Elmt) ((Array) = (decltype(Array))__BufExtend(Array, sizeof(Elmt)), (Array)[BufCount(Array)-1] = Elmt)
#define BufInit(Count, Type) (Type *)__BufInit(Count, sizeof(Type))

static void
BufFree(void *Buf)
{
    if (!Buf) return;
    free(BufHdr(Buf));
}

static void *
__BufInit(uint64_t Count, size_t ElmtSize)
{
    void *HdrBuf = malloc(sizeof(buf_hdr) + Count * ElmtSize);
    buf_hdr *Hdr = (buf_hdr *)HdrBuf;
    Hdr->Count = Count;
    Hdr->Cap = Count;
    
    void *Buf = Hdr + 1;
    return Buf;
}

static void *
__BufExtend(void *Buf, size_t ElmtSize)
{
    void *Result = 0;
    
    if (Buf == 0)
    {
        buf_hdr Hdr = {};
        Hdr.Cap = 2;
        Hdr.Count = 1;
        
        buf_hdr *BufWithHdr = (buf_hdr *)malloc(sizeof(Hdr) + Hdr.Cap * ElmtSize);
        *BufWithHdr = Hdr;
        Result = BufWithHdr + 1;
    }
    else
    {
        buf_hdr *Hdr = BufHdr(Buf);
        Hdr->Count += 1;
        
        if (Hdr->Count > Hdr->Cap)
        {
            uint64_t NewCap = (uint64_t)((f32)Hdr->Cap * 1.5f) + 1;
            Hdr->Cap = NewCap;
            buf_hdr *BufWithHdr = (buf_hdr *)realloc(Hdr, sizeof(buf_hdr) + NewCap * ElmtSize);
            Result = BufWithHdr + 1;
        }
        else
        {
            Result = Buf;
        }
    }
    
    return Result;
}