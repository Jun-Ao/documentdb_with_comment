/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_query_common.h
 *
 * Private and common declarations of functions for handling bson query
 * Shared across runtime and index implementations.
 * 用于处理 BSON 查询的私有和公共函数声明
 * 在运行时和索引实现之间共享
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_QUERY_COMMON_H
#define BSON_QUERY_COMMON_H

/* 包含 BSON 核心数据结构和操作定义 */
#include "io/bson_core.h"
/* 包含 DocumentDB 错误处理定义 */
#include "utils/documentdb_errors.h"

/*
 * 范围查询参数结构
 *
 * 定义 $gte、$gt、$lte、$lt 等范围查询操作符的参数。
 * 这些操作符用于数值、日期等类型的范围比较。
 */
typedef struct DollarRangeParams
{
	/* 范围查询的最小值（下界） */
	bson_value_t minValue;

	/* 范围查询的最大值（上界） */
	bson_value_t maxValue;

	/* 是否包含最小值（true 表示 >=，false 表示 >） */
	bool isMinInclusive;

	/* 是否包含最大值（true 表示 <=，false 表示 <） */
	bool isMaxInclusive;

	/* 是否需要全表扫描（当无法使用索引时设为 true） */
	bool isFullScan;

	/* 扫描方向：1 表示升序，-1 表示降序 */
	int32_t orderScanDirection;

	/* 是否为 $elemMatch 查询（数组元素匹配） */
	bool isElemMatch;

	/* $elemMatch 查询的匹配值 */
	bson_value_t elemMatchValue;
} DollarRangeParams;

/*
 * ParseQueryDollarRange - 解析查询中的范围操作符
 *
 * 从过滤器元素中解析 $gte、$gt、$lte、$lt 等范围查询操作符，
 * 构建范围查询参数结构。
 *
 * 参数:
 *   filterElement - 过滤器元素（BSON 格式）
 *
 * 返回值:
 *   新分配的范围查询参数结构指针
 *
 * 支持的操作符:
 * - $gt : 大于
 * - $gte : 大于等于
 * - $lt : 小于
 * - $lte : 小于等于
 *
 * 示例查询:
 *   { "age": { $gte: 18, $lt: 65 } }
 *   { "price": { $gt: 100, $lte: 500 } }
 */
DollarRangeParams * ParseQueryDollarRange(pgbsonelement *filterElement);

/*
 * InitializeQueryDollarRange - 初始化查询范围参数
 *
 * 从 BSON 值初始化范围查询参数结构。
 *
 * 参数:
 *   rangeValue - 包含范围查询条件的 BSON 值
 *   params - 输出参数，用于存储初始化的范围参数
 *
 * 初始化内容:
 * - 设置最小值和最大值
 * - 设置包含/排除标志
 * - 确定扫描方向
 * - 判断是否需要全表扫描
 */
void InitializeQueryDollarRange(const bson_value_t *rangeValue,
								DollarRangeParams *params);

#endif
