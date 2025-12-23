/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/api_hooks_def.h
 *
 * Definition of hooks for the extension that allow for handling
 * 扩展的钩子定义，用于处理
 * distribution type scenarios. These can be overriden to implement
 * 分布式场景。这些钩子可以被覆盖以实现
 * custom distribution logic.
 * 自定义分布式逻辑
 *
 *-------------------------------------------------------------------------
 */

#ifndef EXTENSION_API_HOOKS_DEF_H
#define EXTENSION_API_HOOKS_DEF_H

#include "api_hooks_common.h"
#include <access/amapi.h>
#include <nodes/parsenodes.h>
#include <nodes/pathnodes.h>

#include "metadata/collection.h"

/* Section: General Extension points */
/* 第一节：通用扩展点 */

/*
 * Returns true if the current Postgres server is a Query Coordinator
 * 如果当前 PostgreSQL 服务器是查询协调器，则返回 true
 * that also owns the metadata management of schema (DDL).
 * 并且拥有模式（DDL）的元数据管理权
 */
typedef bool (*IsMetadataCoordinator_HookType)(void);
extern IsMetadataCoordinator_HookType is_metadata_coordinator_hook;
// 钩子函数指针：判断当前节点是否是元数据协调器

/*
 * Indicates whether the Change Stream feature is currently enabled
 * 指示变更流功能当前是否已启用
 * Change Stream: MongoDB 的变更数据捕获功能
 */
typedef bool (*IsChangeStreamEnabledAndCompatible)(void);
extern IsChangeStreamEnabledAndCompatible is_changestream_enabled_and_compatible_hook;
// 钩子函数指针：检查变更流功能是否可用

/*
 * Runs a command on the MetadataCoordinator if the current node is not a
 * 如果当前节点不是元数据协调器，则在元数据协调器上运行命令
 * Metadata Coordinator. The response is returned as a "record" struct
 * 响应以"记录"结构返回
 * with the nodeId responding, whether or not the command succeeded and
 * 包含响应的节点 ID、命令是否成功以及
 * the response datum serialized as a string.
 * 序列化为字符串的响应数据
 * If success, then this is the response datum in text format.
 * 如果成功，这是文本格式的响应数据
 * If failed, then this contains the error string from the failure.
 * 如果失败，这包含失败的错误字符串
 */
typedef DistributedRunCommandResult (*RunCommandOnMetadataCoordinator_HookType)(const
																				char *
																				query);
extern RunCommandOnMetadataCoordinator_HookType run_command_on_metadata_coordinator_hook;
// 钩子函数指针：在元数据协调器上运行命令

/*
 * Runs a query via SPI with commutative writes on for distributed scenarios.
 * 通过 SPI 运行查询，在分布式场景中启用可交换写入
 * Returns the Datum returned by the executed query.
 * 返回执行查询返回的 Datum
 */
typedef Datum (*RunQueryWithCommutativeWrites_HookType)(const char *query, int nargs,
														Oid *argTypes,
														Datum *argValues, char *argNulls,
														int expectedSPIOK, bool *isNull);
extern RunQueryWithCommutativeWrites_HookType run_query_with_commutative_writes_hook;
// 钩子函数指针：运行可交换写入查询


/*
 * Runs a query via SPI with sequential shard execution for distributed scenarios
 * 通过 SPI 运行查询，在分布式场景中使用顺序分片执行
 * Returns the Datum returned by the executed query.
 * 返回执行查询返回的 Datum
 */
typedef Datum (*RunQueryWithSequentialModification_HookType)(const char *query, int
															 expectedSPIOK, bool *isNull);
extern RunQueryWithSequentialModification_HookType
	run_query_with_sequential_modification_mode_hook;
// 钩子函数指针：运行顺序修改查询（用于 DDL 命令）


/* Section: Create Table Extension points */
/* 第二节：创建表扩展点 */

/*
 * Distributes a given postgres table with the provided distribution column.
 * 使用提供的分布列分布给定的 PostgreSQL 表
 * Optionally supports colocating the distributed table with another distributed table.
 * 可选地支持将分布式表与另一个分布式表共置
 */
typedef const char *(*DistributePostgresTable_HookType)(const char *postgresTable, const
														char *distributionColumn,
														const char *colocateWith,
														int shardCount);
extern DistributePostgresTable_HookType distribute_postgres_table_hook;
// 钩子函数指针：分布 PostgreSQL 表


/*
 * Entrypoint to modify a list of column names for queries
 * 修改查询列名列表的入口点
 * For a base RTE (table)
 * 用于基础 RTE（Range Table Entry，范围表条目，即表）
 */
typedef List *(*ModifyTableColumnNames_HookType)(List *tableColumns);
extern ModifyTableColumnNames_HookType modify_table_column_names_hook;
// 钩子函数指针：修改表的列名列表

/*
 * Creates a user using an external identity provider
 * 使用外部身份提供程序创建用户
 */
typedef bool (*CreateUserWithExernalIdentityProvider_HookType)(const char *userName,
															   char *pgRole, bson_value_t
															   customData);
extern CreateUserWithExernalIdentityProvider_HookType
	create_user_with_exernal_identity_provider_hook;
// 钩子函数指针：使用外部身份提供程序创建用户

/*
 * Drops a user using an external identity provider
 * 使用外部身份提供程序删除用户
 */
typedef bool (*DropUserWithExernalIdentityProvider_HookType)(const char *userName);
extern DropUserWithExernalIdentityProvider_HookType
	drop_user_with_exernal_identity_provider_hook;
// 钩子函数指针：使用外部身份提供程序删除用户

/*
 * Method to verify if a user is native
 * 验证用户是否为原生用户的方法
 */
typedef bool (*IsUserExternal_HookType)(const char *userName);
extern IsUserExternal_HookType
	is_user_external_hook;
// 钩子函数指针：验证用户是否为外部用户


/*
 * Method to get user info from external identity provider
 * 从外部身份提供程序获取用户信息的方法
 */
typedef const pgbson *(*GetUserInfoFromExternalIdentityProvider_HookType)();
extern GetUserInfoFromExternalIdentityProvider_HookType
	get_user_info_from_external_identity_provider_hook;
// 钩子函数指针：从外部身份提供程序获取用户信息


/* Method for username validation */
/* 用户名验证方法 */
typedef bool (*UserNameValidation_HookType)(const char *username);
extern UserNameValidation_HookType username_validation_hook;
// 钩子函数指针：验证用户名是否有效


/* Method for password validation */
/* 密码验证方法 */
typedef bool (*PasswordValidation_HookType)(const char *username, const char *password);
extern PasswordValidation_HookType password_validation_hook;
// 钩子函数指针：验证密码是否有效


/*
 * Hook for enabling running a query with nested distribution enabled.
 * 用于启用嵌套分布式运行查询的钩子
 */
typedef void (*RunQueryWithNestedDistribution_HookType)(const char *query,
														int nArgs, Oid *argTypes,
														Datum *argDatums,
														char *argNulls,
														bool readOnly,
														int expectedSPIOK,
														Datum *datums,
														bool *isNull,
														int numValues);
extern RunQueryWithNestedDistribution_HookType run_query_with_nested_distribution_hook;
// 钩子函数指针：运行嵌套分布式查询

typedef void (*AllowNestedDistributionInCurrentTransaction_HookType)(void);
extern AllowNestedDistributionInCurrentTransaction_HookType
	allow_nested_distribution_in_current_transaction_hook;
// 钩子函数指针：允许在当前事务中使用嵌套分布式

typedef bool (*IsShardTableForDocumentDbTable_HookType)(const char *relName, const
														char *numEndPointer);

extern IsShardTableForDocumentDbTable_HookType is_shard_table_for_documentdb_table_hook;
// 钩子函数指针：判断表是否为 DocumentDB 的分片表

typedef void (*HandleColocation_HookType)(MongoCollection *collection,
										  const bson_value_t *colocationOptions);

extern HandleColocation_HookType handle_colocation_hook;
// 钩子函数指针：处理表的共置配置

typedef Query *(*RewriteListCollectionsQueryForDistribution_HookType)(Query *query);
extern RewriteListCollectionsQueryForDistribution_HookType
	rewrite_list_collections_query_hook;
// 钩子函数指针：重写 listCollections 查询以支持分布式

typedef Query *(*RewriteConfigQueryForDistribution_HookType)(Query *query);
extern RewriteConfigQueryForDistribution_HookType rewrite_config_shards_query_hook;
extern RewriteConfigQueryForDistribution_HookType rewrite_config_chunks_query_hook;
// 钩子函数指针：重写配置查询（分片和数据块）以支持分布式

typedef const char *(*TryGetShardNameForUnshardedCollection_HookType)(Oid relationOid,
																	  uint64 collectionId,
																	  const char *
																	  tableName);
extern TryGetShardNameForUnshardedCollection_HookType
	try_get_shard_name_for_unsharded_collection_hook;
// 钩子函数指针：尝试获取非分片集合的分片名称

/*
 * Hook for creating an update tracker if tracking is enabled.
 * 用于创建更新跟踪器的钩子（如果启用了跟踪）
 */
typedef BsonUpdateTracker *(*CreateBsonUpdateTracker_HookType)(void);
extern CreateBsonUpdateTracker_HookType create_update_tracker_hook;
// 钩子函数指针：创建 BSON 更新跟踪器

typedef pgbson *(*BuildUpdateDescription_HookType)(BsonUpdateTracker *);
extern BuildUpdateDescription_HookType build_update_description_hook;
// 钩子函数指针：构建更新描述

typedef const char *(*GetDistributedApplicationName_HookType)(void);
extern GetDistributedApplicationName_HookType get_distributed_application_name_hook;
// 钩子函数指针：获取分布式应用程序名称

typedef bool (*IsNtoReturnSupported_HookType)(void);
extern IsNtoReturnSupported_HookType is_n_to_return_supported_hook;
// 钩子函数指针：检查是否支持 ntoreturn 规范

typedef bool (*EnsureMetadataTableReplicated_HookType)(const char *);
extern EnsureMetadataTableReplicated_HookType ensure_metadata_table_replicated_hook;
// 钩子函数指针：确保元数据表已复制

typedef void (*PostSetupCluster_HookType)(bool, bool (shouldUpgradeFunc(void *, int, int,
																		int)), void *);
extern PostSetupCluster_HookType post_setup_cluster_hook;
// 钩子函数指针：集群设置后的回调

/*
 * Hook for customizing the validation of vector query spec.
 * 自定义向量查询规范验证的钩子
 */
typedef struct VectorSearchOptions VectorSearchOptions;
typedef void
(*TryCustomParseAndValidateVectorQuerySpec_HookType)(const char *key,
													 const bson_value_t *value,
													 VectorSearchOptions *
													 vectorSearchOptions);
extern TryCustomParseAndValidateVectorQuerySpec_HookType
	try_custom_parse_and_validate_vector_query_spec_hook;
// 钩子函数指针：自定义向量查询规范验证

extern bool DefaultInlineWriteOperations;
extern bool ShouldUpgradeDataTables;
// 全局配置变量

typedef char *(*TryGetExtendedVersionRefreshQuery_HookType)(void);
extern TryGetExtendedVersionRefreshQuery_HookType
	try_get_extended_version_refresh_query_hook;
// 钩子函数指针：获取扩展版本刷新查询

typedef void (*GetShardIdsAndNamesForCollection_HookType)(Oid relationOid, const
														  char *tableName,
														  Datum **shardOidArray,
														  Datum **shardNameArray,
														  int32_t *shardCount);
extern GetShardIdsAndNamesForCollection_HookType
	get_shard_ids_and_names_for_collection_hook;
// 钩子函数指针：获取集合的分片 ID 和名称


typedef const char *(*GetPidForIndexBuild_HookType)(void);
extern GetPidForIndexBuild_HookType get_pid_for_index_build_hook;
// 钩子函数指针：获取索引构建的进程 ID


typedef const char *(*TryGetIndexBuildJobOpIdQuery_HookType)(void);
extern TryGetIndexBuildJobOpIdQuery_HookType try_get_index_build_job_op_id_query_hook;
// 钩子函数指针：获取索引构建作业的操作 ID 查询


typedef char *(*TryGetCancelIndexBuildQuery_HookType)(int32_t indexId, char cmdType);
extern TryGetCancelIndexBuildQuery_HookType try_get_cancel_index_build_query_hook;
// 钩子函数指针：获取取消索引构建的查询


typedef bool (*ShouldScheduleIndexBuilds_HookType)();
extern ShouldScheduleIndexBuilds_HookType should_schedule_index_builds_hook;
// 钩子函数指针：判断是否应该调度索引构建作业

typedef List *(*GettShardIndexOids_HookType)(uint64_t collectionId, int indexId, bool
											 ignoreMissing);
extern GettShardIndexOids_HookType get_shard_index_oids_hook;
// 钩子函数指针：获取分片索引的 OID 列表

typedef void (*UpdatePostgresIndex_HookType)(uint64_t collectionId, int indexId, int
											 operation, bool value);
extern UpdatePostgresIndex_HookType update_postgres_index_hook;
// 钩子函数指针：更新 PostgreSQL 索引

typedef const char *(*GetOperationCancellationQuery_HookType)(int64 shardId,
															  StringView *opIdString,
															  int *nargs, Oid **argTypes,
															  Datum **argValues,
															  char **argNulls);
extern GetOperationCancellationQuery_HookType get_operation_cancellation_query_hook;
// 钩子函数指针：获取操作取消查询

typedef bool (*DefaultEnableCompositeOpClass_HookType)(void);
extern DefaultEnableCompositeOpClass_HookType default_enable_composite_op_class_hook;
// 钩子函数指针：判断是否默认启用复合操作符类

#endif
