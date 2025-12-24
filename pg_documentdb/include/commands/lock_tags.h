/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/lock_tags.h
 *
 * 文档数据库咨询锁标签常量定义
 * DocumentDBAdvisoryLockField4 下定义的常量用于获取特定于 documentdb_api 的咨询锁时，
 * 作为 "LOCKTAG" 结构体的 "locktag_field4" 字段使用。
 *
 * 在 PostgreSQL 中，"locktag_field4" 字段用于区分不同类型的咨询锁。
 * 1 和 2 被 PostgreSQL 使用，4-12 被 Citus 使用；因此从 100 开始是一个足够好的选择，
 * 不会与 documentdb_api 应该兼容的其他已知咨询锁产生冲突。
 *
 * 参见 Citus 中的 AdvisoryLocktagClass 定义（citus/src/include/distributed/resource_lock.h）
 *
 *-------------------------------------------------------------------------
 */
#ifndef LOCK_TAGS_H
#define LOCK_TAGS_H

#include <c.h>

/* 文档数据库咨询锁字段4枚举定义 */
typedef enum
{
	/* 用于获取创建索引的排他性咨询锁 */
	LT_FIELD4_EXCL_CREATE_INDEXES = 100,

	/* 用于索引构建进行中的锁标签 */
	LT_FIELD4_IN_PROG_INDEX_BUILD,

	/* 用于后台创建索引的排他性会话锁 */
	LT_FIELD4_EXCL_CREATE_INDEX_BACKGROUND
} DocumentDBAdvisoryLockField4;

#endif
