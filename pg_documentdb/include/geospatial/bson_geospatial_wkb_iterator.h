/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_wkb_iterator.h
 *
 * WKB缓冲区的自定义迭代器
 *
 * 本文件定义了DocumentDB中用于遍历WKB（Well-Known Binary）缓冲区的迭代器实现，
 * 提供了高效的WKB几何对象遍历和访问功能。
 *
 *-------------------------------------------------------------------------
 */
#ifndef WKBBufferITERATOR_H
#define WKBBufferITERATOR_H

#include "postgres.h"

#include "geospatial/bson_geospatial_private.h"
#include "utils/documentdb_errors.h"

/* WKB缓冲区迭代器结构体 */
typedef struct WKBBufferIterator
{
	/* 指向WKB缓冲区起始位置的指针 */
	const char *headptr;

	/* 迭代过程中当前在缓冲区中的位置指针 */
	char *currptr;

	/* 缓冲区的原始长度 */
	int len;
}WKBBufferIterator;


/*
 * 描述WKB缓冲区中形状的常量指针、长度和类型集合
 * 设计为不可变的常量结构体，避免意外修改缓冲区
 * 通过引用原始缓冲区中的指针来获取单个几何缓冲区，避免了memcpy操作
 *
 * 未来还可以添加其他形状特定的常量属性
 */
typedef struct WKBGeometryConst
{
	/* 形状定义 */
	const WKBGeometryType geometryType;    // 几何类型
	const char *geometryStart;            // 几何数据起始位置
	const int32 length;                    // 几何数据长度

	/* 多边形状态 */
	const char *ringPointsStart;           // 环点数据起始位置
	const int32 numRings;                  // 环数量
	const int32 numPoints;                 // 点数量
} WKBGeometryConst;


/* WKB访问者函数结构体，用于在遍历WKB缓冲区时执行自定义操作 */
typedef struct WKBVisitorFunctions
{
	/*
	 * 遍历WKB缓冲区类型表示的完整几何对象时执行
	 * 可以是原子类型，如点、线、多边形，或多重集合，如多点、几何集合等
	 */
	void (*VisitGeometry)(const WKBGeometryConst *wkbGeometry, void *state);

	/*
	 * 对多重集合几何中的每个单独几何对象执行
	 */
	void (*VisitSingleGeometry)(const WKBGeometryConst *wkbGeometry, void *state);

	/*
	 * 遍历时对每个找到的单个点调用，点可以是任何几何的一部分，如线、多边形环、多点等
	 */
	void (*VisitEachPoint)(const WKBGeometryConst *wkbGeometry, void *state);

	/* 目前仅在多边形验证期间调用，以检查每个环的有效性 */
	void (*VisitPolygonRing)(const WKBGeometryConst *wkbGeometry, void *state);

	/*
	 * 是否继续遍历WKB缓冲区，可用于停止遍历
	 */
	bool (*ContinueTraversal)(void *state);
} WKBVisitorFunctions;


/* 从给定的WKB缓冲区StringInfo初始化WKBBufferIterator */
static inline void
InitIteratorFromWKBBuffer(WKBBufferIterator *iter, StringInfo wkbBuffer)
{
	iter->headptr = wkbBuffer->data;

	/* 将currptr也设置为wkbBuffer->data，因为我们从头开始 */
	iter->currptr = wkbBuffer->data;

	iter->len = wkbBuffer->len;
}


/* 从给定的char指针和长度初始化WKBBufferIterator */
static inline void
InitIteratorFromPtrAndLen(WKBBufferIterator *iter, const char *currptr, int32 len)
{
	iter->headptr = currptr;

	/* 将currptr也设置为wkbBuffer->data，因为我们从头开始 */
	iter->currptr = (char *) currptr;

	iter->len = len;
}


/* 工具函数：按给定字节数递增迭代器中的当前指针 */
static inline void
IncrementWKBBufferIteratorByNBytes(WKBBufferIterator *iter, size_t bytes)
{
	size_t remainingLength = (size_t) iter->len - (iter->currptr - iter->headptr);
	if (remainingLength >= bytes)
	{
		iter->currptr += bytes;
	}
	else
	{
		size_t overflow = bytes - remainingLength;
		ereport(ERROR, (
					errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
					errmsg(
						"Requested to increment WKB buffer %ld bytes beyond limit.",
						overflow),
					errdetail_log(
						"Requested to increment WKB buffer %ld bytes beyond limit.",
						overflow)));
	}
}


/* 遍历WKB缓冲区
 * 输入参数：
 *   - wkbBuffer: WKB缓冲区
 *   - visitorFuncs: 访问者函数集合
 *   - state: 访问者状态
 */
void TraverseWKBBuffer(const StringInfo wkbBuffer, const
					   WKBVisitorFunctions *visitorFuncs, void *state);

/* 遍历WKB字节流
 * 输入参数：
 *   - wkbBytea: WKB字节流
 *   - visitorFuncs: 访问者函数集合
 *   - state: 访问者状态
 */
void TraverseWKBBytea(const bytea *wkbBytea, const WKBVisitorFunctions *visitorFuncs,
					  void *state);

#endif
