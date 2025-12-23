/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/documentdb_distributed_init.c
 *
 * Initialization of the shared library initialization for distribution for Hleio API.
 * 用于分布式 API 的共享库初始化
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/guc.h>

#include "documentdb_distributed_init.h"


/* --------------------------------------------------------- */
/* GUCs and default values */
/* GUC（Grand Unified Configuration）参数和默认值 */
/* --------------------------------------------------------- */

/* 元数据引用表同步功能的默认启用状态 */
#define DEFAULT_ENABLE_METADATA_REFERENCE_SYNC true
bool EnableMetadataReferenceTableSync = DEFAULT_ENABLE_METADATA_REFERENCE_SYNC;

/* 分片重新平衡器 API 的默认启用状态 */
#define DEFAULT_ENABLE_SHARD_REBALANCER false
bool EnableShardRebalancer = DEFAULT_ENABLE_SHARD_REBALANCER;

/* 集群管理员角色的默认值 */
#define DEFAULT_CLUSTER_ADMIN_ROLE ""
char *ClusterAdminRole = DEFAULT_CLUSTER_ADMIN_ROLE;

/* 移动集合功能的默认启用状态 */
#define DEFAULT_ENABLE_MOVE_COLLECTION true
bool EnableMoveCollection = DEFAULT_ENABLE_MOVE_COLLECTION;

/* --------------------------------------------------------- */
/* Top level exports */
/* 顶层导出函数 */
/* --------------------------------------------------------- */

/*
 * InitDocumentDBDistributedConfigurations - 初始化 DocumentDB 分布式配置
 * @prefix: 配置参数的前缀（通常是 "documentdb_distributed"）
 *
 * 该函数初始化所有 DocumentDB 分布式相关的 GUC 配置参数。
 * 这些参数可以在 postgresql.conf 中配置，也可以在运行时通过 SET 命令修改。
 *
 * 配置参数说明：
 * 1. enable_metadata_reference_table_sync: 启用/禁用元数据引用表同步
 * 2. enable_shard_rebalancer_apis: 启用/禁用分片重新平衡器 API
 * 3. enable_move_collection: 启用/禁用移动集合功能
 * 4. clusterAdminRole: 集群管理员角色名称
 */
void
InitDocumentDBDistributedConfigurations(const char *prefix)
{
	/* 定义 enable_metadata_reference_table_sync 配置参数
	 * 该参数控制是否启用元数据引用表的自动同步
	 */
	DefineCustomBoolVariable(
		psprintf("%s.enable_metadata_reference_table_sync", prefix),
		gettext_noop(
			"Determines whether or not to enable metadata reference table syncs."),
		NULL, &EnableMetadataReferenceTableSync, DEFAULT_ENABLE_METADATA_REFERENCE_SYNC,
		PGC_USERSET, 0, NULL, NULL, NULL);

	/* 定义 enable_shard_rebalancer_apis 配置参数
	 * 该参数控制是否启用分片重新平衡器相关的 API
	 */
	DefineCustomBoolVariable(
		psprintf("%s.enable_shard_rebalancer_apis", prefix),
		gettext_noop(
			"Determines whether or not to enable shard rebalancer APIs."),
		NULL, &EnableShardRebalancer, DEFAULT_ENABLE_SHARD_REBALANCER,
		PGC_USERSET, 0, NULL, NULL, NULL);

	/* 定义 enable_move_collection 配置参数
	 * 该参数控制是否启用移动集合的功能
	 */
	DefineCustomBoolVariable(
		psprintf("%s.enable_move_collection", prefix),
		gettext_noop(
			"Determines whether or not to enable move collection."),
		NULL, &EnableMoveCollection, DEFAULT_ENABLE_MOVE_COLLECTION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	/* 定义 clusterAdminRole 配置参数
	 * 该参数指定集群管理员的 PostgreSQL 角色名称
	 */
	DefineCustomStringVariable(
		psprintf("%s.clusterAdminRole", prefix),
		gettext_noop(
			"The cluster admin role."),
		NULL, &ClusterAdminRole, DEFAULT_CLUSTER_ADMIN_ROLE,
		PGC_USERSET, 0, NULL, NULL, NULL);
}
