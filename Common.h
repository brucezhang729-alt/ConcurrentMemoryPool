#pragma once
#include <iostream>
#include <vector>
#include <ctime>
#include <algorithm>
#include<assert.h>
#include<thread>
#include<mutex>
#include<Windows.h>
#include<unordered_map>


using std::cout;
using std::endl; 
static const size_t MAX_BYTES = 256 * 1024;
static const size_t NFREE_LIST = 208;
static const size_t NPAGES = 129;
static const size_t PAGE_SHIFT = 13;

#ifdef _WIN32
	typedef size_t PAGE_ID;
#elif _WIN64
	typedef unsigned long long PAGE_ID;
#endif //

	// 直接去堆上按⻚申请空间 
	inline static void* SystemAlloc(size_t kpage)
	{
#ifdef _WIN32
		void* ptr = VirtualAlloc(0, kpage * (1 << 12), MEM_COMMIT | MEM_RESERVE,
			PAGE_READWRITE);
#else
		// linux下brk mmap等 
#endif
		if (ptr == nullptr)
			throw std::bad_alloc();
		return ptr;
	}
	// 释放堆上按⻚申请的空间
	inline static void SystemFree(void* ptr)
	{
#ifdef _WIN32
		VirtualFree(ptr, 0, MEM_RELEASE);
#else
		// sbrk unmmap等 
#endif
	}

//当前文件可见
static void*& NextObj(void* obj)
{ 
	return *(void**)obj;
}
//管理切分好的小对象的自由链表
class FreeList
{
public:
	void Push(void* obj)
	{
		//头插
		assert(obj);
		//*(void**)obj = _freeList;
		NextObj(obj) = _freeList;
		_freeList = obj;
		++_size;
	}

	void PushRange(void* start, void* end,size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;
		_size += n;
	}	

	void PopRange(void*& start, void*& end, size_t n)
	{
		assert(n>=_size);
		assert(_freeList);
		start = _freeList;
		void* cur = _freeList;
		for (size_t i = 1; i < n; ++i)
		{
			cur = NextObj(cur);
		}
		end = cur;
		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;

	}

	void* Pop()
	{
		//头删
		assert(_freeList);
		void* obj = _freeList;
		_freeList = NextObj(obj);
		--_size;
		return obj;
	}
	bool Empty()
	{
		return _freeList == nullptr;
	}

	size_t MaxSize()
	{
		return _maxSize;
	}

	size_t Size()
	{
		return _size;
	}
private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;
	size_t _size = 0;
};

// 计算对象大小的对齐映射规则
class SizeClass
{
public:
	/*size_t _RoundUp(size_t size, size_t AlignNum)
	{
		size_t alignsize;
		if (size % AlignNum != 0)
		{
			alignsize = (size / AlignNum + 1) * 8;
		}
		else
		{
			alignsize = size;
		}
		return alignsize;
	}*/


	static inline size_t _RoundUp(size_t bytes, size_t align)
	{
		return (((bytes)+align - 1) & ~(align - 1));
	}


	static size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			return _RoundUp(size, 8);
		}
		else if (size <= 1024)
		{
			return _RoundUp(size, 16);
		}
		else if (size <= 8 * 1024)
		{
			return _RoundUp(size, 128);
		}
		else if (size <= 64 * 1024)
		{
			return _RoundUp(size, 1024);
		}
		else if (size <= 256 * 1024)
		{
			return _RoundUp(size, 8 * 1024);
		}
		else
		{
			return _RoundUp(size, 1 << 13);
		}
	}
	/*size_t _Index(size_t bytes,size_t alignNum)
	{
		if (bytes % alignNum == 0)
		{
			return bytes / alignNum - 1;
		}
		else
		{
			return bytes / alignNum;
		}
	}*/

	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return (((bytes + (1 << align_shift) - 1) >> align_shift) - 1);
	}
	// 计算映射的哪?个?由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);
		// 每个区间有多少个链 
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128) {
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) {
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 81024) {
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 64 * 1024) {
			return _Index(bytes - 8 * 1024, 10) + group_array[2] +
				group_array[1] + group_array[0];
		}
		else if (bytes <= 256 * 1024) {
			return _Index(bytes - 64 * 1024, 13) + group_array[3] +
				group_array[2] + group_array[1] + group_array[0];
		}
		else {
			assert(false);
		}
		return -1;
	}

	// 一次 ThreadCache 从中心缓存获取多少个
	static size_t NumMoveSize(size_t size)
	{
		if (size == 0)
			return 0;
		// [2, 512]，⼀次批量移动多少个对象的(慢启动)上限值 
		// ⼩对象⼀次批量上限⾼ 
		// ⼩对象⼀次批量上限低 
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		else if (num > 512)
			num = 512;
		return num;
	}
	// 计算⼀次向系统获取⼏个⻚
	// 单个对象 8byte 
	// ...
	// 单个对象 256KB 
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = num * size;
		npage >>= PAGE_SHIFT;
		if (npage == 0)
			npage = 1;
		return npage;
	}
};


// 管理多个连续页大块内存跨度结构
struct Span
{
	size_t _pageId; // 大块内存起始页的页号
	size_t _n;      // 页数量

	Span* _next;	//双向链表结构
	Span* _prev;

	size_t _objSize; //切好小块内存的大小
	size_t _useCount; //切好小块内存，被分配给threadcache的计数器
	void* _freeList; //切好小块内存的自由链表

	bool _isUse; //是否正在使用

};

//带头双向循环链表
class Spanlist
{
public:
	Spanlist()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}
	//迭代器
	Span* Begin()
	{
		return _head->_next;
	}

	Span* End()
	{
		return _head;
	}

	bool Empty()
	{ 
		return _head->_next == _head;
	}

	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}
	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;
		prev->_next = newSpan;
		newSpan->_prev = prev;
		newSpan->_next = pos;
		pos->_prev = newSpan;
	}
	void Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head); //不能是哨兵位
		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;

	}
private:
	Span* _head = nullptr;
public:
	std::mutex _mtx; //桶锁


};