#include"ThreadCache.h"
#include"CentralCache.h"
#include"Common.h"

void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);
	size_t alighSize = SizeClass::RoundUp(size);
	size_t index = SizeClass::Index(size);
	if (!_freeLists[index].Empty())
	{
		return _freeLists[index].Pop();
	}
	else
	{
		return FetchFromCentralCache(index, alighSize);
	}
}

void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(size <= MAX_BYTES);
	assert(ptr);
	// 找出映射的自由链表桶，插入
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);
	// 当链表长度大于一次批量申请的内存时就开始还一段list到CentralCache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ListTooLong(_freeLists[index], size);
	}
}

// 释放对象时，链表过⻓时，回收内存回到中⼼缓存 
void ListTooLong(FreeList& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, list.MaxSize());

	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}


void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// 慢开始的调节算法
	// 1.最开始不会一次向CentralCache一次批量太多，考虑到内存消耗
	// 2.如果不需要这个size大小的内存需求，那么batchNum就会不断增长，直到上限
	// 3.size越大，一次向CentralCache要的batchNum就越小
	// 4.size越小，一次向CentralCache要的batchNum就越大
	size_t batchNum = min(SizeClass::NumMoveSize(size), _freeLists[index].MaxSize());
	if (_freeLists[index].MaxSize() == batchNum)
	{
		int MaxSize = _freeLists[index].MaxSize();
		MaxSize += 1;
	}
	void* start = nullptr;
	void* end = nullptr;

	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);

	assert(actualNum >= 1);

	if (actualNum == 1)
	{
		assert(start == end);
		return start;
	}
	else
	{
		_freeLists[index].PushRange(NextObj(start), end,actualNum);
		return start;
	}
	
	return nullptr;
}