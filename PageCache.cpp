#include"PageCache.h"


PageCache PageCache::_sInst;

//获取 k 页的span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0 && k < NPAGES);
	// 大于128页的span直接向堆申请
	if (k > NPAGES-1)
	{
		void* ptr = SystemAlloc(k);
		/*Span* span = new Span;*/
		Span* span = _spanPool.New();
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k; 

		_idSpanMap[span->_pageId] = span;

		return span;
	}

	// 先检查第k个桶里面有没有 span
	if (!_spanLists[k].Empty()) 
	{
		return _spanLists->PopFront();
	}
	//检查一下后面的桶是否有空的 span，如果有，可以把span进行切分
	for (size_t i = k + 1; i < NPAGES; i++)
	{
		if(!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			/*Span* kSpan = new Span;*/
			Span* kSpan = _spanPool.New();
			// 在nSpan的头部切下来一个K页
			// k页span返回
			// nSpan在挂到对应映射的位置
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			nSpan->_pageId += k;
			nSpan->_n -= k;

			_spanLists[nSpan->_n].PushFront(nSpan);
			// 存储nSpan的首尾页号跟nSpan映射，方便回收PageCache时，进行合并查找
			_idSpanMap[nSpan->_pageId] = nSpan;
			_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;


			// 建立 id 和 Span的映射关系，方便回收小块内存
			for (PAGE_ID i = 0; i < kSpan->_n; ++i)
			{
				_idSpanMap[kSpan->_pageId + i] = kSpan;
			}


			return kSpan;
		}

	}
	// 走到这个位置就说明后面没有大页的span
	// 这时候就去找堆要一个128页的span
	/*Span* bigSpan = new Span;*/
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES-1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;

	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}

// 获取从对象到span的映射 
Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = ((PAGE_ID)obj >> PAGE_SHIFT);

	std::unique_lock < std::mutex>lock(_pageMtx);

	auto ret = _idSpanMap.find(id);
	if (ret != _idSpanMap.end())
	{
		return ret->second;
	}
	else
	{
		assert(false);
		return nullptr;
	}
}

// 释放空闲span回到Pagecache，并合并相邻的span 
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// 大于128页的span直接向堆释放
	if (span->_n > NPAGES - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT ) ;
		SystemFree(ptr);
		/*delete span;*/
		_spanPool.Delete(span);

		return;
	}
	// 向前合并
	// 对span进行合并，合并完之后挂到对应的桶里面,缓解内存碎片问题
	while (1)
	{
		PAGE_ID prevId = span->_pageId - 1;
		auto ret = _idSpanMap.find(prevId);
		// 前面的页号没有找到，说明前面没有span，直接break
		if (ret == _idSpanMap.end())
		{
			break;
		}
		// 前面的页号找到了，说明前面有span且在使用，不合并
		Span* prevSpan = ret->second;
		if (ret->second->_isUse == true)
		{
			break;
		}
		// 合并大小超过128，不进行合并
		if ((prevSpan->_n + span->_n) > NPAGES - 1)
		{
			break;
		}

		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);
		/*delete prevSpan;*/
		_spanPool.Delete(prevSpan);
	}

	// 向后合并
	while (1)
	{
		PAGE_ID nextId = span->_pageId + span->_n;
		auto ret = _idSpanMap.find(nextId);
		if(ret==_idSpanMap.end())
		{
			break;
		}
		Span* nextSpan = ret->second;
		if (nextSpan->_isUse == true)
		{
			break;
		}
		if (nextSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		span->_n += nextSpan->_n;

		_spanLists[nextSpan->_n].Erase(nextSpan);
		/*delete nextSpan;*/
		_spanPool.Delete(nextSpan);
		
	}
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	_idSpanMap[span->_pageId] = span;
	_idSpanMap[span->_pageId + span->_n-1] = span;
}

