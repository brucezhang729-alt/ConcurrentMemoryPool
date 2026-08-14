#pragma once
#include"Common.h"
#include"ObjectPool.h"

//// 直接去堆上按⻚申请空间 
//inline static void* SystemAlloc(size_t kpage)
//{
//#ifdef _WIN32
//	void* ptr = VirtualAlloc(0, kpage * (1 << 12), MEM_COMMIT | MEM_RESERVE,
//		PAGE_READWRITE);
//#else
//	// linux下brk mmap等 
//#endif
//	if (ptr == nullptr)
//		throw std::bad_alloc();
//	return ptr;
//}




template<class T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;
		// 1. 优先复用自由链表归还回来的内存块
		if (_freeList)
		{
			void* next = *(void**)_freeList;
			obj = (T*)_freeList;
			_freeList = next;
		}
		else
		{
			// 确保每个对象占用的内存至少能放下下一个指针 (32位下4字节，64位下8字节)
			size_t objSize = max(sizeof(T), sizeof(void*));

			// 2. 剩余内存不足以分配一个 objSize 时，向系统申请大块内存
			if (_remainBytes < objSize)
			{
				_remainBytes = 128 * 1024; // 128 KB
				_memory = (char*)malloc(_remainBytes);
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}

			obj = (T*)_memory;
			_memory += objSize;
			_remainBytes -= objSize; // 修复：必须扣除实际分配的 objSize
		}

		// 3. 定位 new (placement new)，显式调用 T 的构造函数初始化对象
		new(obj) T;

		return obj;
	}

	void Delete(T* obj)
	{
		// 显式调用 obj 的析构函数清理对象
		obj->~T();

		// 头插入自由链表 freeList
		*(void**)obj = _freeList;
		_freeList = obj;
	}

private:
	char* _memory = nullptr;      // 指向大块内存的指针
	size_t _remainBytes = 0;      // 大块内存在切分过程中剩余的字节数
	void* _freeList = nullptr;    // 指向回收内存的自由链表头指针
};

//struct TreeNode
//{
//	int _val;
//	TreeNode* _left;
//	TreeNode* _right;
//	TreeNode()
//		: _val(0)
//		, _left(nullptr)
//		, _right(nullptr)
//	{
//	}
//};
//
//void TestObjectPool()
//{
//	// 申请释放的轮次 
//	const size_t Rounds = 3;
//	// 每轮申请释放多少次 
//	const size_t N = 1000000;
//
//	// 测试原生 operator new / delete
//	size_t begin1 = clock();
//	std::vector<TreeNode*> v1;
//	v1.reserve(N);
//	for (size_t j = 0; j < Rounds; ++j)
//	{
//		for (size_t i = 0; i < N; ++i)
//		{
//			v1.push_back(new TreeNode);
//		}
//		for (size_t i = 0; i < N; ++i)
//		{
//			delete v1[i];
//		}
//		v1.clear();
//	}
//	size_t end1 = clock();
//
//	// 测试定长内存池 ObjectPool
//	ObjectPool<TreeNode> TNPool;
//	size_t begin2 = clock();
//	std::vector<TreeNode*> v2;
//	v2.reserve(N);
//	for (size_t j = 0; j < Rounds; ++j)
//	{
//		for (size_t i = 0; i < N; ++i)
//		{
//			v2.push_back(TNPool.New());
//		}
//		for (size_t i = 0; i < N; ++i)
//		{
//			TNPool.Delete(v2[i]);
//		}
//		v2.clear();
//	}
//	size_t end2 = clock();
//
//	cout << "new cost time: " << end1 - begin1 << " ms" << endl;
//	cout << "object pool cost time: " << end2 - begin2 << " ms" << endl;
//}
//
