/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/insert.h
 *
 * 文档插入操作的函数声明
 * 提供插入和插入或替换文档的功能
 *
 *-------------------------------------------------------------------------
 */
#ifndef COMMANDS_INSERT_H
#define COMMANDS_INSERT_H

#include <io/bson_core.h>
#include "commands/commands_common.h"

/* 为插入操作创建集合的函数
 * databaseNameDatum: 数据库名称（Datum 类型）
 * collectionNameDatum: 集合名称（Datum 类型）
 * 返回：MongoCollection 对象指针
 */
MongoCollection * CreateCollectionForInsert(Datum databaseNameDatum,
											Datum collectionNameDatum);
/* 插入文档的函数
 * collectionId: 集合 ID
 * shardTableName: 分片表名
 * shardKeyValue: 分片键值
 * objectId: 文档对象 ID（BSON 格式）
 * document: 要插入的文档内容（BSON 格式）
 * 返回：插入是否成功
 */
bool InsertDocument(uint64 collectionId, const char *shardTableName, int64 shardKeyValue,
					pgbson *objectId, pgbson *document);

/* 插入或替换文档的函数（Upsert 操作）
 * collectionId: 集合 ID
 * shardTableName: 分片表名
 * shardKeyValue: 分片键值
 * objectId: 文档对象 ID（BSON 格式）
 * document: 要插入的文档内容（BSON 格式）
 * updateSpecValue: 更新规范值（用于替换操作）
 * 返回：操作是否成功
 */
bool InsertOrReplaceDocument(uint64 collectionId, const char *shardTableName, int64
							 shardKeyValue,
							 pgbson *objectId, pgbson *document,
							 const bson_value_t *updateSpecValue);
#endif
