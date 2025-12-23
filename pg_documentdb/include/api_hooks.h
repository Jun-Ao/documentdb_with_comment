/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/api_hooks.h
 *
 * Exports related to hooks for the public API surface that enable distribution.
 * 与支持分布式功能的公共 API 表面钩子相关的导出
 *
 *-------------------------------------------------------------------------
 */

#ifndef EXTENSION_API_HOOKS_H
#define EXTENSION_API_HOOKS_H

#include <access/amapi.h>

#include "api_hooks_common.h"
#include "metadata/collection.h"

/* Section: General Extension points */
/* 第一节：通用扩展点 */


/*
 * Returns true if the current Postgres server is a Query Coordinator
 * 如果当前 PostgreSQL 服务器是查询协调器，则返回 true
 * that also owns the metadata management of schema (DDL).
 * 并且拥有模式（DDL）的元数据管理权
 * 在分布式部署中，通常有一个节点负责元数据管理，这个函数用于判断当前节点是否是元数据协调器
 */
bool IsMetadataCoordinator(void);


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
DistributedRunCommandResult RunCommandOnMetadataCoordinator(const char *query);

/*
 * Runs a query via SPI with commutative writes on for distributed scenarios.
 * 通过 SPI 运行查询，在分布式场景中启用可交换写入
 * Returns the Datum returned by the executed query.
 * 返回执行查询返回的 Datum
 * SPI: Server Programming Interface，PostgreSQL 的服务器编程接口
 */
Datum RunQueryWithCommutativeWrites(const char *query, int nargs, Oid *argTypes,
									Datum *argValues, char *argNulls,
									int expectedSPIOK, bool *isNull);


/*
 * Sets up the system to allow nested distributed query execution for the current
 * 设置系统以允许当前事务范围内的嵌套分布式查询执行
 * transaction scope.
 * Note: This should be used very cautiously in any place where data correctness is
 * 注意：在需要数据正确性的任何地方都应非常谨慎地使用此功能
 * required.
 */
void RunMultiValueQueryWithNestedDistribution(const char *query, int nargs, Oid *argTypes,
											  Datum *argValues, char *argNulls, bool
											  readOnly,
											  int expectedSPIOK, Datum *datums,
											  bool *isNull, int numValues);


/*
 * Sets up the system to allow sequential execution for commands the current
 * 设置系统以允许当前事务范围内的命令顺序执行
 * transaction scope.
 * Note: This should be used for DDL commands.
 * 注意：应用于 DDL 命令
 * DDL: Data Definition Language，数据定义语言（如 CREATE、ALTER、DROP）
 */
Datum RunQueryWithSequentialModification(const char *query, int expectedSPIOK,
										 bool *isNull);

/*
 * Whether or not the the base tables have sharding with distribution (true if DistributePostgresTable
 * 基础表是否具有分布式分片（如果运行了 DistributePostgresTable 则为 true）
 * is run).
 * the documents table name and the substring where the collectionId was found is provided as an input.
 * 输入：文档表名和找到 collectionId 的子字符串
 */
bool IsShardTableForDocumentDbTable(const char *relName, const char *numEndPointer);


/* Section: Create Table Extension points */
/* 第二节：创建表扩展点 */

/*
 * Distributes a given postgres table with the provided distribution column.
 * 使用提供的分布列分布给定的 PostgreSQL 表
 * Optionally supports colocating the distributed table with another distributed table.
 * 可选地支持将分布式表与另一个分布式表共置
 * Returns the distribution column used (may be equal to the one passed on or NULL).
 * 返回使用的分布列（可能等于传入的列或为 NULL）
 * shardCount: the number of shards or 0 if unspecified and sharded.
 * shardCount: 分片数量，如果未指定则为 0 并进行分片
 * For unsharded, specify 0.
 * 对于非分片表，指定 0
 */
const char * DistributePostgresTable(const char *postgresTable, const
									 char *distributionColumn,
									 const char *colocateWith, int shardCount);


/*
 * Entrypoint to modify a list of column names for queries
 * 修改查询列名列表的入口点
 * For a base RTE (table)
 * 用于基础 RTE（Range Table Entry，范围表条目，即表）
 */
List * ModifyTableColumnNames(List *tableColumns);

/*
 * Create users with external identity provider
 * 使用外部身份提供程序创建用户
 * 支持通过外部身份验证系统（如 Azure AD、LDAP 等）创建用户
 */
bool CreateUserWithExternalIdentityProvider(const char *userName, char *pgRole,
											bson_value_t customData);

/*
 * Drop users with external identity provider
 * 删除使用外部身份提供程序的用户
 */
bool DropUserWithExternalIdentityProvider(const char *userName);

/*
 * Verify if the user is external
 * 验证用户是否为外部用户
 */
bool IsUserExternal(const char *userName);

/*
 * Get user info from external identity provider
 * 从外部身份提供程序获取用户信息
 */
const pgbson * GetUserInfoFromExternalIdentityProvider(const char *userName);

/*
 * Default password validation implementation
 * 默认密码验证实现
 * Returns true if password is valid, false otherwise
 * 如果密码有效返回 true，否则返回 false
 */
bool IsPasswordValid(const char *username, const char *password);

/*
 * Default username validation implementation
 * 默认用户名验证实现
 * Returns true if username is valid, false otherwise
 * 如果用户名有效返回 true，否则返回 false
 */
bool IsUsernameValid(const char *username);

/*
 * Hook for handling colocation of tables
 * 处理表共置的钩子
 * Colocation: 共置，指将相关数据放在同一物理位置以提高查询性能
 */
void HandleColocation(MongoCollection *collection, const bson_value_t *colocationOptions);


/*
 * Mutate's listCollections query generation for distribution data.
 * 为分布式数据变更 listCollections 查询生成
 * This is an optional hook and can manage listCollection to update shardCount
 * 这是一个可选钩子，可以管理 listCollection 以更新 shardCount
 * and colocation information as required. Noops for single node.
 * 和共置信息（根据需要）。单节点环境为空操作
 */
Query * MutateListCollectionsQueryForDistribution(Query *cosmosMetadataQuery);


/*
 * Mutates the shards query for handling distributed scenario.
 * 变更分片查询以处理分布式场景
 */
Query * MutateShardsQueryForDistribution(Query *metadataQuery);


/*
 * Mutates the chunks query for handling distributed scenario.
 * 变更数据块查询以处理分布式场景
 * Chunk: 数据块，分布式系统中数据分配的基本单位
 */
Query * MutateChunksQueryForDistribution(Query *cosmosMetadataQuery);


/*
 * Given a table OID, if the table is not the actual physical shard holding the data (say in a
 * 给定表 OID，如果表不是持有数据的实际物理分片（例如在
 * distributed setup), tries to return the full shard name of the actual table if it can be found locally
 * 分布式设置中），尝试在本地找到时返回实际表的完整分片名称
 * or NULL otherwise (e.g. for ApiDataSchema.documents_1 returns ApiDataSchema.documents_1_12341 or NULL, or "")
 * 否则返回 NULL（例如，对于 ApiDataSchema.documents_1 返回 ApiDataSchema.documents_1_12341 或 NULL，或 ""）
 * NULL implies that the request can be tried again. "" implies that the shard cannot be resolved locally.
 * NULL 表示可以重试请求。"" 表示无法在本地解析分片
 */
const char * TryGetShardNameForUnshardedCollection(Oid relationOid, uint64 collectionId,
												   const char *tableName);

// 获取分布式应用程序名称
// 返回当前分布式环境的应用程序标识符
const char * GetDistributedApplicationName(void);


/*
 * This checks whether the current server version supports ntoreturn spec.
 * 检查当前服务器版本是否支持 ntoreturn 规范
 * ntoreturn: MongoDB 查询选项，限制返回的文档数量
 */
bool IsNtoReturnSupported(void);


/*
 * Returns if the change stream feature is enabled.
 * 返回变更流功能是否已启用
 * Change Stream: MongoDB 的变更数据捕获功能，用于实时监听数据变更
 */
bool IsChangeStreamFeatureAvailableAndCompatible(void);

/*
 * Ensure the given metadata catalog table is replicated.
 * 确保给定的元数据目录表已复制
 * 在分布式环境中，元数据表需要在多个节点间复制以保证一致性
 */
bool EnsureMetadataTableReplicated(const char *tableName);

/*
 * The hook allows the extension to do any additional setup
 * 此钩子允许扩展在集群初始化或升级后执行任何额外的设置
 * after the cluster has been initialized or upgraded.
 */
void PostSetupClusterHook(bool isInitialize, bool (shouldUpgradeFunc(void *, int, int,
																	 int)), void *state);


/*
 * Hook for customizing the validation of vector query spec.
 * 自定义向量查询规范验证的钩子
 * 向量搜索是 AI 应用的重要功能
 */
typedef struct VectorSearchOptions VectorSearchOptions;
void TryCustomParseAndValidateVectorQuerySpec(const char *key,
											  const bson_value_t *value,
											  VectorSearchOptions *vectorSearchOptions);


// 尝试获取扩展版本刷新查询
// 用于检查和更新扩展版本信息
char * TryGetExtendedVersionRefreshQuery(void);


// 允许在当前事务中进行嵌套分布式操作
// 在某些复杂场景下需要在分布式事务中嵌套其他分布式操作
void AllowNestedDistributionInCurrentTransaction(void);

// 获取集合的分片 ID 和名称
// 参数：
//   relationOid - 关系的 OID
//   tableName - 表名
//   shardOidArray - 输出参数，分片 OID 数组
//   shardNameArray - 输出参数，分片名称数组
//   shardCount - 输出参数，分片数量
void GetShardIdsAndNamesForCollection(Oid relationOid, const char *tableName,
									  Datum **shardOidArray, Datum **shardNameArray,
									  int32_t *shardCount);


// 获取索引构建的进程 ID
// 返回正在执行索引构建的后台进程 ID
const char * GetPidForIndexBuild(void);


// 尝试获取索引构建作业的操作 ID 查询
// 用于查询后台索引构建作业的状态
const char * TryGetIndexBuildJobOpIdQuery(void);


// 尝试获取取消索引构建的查询
// 参数：
//   indexId - 要取消的索引 ID
//   cmdType - 命令类型
// 返回：取消查询的 SQL 字符串
char * TryGetCancelIndexBuildQuery(int32_t indexId, char cmdType);


// 判断是否应该调度索引构建作业
// 在分布式环境中，索引构建可能需要特殊调度
bool ShouldScheduleIndexBuildJobs(void);

// 获取分片索引的 OID 列表
// 参数：
//   collectionId - 集合 ID
//   indexId - 索引 ID
//   ignoreMissing - 如果为 true，缺失索引时不报错
// 返回：分片索引 OID 列表
List * GetShardIndexOids(uint64_t collectionId, int indexId, bool ignoreMissing);

// 使用覆盖更新 PostgreSQL 索引
// 允许覆盖默认的索引更新行为
// 参数：
//   collectionId - 集合 ID
//   indexId - 索引 ID
//   operation - 操作类型
//   value - 操作值
//   default_update - 默认更新函数
void UpdatePostgresIndexWithOverride(uint64_t collectionId, int indexId, int operation,
									 bool value,
									 void (*default_update)(uint64_t, int, int, bool));

// 获取操作取消查询
// 用于取消在特定分片上运行的操作
// 参数：
//   shardId - 分片 ID
//   opIdStringView - 操作 ID 字符串视图
//   nargs - 输出参数，参数数量
//   argTypes - 输出参数，参数类型数组
//   argValues - 输出参数，参数值数组
//   argNulls - 输出参数，参数 NULL 标记数组
//   default_get_query - 默认获取查询函数
const char * GetOperationCancellationQuery(int64 shardId, StringView *opIdStringView,
										   int *nargs, Oid **argTypes, Datum **argValues,
										   char **argNulls,
										   const char *(*default_get_query)(int64,
																			StringView *,
																			int *nargs,
																			Oid **argTypes,
																			Datum **
																			argValues,
																			char **
																			argNulls));

// 判断是否默认使用复合操作符类
// 复合操作符类用于优化多字段索引的性能
bool ShouldUseCompositeOpClassByDefault(void);

#endif
