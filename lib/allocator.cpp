#include "allocator.h"
#include <iostream>
#include <cassert>
#include <cstddef>
#include <string>

#include "chunk_metadata.h"
#include "bst_node.h"
#include <iomanip>
#include <garbage_collector.h>

#define LBR '\n'


Allocator::Allocator(bool debug_mode):DEBUG_MODE(debug_mode), gc(NULL)
{       
    out << "INITILIZATING NODE POOL.." << LBR;
    log_info();

    this->node_index = 0;
    this->node_pool = static_cast<BST_Node*>(sbrk(MAX_NODES * sizeof(BST_Node)));
    node_used = static_cast<bool*>(sbrk(MAX_NODES * sizeof(bool)));

    if (node_pool == (void*)-1 || node_used == (void*)-1) {
        std::cerr << "Failed to initialize node pool" << LBR;
        exit(1);
    }

    std::fill(node_used, node_used + MAX_NODES, false);
    

    out << "INITILIZATING HEAP.. " <<LBR;
    log_info();
    out << "INITIAL HEAP CAPACITY " << INITIAL_HEAP_CAPACITY << LBR;
    log_info();
    out << "Chunk Metadata Size : " << sizeof(Chunk_Metadata) << LBR;
    log_info();
    heap_start = sbrk(INITIAL_HEAP_CAPACITY);
    if (heap_start == (void*)-1) {
        std::cerr << "Failed to allocate initial heap space" << LBR;
        exit(1);
    }

    HEAP_CAPACITY = INITIAL_HEAP_CAPACITY;
    used_heap_size = 0;

    out<<"Heap initialized at heap_start : " << heap_start << " with capacity of " << HEAP_CAPACITY << LBR;
    log_info();
    gc = &Garbage_Collector::getInstance(heap_start, HEAP_CAPACITY, debug_mode);
}

Allocator& Allocator::getInstance(bool debug_mode)
{    
    static Allocator instance(debug_mode); // Static instance created only once
    return instance;
}

// Private API called by Public allocate() API to prevent users from disabling gc_collect_flag
void* Allocator::allocate(std::size_t size, bool gc_collect_flag)
{
    if (DEBUG_MODE) std::cout << "\n\n\n" << LBR;

    out << "Received Allocation Request for " << size << LBR;
    log_info();

    if (size <= 0) {
        return nullptr;
    }
    
    if (used_heap_size + size + sizeof(Chunk_Metadata) >= HEAP_CAPACITY) {
        out << "Heap Size not sufficient: used_heap_size + size + sizeof(Chunk_Metadata) >= HEAP_CAPACITY " << used_heap_size + size + sizeof(Chunk_Metadata) << LBR;
        log_info();

        // If there is no free space, then call the collect method in garbage collector
        if (gc_collect_flag){
            out << "Calling Garbage Collector to collect free space" << LBR;
            log_info();
            gc->gc_collect();
            return allocate(size, false);
        }

        // If there is still no space after gc collect then expand memory
        if (expand_heap(size + sizeof(Chunk_Metadata)) != 0) {
            // If OS does not provide more memory -> Throw error
            std::cerr << "Error: HEAP OVERFLOW" << LBR;
            exit(1);
        }
    }

    // first chunk entry in heap    
    if (used_heap_size == 0) {
        out << "Creating first chunk " << LBR;
        log_info();

        Chunk_Metadata* metadata = reinterpret_cast<Chunk_Metadata*>(heap_start);
        metadata->chunk_size = size;
        metadata->next = nullptr;
        metadata->prev = nullptr;
        metadata->is_free = false;

        // Update the used_heap_size to include metadata and the requested chunk
        used_heap_size = sizeof(Chunk_Metadata) + size;

        // Return the pointer to the start of the chunk's data (after metadata)
        void* chunk_ptr = reinterpret_cast<void*>(
            reinterpret_cast<char*>(heap_start) + sizeof(Chunk_Metadata)
        );

        allocated_chunks_root = insert_in_bst(allocated_chunks_root, chunk_ptr, size);

        return chunk_ptr;
    }

    // now if the used_heap_size is not 0
    // the look for free chunks in the heap first 
    // linear search the heap from heap_start to heap_start + used_heap_size 
    // to iterate, retrieve back the heap metadata from heap_start and do while(metadata_ptr->next != NULL)
    // check if the is_free flag is true and if the chunk_size >= size
    // we need to implement 'best-fit' stratergy
    // so iterate though all the chunks and find the least free chunk with chunk_size >= size
    // if the chunk_size == size then retrieve back the chunk pointer from metdatadata's currentChunk() function
    // if the chunk_size > size, check if the chunk_size - size > sizeof(Chunk_Metadata) 
    // if it is greater then create a new chunk immediatly after the chunk and insert the metadata as follows
    // let the is_free = true for that
    // let chunk_size = previous chunk_size - new chunk_size - sizeof(Chunk_Metadata)
    // handle the next and prev pointers of chunk metadata
    // If no chunk was found, append the metadata to the end of chunk and return the currentChunk() ptr

    Chunk_Metadata* best_fit = nullptr;
    Chunk_Metadata* last_chunk = reinterpret_cast<Chunk_Metadata*>(heap_start);
    Chunk_Metadata* current = reinterpret_cast<Chunk_Metadata*>(heap_start);

    // Linear search through the allocated chunks
 
    while (current!=nullptr && reinterpret_cast<char*>(current) < reinterpret_cast<char*>(heap_start) + used_heap_size) {
        if (current->is_free && current->chunk_size >= size) {
            if (!best_fit || current->chunk_size < best_fit->chunk_size) {
                best_fit = current; // Update best fit
            }
        }
        last_chunk = current;
        current = current->next; // Move to the next chunk
    }

    // If a suitable free chunk was found
    if (best_fit) {
        out << "Best fit Found" << LBR
            << " best_fit->chunk_size=" << best_fit->chunk_size << LBR
            << " requested chunk_size=" << size << LBR;
        log_info();

        // If the chunk size exactly matches the requested size
        if (best_fit->chunk_size == size) {
            out << "Perfect Fit Found" << LBR;
            log_info();
            best_fit->is_free = false; // Mark as allocated
            void* chunk_ptr = best_fit->currentChunk();
            allocated_chunks_root = insert_in_bst(allocated_chunks_root, chunk_ptr, size);
            return chunk_ptr;
        }

        // If the chunk size is larger than the requested size, split the chunk
        if (best_fit->chunk_size > size) {
            int remaining_size = best_fit->chunk_size - size - sizeof(Chunk_Metadata);

            out << "Imperfect Fit Found" << LBR
                << " remaining_size=" << remaining_size << LBR
                << " sizeof(Chunk_Metadata)=" << sizeof(Chunk_Metadata) << LBR;
            log_info();

            // Ensure the remaining chunk is large enough to hold metadata
            if (remaining_size > 0) {
                // Create a new chunk immediately after the best fit chunk
                out << "Request for new chunk creation" << LBR;
                log_info();

                Chunk_Metadata* new_chunk = reinterpret_cast<Chunk_Metadata*>(
                    reinterpret_cast<char*>(best_fit) + sizeof(Chunk_Metadata) + size
                );

                new_chunk->chunk_size = remaining_size;
                new_chunk->is_free = true;

                new_chunk->next = best_fit->next; 
                new_chunk->prev = best_fit; 
                best_fit->next = new_chunk; 

                if (new_chunk->next) {
                    new_chunk->next->prev = new_chunk; 
                }

                best_fit->chunk_size = size;

                out << "New chunk created at " << new_chunk << LBR
                    << " is_free=" << new_chunk->is_free << LBR
                    << " chunk_size=" << new_chunk->chunk_size << LBR
                    << " new_chunk->next=" << new_chunk->next << LBR
                    << " new_chunk->prev=" << new_chunk->prev << LBR;
                log_info();                
            }
        }

        best_fit->is_free = false; 
        void* chunk_ptr = best_fit->currentChunk();
        allocated_chunks_root = insert_in_bst(allocated_chunks_root, chunk_ptr, size);

        out << "Best Fit chunk at " << best_fit << LBR
            << " best_fit->is_free=" << best_fit->is_free << LBR
            << " best_fit->chunk_size=" << best_fit->chunk_size << LBR
            << " best_fit->next=" << best_fit->next << LBR
            << " best_fit->prev=" << best_fit->prev << LBR;
        log_info();

        return chunk_ptr; 
    }

    // If no suitable free chunk was found, append to the end
    Chunk_Metadata* new_chunk = reinterpret_cast<Chunk_Metadata*>(
        reinterpret_cast<char*>(heap_start) + used_heap_size
    );

    new_chunk->chunk_size = size;
    new_chunk->is_free = false; 
    new_chunk->next = nullptr;
    new_chunk->prev = last_chunk;
    last_chunk->next = new_chunk;
   
    used_heap_size += sizeof(Chunk_Metadata) + size;
    void* chunk_ptr = new_chunk->currentChunk();
    allocated_chunks_root = insert_in_bst(allocated_chunks_root, chunk_ptr, size);

    return chunk_ptr;
}

void* Allocator::allocate(std::size_t size, void** root)
{
    if (root != NULL) {
        out << "Allocate request -> root = " << root << LBR;
        log_info();
        *root = allocate(size, GC_ENABLED);
        gc->add_gc_roots(root);
        return *root;
    }
    return allocate(size, GC_ENABLED);
}
void Allocator::log_info()
{
    if(DEBUG_MODE){
        std::string str = out.str();
        std::cout << "[INFO]    " << str << LBR;
    }
    out.str(""); // Clear out the contents after logging
    out.clear();
}
