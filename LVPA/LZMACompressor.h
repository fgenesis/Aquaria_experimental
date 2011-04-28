
#ifndef _LZMACOMPRESSOR_H
#define _LZMACOMPRESSOR_H

#include "ICompressor.h"


class LZMACompressor : public ICompressor
{
public:
    virtual void Compress(uint32 level = 1, ProgressCallback pcb = NULL);
    virtual void Decompress(void);
};


#endif
