/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/metadata/collection.h
 *
 * 集合、视图和数据表的通用声明。
 *
 * 本文件定义了MongoDB集合相关的数据结构、常量和函数声明，
 * 包括集合元数据管理、视图定义、模式验证等功能。
 *
 *-------------------------------------------------------------------------
 */

#ifndef MONGO_COLLECTIONS_H
#define MONGO_COLLECTIONS_H

#include <storage/lockdefs.h>
#include <access/attnum.h>
#include <utils/uuid.h>
#include <utils/array.h>

#include "io/bson_core.h"


/* 视图的最大深度限制 */
#define MAX_VIEW_DEPTH 20

/* 数据库名称的最大长度限制 */
#define MAX_DATABASE_NAME_LENGTH (64)

/* 集合名称的最大长度限制 */
#define MAX_COLLECTION_NAME_LENGTH (256)

/* 命名空间名称的最大长度限制（数据库名.集合名） */
#define MAX_NAMESPACE_NAME_LENGTH (64 + 256 + 1)

/* 文档数据表名称前缀 */
#define DOCUMENT_DATA_TABLE_NAME_PREFIX "documents_"

/* 文档数据表名称格式（前缀+UUID） */
#define DOCUMENT_DATA_TABLE_NAME_FORMAT DOCUMENT_DATA_TABLE_NAME_PREFIX UINT64_FORMAT

/* 数据表文档列的常量定义 */
#define DOCUMENT_DATA_TABLE_DOCUMENT_VAR_COLLATION (InvalidOid)       /* 文档列的排序规则 */
#define DOCUMENT_DATA_TABLE_DOCUMENT_VAR_TYPMOD ((int32) (-1))         /* 文档列的类型修饰符 */


/* 数据表布局的属性编号常量 */
#define DOCUMENT_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER ((AttrNumber) 1)  /* 分片键值列属性编号 */
#define DOCUMENT_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER ((AttrNumber) 2)      /* 对象ID列属性编号 */
#define DOCUMENT_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER ((AttrNumber) 3)        /* 文档列属性编号 */

/* 变更流表属性编号常量 */
#define DOCUMENT_CHANGE_STREAM_TABLE_DOCUMENT_VAR_ATTR_NUMBER ((AttrNumber) 1)  /* 变更流文档列属性编号 */
#define DOCUMENT_CHANGE_STREAM_TABLE_CONTINUATION_VAR_ATTR_NUMBER ((AttrNumber) 2)  /* 变更流继续列属性编号 */


/*
 * MongoCollectionName 表示MongoDB集合的限定名称。
 * 包含数据库名称和集合名称，用于唯一标识一个集合。
 */
typedef struct
{
	char databaseName[MAX_DATABASE_NAME_LENGTH];  /* 数据库名称 */

	char collectionName[MAX_COLLECTION_NAME_LENGTH];  /* 集合名称 */
} MongoCollectionName;

/*
 * 验证级别枚举，用于对现有文档进行模式验证。
 * 支持的级别："off"（关闭）、"strict"（严格）、"moderate"（中等）
 */
typedef enum
{
	ValidationLevel_Invalid = 0,   /* 无效的验证级别 */
	ValidationLevel_Strict,        /* 严格验证模式 - 拒绝所有不符合模式的文档 */
	ValidationLevel_Moderate,      /* 中等验证模式 - 允许弹性但进行验证 */
	ValidationLevel_Off            /* 关闭验证模式 - 不进行验证 */
} ValidationLevels;

/*
 * 验证操作枚举，用于模式验证时指定如何处理无效文档。
 * 支持的操作："warn"（警告）、"error"（错误）
 */
typedef enum
{
	ValidationAction_Invalid = 0,  /* 无效的验证操作 */
	ValidationAction_Warn,         /* 警告模式 - 记录警告但允许文档 */
	ValidationAction_Error         /* 错误模式 - 拒绝无效文档并报错 */
} ValidationActions;

/* 此结构体存储与集合关联的模式验证选项 */
typedef struct
{
	pgbson *validator;              /* 验证器BSON文档 */
	ValidationLevels validationLevel;  /* 验证级别 */
	ValidationActions validationAction;  /* 验证操作 */
} SchemaValidatorInfo;

/*
 * MongoCollection 包含单个MongoDB集合的元数据信息。
 * 该结构体是DocumentDB中集合管理的核心数据结构，存储了集合的所有相关信息。
 */
typedef struct
{
	MongoCollectionName name;      /* Mongo集合的限定名称（数据库名+集合名） */

	uint64 collectionId;          /* Mongo集合的内部标识符 */

	char tableName[NAMEDATALEN];   /* 对应的PostgreSQL表名称 */

	Oid relationId;               /* PostgreSQL表的OID标识符 */

	pgbson *shardKey;             /* 分片键的BSON文档定义 */

	pgbson *viewDefinition;        /* 视图定义（如果适用） */

	pg_uuid_t collectionUUID;      /* 与指定集合或视图关联的唯一标识符(UUID) */

	AttrNumber mongoDataCreationTimeVarAttrNumber;  /* 创建时间列的属性编号 */

	/*
	 * 分片表的可选名称（如果它有分布在当前节点的分布式表关联）
	 * 如果不可用则为空字符串（默认值）
	 */
	char shardTableName[NAMEDATALEN];

	bool isShardRemote;            /* shardTableName的分片是否为远程分片 */

	SchemaValidatorInfo schemaValidator;  /* 模式验证器（如果适用） */
} MongoCollection;


/*
 * ViewDefinition 是单个视图定义BSON文档的分解版本。
 * 将BSON格式的视图定义分解为结构化的数据结构，便于处理。
 */
typedef struct
{
	const char *viewSource;        /* 此视图定义的源集合或视图 */

	/* 应用于视图的可选管道
	 * 如果未指定则为 BSON_TYPE_EOD
	 */
	bson_value_t pipeline;
} ViewDefinition;


/* 将视图规范分解为ViewDefinition结构体 */
void DecomposeViewDefinition(pgbson *viewSpec, ViewDefinition *viewDefinition);

/* 从ViewDefinition结构体创建视图定义BSON文档 */
pgbson * CreateViewDefinition(const ViewDefinition *viewDefinition);

/* 从模式验证器信息创建模式验证器定义BSON文档 */
pgbson * CreateSchemaValidatorInfoDefinition(const SchemaValidatorInfo *schemaValidatorInfo);

/* 验证视图定义的有效性 */
void ValidateViewDefinition(Datum databaseDatum, const char *viewName, const ViewDefinition *definition);

/* 验证数据库集合的有效性 */
void ValidateDatabaseCollection(Datum databaseDatum, Datum collectionDatum);


/* 根据名称获取Mongo集合元数据 */
MongoCollection * GetMongoCollectionByNameDatum(Datum dbNameDatum,
												Datum collectionNameDatum,
												LOCKMODE lockMode);

/* 根据名称获取Mongo集合或视图的元数据 */
MongoCollection * GetMongoCollectionOrViewByNameDatum(Datum dbNameDatum,
													  Datum collectionNameDatum,
													  LOCKMODE lockMode);

/* 根据名称获取临时Mongo集合的元数据 */
MongoCollection * GetTempMongoCollectionByNameDatum(Datum dbNameDatum,
													Datum collectionNameDatum,
													char *collectionName,
													LOCKMODE lockMode);

/*
 * 获取物理分片表的OID（如果适用且在当前节点可用）
 * 如果找不到有效的分片表（由于表有多个分片或在不同的机器上），返回InvalidOid
 */
Oid TryGetCollectionShardTable(MongoCollection *collection, LOCKMODE lockMode);

/*
 * 检查数据库是否存在。检查不区分大小写。如果存在，返回TRUE
 * 并用目录表中的数据库名填充输出参数dbNameInTable，否则返回FALSE
 */
bool TryGetDBNameByDatum(Datum databaseNameDatum, char *dbNameInTable);


/*
 * 检查给定集合是否属于不可写的系统命名空间组。
 * 如果是，则执行错误报告（ereport）。
 */
void ValidateCollectionNameForUnauthorizedSystemNs(const char *collectionName,
												   Datum databaseNameDatum);


/*
 * 检查给定集合名称是否属于有效的系统命名空间
 */
void ValidateCollectionNameForValidSystemNamespace(StringView *collectionView,
												   Datum databaseNameDatum);


/*
 * 给定MongoCollection的数据表是否已在当前事务中创建？
 */
bool IsDataTableCreatedWithinCurrentXact(const MongoCollection *collection);

/* 复制给定的MongoCollection结构体 */
MongoCollection * CopyMongoCollection(const MongoCollection *collection);

/* 根据集合ID获取Mongo集合元数据 */
MongoCollection * GetMongoCollectionByColId(uint64 collectionId, LOCKMODE lockMode);

/* 根据集合分片的relation ID获取Mongo集合元数据 */
MongoCollection * GetMongoCollectionByRelationShardId(Oid relationId);

/* 根据集合ID获取数据表（documents_*）的OID */
Oid GetRelationIdForCollectionId(uint64 collectionId, LOCKMODE lockMode);

/* create_collection()函数的C语言包装器 */
bool CreateCollection(Datum dbNameDatum, Datum collectionNameDatum);

/* rename_collection()函数的C语言包装器 */
void RenameCollection(Datum dbNameDatum, Datum srcCollectionNameDatum, Datum
					  destCollectionNameDatum, bool dropTarget);

/* 当缓存失效时由metadata_cache.c调用 */
void ResetCollectionsCache(void);
void InvalidateCollectionByRelationId(Oid relationId);

/* 插入/更新模式验证元数据 */
void UpsertSchemaValidation(Datum databaseDatum,
							Datum collectionNameDatum,
							const bson_value_t *validator,
							char *validationLevel,
							char *validationAction);

/* 解析并获取验证器规范 */
const bson_value_t * ParseAndGetValidatorSpec(bson_iter_t *iter, const
											  char *validatorName,
											  bool *hasValue);

/* 解析并获取验证级别选项 */
char * ParseAndGetValidationLevelOption(bson_iter_t *iter, const
										char *validationLevelName, bool *hasValue);

/* 解析并获取验证操作选项 */
char * ParseAndGetValidationActionOption(bson_iter_t *iter, const
										 char *validationActionName, bool *hasValue);

/* 使用ID更新MongoCollection信息 */
void UpdateMongoCollectionUsingIds(MongoCollection *mongoCollection, uint64 collectionId,
								   Oid shardOid);

/* 设置未分片的数据共置信息 */
void SetUnshardedColocationData(text *databaseDatum, const char **shardingColumn, const
								char **colocateWith);

/* 创建重试表 */
void CreateRetryTable(char *retryTableName, char *colocateWith, const
					  char *distributionColumnUsed, int shardCount);

/* 获取Mongo集合的分片OID和名称 */
bool GetMongoCollectionShardOidsAndNames(MongoCollection *collection,
										 ArrayType **shardIdArray,
										 ArrayType **shardNames);
#endif
