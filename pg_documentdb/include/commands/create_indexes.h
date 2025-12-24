/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/create_indexes.h
 *
 * Internal implementation of ApiSchema.create_indexes.
 *
 *-------------------------------------------------------------------------
 */
#ifndef CREATE_INDEXES_H
#define CREATE_INDEXES_H

#include <postgres.h>
#include <tcop/dest.h>
#include <tcop/utility.h>

#include "metadata/index.h"
#include "io/bson_core.h"
#include "operators/bson_expression.h"
#include "vector/vector_spec.h"

/* 索引选项字符串的最大长度限制 */
#define MAX_INDEX_OPTIONS_LENGTH 1500

/*
 * 用于索引构建并发中止错误代码的错误消息模板
 * 当索引或集合被并发删除/重新创建时触发
 */
#define COLLIDX_CONCURRENTLY_DROPPED_RECREATED_ERRMSG \
	"Index build failed :: caused by :: index or " \
	"collection dropped/re-created concurrently"

/* 每个集合允许创建的最大索引数量 */
extern int32 MaxIndexesPerCollection;


/* 索引定义键路径结构体 */
typedef struct IndexDefKeyPath
{
	/* 为索引构建的路径（参见 IndexDefKey） */
	const char *path;

	/* 此路径的索引类型 */
	MongoIndexKind indexKind;

	/* 是否为通配符索引 */
	bool isWildcard;

	/* 排序方向：1 表示升序，-1 表示降序 */
	int sortDirection;
} IndexDefKeyPath;


/* 索引定义键结构体 - 描述索引键的各种属性 */
typedef struct
{
	/* 是否为 _id 类型的索引 */
	bool isIdIndex;

	/* 索引路径是否包含通配符 */
	bool isWildcard;

	/* 索引路径是否包含哈希索引 */
	bool hasHashedIndexes;

	/* 索引路径是否包含 cosmosdb 索引 */
	bool hasCosmosIndexes;

	/* 索引路径是否包含文本索引 */
	bool hasTextIndexes;

	/* 文本索引路径列表（每个条目为 TextIndexWeights） */
	List *textPathList;

	/* 索引路径是否包含 2d 索引 */
	bool has2dIndex;

	/* 索引路径是否包含 2dsphere 索引 */
	bool has2dsphereIndex;

	/* 索引路径是否包含降序索引 */
	bool hasDescendingIndex;

	/* 键定义是否支持复合术语 */
	bool canSupportCompositeTerm;

	/*
	 * 索引定义键路径列表，每个路径代表一个特定的索引字段/路径
	 * 如果不是通配符索引。例如，{"key" : { "a.b": 1, "c.d": 1 } }
	 * 会使 keyPathList 为 ["a.b", "c.d"]。
	 *
	 * 这意味着，即使它是通配符索引，这些路径也不会包含通配符索引后缀。
	 * 此外，如果通配符索引在整个文档上（即没有前缀路径），
	 * keyPathList 将是一个空列表。如果它是带有前缀路径的通配符索引，
	 * 则 keyPathList 将包含单个元素，因为复合通配符索引是不允许的。
	 */
	List *keyPathList;

	/* 通配符索引类型 */
	MongoIndexKind wildcardIndexKind;
} IndexDefKey;


/* 索引定义结构体 - 完整描述 MongoDB 索引的所有属性和选项 */
typedef struct
{
	/* 索引名称字段 "indexName" 的值 */
	char *name;

	/** 索引选项 **/

	/* 索引版本字段 "v" 的值 */
	int version;

	/* 球面索引的版本 */
	int sphereIndexVersion;

	/* 键字段 "key" 的值 */
	IndexDefKey *key;

	/* 唯一性字段 "unique" 的值 */
	BoolIndexOption unique;

	/* 通配符投影字段 "wildcardProjection" 的值 */
	const BsonIntermediatePathNode *wildcardProjectionTree;

	/* 部分过滤表达式字段 "partialFilterExpression" 的值 */
	Expr *partialFilterExpr;

	/* 稀疏索引字段 "sparse" 的值 */
	BoolIndexOption sparse;

	/* TTL 索引的文档过期时间字段（单位：秒），NULL 表示未指定 */
	int *expireAfterSeconds;

	/** 需要存储在元数据中的 BSON 对象 **/

	/* "key" 字段持有的原始文档 */
	pgbson *keyDocument;

	/* "partialFilterExpression" 字段持有的原始文档 */
	pgbson *partialFilterExprDocument;

	/*
	 * "wildcardProjection" 字段持有的规范化文档
	 *
	 * 例如：如果索引规范中给定的 "wildcardProjection" 文档是
	 * "{"a.b": 0.4, "b": 5, "a": {"x": 1}, "b": 1}",
	 * 那么（规范化后的）wildcardProjDocument 将等于：
	 * "{"a": {"b": true, "x": true}, "b": true, "_id": false}"。
	 *
	 * 这意味着：
	 * - 每个键都是单字段路径
	 * - 冗余的路径规范将被忽略
	 * - 路径的包含性由布尔值指定
	 * - "_id" 字段的包含性总是提供（默认为 false）
	 */
	pgbson *wildcardProjectionDocument;

	/*
	 * 与 Cosmos Search 索引相关的搜索选项
	 */
	CosmosSearchOptions *cosmosSearchOptions;

	/* 文本索引的默认语言 */
	char *defaultLanguage;

	/* 文档中用于指定语言覆盖的术语 */
	char *languageOverride;

	/* 可选的权重文档 */
	pgbson *weightsDocument;

	/* 2d 索引的可选边界，NULL 表示未指定 */
	double *maxBound;
	double *minBound;
	int32_t bits;

	/* 2dsphere 索引的可忽略属性 */
	int32_t *finestIndexedLevel;
	int32_t *coarsestIndexedLevel;

	/* 启用大索引术语的特性标志 */
	BoolIndexOption enableLargeIndexKeys;

	/* 启用复合术语索引的特性标志 */
	BoolIndexOption enableCompositeTerm;

	/* 标志指示我们是否应创建唯一索引而不添加唯一约束到表。
	 * 然后只有当存在等价唯一索引时，我们才能将其转换为唯一索引。
	 */
	BoolIndexOption buildAsUnique;

	/* 启用复合术语索引的特性标志 */
	BoolIndexOption enableReducedWildcardTerms;

	/*
	 * 是否应创建为阻塞索引创建。默认为关闭（并发）。
	 */
	bool blocking;
} IndexDef;

/*
 * 后台索引创建请求结构体
 * 用于在后台批量创建索引的请求队列
 */
typedef struct
{
	/* 索引 ID 列表 */
	List *indexIds;
	/* 命令类型 */
	char cmdType;
} SubmittedIndexRequests;

/*
 * 创建索引结果结构体
 * 包含构建需要发送给客户端的 BSON 对象时使用的数据
 * 这是 createIndexes() 命令执行后的返回结果
 */
typedef struct
{
	/* 操作是否成功 */
	bool ok;
	/* 是否自动创建了集合 */
	bool createdCollectionAutomatically;
	/* 创建前的索引数量 */
	int numIndexesBefore;
	/* 创建后的索引数量 */
	int numIndexesAfter;
	/* 备注 */
	char *note;

	/* 错误报告；仅在 "ok" 为 false 时有效 */
	char *errmsg;
	int errcode;

	/* 后台索引创建相关的请求信息 */
	SubmittedIndexRequests *request;
} CreateIndexesResult;


/* 创建索引参数结构体
 * 代表传递给 dbcommand/createIndexes 的完整 "arg" 文档
 */
typedef struct
{
	/* "createIndexes" 字段的值 */
	char *collectionName;

	/*
	 * "indexes" 字段的值。
	 * 包含 "indexes" 数组中每个文档的 IndexDef 对象。
	 */
	List *indexDefList;

	/* 对于未知的索引选项，是忽略还是抛出错误 */
	bool ignoreUnknownIndexOptions;

	/* 使用 CREATE INDEX (NON-CONCURRENTLY) 阻塞写操作来创建索引 */
	bool blocking;

	/* TODO: 其他事项如 commitQuorum, comment ... */
} CreateIndexesArg;

/* 判断节点是否为创建索引语句调用 */
bool IsCallCreateIndexesStmt(const Node *node);
/* 判断节点是否为重新索引语句调用 */
bool IsCallReIndexStmt(const Node *node);
/* 解析创建索引参数，从 BSON 文档中提取索引定义 */
CreateIndexesArg ParseCreateIndexesArg(Datum dbNameDatum, pgbson *arg,
									   bool buildAsUniqueForPrepareUnique);
/* 同步创建索引（阻塞操作，不允许多个索引同时创建） */
CreateIndexesResult create_indexes_non_concurrently(Datum dbNameDatum,
													CreateIndexesArg createIndexesArg,
													bool skipCheckCollectionCreate,
													bool uniqueIndexOnly);
/* 异步创建索引（并发操作，允许其他索引同时构建） */
CreateIndexesResult create_indexes_concurrently(Datum dbNameDatum,
												CreateIndexesArg createIndexesArg,
												bool uniqueIndexOnly);
/* 处理创建索引的命令调用（SQL 接口） */
void command_create_indexes(const CallStmt *callStmt,
							ProcessUtilityContext context,
							const ParamListInfo params,
							DestReceiver *destReceiver);
/* 处理重新索引的命令调用 */
void command_reindex(const CallStmt *callStmt,
					 ProcessUtilityContext context,
					 const ParamListInfo params,
					 DestReceiver *destReceiver);
/* 检查指定索引是否正在构建中 */
bool IndexBuildIsInProgress(int indexId);
/* 为调用语句初始化函数调用信息（FCInfo） */
void InitFCInfoForCallStmt(FunctionCallInfo fcinfo, const CallStmt *callStmt,
						   ProcessUtilityContext context,
						   const ParamListInfo params);
/* 将元组发送给客户端 */
void SendTupleToClient(HeapTuple tup, TupleDesc tupDesc,
					   DestReceiver *destReceiver);
/* 检查索引冲突并修剪已存在的索引，返回冲突列表 */
List * CheckForConflictsAndPruneExistingIndexes(uint64 collectionId,
												List *indexDefList,
												List **inBuildIndexIds);
/* 创建 PostgreSQL 索引创建命令字符串 */
char * CreatePostgresIndexCreationCmd(uint64 collectionId, IndexDef *indexDef, int
									  indexId,
									  bool concurrently, bool isTempCollection);
/* 执行 PostgreSQL 索引创建命令 */
void ExecuteCreatePostgresIndexCmd(char *cmd, bool concurrently, const Oid userOid,
								   bool useSerialExecution);
/* 更新 PostgreSQL 索引的统计信息 */
void UpdateIndexStatsForPostgresIndex(uint64 collectionId, List *indexIdList);
/* 为创建索引获取咨询性排他锁 */
void AcquireAdvisoryExclusiveLockForCreateIndexes(uint64 collectionId);
/* 根据索引定义创建索引规范 */
IndexSpec MakeIndexSpecForIndexDef(IndexDef *indexDef);
/* 创建发送给客户端的创建索引消息（BSON 格式） */
pgbson * MakeCreateIndexesMsg(CreateIndexesResult *result);
/* 判断两个通配符投影文档是否等价 */
bool WildcardProjDocsAreEquivalent(const pgbson *leftWPDocument,
								   const pgbson *rightWPDocument);

#endif
