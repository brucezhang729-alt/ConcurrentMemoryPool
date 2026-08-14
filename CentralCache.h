#pragma once
#include"Common.h"

// 单例模式
class CentralCache
{
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}

	// 获取⼀个⾮空的span 
	Span* GetOneSpan(Spanlist& list, size_t byte_size);


	// 从中⼼缓存获取⼀定数量的对象给thread cache 
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);

	// 将⼀定数量的对象释放到span跨度 
	void ReleaseListToSpans(void* start, size_t byte_size);

private:
	Spanlist _spanLists[NFREE_LIST];

private: 
	CentralCache()
	{ }

	CentralCache(const CentralCache&) = delete;
	static CentralCache _sInst;
};