/* Copyright 2012 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#ifndef Allocator_h
#define Allocator_h

#include "BaseUtil.h"

// Base class for allocators that can be provided to Vec class
// (and potentially others). Needed because e.g. in crash handler
// we want to use Vec but not use standard malloc()/free() functions
class Allocator {
public:
    Allocator() {}
    virtual ~Allocator() { };
    virtual void *Alloc(size_t size) = 0;
    virtual void *Realloc(void *mem, size_t size) = 0;
    virtual void Free(void *mem) = 0;

    // helper functions that fallback to malloc()/free() if allocator is NULL
    // helps write clients where allocator is optional
    static void *Alloc(Allocator *a, size_t size) {
        if (!a)
            return malloc(size);
        return a->Alloc(size);
    }

    static void Free(Allocator *a, void *p) {
        if (!a)
            free(p);
        else
            a->Free(p);
    }

    static void* Realloc(Allocator *a, void *mem, size_t size) {
        if (!a)
            return realloc(mem, size);
        return a->Realloc(mem, size);
    }

    static void *Dup(Allocator *a, void *mem, size_t size, size_t padding=0) {
        void *newMem = Allocator::Alloc(a, size + padding);
        if (newMem)
            memcpy(newMem, mem, size);
        return newMem;
    }
};

static inline size_t RoundUpTo8(size_t n)
{
    return ((n+8-1)/8)*8;
}

// PoolAllocator is for the cases where we need to allocate pieces of memory
// that are meant to be freed together. It simplifies the callers (only need
// to track this object and not all allocated pieces). Allocation and freeing
// is faster. The downside is that free() is a no-op i.e. it can't free memory
// for re-use.
//
// Note: we could be a bit more clever here by allocating data in 4K chunks
// via VirtualAlloc() etc. instead of malloc(), which would lower the overhead
class PoolAllocator : public Allocator {

    // we'll allocate block of the minBlockSize unless
    // asked for a block of bigger size
    size_t  minBlockSize;

    struct MemBlockNode {
        struct MemBlockNode *next;
        size_t               size;
        size_t               free;

        size_t               Used() { return size - free; }

        // data follows here
    };

    char *          currMem;
    MemBlockNode *  currBlock;
    MemBlockNode *  firstBlock;

    void Init() {
        currBlock = NULL;
        firstBlock = NULL;
        currMem = NULL;
    }

public:

    PoolAllocator()  {
        minBlockSize = 4096;
        Init();
    }

    void SetMinBlockSize(size_t newMinBlockSize) {
        CrashIf(currBlock); // can only be changed before first allocation
        minBlockSize = newMinBlockSize;
    }

    void FreeAll() {
        MemBlockNode *curr = firstBlock;
        while (curr) {
            MemBlockNode *next = curr->next;
            free(curr);
            curr = next;
        }
        Init();
    }

    virtual ~PoolAllocator() {
        FreeAll();
    }

    void AllocBlock(size_t minSize) {
        minSize = RoundUpTo8(minSize);
        size_t size = minBlockSize;
        if (minSize > size)
            size = minSize;
        MemBlockNode *node = (MemBlockNode*)calloc(1, sizeof(MemBlockNode) + size);
        if (!firstBlock)
            firstBlock = node;
        currMem = (char*)node + sizeof(MemBlockNode);
        node->size = size;
        node->free = size;
        currBlock->next = node;
        currBlock = node;
    }

    // Allocator methods
    virtual void *Realloc(void *mem, size_t size) {
        // TODO: we can't do that because we don't know the original
        // size of memory piece pointed by mem. We can remember it
        // within the block that we allocate
        CrashAlwaysIf(true);
        return NULL;
    }

    virtual void Free(void *mem) {
        // does nothing, we can't free individual pieces of memory
    }

    virtual void *Alloc(size_t size) {
        size = RoundUpTo8(size);
        if (!currBlock || (currBlock->free < size))
            AllocBlock(size);

        void *mem = (void*)currMem;
        currMem += size;
        currBlock->free -= size;
        return mem;
    }

    // assuming allocated memory was for pieces of uniform size,
    // find the address of n-th piece
    void *FindNthPieceOfSize(size_t size, size_t n) {
        size = RoundUpTo8(size);
        MemBlockNode *curr = firstBlock;
        while (curr) {
            size_t piecesInBlock = curr->Used() / n;
            if (piecesInBlock > n) {
                char *p = (char*)curr + sizeof(MemBlockNode) + (n * size);
                    return (void*)p;
            }
            n -= piecesInBlock;
            curr = curr->next;
        }
        return NULL;
    }

    template <typename T>
    T *GetAtPtr(size_t idx) {
        void *mem = FindNthPieceOfSize(sizeof(T), idx);
        return reinterpret_cast<T*>(mem);
    }

    // only valid for structs, could alloc objects with
    // placement new()
    template <typename T>
    T *AllocStruct() {
        return (T *)Alloc(sizeof(T));
    }
};

// A helper for allocating an array of elements of type T
// either on stack (if they fit within StackBufInBytes)
// or in memory. Allocating on stack is a perf optimization
// note: not the best name
template <typename T, int StackBufInBytes>
class FixedArray {
    T stackBuf[StackBufInBytes / sizeof(T)];
    T *memBuf;
public:
    FixedArray(size_t elCount) {
        memBuf = NULL;
        size_t stackEls = StackBufInBytes / sizeof(T);
        if (elCount > stackEls)
            memBuf = (T*)malloc(elCount * sizeof(T));
    }

    ~FixedArray() {
        free(memBuf);
    }

    T *Get() {
        if (memBuf)
            return memBuf;
        return &(stackBuf[0]);
    }
};

#endif
