/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/delete.h
 *
 * 单文档删除操作的导出声明
 * 定义了删除操作的参数结构和返回结果
 *
 *-------------------------------------------------------------------------
 */
#ifndef DELETE_H
#define DELETE_H

#include <postgres.h>

#include "collation/collation.h"
#include "metadata/collection.h"


/*
 * 单文档删除操作参数结构体
 * 描述针对单个文档的删除操作参数
 */
typedef struct
{
	/* 删除查询条件（BSON 格式） */
	const bson_value_t *query;

	/* 用于选择单行时的排序顺序 */
	const bson_value_t *sort;

	/* 是否返回被删除的文档 */
	bool returnDeletedDocument;

	/* 如果返回文档时需要返回的字段 */
	const bson_value_t *returnFields;

	/* 解析后的变量规范 */
	const bson_value_t *variableSpec;

	/* 排序规则字符串 */
	const char collationString[MAX_ICU_COLLATION_LENGTH];
} DeleteOneParams;


/*
 * 单行删除结果结构体
 * 反映在单个分片上执行单行删除操作的结果
 */
typedef struct
{
	/* 是否有一行匹配查询并被删除 */
	bool isRowDeleted;

	/* 被删除文档的对象ID（仅在 delete_one 内部使用） */
	pgbson *objectId;

	/* 被删除文档的值（可能已被投影），如果请求且有匹配项 */
	pgbson *resultDeletedDocument;
} DeleteOneResult;


/* 调用删除单个文档的函数
 * 执行实际的删除操作，处理分片键哈希、事务等
 */
void CallDeleteOne(MongoCollection *collection, DeleteOneParams *deleteOneParams,
				   int64 shardKeyHash, text *transactionId, bool forceInlineWrites,
				   DeleteOneResult *result);

#endif
