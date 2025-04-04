#include <iostream>
#include <unistd.h>
#include <sys/mman.h>

// todo: use a diff struct for memory allocated via mmap to avoid unncessary fields

class SbrkMemoryAllocator
{
private:
    struct MemoryBlock
    {
        size_t size;
        bool isFree;
        bool isMmapAllocated;
        MemoryBlock* next; // next blk
        MemoryBlock* nextFree; // next blk in free list
    };

    constexpr static size_t MIN_PAYLOAD_SIZE = 8;
    constexpr static size_t MIN_USEABLE_SIZE = sizeof(MemoryBlock)+MIN_PAYLOAD_SIZE;

    constexpr static size_t MMAP_THRESHOLD = 128*1024; // 128KB

    MemoryBlock* freeListHead = nullptr; // only free blocks
    MemoryBlock* blockListHead = nullptr; // all blocks


public:
    void* malloc(size_t size)
    {
        if (size >= MMAP_THRESHOLD)
        {
            size_t totalSize = size+sizeof(MemoryBlock);
            void* mem = mmap(nullptr, totalSize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
            if (mem == MAP_FAILED) return nullptr;
            MemoryBlock* block = initialiseBlock(mem, size, true);
            return reinterpret_cast<void*>(block+1);
        }

        MemoryBlock* freeBlock = findFreeBloc(size);
        if (freeBlock)
        {
            if (shouldSplitBlock(freeBlock, size)) splitBlock(freeBlock, size);
            freeBlock->isFree = false;
            removeFromFreeList(freeBlock);
            // user should not have access to metadata of the memory block; possible overwriting metadata
            return reinterpret_cast<void*>(freeBlock+1);
        }

        void* mem = sbrk(size+sizeof(MemoryBlock));
        if (mem == reinterpret_cast<void*>(static_cast<std::intptr_t>(-1))) return nullptr;

        MemoryBlock* block = initialiseBlock(mem, size, false);
        appendToBlockList(block);

        return reinterpret_cast<void*>(block+1);
    }

    void free(void* ptr)
    {
        if (!ptr) return;
        auto* block = reinterpret_cast<MemoryBlock*>(static_cast<char*>(ptr) - sizeof(MemoryBlock));

        if (block->isMmapAllocated)
        {
            munmap(block, block->size + sizeof(MemoryBlock));
            return;
        }
        block->isFree = true;
        addToFreeList(block);
        mergeContigousFreeBlocks();
    }

private:
    MemoryBlock* initialiseBlock(void* mem, size_t size, bool isMmapAllocated)
    {
        auto* block = reinterpret_cast<MemoryBlock*>(mem);
        block->size = size;
        block->isFree = false;
        block->isMmapAllocated = isMmapAllocated;
        block->next = nullptr;
        block->nextFree = nullptr;
        return block;
    }
    void appendToBlockList(MemoryBlock* block)
    {
        if (!blockListHead)
        {
            blockListHead = block;
            return;
        }
        MemoryBlock* curr = blockListHead;
        while (curr->next)
        {
            curr = curr->next;
        }
        curr->next = block;
    }

    void addToFreeList(MemoryBlock* block)
    {
        block->nextFree = freeListHead;
        freeListHead = block;
    }

    void removeFromFreeList(MemoryBlock* block)
    {
        if (!freeListHead) return;

        if (freeListHead == block)
        {
            freeListHead = freeListHead->next;
            return;
        }

        MemoryBlock* curr = freeListHead;
        while (curr->nextFree && curr->nextFree != block)
        {
            curr = curr->nextFree;
        }

        if (curr->nextFree == block)
        {
            curr->nextFree = block->nextFree;
        }
    }

    // first fit algorithm
    MemoryBlock* findFreeBloc(size_t size)
    {
        MemoryBlock* curr = freeListHead;
        while (curr)
        {
            if (curr->isFree && curr->size >= size) return curr;
            curr = curr->nextFree;
        }
        return nullptr;
    }

    bool shouldSplitBlock(MemoryBlock* block, size_t size)
    {
        return block->size >= size+MIN_USEABLE_SIZE;
    }

    void splitBlock(MemoryBlock* block, size_t size)
    {
        auto* payloadStart = reinterpret_cast<char*>(block+1);
        auto* newBlock = reinterpret_cast<MemoryBlock*>(payloadStart+size);

        newBlock->size = block->size - size - sizeof(MemoryBlock);
        newBlock->isFree = true;
        newBlock->next = block->next;
        addToFreeList(newBlock);

        block->size = size;
        block->next = newBlock;
    }

    void mergeContigousFreeBlocks()
    {
        MemoryBlock* curr = blockListHead;
        while (curr && curr->nextFree)
        {
            MemoryBlock* next = curr->nextFree;
            if (curr->isFree && next->isFree)
            {
                // ensure that both blocks are contigous. note: bound is not inclusive
                auto* currBlockBound = reinterpret_cast<char*>(curr+1) + curr->size;
                if (currBlockBound == reinterpret_cast<char*>(next))
                {
                    // just to maintain correctness of block metadata, the memory is already allocated
                    curr->size += sizeof(MemoryBlock)+next->size;
                    curr->next = next->next;
                    removeFromFreeList(next);
                    continue;
                }
            curr = curr->next;
            }
        }
    }
};



int main()
{
    SbrkMemoryAllocator allocator;

    char* buffer = static_cast<char*>(allocator.malloc(256));
    memcpy(buffer, "Testing allocator!", 19);
    std::cout << "Buffer: " << buffer << std::endl;
    allocator.free(static_cast<void*>(buffer));
    char* buffer2 = static_cast<char*>(allocator.malloc(32));
    memcpy(buffer2, "Test2!", 7);
    std::cout << "Buffer2: " << buffer2 << std::endl;

    // we should still see some of original buffer since we did not overwrite most of it, simply freed
    std::cout << "Buffer: " << buffer+12 << std::endl;


    constexpr size_t LARGE_ALLOC = 512 * 1024;  // 512 KB, triggers mmap
    char* bigBuffer = static_cast<char*>(allocator.malloc(LARGE_ALLOC));
    if (bigBuffer) {
        strcpy(bigBuffer, "This is mmap memory!");
        std::cout << "BigBuffer (mmap): " << bigBuffer << std::endl;
        allocator.free(bigBuffer);  // should trigger munmap
    } else {
        std::cerr << "Failed to allocate large mmap block!" << std::endl;
    }
    return 0;
}
