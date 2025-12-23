/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/distribution/cluster_operations.c
 *
 * Implementation of a set of cluster operations (e.g. upgrade, initialization, etc).
 *-------------------------------------------------------------------------
 * 集群操作实现
 *
 * 本文件实现了 DocumentDB 分布式集群的初始化、升级和配置管理功能。
 *
 * 主要功能：
 * 1. 集群初始化（command_initialize_cluster）：首次部署时设置分布式环境
 * 2. 集群升级（command_complete_upgrade）：执行扩展版本升级
 * 3. 元数据表管理：创建和配置分布式参考表
 * 4. 版本管理：跟踪集群和扩展的版本信息
 * 5. 权限管理：配置只读角色和管理员角色权限
 *
 * 核心概念：
 * - 集群版本：记录集群的部署和升级版本
 * - 参考表（Reference Table）：在所有工作节点上复制的小型元数据表
 * - 分布式函数：可以下推到工作节点执行的函数
 * - 版本兼容性：处理混合版本环境下的架构变更
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/resowner.h"
#include "lib/stringinfo.h"
#include "access/xact.h"
#include "utils/typcache.h"
#include "parser/parse_type.h"
#include "nodes/makefuncs.h"

#include "utils/documentdb_errors.h"
#include "metadata/collection.h"
#include "metadata/index.h"
#include "metadata/metadata_cache.h"
#include "utils/guc_utils.h"
#include "utils/lsyscache.h"
#include "utils/query_utils.h"
#include "utils/version_utils.h"
#include "utils/error_utils.h"
#include "utils/version_utils_private.h"
#include "utils/data_table_utils.h"
#include "api_hooks.h"

extern char *ApiExtensionName;
extern char *ApiGucPrefix;
extern char *ClusterAdminRole;

/* 分布式 API 的 schema 名称 */
char *ApiDistributedSchemaName = "documentdb_api_distributed";
char *ApiDistributedSchemaNameV2 = "documentdb_api_distributed";
/* 分布式扩展的名称 */
char *DistributedExtensionName = "documentdb_distributed";
/* 是否创建分布式函数的标志 */
bool CreateDistributedFunctions = false;

/*
 * 集群操作版本信息结构
 * -----
 * 用于跟踪集群的版本状态，包括当前安装的版本和上次升级的版本
 */
typedef struct ClusterOperationVersions
{
	ExtensionVersion InstalledVersion;    /* 当前安装的扩展版本 */
	ExtensionVersion LastUpgradeVersion;  /* 上次升级到的版本 */
} ClusterOperationVersions;

extern char * GetIndexQueueName(void);

/* 静态函数声明 */
/* 获取集群初始化版本 */
static char * GetClusterInitializedVersion(void);
/* 分发 CRUD 函数到工作节点 */
static void DistributeCrudFunctions(void);
/* 创建索引构建队列表 */
static void CreateIndexBuildsTable(bool includeOptions, bool includeDropCommandType);
/* 创建数据库名称验证触发器 */
static void CreateValidateDbNameTrigger(void);
/* 修改默认数据库对象（用于版本兼容） */
static void AlterDefaultDatabaseObjects(void);
/* 更新集群元数据版本信息 */
static char * UpdateClusterMetadata(bool isInitialize);
/* 创建分布式参考表 */
static void CreateReferenceTable(const char *tableName);
/* 创建分布式函数 */
static void CreateDistributedFunction(const char *functionName, const
									  char *distributionArgName,
									  const char *colocateWith, const
									  char *forceDelegation);
/* 删除旧的变更流相关对象 */
static void DropLegacyChangeStream(void);
/* 触发集群元数据缓存失效 */
static void TriggerInvalidateClusterMetadata(void);
/* 为集合表添加视图定义列 */
static void AddCollectionsTableViewDefinition(void);
/* 为集合表添加验证列 */
static void AddCollectionsTableValidationColumns(void);
/* 创建扩展版本触发器 */
static void CreateExtensionVersionsTrigger(void);
/* 比较两个版本是否相等 */
static bool VersionEquals(ExtensionVersion versionA, ExtensionVersion versionB);
/* 获取已安装的扩展版本 */
static void GetInstalledVersion(ExtensionVersion *installedVersion);
/* 解析版本字符串 */
static void ParseVersionString(ExtensionVersion *extensionVersion, char *versionString);
/* 设置集群（初始化或升级） */
static bool SetupCluster(bool isInitialize);
/* 设置只读角色的权限 */
static void SetPermissionsForReadOnlyRole(void);
/* 检查并复制参考表 */
static void CheckAndReplicateReferenceTable(const char *schema, const char *tableName);
/* 更新 changes 表的所有者为管理员角色 */
static void UpdateChangesTableOwnerToAdminRole(void);


PG_FUNCTION_INFO_V1(command_initialize_cluster);
PG_FUNCTION_INFO_V1(command_complete_upgrade);

/*
 * command_initialize_cluster implements the core
 * logic to initialize the extension in the cluster
 * -----
 * 实现 initialize_cluster 命令
 *
 * 此函数在首次部署 DocumentDB 分布式扩展时调用，负责：
 * 1. 检查集群是否已初始化
 * 2. 创建必要的元数据表（参考表）
 * 3. 配置分布式函数
 * 4. 设置权限和触发器
 *
 * 注意：如果集群已经初始化，此函数将跳过所有操作。
 */
Datum
command_initialize_cluster(PG_FUNCTION_ARGS)
{
	char *initializedVersion = GetClusterInitializedVersion();
	if (initializedVersion != NULL)
	{
		ereport(NOTICE, errmsg(
					"Initialize: version is up-to-date. Skipping initialize_cluster"));
		PG_RETURN_VOID();
	}

	bool isInitialize = true;
	SetupCluster(isInitialize);

	PG_RETURN_VOID();
}


/*
 * command_complete_upgrade executes the necessary steps
 * to perform an extension upgrade.
 * -----
 * 实现 complete_upgrade 命令
 *
 * 此函数在扩展升级时调用，负责：
 * 1. 暂时关闭磁盘满的只读限制（确保升级可以执行）
 * 2. 执行版本升级脚本
 * 3. 更新元数据表结构
 * 4. 恢复 GUC 设置
 *
 * 返回值：true 表示执行了升级，false 表示已是最新版本
 */
Datum
command_complete_upgrade(PG_FUNCTION_ARGS)
{
	/* Since complete_upgrade is internal operation, if the disk is full and we have readonly setting on, we should be able to upgrade so we turn off. */
	/* 由于 complete_upgrade 是内部操作，如果磁盘已满且启用了只读设置，我们应该能够升级，因此关闭只读限制 */
	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally(psprintf("%s.IsPgReadOnlyForDiskFull", ApiGucPrefix), "false");

	bool isInitialize = false;
	bool upgraded = SetupCluster(isInitialize);

	RollbackGUCChange(savedGUCLevel);

	PG_RETURN_BOOL(upgraded);
}


/*
 * Helper function that checks if the setup scripts for a given extension version must be executed
 * in SetupCluster. It checks whether it's greater than the last upgraded version and less or equal
 * than the current installed version.
 * -----
 * 辅助函数：检查是否应该执行指定版本的设置脚本
 *
 * 判断逻辑：
 * - 该版本必须大于上次升级的版本
 * - 该版本必须小于或等于当前安装的版本
 *
 * 这确保了设置脚本只在适当的版本范围内执行一次。
 */
static inline bool
ShouldRunSetupForVersion(ClusterOperationVersions *versions,
						 MajorVersion major, int minor, int patch)
{
	return !IsExtensionVersionAtleast(versions->LastUpgradeVersion, major, minor,
									  patch) &&
		   IsExtensionVersionAtleast(versions->InstalledVersion, major, minor, patch);
}


/* 钩子函数的包装器，用于 ShouldRunSetupForVersion */
static bool
ShouldRunSetupForVersionForHook(void *versionsVoid,
								int major, int minor, int patch)
{
	return ShouldRunSetupForVersion((ClusterOperationVersions *) versionsVoid,
									(MajorVersion) major, minor, patch);
}


/*
 * Function that runs the necessary steps to initialize and upgrade a cluster.
 * -----
 * 执行集群初始化和升级的核心函数
 *
 * 此函数是集群设置的核心，根据版本差异执行相应的升级脚本。
 *
 * 主要步骤：
 * 1. 确保元数据表已复制到所有节点
 * 2. 更新集群版本元数据
 * 3. 根据版本范围执行相应的设置脚本
 * 4. 调用后设置钩子允许扩展进行额外配置
 * 5. 触发元数据缓存失效
 *
 * 版本升级脚本示例：
 * - v0.0.5: 创建参考表、分发 CRUD 函数
 * - v0.0.7: 添加集合表视图定义
 * - v0.0.8: 创建扩展版本触发器、添加验证列
 * - v0.1.2: 创建索引构建队列表
 * - v0.1.4: 删除旧的变更流对象
 * - v0.1.7: 设置只读角色权限
 * - v0.2.3: 添加主键约束
 * - v0.10.2: 更新 changes 表所有者
 *
 * 返回值：true 表示执行了升级，false 表示已是最新版本
 */
static bool
SetupCluster(bool isInitialize)
{
	ExtensionVersion lastUpgradeVersion = { 0 };
	ExtensionVersion installedVersion = { 0 };

	/* Ensure that the cluster_data table is replicated on all nodes
	 * otherwise, writes to cluster_data will fail.
	 */
	EnsureMetadataTableReplicated("collections");

	char *lastUpgradeVersionString = UpdateClusterMetadata(isInitialize);
	ParseVersionString(&lastUpgradeVersion, lastUpgradeVersionString);

	GetInstalledVersion(&installedVersion);

	/* For initialize, lastUpgradeVersion will always be 1.0-4, which is the default version for a new cluster until we finish SetupCluster. */
	if (VersionEquals(installedVersion, lastUpgradeVersion))
	{
		ereport(NOTICE, errmsg(
					"version is up-to-date. Skipping function"));
		return false;
	}

	if (!isInitialize)
	{
		ereport(NOTICE, errmsg(
					"Previous Version Major=%d, Minor=%d, Patch=%d; Current Version Major=%d, Minor=%d, Patch=%d",
					lastUpgradeVersion.Major, lastUpgradeVersion.Minor,
					lastUpgradeVersion.Patch,
					installedVersion.Major, installedVersion.Minor,
					installedVersion.Patch));
	}

	ClusterOperationVersions versions =
	{
		.InstalledVersion = installedVersion,
		.LastUpgradeVersion = lastUpgradeVersion
	};

	/* If the version is < 0.0-5 or if it's an initialize ensure metadata collections are created */
	if (isInitialize ||
		ShouldRunSetupForVersion(&versions, DocDB_V0, 0, 5))
	{
		/*
		 * We should only create and modify schema objects here for versions that are no longer covered by the upgrade path.
		 */
		StringInfo relationName = makeStringInfo();
		appendStringInfo(relationName, "%s.collections", ApiCatalogSchemaName);
		CreateReferenceTable(relationName->data);

		resetStringInfo(relationName);
		appendStringInfo(relationName, "%s.collection_indexes", ApiCatalogSchemaName);
		CreateReferenceTable(relationName->data);
		DistributeCrudFunctions();

		CreateValidateDbNameTrigger();

		/* As of 1.23 the schema installs the type columns. */
		if (!IsExtensionVersionAtleast(installedVersion, DocDB_V0, 23, 0))
		{
			AlterDefaultDatabaseObjects();
		}

		resetStringInfo(relationName);
		appendStringInfo(relationName, "%s.%s_cluster_data", ApiDistributedSchemaName,
						 ExtensionObjectPrefix);
		CreateReferenceTable(relationName->data);
	}

	/*
	 * For initialize, lastUpgradeVersion will always be 1.4-0, so all of the below conditions will apply if the installedVersion meets the requirement.
	 */
	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 7, 0))
	{
		AddCollectionsTableViewDefinition();
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 7, 0) &&
		!ShouldRunSetupForVersion(&versions, DocDB_V0, 12, 0))
	{
		/* Schedule happens again at 1.12 */
		ScheduleIndexBuildTasks(ExtensionObjectPrefix);
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 8, 0))
	{
		CreateExtensionVersionsTrigger();

		/* We invalidate the cache in order to enable the extension versions trigger we just created. */
		TriggerInvalidateClusterMetadata();
		AddCollectionsTableValidationColumns();
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 12, 0) &&
		!ShouldRunSetupForVersion(&versions, DocDB_V0, 109, 0))
	{
		bool includeOptions = false;
		bool includeDropCommandType = false;
		CreateIndexBuildsTable(includeOptions, includeDropCommandType);
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 12, 0) &&
		!ShouldRunSetupForVersion(&versions, DocDB_V0, 15, 0))
	{
		/* Unschedule index tasks from old queue. */
		char *oldExtensionPrefix = ExtensionObjectPrefix;
		UnscheduleIndexBuildTasks(oldExtensionPrefix);

		char *extensionPrefix = ExtensionObjectPrefixV2;
		ScheduleIndexBuildTasks(extensionPrefix);
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 14, 0))
	{
		DropLegacyChangeStream();
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 15, 0))
	{
		/* reduce the Index background cron job schedule to 2 seconds by default. */
		char *extensionPrefix = ExtensionObjectPrefixV2;
		UnscheduleIndexBuildTasks(extensionPrefix);
		ScheduleIndexBuildTasks(extensionPrefix);
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 17, 1))
	{
		SetPermissionsForReadOnlyRole();
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 21, 0))
	{
		if (!isInitialize && ClusterAdminRole[0] != '\0')
		{
			StringInfo cmdStr = makeStringInfo();
			bool isNull = false;
			appendStringInfo(cmdStr,
							 "GRANT %s, %s TO %s WITH ADMIN OPTION;",
							 ApiAdminRoleV2,
							 ApiReadOnlyRole,
							 quote_identifier(ClusterAdminRole));
			ExtensionExecuteQueryViaSPI(cmdStr->data, false, SPI_OK_UTILITY,
										&isNull);
		}
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 23, 0))
	{
		/* Re-add the primary key in the context of the cluster operations. */
		StringInfo cmdStr = makeStringInfo();
		bool isNull = false;
		appendStringInfo(cmdStr,
						 "ALTER TABLE %s.%s_cluster_data DROP CONSTRAINT IF EXISTS %s_cluster_data_pkey",
						 ApiDistributedSchemaName, ExtensionObjectPrefix,
						 ExtensionObjectPrefix);
		ExtensionExecuteQueryViaSPI(cmdStr->data, false, SPI_OK_UTILITY,
									&isNull);

		resetStringInfo(cmdStr);
		appendStringInfo(cmdStr,
						 "ALTER TABLE %s.%s_cluster_data ADD PRIMARY KEY(metadata)",
						 ApiDistributedSchemaName, ExtensionObjectPrefix);
		ExtensionExecuteQueryViaSPI(cmdStr->data, false, SPI_OK_UTILITY,
									&isNull);
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 23, 2))
	{
		CheckAndReplicateReferenceTable(ApiCatalogSchemaName, "collections");
		CheckAndReplicateReferenceTable(ApiCatalogSchemaName, "collection_indexes");

		StringInfo relationName = makeStringInfo();
		appendStringInfo(relationName, "%s_cluster_data", ExtensionObjectPrefix);
		CheckAndReplicateReferenceTable(ApiDistributedSchemaName, relationName->data);
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 102, 0))
	{
		UpdateChangesTableOwnerToAdminRole();
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 109, 0))
	{
		bool includeOptions = true;
		bool includeDropCommandType = true;
		CreateIndexBuildsTable(includeOptions, includeDropCommandType);
	}

	/* we call the post setup cluster hook to allow the extension to do any additional setup */
	PostSetupClusterHook(isInitialize, &ShouldRunSetupForVersionForHook, &versions);

	TriggerInvalidateClusterMetadata();
	return true;
}


/*
 * Returns the current installed version of the extension in the cluster. If it's not installed,
 * then NULL is returned.
 * -----
 * 获取集群的初始化版本
 *
 * 从 cluster_data 表中查询 initialized_version 字段。
 * 如果返回 NULL，表示集群尚未初始化。
 */
static char *
GetClusterInitializedVersion()
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT %s.bson_get_value_text(metadata, 'initialized_version') FROM "
					 "%s.%s_cluster_data;", CoreSchemaName, ApiDistributedSchemaName,
					 ExtensionObjectPrefix);

	bool isNull = false;
	bool readOnly = true;
	Datum resultDatum = ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT,
													&isNull);
	if (isNull)
	{
		return NULL;
	}
	return text_to_cstring(DatumGetTextP(resultDatum));
}


/*
 * Creates all distributed objects required by the extension to run in the cluster.
 * -----
 * 创建扩展在集群中运行所需的所有分布式对象
 *
 * 此函数负责：
 * 1. 将 changes 表转换为分布式表（使用 shard_key_value 分片）
 * 2. 创建分布式 CRUD 函数（delete_one、insert_one、update_one）
 *
 * 分布式函数允许这些操作直接在工作节点上执行，减少网络开销。
 * 这些函数与 changes 表共置，确保数据局部性。
 */
static void
DistributeCrudFunctions()
{
	/* TODO: when we move to OSS revisit change stream stuff. */
	/* Table is distributed and co-located with the collections it is tracking */
	const char *distributionArgName = "p_shard_key_value";
	const char *forceDelegation = "true";

	char changesRelation[50];
	sprintf(changesRelation, "%s.changes", ApiDataSchemaName);
	Oid changesRelationOid = get_relname_relid("changes", ApiDataNamespaceOid());

	if (changesRelationOid != InvalidOid)
	{
		const char *distributionColumn = "shard_key_value";
		const char *colocateWith = "none";
		int shardCount = 0;
		DistributePostgresTable(changesRelation, distributionColumn, colocateWith,
								shardCount);
	}

	if (!CreateDistributedFunctions)
	{
		return;
	}

	StringInfo relationName = makeStringInfo();

	/* Push down the delete/insert/update one function calls */
	appendStringInfo(relationName,
					 "%s.delete_one(bigint,bigint,%s,%s,bool,%s,text)",
					 ApiInternalSchemaName, FullBsonTypeName, FullBsonTypeName,
					 FullBsonTypeName);
	CreateDistributedFunction(
		relationName->data,
		distributionArgName,
		changesRelation,
		forceDelegation
		);

	resetStringInfo(relationName);
	appendStringInfo(relationName, "%s.insert_one(bigint,bigint,%s,text)",
					 ApiInternalSchemaName, FullBsonTypeName);
	CreateDistributedFunction(
		relationName->data,
		distributionArgName,
		changesRelation,
		forceDelegation
		);

	resetStringInfo(relationName);
	appendStringInfo(relationName,
					 "%s.update_one(bigint,bigint,%s,%s,%s,bool,%s,bool,%s,%s,text)",
					 ApiInternalSchemaName, FullBsonTypeName, FullBsonTypeName,
					 FullBsonTypeName,
					 FullBsonTypeName, FullBsonTypeName, FullBsonTypeName);
	CreateDistributedFunction(
		relationName->data,
		distributionArgName,
		changesRelation,
		forceDelegation
		);
}


/*
 * Create index queue table, its indexes and grant permissions.
 * -----
 * 创建索引队列表及其索引和权限
 *
 * 此函数创建用于后台索引构建的队列表：
 * 1. 删除旧的队列表（如果存在）
 * 2. 创建新的队列表
 * 3. 将表添加到 Citus 元数据（作为本地表）
 *
 * 参数：
 * - includeOptions: 是否包含索引选项字段
 * - includeDropCommandType: 是否包含删除命令类型字段
 */
static void
CreateIndexBuildsTable(bool includeOptions, bool includeDropCommandType)
{
	bool readOnly = false;
	bool isNull = false;
	StringInfo queryStr = makeStringInfo();
	const char *indexQueueTableName = GetIndexQueueName();
	appendStringInfo(queryStr,
					 "DROP TABLE IF EXISTS %s;", indexQueueTableName);
	ExtensionExecuteQueryViaSPI(queryStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	CreateIndexQueueIfNotExists(includeOptions, includeDropCommandType);

	resetStringInfo(queryStr);
	appendStringInfo(queryStr,
					 "SELECT citus_add_local_table_to_metadata('%s')",
					 indexQueueTableName);
	ExtensionExecuteQueryViaSPI(queryStr->data, readOnly, SPI_OK_SELECT,
								&isNull);
}


/*
 * Create validate_dbname trigger on the collections table.
 * -----
 * 在集合表上创建数据库名称验证触发器
 *
 * 此触发器确保插入或更新的集合使用有效的数据库名称。
 * 防止非法或不存在的数据库被引用。
 */
static void
CreateValidateDbNameTrigger()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "CREATE OR REPLACE TRIGGER collections_trigger_validate_dbname "
					 "BEFORE INSERT OR UPDATE ON %s.collections "
					 "FOR EACH ROW EXECUTE FUNCTION "
					 "%s.trigger_validate_dbname();", ApiCatalogSchemaName,
					 ApiCatalogToApiInternalSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
}


/*
 * Handle failures if the worker has the attribute.
 * This handles mixed schema versioning.
 * -----
 * 处理工作节点已存在属性的情况
 *
 * 此函数用于处理混合版本环境下的架构变更。
 * 使用子事务来优雅地处理失败场景。
 *
 * 返回值：true 表示属性添加成功，false 表示属性已存在
 */
static bool
AddAttributeHandleIfExists(const char *addAttributeQuery)
{
	volatile bool isSuccess = false;

	/* use a subtransaction to correctly handle failures */
	MemoryContext oldContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;
	BeginInternalSubTransaction(NULL);

	PG_TRY();
	{
		bool readOnly = false;
		bool isNull = false;
		ExtensionExecuteQueryViaSPI(addAttributeQuery, readOnly, SPI_OK_UTILITY,
									&isNull);

		/* Commit the inner transaction, return to outer xact context */
		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;
		isSuccess = true;
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(oldContext);
		ErrorData *errorData = CopyErrorDataAndFlush();

		/* Abort inner transaction */
		RollbackAndReleaseCurrentSubTransaction();

		/* Rollback changes MemoryContext */
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;

		if (errorData->sqlerrcode != ERRCODE_DUPLICATE_COLUMN)
		{
			ReThrowError(errorData);
		}

		isSuccess = false;
	}
	PG_END_TRY();

	return isSuccess;
}


/*
 * Change internal tables to include new fields and constraints required by the extension.
 * TODO: Remove this after Cluster Version 1.23-0
 * -----
 * 修改内部表以包含扩展所需的新字段和约束
 *
 * 此函数处理混合版本环境下的类型变更：
 * 1. 为 index_spec_type_internal 类型添加 cosmos_search_options 属性
 * 2. 为 index_spec_type_internal 类型添加 index_options 属性
 *
 * 注意：此函数仅在集群版本低于 1.23-0 时需要。
 *       之后可以移除，因为这些变更将在扩展升级脚本中处理。
 *
 * 混合版本处理：
 * - 如果工作节点已有属性但协调器没有，禁用 DDL 传播后重试
 * - 使用子事务确保错误可以优雅地处理
 */
static void
AlterDefaultDatabaseObjects()
{
	StringInfo cmdStr = makeStringInfo();

	/* -- We do the ALTER TYPE in the Initialize/Complete function so that we handle the */
	/* -- upgrade scenarios where worker/coordinator are in mixed versions. Ser/Der/Casting would */
	/* -- fail if any one upgrades first and we did the ALTER TYPE in the extension upgrade. */
	/* -- by doing this in the initialize/complete, we guarantee it happens once from the DDL */
	/* -- coordinator and it's transactional. */
	resetStringInfo(cmdStr);
	appendStringInfo(cmdStr,
					 "ALTER TYPE %s.index_spec_type_internal ADD ATTRIBUTE cosmos_search_options %s.bson;",
					 ApiCatalogSchemaName, CoreSchemaName);
	bool attributeAdded = AddAttributeHandleIfExists(cmdStr->data);

	if (!attributeAdded)
	{
		/* Scenario where worker has it but coordinator doesn't, disable DDL propagation and try again */
		int gucLevel = NewGUCNestLevel();
		SetGUCLocally("citus.enable_ddl_propagation", "off");
		AddAttributeHandleIfExists(cmdStr->data);
		RollbackGUCChange(gucLevel);
	}

	/* -- all new options will go into this one bson field. */
	/* -- Older options will be cleaned up in a separate release. */
	resetStringInfo(cmdStr);
	appendStringInfo(cmdStr,
					 "ALTER TYPE %s.index_spec_type_internal ADD ATTRIBUTE index_options %s.bson;",
					 ApiCatalogSchemaName, CoreSchemaName);
	attributeAdded = AddAttributeHandleIfExists(cmdStr->data);
	if (!attributeAdded)
	{
		/* Scenario where worker has it but coordinator doesn't, disable DDL propagation and try again */
		int gucLevel = NewGUCNestLevel();
		SetGUCLocally("citus.enable_ddl_propagation", "off");
		AddAttributeHandleIfExists(cmdStr->data);
		RollbackGUCChange(gucLevel);
	}
}


/*
 * Adds bson column view_definition to the collections table.
 * -----
 * 为集合表添加 view_definition 列
 *
 * 此列用于存储视图集合的定义（如果集合是视图）。
 * 类型为 documentdb_core.bson，默认值为 null。
 */
static void
AddCollectionsTableViewDefinition()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "ALTER TABLE %s.collections ADD IF NOT EXISTS view_definition "
					 "%s.bson default null;", ApiCatalogSchemaName, CoreSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
}


/*
 * Add schema validation columns to the collections table.
 * -----
 * 为集合表添加模式验证列
 *
 * 添加三个新列用于支持 MongoDB 的 JSON Schema 验证功能：
 * 1. validator (bson): 存储验证规则文档
 * 2. validation_level (text): 验证级别（off/strict/moderate）
 * 3. validation_action (text): 验证失败时的操作（warn/error）
 */
static void
AddCollectionsTableValidationColumns()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "ALTER TABLE %s.collections "
					 "ADD COLUMN IF NOT EXISTS validator %s.bson DEFAULT null, "
					 "ADD COLUMN IF NOT EXISTS validation_level text DEFAULT null CONSTRAINT validation_level_check CHECK (validation_level IN ('off', 'strict', 'moderate')), "
					 "ADD COLUMN IF NOT EXISTS validation_action text DEFAULT null CONSTRAINT validation_action_check CHECK (validation_action IN ('warn', 'error'));",
					 ApiCatalogSchemaName, CoreSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
}


/*
 * Creates trigger for updates or deletes in the cluster_data table from the catalog schema.
 * -----
 * 创建扩展版本触发器
 *
 * 此触发器在 cluster_data 表发生更新或删除时触发，
 * 用于更新其他进程中的缓存版本信息。
 *
 * 触发时机：AFTER UPDATE OR DELETE
 * 触发级别：FOR STATEMENT（语句级）
 * 作用：确保所有进程获取最新的集群元数据
 */
static void
CreateExtensionVersionsTrigger()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "CREATE TRIGGER %s_versions_trigger "
					 "AFTER UPDATE OR DELETE ON "
					 "%s.%s_cluster_data "
					 "FOR STATEMENT EXECUTE FUNCTION "
					 "%s.update_%s_version_data();",
					 ExtensionObjectPrefix, ApiDistributedSchemaName,
					 ExtensionObjectPrefix, ApiInternalSchemaName,
					 ExtensionObjectPrefix);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
}


/*
 * Create a distributed reference table.
 * -----
 * 创建分布式参考表
 *
 * 参考表是 Citus 中的一种特殊表类型，会在所有工作节点上完整复制。
 * 适用于：
 * - 小型元数据表（如 collections、collection_indexes）
 * - 需要在所有节点上频繁访问的配置表
 *
 * 参数：tableName - 要创建为参考表的表名
 */
static void
CreateReferenceTable(const char *tableName)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT create_reference_table('%s');",
					 tableName);

	bool isNull = false;
	bool readOnly = false;
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT,
								&isNull);
}


/*
 * Create a distributed function.
 * -----
 * 创建分布式函数
 *
 * 分布式函数可以在工作节点上执行，减少数据传输。
 * 函数会根据分布参数的值路由到相应的工作节点。
 *
 * 参数：
 * - functionName: 函数的完整签名（包括参数类型）
 * - distributionArgName: 用于分片的参数名
 * - colocateWith: 共置表名（通常是与 changes 表共置）
 * - forceDelegation: 是否强制委托到工作节点执行
 */
static void
CreateDistributedFunction(const char *functionName, const char *distributionArgName,
						  const char *colocateWith, const char *forceDelegation)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT create_distributed_function('%s', '%s', colocate_with := '%s', force_delegation := %s);",
					 functionName,
					 distributionArgName,
					 colocateWith,
					 forceDelegation);

	bool isNull = false;
	bool readOnly = false;
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT,
								&isNull);
}


/*
 * Cleaning change_stream related constructs that were used for backward compatibility.
 */
static void
DropLegacyChangeStream()
{
	bool readOnly = false;
	bool isNull = false;

	ArrayType *arrayValue = GetCollectionIds();
	if (arrayValue == NULL)
	{
		return;
	}

	StringInfo cmdStr = makeStringInfo();
	Datum *elements = NULL;
	int numElements = 0;
	bool *val_is_null_marker;
	deconstruct_array(arrayValue, INT8OID, sizeof(int64), true, TYPALIGN_INT,
					  &elements, &val_is_null_marker, &numElements);

	for (int i = 0; i < numElements; i++)
	{
		int64_t collection_id = DatumGetInt64(elements[i]);

		resetStringInfo(cmdStr);
		appendStringInfo(cmdStr,
						 "ALTER TABLE IF EXISTS %s.documents_%ld DROP COLUMN IF EXISTS change_description;",
						 ApiDataSchemaName, collection_id);
		ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
									&isNull);

		resetStringInfo(cmdStr);
		appendStringInfo(cmdStr,
						 "DROP TRIGGER IF EXISTS record_changes_trigger ON %s.documents_%ld;",
						 ApiDataSchemaName, collection_id);
		ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
									&isNull);
	}
}


/*
 * Invalidate the cluster version metadata cache for all active processes.
 */
static void
TriggerInvalidateClusterMetadata()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "UPDATE %s.%s_cluster_data SET metadata = metadata;",
					 ApiDistributedSchemaName, ExtensionObjectPrefix);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UPDATE,
								&isNull);
}


/*
 * Utility funtion that checks whether 2 ExtensionVersion objects are equal (i.e. both store the same version number)
 */
static bool
VersionEquals(ExtensionVersion versionA, ExtensionVersion versionB)
{
	return versionA.Major == versionB.Major &&
		   versionA.Minor == versionB.Minor &&
		   versionA.Patch == versionB.Patch;
}


/*
 * Receives an ExtensionVersion object and populates it with the contents of a version string in the form of "Major.Minor-Patch".
 */
static void
ParseVersionString(ExtensionVersion *extensionVersion, char *versionString)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT regexp_split_to_array(TRIM(BOTH '\"' FROM '%s'), '[-\\.]')::int4[];",
					 versionString);

	bool readOnly = true;
	bool isNull = false;
	Datum versionDatum = ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly,
													 SPI_OK_SELECT, &isNull);

	ArrayType *arrayValue = DatumGetArrayTypeP(versionDatum);

	Datum *elements = NULL;
	int numElements = 0;
	bool *val_is_null_marker;
	deconstruct_array(arrayValue, INT4OID, sizeof(int), true, TYPALIGN_INT,
					  &elements, &val_is_null_marker, &numElements);

	Assert(numElements == 3);
	extensionVersion->Major = DatumGetInt32(elements[0]);
	extensionVersion->Minor = DatumGetInt32(elements[1]);
	extensionVersion->Patch = DatumGetInt32(elements[2]);
}


/*
 * Receives an ExtensionVersion object and populates it with the current installed version of the extension.
 */
static void
GetInstalledVersion(ExtensionVersion *installedVersion)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT regexp_split_to_array((SELECT extversion FROM pg_extension WHERE extname = '%s'), '[-\\.]')::int4[];",
					 ApiExtensionName);

	bool readOnly = true;
	bool isNull = false;
	Datum versionDatum = ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly,
													 SPI_OK_SELECT, &isNull);

	ArrayType *arrayValue = DatumGetArrayTypeP(versionDatum);

	Datum *elements = NULL;
	int numElements = 0;
	bool *val_is_null_marker;
	deconstruct_array(arrayValue, INT4OID, sizeof(int), true, TYPALIGN_INT,
					  &elements, &val_is_null_marker, &numElements);

	Assert(numElements == 3);
	installedVersion->Major = DatumGetInt32(elements[0]);
	installedVersion->Minor = DatumGetInt32(elements[1]);
	installedVersion->Patch = DatumGetInt32(elements[2]);
}


/*
 * SetPermissionsForReadOnlyRole - Set the right permissions for ApiReadOnlyRole
 */
static void
SetPermissionsForReadOnlyRole()
{
	bool readOnly = false;
	bool isNull = false;
	StringInfo cmdStr = makeStringInfo();

	appendStringInfo(cmdStr,
					 "GRANT SELECT ON TABLE %s.%s_cluster_data TO %s;",
					 ApiDistributedSchemaName, ExtensionObjectPrefix, ApiReadOnlyRole);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	ArrayType *arrayValue = GetCollectionIds();
	if (arrayValue == NULL)
	{
		return;
	}

	Datum *elements = NULL;
	int numElements = 0;
	bool *val_is_null_marker;
	deconstruct_array(arrayValue, INT8OID, sizeof(int64), true, TYPALIGN_INT,
					  &elements, &val_is_null_marker, &numElements);

	for (int i = 0; i < numElements; i++)
	{
		int64_t collection_id = DatumGetInt64(elements[i]);
		resetStringInfo(cmdStr);
		appendStringInfo(cmdStr,
						 "GRANT SELECT ON %s.documents_%ld TO %s;",
						 ApiDataSchemaName, collection_id, ApiReadOnlyRole);
		ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
									&isNull);
	}
}


/*
 * Checks if the given reference table is replicated.
 */
static void
CheckAndReplicateReferenceTable(const char *schema, const char *tableName)
{
	StringInfo relationName = makeStringInfo();
	appendStringInfo(relationName, "%s_%s", ExtensionObjectPrefix, "cluster_data");
	if (!(strcmp(tableName, "collections") == 0 ||
		  strcmp(tableName, "collection_indexes") == 0 ||
		  strcmp(tableName, relationName->data) == 0))
	{
		return;
	}

	StringInfo queryStringInfo = makeStringInfo();
	appendStringInfo(queryStringInfo,
					 "SELECT shardid FROM pg_catalog.pg_dist_shard WHERE logicalrelid = '%s.%s'::regclass",
					 schema, tableName);

	bool isNull = false;
	ExtensionExecuteQueryViaSPI(queryStringInfo->data, false, SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		/* Not replicated, replicate it.*/
		resetStringInfo(relationName);
		appendStringInfo(relationName, "%s.%s", schema, tableName);
		CreateReferenceTable(relationName->data);
	}
}


static char *
UpdateClusterMetadata(bool isInitialize)
{
	bool isNull = false;

	Datum args[1] = { CStringGetTextDatum(DistributedExtensionName) };
	Oid argTypes[1] = { TEXTOID };
	Datum catalogExtVersion = ExtensionExecuteQueryWithArgsViaSPI(
		"SELECT extversion FROM pg_extension WHERE extname = $1", 1, argTypes, args, NULL,
		true, SPI_OK_SELECT, &isNull);
	Assert(!isNull);

	Datum clusterVersionDatum = ExtensionExecuteQueryViaSPI(
		FormatSqlQuery(
			"SELECT %s.bson_get_value_text(metadata, 'last_deploy_version') FROM %s.%s_cluster_data",
			CoreSchemaNameV2, ApiDistributedSchemaName, ExtensionObjectPrefix),
		true,
		SPI_OK_SELECT,
		&isNull);
	Assert(!isNull);

	char *catalogVersion = TextDatumGetCString(catalogExtVersion);
	char *clusterVersion = TextDatumGetCString(clusterVersionDatum);

	if (strcmp(clusterVersion, catalogVersion) == 0)
	{
		elog(NOTICE, "version is up-to-date. Skipping function");
		return clusterVersion;
	}

	/*  get Citus version. */
	Datum citusVersion = ExtensionExecuteQueryViaSPI(
		"SELECT coalesce(metadata->>'last_upgrade_version', '11.0-1') FROM pg_dist_node_metadata",
		true, SPI_OK_SELECT, &isNull);
	Assert(!isNull);

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendUtf8(&writer, "last_deploy_version", -1, catalogVersion);
	PgbsonWriterAppendUtf8(&writer, "last_citus_version", -1, TextDatumGetCString(
							   citusVersion));

	/* Seed the first version */
	if (isInitialize)
	{
		PgbsonWriterAppendUtf8(&writer, "initialized_version", -1, catalogVersion);
	}

	Datum updateArgs[1] = { PointerGetDatum(PgbsonWriterGetPgbson(&writer)) };
	Oid updateTypes[1] = { BsonTypeId() };

	/* Modify the existing row data */
	const char *updateQuery = FormatSqlQuery(
		"UPDATE %s.%s_cluster_data SET metadata = %s.bson_dollar_set(metadata, $1)",
		ApiDistributedSchemaName, ExtensionObjectPrefix, ApiCatalogSchemaName);
	ExtensionExecuteQueryWithArgsViaSPI(updateQuery, 1, updateTypes, updateArgs, NULL,
										false, SPI_OK_UPDATE, &isNull);
	return clusterVersion;
}


/* If the changes table exists, then update the owner to the cluster admin role */
static void
UpdateChangesTableOwnerToAdminRole(void)
{
	/* First query if the changes table exists */
	const char *query = FormatSqlQuery(
		"SELECT relowner::regrole::text FROM pg_class WHERE relname = 'changes' AND relnamespace = %d",
		ApiDataNamespaceOid());
	bool isNull = false;
	Datum resultDatum = ExtensionExecuteQueryViaSPI(query, true, SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		/* Changes table does not exist, can bail */
		return;
	}

	/* Get the current owner of the changes table */
	char *currentOwner = TextDatumGetCString(resultDatum);

	/* If the current owner is already the cluster admin role, no need to change */
	if (strcmp(currentOwner, ApiAdminRole) == 0 ||
		strcmp(currentOwner, ApiAdminRoleV2) == 0)
	{
		return;
	}

	/* Otherwise update the owner to the ApiAdminRole */
	const char *alterQuery = FormatSqlQuery(
		"ALTER TABLE %s.changes OWNER TO %s;",
		ApiDataSchemaName, ApiAdminRole);

	/* Execute the query to change the owner */
	ExtensionExecuteQueryViaSPI(alterQuery, false, SPI_OK_UTILITY, &isNull);
}
