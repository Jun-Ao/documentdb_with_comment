/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/planner/documentdb_plan_cache.h
 *
 * pg_documentdb查询计划缓存通用声明
 *
 * 本文件定义了DocumentDB中查询计划缓存的核心接口和常量，
 * 用于缓存和重用SQL查询计划，提高查询性能。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_PLAN_CACHE_H
#define BSON_PLAN_CACHE_H

#include <executor/spi.h>

/*
 * 不同查询类型的ID前缀定义
 *
 * 查询ID的前32位用于标识操作类型，
 * 后32位用于标识不同的查询变体。
 */
/* 插入操作查询ID */
#define QUERY_ID_INSERT (1L << 32)

/* 通过TID更新操作查询ID */
#define QUERY_ID_UPDATE_BY_TID (2L << 32)

/* 通过TID删除操作查询ID */
#define QUERY_ID_DELETE_BY_TID (3L << 32)

/* 待移除：一旦我们完全支持let和collation的删除操作 */
#define QUERY_DELETE_WITH_FILTER (4L << 32);

/* 带分片键的删除过滤查询ID */
#define QUERY_DELETE_WITH_FILTER_SHARDKEY (5L << 32)

/* 带ID的删除过滤查询ID */
#define QUERY_DELETE_WITH_FILTER_ID (6L << 32)

/* 带分片键和ID的删除过滤查询ID */
#define QUERY_DELETE_WITH_FILTER_SHARDKEY_ID (7L << 32)

/* 调用更新一个操作查询ID */
#define QUERY_CALL_UPDATE_ONE (8L << 32)

/* 删除一个操作查询ID */
#define QUERY_DELETE_ONE (9L << 32)

/* 带ID的删除一个操作查询ID */
#define QUERY_DELETE_ONE_ID (10L << 32)

/* 带ID的删除一个操作并返回文档查询ID */
#define QUERY_DELETE_ONE_ID_RETURN_DOCUMENT (11L << 32)

/* 待移除：一旦我们完全支持let和collation的删除操作 */
#define QUERY_UPDATE_SELECT_UPDATE_CANDIDATE (12L << 32)

/* 非ObjectId的更新选择更新候选查询ID */
#define QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_NON_OBJECT_ID (13L << 32)

/* 仅ObjectId的更新选择更新候选查询ID */
#define QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_ONLY_OBJECT_ID (14L << 32)

/* 带双过滤器的更新选择更新候选查询ID */
#define QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_BOTH_FILTER (15L << 32)

/* 重试记录插入查询ID */
#define QUERY_ID_RETRY_RECORD_INSERT (20L << 32)

/* 重试记录删除查询ID */
#define QUERY_ID_RETRY_RECORD_DELETE (21L << 32)

/* 重试记录选择查询ID */
#define QUERY_ID_RETRY_RECORD_SELECT (22L << 32)

/* 插入或替换查询ID（旧版本） */
#define QUERY_ID_INSERT_OR_REPLACE (23L << 32)

/* 插入或替换查询ID（新版本） */
#define QUERY_ID_INSERT_OR_REPLACE_NEW (24L << 32)

/* 带let和collation的删除过滤查询ID */
#define QUERY_DELETE_WITH_FILTER_LET_AND_COLLATION (30L << 32)

/* 带分片键和let和collation的删除过滤查询ID */
#define QUERY_DELETE_WITH_FILTER_SHARDKEY_LET_AND_COLLATION (31L << 32)

/* 带ID和let和collation的删除过滤查询ID */
#define QUERY_DELETE_WITH_FILTER_ID_LET_AND_COLLATION (32L << 32)

/* 带分片键、ID和let和collation的删除过滤查询ID */
#define QUERY_DELETE_WITH_FILTER_SHARDKEY_ID_LET_AND_COLLATION (33L << 32)

/* 带let和collation的删除一个操作查询ID */
#define QUERY_DELETE_ONE_LET_AND_COLLATION (34L << 32)

/* 带ID和let和collation的删除一个操作查询ID */
#define QUERY_DELETE_ONE_ID_LET_AND_COLLATION (35L << 32)

/* 带let和collation的更新选择更新候选查询ID */
#define QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_LET_AND_COLLATION (36L << 32)

/* 非ObjectId和带let和collation的更新选择更新候选查询ID */
#define QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_NON_OBJECT_ID_LET_AND_COLLATION (37L << 32)

/* 带双过滤器和let和collation的更新选择更新候选查询ID */
#define QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_BOTH_FILTER_LET_AND_COLLATION (38L << 32)


/* 更新多个操作的偏移量定义 */
#define QUERY_UPDATE_MANY_SHARD_KEY_QUERY_OFFSET (1L << 32)
#define QUERY_UPDATE_MANY_OBJECTID_QUERY_OFFSET (2L << 32)
#define QUERY_UPDATE_MANY_SHARD_KEY_OBJECT_ID_QUERY_OFFSET \
	QUERY_UPDATE_MANY_SHARD_KEY_QUERY_OFFSET + QUERY_UPDATE_MANY_OBJECTID_QUERY_OFFSET

/* 查询过滤器函数更新多个操作（40L << 32 到 43L << 32） */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION (40L << 32)

/* 带分片键的查询过滤器函数更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION_WITH_SHARD_KEY \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION + \
	QUERY_UPDATE_MANY_SHARD_KEY_QUERY_OFFSET

/* 带ObjectId的查询过滤器函数更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION_WITH_OBJECT_ID \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION + QUERY_UPDATE_MANY_OBJECTID_QUERY_OFFSET

/* 带分片键和ObjectId的查询过滤器函数更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION_WITH_SHARD_KEY_AND_OBJECT_ID \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION + \
	QUERY_UPDATE_MANY_SHARD_KEY_OBJECT_ID_QUERY_OFFSET

/* 查询过滤器操作符更新多个操作（44L << 32 到 47L << 32） */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR (44L << 32)

/* 带分片键的查询过滤器操作符更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR_WITH_SHARD_KEY \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR + \
	QUERY_UPDATE_MANY_SHARD_KEY_QUERY_OFFSET

/* 带ObjectId的查询过滤器操作符更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR_WITH_OBJECT_ID \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR + QUERY_UPDATE_MANY_OBJECTID_QUERY_OFFSET

/* 带分片键和ObjectId的查询过滤器操作符更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR_WITH_SHARD_KEY_AND_OBJECT_ID \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR + \
	QUERY_UPDATE_MANY_SHARD_KEY_OBJECT_ID_QUERY_OFFSET

/* 新更新BSON的偏移量 */
#define QUERY_UPDATE_MANY_WITH_NEW_UPDATE_BSON_OFFSET (8L << 32)

/* 新函数查询过滤器函数更新多个操作（48L << 32 到 51L << 32） */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION_NEW_FUNC \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION + \
	QUERY_UPDATE_MANY_WITH_NEW_UPDATE_BSON_OFFSET

/* 新函数带分片键的查询过滤器函数更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION_WITH_SHARD_KEY_NEW_FUNC \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION_WITH_SHARD_KEY + \
	QUERY_UPDATE_MANY_WITH_NEW_UPDATE_BSON_OFFSET

/* 新函数带ObjectId的查询过滤器函数更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION_WITH_OBJECT_ID_NEW_FUNC \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION_WITH_OBJECT_ID + \
	QUERY_UPDATE_MANY_WITH_NEW_UPDATE_BSON_OFFSET

/* 新函数带分片键和ObjectId的查询过滤器函数更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION_WITH_SHARD_KEY_AND_OBJECT_ID_NEW_FUNC \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_FUNCTION_WITH_SHARD_KEY_AND_OBJECT_ID + \
	QUERY_UPDATE_MANY_WITH_NEW_UPDATE_BSON_OFFSET

/* 新函数查询过滤器操作符更新多个操作（52L << 32 到 55L << 32） */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR_NEW_FUNC \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR + \
	QUERY_UPDATE_MANY_WITH_NEW_UPDATE_BSON_OFFSET

/* 新函数带分片键的查询过滤器操作符更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR_WITH_SHARD_KEY_NEW_FUNC \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR_WITH_SHARD_KEY + \
	QUERY_UPDATE_MANY_WITH_NEW_UPDATE_BSON_OFFSET

/* 新函数带ObjectId的查询过滤器操作符更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR_WITH_OBJECT_ID_NEW_FUNC \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR_WITH_OBJECT_ID + \
	QUERY_UPDATE_MANY_WITH_NEW_UPDATE_BSON_OFFSET

/* 新函数带分片键和ObjectId的查询过滤器操作符更新多个操作 */
#define QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR_WITH_SHARD_KEY_AND_OBJECT_ID_NEW_FUNC \
	QUERY_UPDATE_MANY_WITH_QUERY_FILTER_OPERATOR_WITH_SHARD_KEY_AND_OBJECT_ID + \
	QUERY_UPDATE_MANY_WITH_NEW_UPDATE_BSON_OFFSET


/* 控制查询计划缓存大小的GUC参数 */
extern int QueryPlanCacheSizeLimit;

/*
 * 初始化查询计划缓存
 * 此函数在系统启动时调用，用于初始化查询计划缓存的数据结构和配置
 */
void InitializeQueryPlanCache(void);

/*
 * 获取SPI查询计划
 * 输入参数：
 *   - collectionId: 集合ID
 *   - queryId: 查询ID
 *   - query: 查询字符串
 *   - argTypes: 参数类型数组
 *   - argCount: 参数数量
 * 返回值：SPI查询计划指针
 */
SPIPlanPtr GetSPIQueryPlan(uint64 collectionId, uint64 queryId,
						   const char *query, Oid *argTypes, int argCount);

/*
 * 获取带本地分片的SPI查询计划
 * 输入参数：
 *   - collectionId: 集合ID
 *   - shardTableName: 分片表名称
 *   - queryId: 查询ID
 *   - query: 查询字符串
 *   - argTypes: 参数类型数组
 *   - argCount: 参数数量
 * 返回值：SPI查询计划指针
 */
SPIPlanPtr GetSPIQueryPlanWithLocalShard(uint64 collectionId, const char *shardTableName,
										 uint64 queryId,
										 const char *query, Oid *argTypes, int argCount);

#endif
