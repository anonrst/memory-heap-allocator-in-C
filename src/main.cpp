#include <iostream>
#include "allocator.h"
#include "garbage_collector.h"

// Example Class to showcase Allocator and GC
class MyClass {
public:
	int myint;

	MyClass(int myint): myint(myint) {
		std::cout << "My Class Constructor Called" << std::endl;
		std::cout << "My Class ID = " << myint << std::endl;
	}

	~MyClass(){
		std::cout << "My Class Destructor Called" << std::endl;
		std::cout << "My Class ID = " << myint << std::endl;
	}

	void foo() {
		std::cout << "My Class Function Called" << std::endl;
		std::cout << "My Class ID = " << myint << std::endl;
	}

};

int main(){

	
	// By Default, DEBUG_MODE = false. To enable debug logs,cchange it to ture
	bool DEBUG_MODE = true;
	Allocator& alloc = Allocator::getInstance(DEBUG_MODE);
	// By Default, GC_ENABLED = true. 
	alloc.GC_ENABLED = true;

	// If you want to manually invoke GC, you can get the instance of GC from the allocator.
	Garbage_Collector& gc = alloc.getGC();

	// Let me create an array of size 3 with MyClass Objects
	MyClass** arr = (MyClass**)alloc.allocate(3 * sizeof(MyClass*), (void**)&arr);

	for (int i = 0; i < 3; i++) {
		// The first argument of allocate_new is the stack variable reference which provides root for GC
		// If provided, the GC will not collect the area referenced by the stack variable during garbage collection
		// You can decide to not provide it by setting the 1st argument = nullptr. As long as the parent node is referencing the heap, the gc will not collect any of the child nodes
		int myObjId = i;
		arr[i] = alloc.allocate_new<MyClass>(nullptr, myObjId);
	}
	/*
	OUTPUT:
	My Class Constructor Called
	My Class ID = 0

	My Class Constructor Called
	My Class ID = 1

	My Class Constructor Called
	My Class ID = 2
	*/

	// Let me invoke the foo() function
	for (int i = 0; i < 3; i++) {
		arr[i]->foo();
	}
	/*
	OUTPUT:
	My Class Function Called
	My Class ID = 0

	My Class Function Called
	My Class ID = 1

	My Class Function Called
	My Class ID = 2
	*/

	// Let me look at snippet of heap and Only works if DEBUG_MODE = true
	alloc.heap_dump();		

	// Now if want to assign the memory to variable, you can do so using the 'assign' function
	// Assign function ensures that the garbage collector will not pick up the objects during the collection process

	MyClass* ptr = alloc.assign(&ptr, arr[2]);

	// Now if me set the arr to nullptr or some other variable, it will not collect the area pointed by object
	arr = nullptr;
	// Let us manually invoke the GC
	gc.gc_collect();
	alloc.heap_dump();		// Only works if DEBUG_MODE = true
	
	alloc.free_ptr<MyClass>(ptr);
	/*
	My Class Destructor Called
	My Class ID = 2
	*/

	alloc.heap_dump();		// Only works if DEBUG_MODE = true
	/*
	Chunks:
		Chunk at: 0x559db2cbd400, Size: 156 bytes, Free, gc_mark : UNMARKED, Next: 0, Prev: 0
	*/


}