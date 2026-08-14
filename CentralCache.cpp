#include"CentralCache.h"
#include"PageCache.h"

CentralCache CentralCache::_sInst;

// 获取⼀个⾮空的span 
Span* CentralCache::GetOneSpan(Spanlist& list,size_t size)
{
	// 先查看当前SpanList中是否还有未分配对象的span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr)
		{
			return it;
		}
		else
		{
			it = it->_next;
		}
	}

	// 先把CentralCahce的桶锁解掉，这样其他线程释放内存，不会阻塞
	list._mtx.unlock();



	// 走到这表示没有非空span,只能去PageCache要
	PageCache::GetInstance()->_pageMtx.lock();
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;
	span->_objSize = size;
	PageCache::GetInstance()->_pageMtx.unlock();

	//对获取的span进行切分，不需要切分，因为此时其他线程访问不到这个span
	// 找页的起始地址
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	// 计算大块内存的大小
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	// 把大块内存切成自由链表连接起来
	// 1.先切一块 ，方便尾插
	span->_freeList = start;
	start += size;
	void* tail = span->_freeList;

	while (start < end)
	{
		NextObj(tail) = start;
		tail = start; 
		start += size;
	}
	// 这里切分完span，需要把span挂到桶里面去的时候，再加上桶锁
	list._mtx.lock();
	list.PushFront(span);
	
	return span;
}

// 从中⼼缓存获取⼀定数量的对象给thread cache 
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock();

	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	// 从span 中获取batchNum个对象
	// 如果不够batchNum个，有多少拿多少
	start = span->_freeList;
	end = start;
	size_t i = 0;
	size_t actualNum = 1;
	while (i < batchNum - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		++i;
		++actualNum;
	}
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	_spanLists[index]._mtx.unlock();

	return actualNum;
}

// 将⼀定数量的对象释放到span跨度 
void CentralCache::ReleaseListToSpans(void* start, size_t byte_size)
{
	size_t index = SizeClass::Index(byte_size);
	_spanLists[index]._mtx.lock();

	while (start)
	{
		void* next = NextObj(start);
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		span->_useCount--;
		// 说明这个span已经没有被threadcache使用了，可以回收到PageCache
		// PageCache可以再尝试去做前后页的合并
		if (span->_useCount == 0)
		{
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			// 释放span给PageCache.使用PageCache的锁就可以了
			// 这时把桶锁解掉
			_spanLists[index]._mtx.lock();

			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();

			_spanLists[index]._mtx.unlock();
		}
		start = next;
	}
	_spanLists[index]._mtx.unlock();



	
}
