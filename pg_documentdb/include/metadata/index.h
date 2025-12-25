/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/metadata/index.h
 *
 * 围绕ApiCatalogSchema.collection_indexes的访问器函数。
 * 本文件定义了索引管理相关的数据结构和函数，包括索引元数据、
 * 索引规范、索引命令队列等功能。
 *
 *-------------------------------------------------------------------------
 */
#ifndef INDEXES_H
#define INDEXES_H

#include <postgres.h>
#include <utils/array.h>

#include "io/bson_core.h"

#define INVALID_INDEX_ID ((int) 0)  /* 无效的索引ID */

/* 文档数据表索引名称格式参数1: ApiCatalogSchema.collection_indexes.index_id */
#define DOCUMENT_DATA_TABLE_INDEX_NAME_FORMAT_PREFIX "documents_rum_index_"
#define DOCUMENT_DATA_TABLE_INDEX_NAME_FORMAT \
	DOCUMENT_DATA_TABLE_INDEX_NAME_FORMAT_PREFIX "%d"

#define DOCUMENT_DATA_PRIMARY_KEY_FORMAT_PREFIX "collection_pk_"

#define ID_FIELD_KEY "_id"           /* ID字段名称 */
#define ID_INDEX_NAME "_id_"          /* ID索引名称 */
#define CREATE_INDEX_COMMAND_TYPE 'C' /* 创建索引命令类型 */
#define REINDEX_COMMAND_TYPE 'R'      /* 重建索引命令类型 */

#define IndexRequestKey "indexRequest"        /* 索引请求键 */
#define IndexRequestKeyLength 12              /* 索引请求键长度 */
#define CmdTypeKey "cmdType"                  /* 命令类型键 */
#define CmdTypeKeyLength 7                    /* 命令类型键长度 */
#define IdsKey "ids"                          /* ID列表键 */
#define IdsKeyLength 3                        /* ID列表键长度 */

extern int MaxNumActiveUsersIndexBuilds;

typedef enum BoolIndexOption
{
	BoolIndexOption_Undefined = 0,  /* 未定义的布尔索引选项 */

	BoolIndexOption_False = 1,      /* 布尔索引选项 - 假 */

	BoolIndexOption_True = 2,       /* 布尔索引选项 - 真 */
} BoolIndexOption;

/*
 * index_spec类型的表示。
 *
 * 如果你决定对此结构体进行任何更改，考虑同步以下函数：
 *  - DatumGetIndexSpec()      - 从Datum获取索引规范
 *  - IndexSpecGetDatum()      - 将索引规范转换为Datum
 *  - CopyIndexSpec()          - 复制索引规范
 *  - IndexSpecOptionsAreEquivalent()  - 检查索引规范选项是否等价
 *  - MakeIndexSpecForIndexDef()        - 为索引定义创建索引规范
 * 以及documentdb_api.sql中的index_spec_type_internal类型。
 */
typedef struct IndexSpec
{
	char *indexName;                /* 索引名称，不能为NULL */

	/** 索引选项从这里开始 **/

	int indexVersion;              /* "v"号指定的索引版本号，必须是正数 */

	pgbson *indexKeyDocument;       /* "key"文档，不能为NULL */

	pgbson *indexPFEDocument;       /* "partialFilterExpression"文档，允许为NULL */

	pgbson *indexWPDocument;        /* "wildcardProjection"文档的标准化形式，允许为NULL */

	BoolIndexOption indexSparse;    /* 是否为"sparse"或未定义 */

	BoolIndexOption indexUnique;    /* 是否为"unique"或未定义 */

	int *indexExpireAfterSeconds;   /* TTL索引的文档过期时间，NULL表示未指定 */

	pgbson *cosmosSearchOptions;    /* 关于cosmosSearch的选项：TODO: 与indexOptions集成 */

	pgbson *indexOptions;          /* 通用索引选项 - 新选项应追加到这里 */
} IndexSpec;

/* 文本索引的一组文本索引权重 */
typedef struct TextIndexWeights
{
	const char *path;    /* 文档中要索引的路径 */

	double weight;       /* 与路径关联的权重 */
} TextIndexWeights;

/*
 * 可以从索引元数据API查询的索引的元数据信息。
 */
typedef struct IndexDetails
{
	int indexId;                      /* 分配给此Mongo索引的ID */

	uint64 collectionId;              /* 此Mongo索引所属的集合ID */

	IndexSpec indexSpec;              /* Mongo索引规范 */

	bool isIndexBuildInProgress;      /* 索引构建是否正在进行中（后台构建） */
} IndexDetails;

/*
 * ApiCatalogSchemaName.{ExtensionObjectPrefix}_index_queue中的索引命令状态
 * 由于GetRequestFromIndexQueue依赖index_cmd_status的顺序来避免饥饿问题，
 * 枚举值保持这样的方式：GetRequestFromIndexQueue总是按顺序选择请求
 * 首先是IndexCmdStatus_Queued，然后是IndexCmdStatus_Failed（升序）。
 */
typedef enum IndexCmdStatus
{
	IndexCmdStatus_Unknown = 0,     /* 未知状态 */

	IndexCmdStatus_Queued = 1,     /* 已排队等待执行 */

	IndexCmdStatus_Inprogress = 2, /* 正在进行中 */

	IndexCmdStatus_Failed = 3,     /* 执行失败 */

	IndexCmdStatus_Skippable = 4,  /* 可跳过 */
} IndexCmdStatus;

/*
 * 索引命令请求。命令可以是CREATE INDEX/DROP INDEX。
 */
typedef struct IndexCmdRequest
{
	char *cmd;                      /* PostgreSQL命令 */

	int indexId;                     /* 分配给此索引的ID */

	uint64 collectionId;             /* 此索引所属的集合ID */

	int16 attemptCount;              /* 我们为每个请求维护的内部重试尝试次数 */

	pgbson *comment;                 /* 上次尝试的注释 */

	TimestampTz updateTime;          /* 请求在extension_index_queue中更新的时间 */

	IndexCmdStatus status;           /* 命令状态 */

	Oid userOid;                    /* 触发CreateIndexes的用户 */

	char cmdType;                   /* 此请求的cmdType */
} IndexCmdRequest;

/* GetIndexBuildJobOpId函数的返回值 */
typedef struct
{
	TimestampTz start_time;  /* 从pg_stat_activity查询开始时间的时间戳 */

	int64 global_pid;        /* 从pg_stat_activity获取的global_pid */
} IndexJobOpId;

/*
 * 表示给定路径的索引类型。
 * 将其视为标志位，以便我们可以检查插件。
 */
typedef enum MongoIndexKind
{
	MongoIndexKind_Unknown = 0x0,      /* 无法识别或不支持的索引插件 */

	MongoIndexKind_Regular = 0x1,      /* 常规升序/降序索引 */

	MongoIndexKind_Hashed = 0x2,        /* 哈希索引 */

	MongoIndexKind_2d = 0x4,           /* 地理空间2D索引 */

	MongoIndexKind_Text = 0x8,          /* 文本搜索索引 */

	MongoIndexKind_2dsphere = 0x10,    /* 地理空间2D球面索引 */

	MongoIndexingKind_CosmosSearch = 0x20,  /* CosmosDB索引类型 */
} MongoIndexKind;

typedef struct
{
	const char *mongoIndexName;  /* Mongo索引名称 */
	bool isSupported;           /* 是否支持 */
	MongoIndexKind indexKind;   /* 索引类型 */
} MongoIndexSupport;

/* 索引构建任务 */
void UnscheduleIndexBuildTasks(char *extensionPrefix);  /* 取消调度索引构建任务 */
void ScheduleIndexBuildTasks(char *extensionPrefix);    /* 调度索引构建任务 */

MongoIndexKind GetMongoIndexKind(char *indexKindName, bool *isSupported);  /* 获取Mongo索引类型 */

/* 查询索引元数据 */
IndexDetails * FindIndexWithSpecOptions(uint64 collectionId,
										const IndexSpec *targetIndexSpec);  /* 根据规范选项查找索引 */
IndexDetails * IndexIdGetIndexDetails(int indexId);                        /* 根据索引ID获取索引详情 */
IndexDetails * IndexNameGetIndexDetails(uint64 collectionId, const char *indexName);  /* 根据索引名称获取索引详情 */
IndexDetails * IndexNameGetReadyIndexDetails(uint64 collectionId, const char *indexName);  /* 根据索引名称获取就绪的索引详情 */
List * IndexKeyGetMatchingIndexes(uint64 collectionId,
								  const pgbson *indexKeyDocument);  /* 根据索引键获取匹配的索引列表 */
List * IndexKeyGetReadyMatchingIndexes(uint64 collectionId,
									   const pgbson *indexKeyDocument);  /* 根据索引键获取就绪的匹配索引列表 */
List * CollectionIdGetIndexes(uint64 collectionId, bool excludeIdIndex,
							  bool enableNestedDistribution);  /* 根据集合ID获取索引列表 */
List * CollectionIdGetValidIndexes(uint64 collectionId, bool excludeIdIndex,
								   bool enableNestedDistribution);  /* 根据集合ID获取有效索引列表 */
List * CollectionIdGetIndexNames(uint64 collectionId, bool excludeIdIndex, bool
								 inProgressOnly);  /* 根据集合ID获取索引名称列表 */
int CollectionIdGetIndexCount(uint64 collectionId);  /* 获取集合的索引数量 */
int CollectionIdsGetIndexCount(ArrayType *collectionIdsArray);  /* 获取多个集合的索引总数 */
bool IndexSpecIsWildcardIndex(const IndexSpec *indexSpec);  /* 检查索引规范是否为通配符索引 */
bool IndexSpecIsOrderedIndex(const IndexSpec *indexSpec);   /* 检查索引规范是否为有序索引 */


/* 修改/写入索引元数据 */
int RecordCollectionIndex(uint64 collectionId, const IndexSpec *indexSpec,
						  bool isKnownValid);  /* 记录集合索引信息 */
int MarkIndexesAsValid(uint64 collectionId, const List *indexIdList);  /* 将索引标记为有效 */
void DeleteAllCollectionIndexRecords(uint64 collectionId);  /* 删除集合的所有索引记录 */
void DeleteCollectionIndexRecord(uint64 collectionId, int indexId);  /* 删除集合的特定索引记录 */

List * MergeTextIndexWeights(List *textIndexes, const bson_value_t *weights,
							 bool *isWildCard, bool includeWildCardInWeights);  /* 合并文本索引权重 */

/*
 * 索引规范的等价性枚举。
 */
typedef enum IndexOptionsEquivalency
{
	IndexOptionsEquivalency_NotEquivalent,  /* 索引规范不等价（它们是不同的） */

	/* 索引规范从选项相互兼容的角度看是等价的，
	 * 应该被视为彼此匹配但实际上是不同索引的索引 */
	IndexOptionsEquivalency_Equivalent,

	/* 每个集合只允许一个文本索引。
	 * 因此，所有后续的文本索引都是等价的，
	 * 但我们需要抛出不同的错误消息。 */
	IndexOptionsEquivalency_TextEquivalent,

	IndexOptionsEquivalency_Equal,  /* 索引规范实际上是完全相同的。 */
} IndexOptionsEquivalency;

/* IndexSpec的公共辅助函数 */
IndexOptionsEquivalency IndexSpecOptionsAreEquivalent(const IndexSpec *leftIndexSpec,
													  const IndexSpec *rightIndexSpec);  /* 检查两个索引规范选项是否等价 */
bool IndexSpecTTLOptionsAreSame(const IndexSpec *leftIndexSpec,
								const IndexSpec *rightIndexSpec);  /* 检查索引规范的TTL选项是否相同 */
pgbson * IndexSpecAsBson(const IndexSpec *indexSpec);  /* 将索引规范转换为BSON格式 */

/* index_spec_type和IndexSpec对象的工具函数 */
BoolIndexOption BoolDatumGetBoolIndexOption(bool datumIsNull, Datum datum);  /* 从Datum获取布尔索引选项 */
IndexSpec * DatumGetIndexSpec(Datum datum);  /* 从Datum获取索引规范 */
IndexSpec MakeIndexSpecForBuiltinIdIndex(void);  /* 为内置ID索引创建索引规范 */
Datum IndexSpecGetDatum(IndexSpec *indexSpec);  /* 将索引规范转换为Datum */
IndexSpec * CopyIndexSpec(const IndexSpec *indexSpec);  /* 复制索引规范 */
void WriteIndexSpecAsCurrentOpCommand(pgbson_writer *finalWriter, const
									  char *databaseName,
									  const char *collectionName, const
									  IndexSpec *indexSpec);  /* 将索引规范写入当前操作命令 */
void RemoveRequestFromIndexQueue(int indexId, char cmdType);  /* 从索引队列中移除请求 */
void MarkIndexRequestStatus(int indexId, char cmdType, IndexCmdStatus status,
							pgbson *comment,
							IndexJobOpId *opId, int16 attemptCount);  /* 标记索引请求状态 */
IndexCmdStatus GetIndexBuildStatusFromIndexQueue(int indexId);  /* 从索引队列获取索引构建状态 */
IndexCmdRequest * GetRequestFromIndexQueue(uint64 collectionId,
										   MemoryContext mcxt);  /* 从索引队列获取请求 */
IndexCmdRequest * GetSkippableRequestFromIndexQueue(int expireTimeInSeconds,
													List *skipCollections,
													MemoryContext mcxt);  /* 从索引队列获取可跳过的请求 */
uint64 * GetCollectionIdsForIndexBuild(List *excludeCollectionIds);  /* 获取索引构建的集合ID列表 */
void AddRequestInIndexQueue(char *createIndexCmd, int indexId, uint64 collectionId, char
							cmd_type, Oid userOid);  /* 添加请求到索引队列 */
char * GetIndexQueueName(void);  /* 获取索引队列名称 */
void CreateIndexQueueIfNotExists(bool includeOptions, bool includeDropCommandType);  /* 如果不存在则创建索引队列 */
const char * GetIndexTypeFromKeyDocument(pgbson *keyDocument);  /* 从键文档获取索引类型 */

/* 静态工具函数 */

static inline bool
GetBoolFromBoolIndexOptionDefaultTrue(BoolIndexOption option)
{
	/* 从布尔索引选项获取布尔值，默认为true
	 * 如果选项为未定义或假，返回false；否则返回true */
	return option == BoolIndexOption_Undefined ||
		   option == BoolIndexOption_False ? false : true;
}


static inline bool
GetBoolFromBoolIndexOptionDefaultFalse(BoolIndexOption option)
{
	/* 从布尔索引选项获取布尔值，默认为false
	 * 只有当选项为真时才返回true */
	return option == BoolIndexOption_True;
}


#endif
