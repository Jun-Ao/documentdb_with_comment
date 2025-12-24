/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/update.h
 *
 * Exports related to implementation of a single-document update.
 * 单文档更新的相关导出接口定义
 *
 *-------------------------------------------------------------------------
 */
#ifndef UPDATE_H
#define UPDATE_H

#include <postgres.h>

#include "metadata/collection.h"
#include "operators/bson_expr_eval.h"


/*
 * UpdateReturnValue specifies whether an update should return
 * no document, the old document, or the new document.
 * 更新返回值枚举，定义更新操作应该返回何种文档
 * UPDATE_RETURNS_NONE - 不返回文档
 * UPDATE_RETURNS_OLD - 返回更新前的旧文档
 * UPDATE_RETURNS_NEW - 返回更新后的新文档
 */
typedef enum
{
	UPDATE_RETURNS_NONE,
	UPDATE_RETURNS_OLD,
	UPDATE_RETURNS_NEW
} UpdateReturnValue;

/*
 * UpdateOneParams describes update operation for a single document.
 * 单文档更新操作参数结构体，定义更新操作的具体参数
 * query - 只更新符合此查询条件的文档
 * update - 要应用的更新操作符
 * isUpsert - 如果没有匹配文档，是否使用 upsert 插入新文档
 * sort - 当选择 1 行时使用的排序方式
 * returnDocument - 是否返回文档以及返回何种文档
 * returnFields - 如果返回文档，指定要返回的字段
 * arrayFilters - 更新操作中指定的数组过滤器
 * bypassDocumentValidation - 是否绕过文档验证
 * variableSpec - 解析后的变量规范
 */
typedef struct
{
	/* update only documents matching this query */
	const bson_value_t *query;

	/* apply this update */
	const bson_value_t *update;

	/* whether to use upsert if no documents match */
	int isUpsert;

	/* sort order to use when selecting 1 row */
	const bson_value_t *sort;

	/* whether to return a document */
	UpdateReturnValue returnDocument;

	/* fields to return if returning a document */
	const bson_value_t *returnFields;

	/* array filters specified in the update */
	const bson_value_t *arrayFilters;

	/* whether to bypass document validation */
	bool bypassDocumentValidation;

	/* parsed variable spec */
	const bson_value_t *variableSpec;
} UpdateOneParams;


/*
 * UpdateOneResult reflects the result of a single-row update
 * on a single shard, which may be a delete.
 * 单行更新结果结构体，记录在单个分片上执行单行更新的结果
 * isRowUpdated - 是否有一行匹配查询并被更新
 * updateSkipped - 是否找到文档但未受更新规范影响
 * isRetry - 更新结果是否来自重试记录
 * reinsertDocument - 分片键值改变，需要重新插入的文档
 * resultDocument - 请求的（可能经过投影的）原始或新文档
 * upsertedObjectId - 插入文档的 ID（如果是 upsert 操作）
 */
typedef struct
{
	/* whether one row matched the query and was updated */
	bool isRowUpdated;

	/* whether we found a document but it was not affected by the update spec */
	bool updateSkipped;

	/* update result came from a retry record */
	bool isRetry;

	/* shard key value changed, reinsertDocument document needs to be inserted */
	pgbson *reinsertDocument;

	/*
	 * Value of the (maybe projected) original or new  document, if requested
	 * and matched any.
	 */
	pgbson *resultDocument;

	/* upserted document ID */
	pgbson *upsertedObjectId;
} UpdateOneResult;


/*
 * UpdateOne - 执行单文档更新操作
 * @collection: 目标集合
 * @updateOneParams: 更新参数
 * @shardKeyHash: 分片键哈希值
 * @transactionId: 事务ID
 * @result: 更新结果输出
 * @forceInlineWrites: 是否强制内联写入
 * @state: 表达式求值状态
 * 执行单个文档的更新操作，支持 upsert、数组过滤等特性
 */
void UpdateOne(MongoCollection *collection, UpdateOneParams *updateOneParams,
			   int64 shardKeyHash, text *transactionId, UpdateOneResult *result,
			   bool forceInlineWrites, ExprEvalState *state);

#endif
