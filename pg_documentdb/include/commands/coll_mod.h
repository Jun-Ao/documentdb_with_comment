/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/coll_mod.h
 *
 * Exports around the functionality of collmod
 * collMod（集合修改）功能的相关导出
 * collMod 是 MongoDB 中用于修改集合属性的命令
 *
 *-------------------------------------------------------------------------
 */
#ifndef DOCUMENTDB_COLL_MOD_H
#define DOCUMENTDB_COLL_MOD_H

// 索引元数据更新操作类型枚举
// 定义了可以应用于索引元数据的不同操作类型
typedef enum IndexMetadataUpdateOperation
{
	// 未知操作类型（默认值）
	INDEX_METADATA_UPDATE_OPERATION_UNKNOWN = 0,

	// 隐藏索引操作
	// 用于设置索引的隐藏状态，隐藏的索引不会被查询优化器使用
	INDEX_METADATA_UPDATE_OPERATION_HIDDEN = 1,

	// 准备唯一索引操作
	// 用于在创建唯一索引前的准备阶段，标记该索引即将成为唯一索引
	INDEX_METADATA_UPDATE_OPERATION_PREPARE_UNIQUE = 2,

	// 唯一索引操作
	// 用于标记索引为唯一索引，确保索引字段的值在集合中唯一
	INDEX_METADATA_UPDATE_OPERATION_UNIQUE = 3,
} IndexMetadataUpdateOperation;

// 更新 PostgreSQL 索引的核心元数据
// 此函数用于在分布式环境中更新所有分片上的索引元数据
// 参数：
//   collectionId - 集合的唯一标识符
//   indexId - 要更新的索引 ID
//   operation - 要执行的元数据更新操作类型
//   value - 操作的布尔值（例如：hidden=true 表示隐藏索引）
//   ignoreMissingShards - 如果为 true，忽略缺失的分片而不报错
void UpdatePostgresIndexCore(uint64_t collectionId, int indexId,
							 IndexMetadataUpdateOperation operation, bool value, bool
							 ignoreMissingShards);

#endif
