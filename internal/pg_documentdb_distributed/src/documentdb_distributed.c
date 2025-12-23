/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/documentdb_distributed.c
 *
 * Initialization of the shared library.
 * 共享库的初始化
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <bson.h>
#include <utils/guc.h>
#include <access/xact.h>
#include <utils/version_utils.h>
#include "distributed_hooks.h"
#include "documentdb_distributed_init.h"

extern bool SkipDocumentDBLoad;

PG_MODULE_MAGIC;

void _PG_init(void);
void _PG_fini(void);


/*
 * _PG_init - 扩展加载时的初始化函数
 *
 * 当 PostgreSQL 加载 pg_documentdb_distributed 扩展时，此函数会被调用。
 * 该函数执行以下操作：
 * 1. 检查是否跳过加载（用于测试场景）
 * 2. 验证扩展是否通过 shared_preload_libraries 加载
 * 3. 初始化分布式钩子（hooks）
 * 4. 初始化配置参数
 * 5. 保留 GUC 前缀
 */
void
_PG_init(void)
{
	/* 检查是否跳过 DocumentDB 加载（用于测试） */
	if (SkipDocumentDBLoad)
	{
		return;
	}

	/* 验证必须通过 shared_preload_libraries 加载
	 * pg_documentdb_distributed 扩展需要在 PostgreSQL 启动过程的早期加载，
	 * 因为它需要注册各种钩子和共享内存结构。
	 */
	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"pg_documentdb_distributed can only be loaded via shared_preload_libraries. "
							"Add pg_documentdb_distributed to shared_preload_libraries configuration "
							"variable in postgresql.conf in coordinator and workers. "
							"Note that pg_documentdb_distributed should be placed right after citus and pg_documentdb.")));
	}

	/* 初始化 DocumentDB 分布式钩子
	 * 这些钩子用于拦截和修改查询计划、执行等
	 */
	InitializeDocumentDBDistributedHooks();

	/* 初始化 DocumentDB 分布式配置参数
	 * 注册各种 GUC（Grand Unified Configuration）参数
	 */
	InitDocumentDBDistributedConfigurations("documentdb_distributed");

	/* 保留 documentdb_distributed GUC 前缀
	 * 防止其他扩展使用相同的前缀
	 */
	MarkGUCPrefixReserved("documentdb_distributed");
}


/*
 * _PG_fini - 扩展卸载前的清理函数
 *
 * 当扩展即将重新加载或卸载时，此函数会被调用。
 * 当前实现为空，不需要执行任何清理操作。
 */
void
_PG_fini(void)
{ }
