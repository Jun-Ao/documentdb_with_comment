/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_sorted_accumulator.h
 *
 * Common declarations related to custom aggregates for group by
 * accumulator with sort specification:
 * first (implicit)
 * last (implicit)
 * top (explicit)
 * bottom (explicit)
 * firstN (implicit)
 * lastN (implicit)
 * topN (explicit)
 * bottomN (explicit)
 *-------------------------------------------------------------------------
 */
#ifndef BSON_SORTED_ACCUMULATOR_H
#define BSON_SORTED_ACCUMULATOR_H
#include "io/bson_core.h"

/* --------------------------------------------------------- */
/* 数据类型定义 */
/* --------------------------------------------------------- */

typedef struct BsonOrderAggValue
{
	/* 实际返回的聚合结果值 */
	pgbson *value;

	/* 排序键值数组
	 * 通过对输入文档应用排序规范计算得出。
	 * 这些值用于文档排序。
	 * 最多允许 32 个排序键。
	 */
	Datum sortKeyValues[32];
} BsonOrderAggValue;

/*
 * 有序聚合状态结构
 *
 * 用于 $first、$last、$top、$bottom 等需要排序的聚合累加器。
 */
typedef struct BsonOrderAggState
{
	/* 聚合结果值数组
	 * 包含 numAggValues 个元素。
	 * 这些值将作为聚合结果返回。
	 */
	BsonOrderAggValue **currentResult;

	/* 聚合结果应返回的值数量
	 * first/last 返回 1 个值
	 * firstN/lastN/topN/bottomN 返回 N 个值
	 */
	int64 numAggValues;

	/* 当前结果中的值数量 */
	int64 currentCount;

	/* 排序键数量（最多 32 个） */
	int numSortKeys;

	/* 排序方向数组
	 * true 表示升序（ASC），false 表示降序（DESC）
	 * 数组长度为 numSortKeys
	 */
	bool sortDirections[32];

	/* 输入表达式规范
	 * 将对每个排序的 pgbson 值应用此表达式。
	 */
	pgbson *inputExpression;
} BsonOrderAggState;

/* Handles serialization of state - 处理状态序列化 */
bytea * SerializeOrderState(BsonOrderAggState *state);

/*
 * DeserializeOrderState - 反序列化有序聚合状态
 *
 * 从字节数组中恢复有序聚合状态对象。
 *
 * 参数:
 *   bytes - 序列化的字节数组
 *   state - 输出参数，用于存储恢复的状态对象
 */
void DeserializeOrderState(bytea *bytes,
						   BsonOrderAggState *state);

/*
 * BsonOrderTransition - 有序聚合的转移函数
 *
 * PostgreSQL 聚合累加器的转移函数，处理每个输入值。
 *
 * 参数:
 *   PG_FUNCTION_ARGS - PostgreSQL 标准宏，包含 fcinfo 等参数
 *   invertSort - false 表示升序，true 表示降序
 *   isSingle - false 表示有 N 参数（firstN/lastN），true 表示无 N 参数（first/last）
 *   storeInputExpression - 是否存储输入表达式
 *
 * 返回值:
 *   更新后的聚合状态
 */
Datum BsonOrderTransition(PG_FUNCTION_ARGS, bool invertSort, bool isSingle, bool
						  storeInputExpression);

/*
 * BsonOrderTransitionOnSorted - 已排序输入的转移函数
 *
 * 当输入已经排序时使用的优化版转移函数。
 *
 * 参数:
 *   PG_FUNCTION_ARGS - PostgreSQL 标准宏
 *   invertSort - false 表示升序，true 表示降序
 *   isSingle - false 表示有 N 参数，true 表示无 N 参数
 *
 * 返回值:
 *   更新后的聚合状态
 */
Datum BsonOrderTransitionOnSorted(PG_FUNCTION_ARGS, bool invertSort, bool isSingle);

/*
 * BsonOrderCombine - 有序聚合的合并函数
 *
 * PostgreSQL 聚合累加器的合并函数，用于合并多个分片的结果。
 *
 * 参数:
 *   PG_FUNCTION_ARGS - PostgreSQL 标准宏
 *   invertSort - false 表示升序，true 表示降序
 *
 * 返回值:
 *   合并后的聚合状态
 */
Datum BsonOrderCombine(PG_FUNCTION_ARGS, bool invertSort);

/*
 * BsonOrderFinal - 有序聚合的最终函数
 *
 * PostgreSQL 聚合累加器的最终函数，生成最终结果。
 *
 * 参数:
 *   PG_FUNCTION_ARGS - PostgreSQL 标准宏
 *   isSingle - false 表示有 N 参数，true 表示无 N 参数
 *   invert - 排序方向标志
 *
 * 返回值:
 *   最终的聚合结果（BSON 格式）
 */
Datum BsonOrderFinal(PG_FUNCTION_ARGS, bool isSingle, bool invert);

/*
 * BsonOrderFinalOnSorted - 已排序输入的最终函数
 *
 * 当输入已经排序时使用的优化版最终函数。
 *
 * 参数:
 *   PG_FUNCTION_ARGS - PostgreSQL 标准宏
 *   isSingle - false 表示有 N 参数，true 表示无 N 参数
 *
 * 返回值:
 *   最终的聚合结果（BSON 格式）
 */
Datum BsonOrderFinalOnSorted(PG_FUNCTION_ARGS, bool isSingle);

#endif
