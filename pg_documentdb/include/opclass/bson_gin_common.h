/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/opclass/bson_gin_common.h
 *
 * BSON GIN索引方法的通用声明。
 * 本文件定义了BSON GIN索引使用的通用数据结构和函数，
 * 包括索引策略枚举、操作符映射等核心定义。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GIN_COMMON_H
#define BSON_GIN_COMMON_H

/*
 * Maps the set of operators for the gin index as strategies that are used in
 * gin operator functions.
 */  /* 将GIN索引的操作符集合映射为策略，用于GIN操作符函数中 */
typedef enum BsonIndexStrategy
{
	BSON_INDEX_STRATEGY_INVALID = 0,  /* 无效索引策略 */
	BSON_INDEX_STRATEGY_DOLLAR_EQUAL = 1,  /* 美元相等策略 - $操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_GREATER = 2,  /* 美元大于策略 - $gt操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL = 3,  /* 美元大于等于策略 - $gte操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_LESS = 4,  /* 美元小于策略 - $lt操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL = 5,  /* 美元小于等于策略 - $lte操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_IN = 6,  /* 美元IN策略 - $in操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_NOT_EQUAL = 7,  /* 美元不等策略 - $ne操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_NOT_IN = 8,  /* 美元NOT IN策略 - $nin操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_REGEX = 9,  /* 美元正则策略 - $regex操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_EXISTS = 10,  /* 美元存在策略 - $exists操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_SIZE = 11,  /* 美元大小策略 - $size操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_TYPE = 12,  /* 美元类型策略 - $type操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_ALL = 13,  /* 美元ALL策略 - $all操作符 */
	BSON_INDEX_STRATEGY_UNIQUE_EQUAL = 14,  /* 唯一相等策略 - 唯一索引约束 */
	BSON_INDEX_STRATEGY_DOLLAR_BITS_ALL_CLEAR = 15,  /* 美元位全清除策略 - $bitsAllClear操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_CLEAR = 16,  /* 美元位任意清除策略 - $bitsAnyClear操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_ELEMMATCH = 17,  /* 美元元素匹配策略 - $elemMatch操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_BITS_ALL_SET = 18,  /* 美元位全设置策略 - $bitsAllSet操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_SET = 19,  /* 美元位任意设置策略 - $bitsAnySet操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_MOD = 20,  /* 美元取模策略 - $mod操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_ORDERBY = 21,  /* 美元排序策略 - $orderby操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_TEXT = 22,  /* 美元文本策略 - $text操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_GEOWITHIN = 23,  /* 美元地理内策略 - $geoWithin操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_GEOINTERSECTS = 24,  /* 美元地理相交策略 - $geoIntersects操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_RANGE = 25,  /* 美元范围策略 - $range操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_NOT_GT = 26,  /* 美元不大于策略 - $not gt操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_NOT_GTE = 27,  /* 美元不大于等于策略 - $not gte操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_NOT_LT = 28,  /* 美元不小于策略 - $not lt操作符 */
	BSON_INDEX_STRATEGY_DOLLAR_NOT_LTE = 29,  /* 美元不小于等于策略 - $not lte操作符 */
	BSON_INDEX_STRATEGY_GEONEAR = 30,  /* 地理最近邻策略 - 地理空间查询 */
	BSON_INDEX_STRATEGY_GEONEAR_RANGE = 31,  /* 地理最近邻范围策略 - 地理范围查询 */
	BSON_INDEX_STRATEGY_COMPOSITE_QUERY = 32,  /* 复合查询策略 - 多条件查询 */
	BSON_INDEX_STRATEGY_IS_MULTIKEY = 33,  /* 多键策略 - 数组索引 */
	BSON_INDEX_STRATEGY_DOLLAR_ORDERBY_REVERSE = 34,  /* 美元反向排序策略 - 反向排序 */
	BSON_INDEX_STRATEGY_HAS_TRUNCATED_TERMS = 35,  /* 有截断术语策略 - 分词处理 */
} BsonIndexStrategy;  /* BSON索引策略枚举 - 定义GIN索引支持的所有查询策略 */


inline static bool
IsNegationStrategy(BsonIndexStrategy strategy)
{
	return (strategy == BSON_INDEX_STRATEGY_DOLLAR_NOT_EQUAL ||
			strategy == BSON_INDEX_STRATEGY_DOLLAR_NOT_IN ||
			strategy == BSON_INDEX_STRATEGY_DOLLAR_NOT_GT ||
			strategy == BSON_INDEX_STRATEGY_DOLLAR_NOT_GTE ||
			strategy == BSON_INDEX_STRATEGY_DOLLAR_NOT_LT ||
			strategy == BSON_INDEX_STRATEGY_DOLLAR_NOT_LTE);
}  /* 检查是否为否定策略 - 判断是否为否定类型的查询操作符 */


#endif
