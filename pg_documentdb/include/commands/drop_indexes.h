
/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/drop_indexes.h
 *
 * 删除索引操作的内部实现
 * ApiSchema.drop_indexes 的函数声明
 *
 *-------------------------------------------------------------------------
 */
#ifndef DROP_INDEXES_H
#define DROP_INDEXES_H

#include "metadata/index.h"

/* 删除 PostgreSQL 索引的函数
 * collectionId: 集合 ID
 * indexId: 索引 ID
 * unique: 是否为唯一索引
 * concurrently: 是否并发执行
 * missingOk: 索引不存在时是否忽略错误
 */
void DropPostgresIndex(uint64 collectionId, int indexId, bool unique,
					   bool concurrently, bool missingOk);
/* 删除带后缀的 PostgreSQL 索引的函数
 * collectionId: 集合 ID
 * index: 索引详情
 * concurrently: 是否并发执行
 * missingOk: 索引不存在时是否忽略错误
 * suffix: 索引后缀
 */
void DropPostgresIndexWithSuffix(uint64 collectionId,
								 IndexDetails *index,
								 bool concurrently,
								 bool missingOk,
								 const char *suffix);

#endif
