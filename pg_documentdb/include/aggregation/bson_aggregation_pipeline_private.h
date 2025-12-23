/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/planner/bson_aggregation_pipeline_private.h
 *
 * Private helpers for the bson_aggregation_pipeline definition
 * BSON 聚合管道定义的私有辅助函数
 *
 * 此文件定义了聚合管道编译器的内部实现细节，包括：
 * 1. 聚合阶段类型枚举（Stage）：所有支持的 MongoDB 聚合阶段
 * 2. 父阶段名称枚举（ParentStageName）：用于嵌套管道的上下文跟踪
 * 3. 管道构建上下文（AggregationPipelineBuildContext）：跟踪管道编译状态
 * 4. 各种阶段处理函数：将每个聚合阶段转换为 SQL 操作
 *
 * 注意：这是内部实现头文件，不应被外部模块直接使用。
 *
 *-------------------------------------------------------------------------
 */

#include <catalog/pg_collation.h>
#include <nodes/parsenodes.h>
#include <nodes/makefuncs.h>

#include "metadata/collection.h"
#include <utils/version_utils.h>
#include "collation/collation.h"

#ifndef BSON_AGGREGATION_PIPELINE_PRIVATE_H
#define BSON_AGGREGATION_PIPELINE_PRIVATE_H

/* 前向声明 */
typedef struct AggregationStageDefinition AggregationStageDefinition;

/*
 * ParentStageName - 父阶段名称枚举
 *
 * 用于标识嵌套管道中的父阶段类型。某些聚合阶段（如 $lookup、$facet）
 * 可以包含子管道，子管道中的阶段需要知道其父阶段类型以正确处理。
 *
 * for nested stage, use this to record its parent stage name
 * 对于嵌套阶段，使用此枚举记录其父阶段名称
 */
typedef enum ParentStageName
{
	ParentStageName_NOPARENT = 0,		/* 无父阶段：顶层管道中的阶段 */
	ParentStageName_LOOKUP,				/* $lookup 阶段的子管道 */
	ParentStageName_FACET,				/* $facet 阶段的子管道 */
	ParentStageName_UNIONWITH,			/* $unionWith 阶段的子管道 */
	ParentStageName_INVERSEMATCH,		/* $inverseMatch 阶段的子管道（DocumentDB 扩展） */
} ParentStageName;


/*
 * Stage - 聚合阶段类型枚举
 *
 * 此枚举定义了所有支持的 MongoDB 聚合管道阶段类型，包括：
 * 1. 内部阶段（DocumentDB 专用）：用于优化和内部实现
 * 2. 公共阶段（MongoDB 标准）：MongoDB 规范中定义的标准阶段
 * 3. 自定义阶段（DocumentDB 扩展）：DocumentDB 特有的扩展功能
 *
 * Enums to represent all kind of aggregations stages.
 * Please keep the list sorted within their groups for easier readability
 * 请保持列表在各组内按字母顺序排列以便于阅读
 */
typedef enum
{
	Stage_Invalid = 0,	/* 无效阶段：用于错误处理和初始化 */

	/* ========== Start internal stages Mongo ========== */
	/* MongoDB 内部阶段（DocumentDB 实现） */

	/*
	 * 禁用优化阶段
	 * 阻止查询优化器对后续阶段进行优化
	 * 用于测试和确保特定的执行顺序
	 */
	Stage_Internal_InhibitOptimization = 1,

	/* ========== Start Mongo Public stages ========== */
	/* MongoDB 公共阶段（符合 MongoDB 规范） */

	Stage_AddFields = 10,		/* $addFields：添加新字段或重新计算现有字段的值 */
	Stage_Bucket,				/* $bucket：按指定边界将文档分组到桶中 */
	Stage_BucketAuto,			/* $bucketAuto：自动确定边界将文档分组到指定数量的桶 */
	Stage_ChangeStream,			/* $changeStream：返回集合的变更流（实时数据变更通知） */
	Stage_CollStats,			/* $collStats：返回集合的统计信息（如存储大小、索引数量） */
	Stage_Count,				/* $count：计算文档数量（已弃用，推荐使用 $group with $sum） */
	Stage_CurrentOp,			/* $currentOp：返回当前正在执行的操作信息 */
	Stage_Densify,				/* $densify：在时间序列中填充缺失的数据点 */
	Stage_Documents,			/* $documents：从字面值创建文档流 */
	Stage_Facet,				/* $facet：在一个管道中并行执行多个子管道 */
	Stage_Fill,					/* $fill：填充缺失的字段值 */
	Stage_GeoNear,				/* $geoNear：基于地理位置的近邻排序查询 */
	Stage_GraphLookup,			/* $graphLookup：递归图遍历，用于树形/图结构查询 */
	Stage_Group,				/* $group：按指定键分组文档 */
	Stage_IndexStats,			/* $indexStats：返回索引使用统计信息 */
	Stage_Limit,				/* $limit：限制返回的文档数量 */
	Stage_ListLocalSessions,	/* $listLocalSessions：列出当前服务器的会话 */
	Stage_ListSessions,			/* $listSessions：列出会话信息 */
	Stage_Lookup,				/* $lookup：执行左外连接，关联其他集合的文档 */
	Stage_Match,				/* $match：过滤文档，只输出符合条件的文档 */
	Stage_Merge,				/* $merge：将聚合结果合并到指定集合 */
	Stage_Out,					/* $out：将聚合结果写入到指定集合（替换原内容） */
	Stage_Project,				/* $project：重塑文档结构，包括/排除/重命名字段 */
	Stage_Redact,				/* $redact：基于条件控制文档的访问权限 */
	Stage_ReplaceRoot,			/* $replaceRoot：将文档替换为指定字段的值 */
	Stage_ReplaceWith,			/* $replaceWith：将文档替换为指定表达式的值 */
	Stage_Sample,				/* $sample：随机抽取指定数量的文档 */
	Stage_Search,				/* $search：全文搜索（需要 Atlas Search） */
	Stage_SearchMeta,			/* $searchMeta：返回搜索结果的元数据 */
	Stage_Set,					/* $set：添加新字段（同 $addFields） */
	Stage_SetWindowFields,		/* $setWindowFields：窗口函数，用于跨行计算 */
	Stage_Skip,					/* $skip：跳过指定数量的文档 */
	Stage_Sort,					/* $sort：按指定字段排序文档 */
	Stage_SortByCount,			/* $sortByCount：按字段值分组并按计数排序 */
	Stage_UnionWith,			/* $unionWith：合并两个集合的文档 */
	Stage_Unset,				/* $unset：删除指定字段 */
	Stage_Unwind,				/* $unwind：展开数组中的每个元素为单独的文档 */
	Stage_VectorSearch,			/* $vectorSearch：向量相似度搜索（AI 应用） */

	/* ========== Start of pg_documentdb Custom or internal stages ========== */
	/* DocumentDB 自定义或内部阶段 */

	/*
	 * 反向匹配阶段（DocumentDB 扩展）
	 * 用于优化某些特定场景的查询
	 */
	Stage_InverseMatch = 100,

	/*
	 * Lookup 后跟 Unwind 的组合阶段（优化）
	 * 将 $lookup + $unwind 组合为单个操作以提高性能
	 */
	Stage_LookupUnwind
} Stage;


/*
 * AggregationPipelineBuildContext - 聚合管道构建上下文
 *
 * 此结构体在聚合管道编译过程中维护状态信息，用于：
 * - 跟踪当前处理的阶段编号和嵌套层级
 * - 记录游标类型和优化选项
 * - 存储集合元数据和数据库信息
 * - 管理参数化查询的参数计数
 * - 维护排序规范和变量表达式
 *
 * Shared context during aggregation pipeline build phase.
 * 聚合管道构建阶段的共享上下文
 */
typedef struct
{
	/* The current stage number (used for tagging stage identifiers) */
	/* 当前阶段编号（用于标记阶段标识符） */
	int stageNum;

	/* Whether or not a subquery stage should be injected before the next stage */
	/* 是否需要在下一阶段前注入子查询阶段 */
	/*
	 * 说明：
	 * - 某些阶段（如 $project 后的 $sort）需要子查询包装
	 * - 当设置为 true 时，会在当前阶段后创建子查询
	 * - 子查询确保中间结果被正确物化
	 */
	bool requiresSubQuery;

	/* If true, allows 1 project transform, then forces a subquery stage. */
	/* 如果为 true，允许 1 次投影转换，然后强制子查询阶段 */
	/*
	 * 说明：
	 * - 用于控制投影阶段的优化
	 * - 在某些情况下，连续的投影可以合并，但之后必须强制子查询
	 */
	bool requiresSubQueryAfterProject;

	/* Whether the query should retain an expanded target list*/
	/* 查询是否应保留扩展的目标列表 */
	/*
	 * 说明：
	 * - 扩展目标列表包含所有中间字段
	 * - 用于支持后续阶段对中间字段的访问
	 */
	bool expandTargetList;

	/* Whether or not the query requires a persisted cursor */
	/* 查询是否需要持久化游标 */
	/*
	 * 说明：
	 * - 持久化游标状态存储在服务端
	 * - 支持跨请求的 getMore 操作
	 */
	bool requiresPersistentCursor;

	/* Whether or not the stage can handle a single batch cursor */
	/* 阶段是否可以处理单批次游标 */
	/*
	 * 说明：
	 * - 单批次游标一次性返回所有结果
	 * - 适合小结果集场景
	 */
	bool isSingleRowResult;

	/* The namespace 'db.coll' associated with this query */
	/* 查询关联的命名空间（格式：database.collection） */
	const char *namespaceName;

	/* The current parameter count (Note: Increment this before use) */
	/* 当前参数计数（注意：使用前递增） */
	/*
	 * 说明：
	 * - 用于生成参数化查询的参数编号
	 * - 每次使用前需要递增，确保参数编号唯一
	 */
	int currentParamCount;

	/* The current Mongo collection */
	/* 当前的 MongoDB 集合 */
	MongoCollection *mongoCollection;	/* 包含集合的元数据（索引、分片键等） */

	/* the level of nested pipeline for stages that have nested pipelines ($facet/$lookup). */
	/* 具有嵌套管道的阶段（$facet/$lookup）的嵌套管道层级 */
	int nestedPipelineLevel;

	/* The number of nested levels (incremented by MigrateSubQuery) */
	/* 嵌套层级数量（由 MigrateSubQuery 递增） */
	int numNestedLevels;

	/* The database name associated with this request */
	/* 请求关联的数据库名称 */
	text *databaseNameDatum;

	/* The collection name associated with this request (if applicable) */
	/* 请求关联的集合名称（如果适用） */
	StringView collectionNameView;

	/* The sort specification that precedes it (if available).
	 * If the stage changes the sort order, this is reset.
	 * BSON_TYPE_EOD if not available.
	 */
	/* 前面的排序规范（如果有） */
	/*
	 * 说明：
	 * - 跟踪当前有效的排序规范
	 * - 如果阶段改变了排序顺序，这会被重置
	 * - BSON_TYPE_EOD 表示没有可用的排序规范
	 */
	bson_value_t sortSpec;

	/* The path name of the collection, used for filtering of vector search
	 * it is set only when the filter of vector search is specified
	 */
	/* 集合的路径名称，用于向量搜索过滤 */
	/*
	 * 说明：
	 * - 仅在指定向量搜索过滤器时设置
	 * - 用于优化向量搜索的执行计划
	 */
	HTAB *requiredFilterPathNameHashSet;	/* 必需的过滤器路径名哈希表 */

	/* Whether or not the aggregation query allows direct shard delegation
	 * This allows queries to go directly against a local shard *iff* it's available.
	 * This can be done for base streaming queries. TODO: Investigate whether or not
	 * this can be extended to other types of queries.
	 */
	/* 聚合查询是否允许直接分片委托 */
	/*
	 * 说明：
	 * - 允许查询直接针对本地分片执行（如果可用）
	 * - 适用于基础流式查询
	 * - TODO: 研究是否可以扩展到其他类型的查询
	 */
	bool allowShardBaseTable;

	/*
	 * The variable spec expression that preceds it.
	 * 前面的变量规范表达式
	 */
	Expr *variableSpec;	/* 用于支持 $let 和其他变量绑定操作 */

	/* Whether or not the query requires a tailable cursor */
	/* 查询是否需要可追加游标 */
	/*
	 * 说明：
	 * - 可追加游标用于监听集合的追加操作
	 * - 类似于 tail -f 命令的效果
	 */
	bool requiresTailableCursor;

	/*
	 * String indicating a standard ICU collation. An example string is "und-u-ks-level1-kc-true".
	 * We parse the Mongo collation spec and covert it to an ICU standard collation string.
	 * This string uniquely identify collation-based string comparison logic by postgres.
	 * See: https://www.postgresql.org/docs/current/collation.html
	 */
	/* ICU 排序规则字符串 */
	/*
	 * 说明：
	 * - MongoDB 排序规则规范被解析并转换为 ICU 标准排序规则字符串
	 * - 此字符串唯一标识 PostgreSQL 基于排序规则的字符串比较逻辑
	 * - 示例："und-u-ks-level1-kc-true" 表示 Unicode 排序规则
	 */
	const char collationString[MAX_ICU_COLLATION_LENGTH];

	/*
	 * Whether or not to apply the optimization transformation on the stages
	 * 是否对阶段应用优化转换
	 */
	/*
	 * 说明：
	 * - 控制是否启用管道优化（如阶段合并、谓词下推等）
	 * - 某些场景可能需要禁用优化以确保正确性
	 */
	bool optimizePipelineStages;

	/* Whether or not it's a point read query */
	/* 是否为点读查询 */
	/*
	 * 说明：
	 * - 点读查询仅通过 _id 字段查询单条文档
	 * - 性能最优，直接通过主键索引定位
	 */
	bool isPointReadQuery;

	/*Parent Stage Name*/
	/* 父阶段名称 */
	/*
	 * 说明：
	 * - 用于嵌套管道中跟踪父阶段类型
	 * - 影响子管道中的阶段处理逻辑
	 */
	ParentStageName parentStageName;
} AggregationPipelineBuildContext;


/*
 * AggregationStage - 聚合阶段结构
 *
 * 表示聚合管道中的单个阶段，包含：
 * 1. 阶段规范（stageValue）：阶段的 BSON 定义
 * 2. 阶段处理器（stageDefinition）：处理此阶段的函数指针
 */
typedef struct
{
	/* The bson value of the pipeline spec */
	/* 管道规范的 BSON 值 */
	/*
	 * 说明：
	 * - 包含该阶段的所有参数和配置
	 * - 例如：{ "$match": { "status": "active" } }
	 */
	bson_value_t stageValue;

	/* Definition of internal handlers */
	/* 内部处理器的定义 */
	/*
	 * 说明：
	 * - 指向该阶段的处理函数
	 * - 处理函数负责将阶段转换为 SQL 操作
	 */
	AggregationStageDefinition *stageDefinition;
} AggregationStage;


/* ========== 核心基础设施导出函数 ========== */


/* ========== 核心基础设施导出函数 ========== */

/* Core Infra exports */

/*
 * MutateQueryWithPipeline - 使用聚合管道转换查询
 *
 * 将聚合管道应用到基础查询上，生成完整的 PostgreSQL 查询树
 */
Query * MutateQueryWithPipeline(Query *query, List *aggregationStages,
								AggregationPipelineBuildContext *context);

/*
 * MigrateQueryToSubQuery - 将查询迁移为子查询
 *
 * 当管道需要物化中间结果时，将当前查询包装为子查询
 */
Query * MigrateQueryToSubQuery(Query *parse, AggregationPipelineBuildContext *context);

/*
 * CreateMultiArgAggregate - 创建多参数聚合函数
 *
 * 用于生成 PostgreSQL 的 Aggref 节点，支持多参数聚合函数
 */
Aggref * CreateMultiArgAggregate(Oid aggregateFunctionId, List *args, List *argTypes,
								 ParseState *parseState);

/*
 * ExtractAggregationStages - 从 BSON 值中提取聚合阶段列表
 *
 * 解析管道 BSON 数组，返回 AggregationStage 结构的列表
 */
List * ExtractAggregationStages(const bson_value_t *pipelineValue,
								AggregationPipelineBuildContext *context);

/*
 * GenerateBaseTableQuery - 生成基础表查询
 *
 * 创建一个针对集合表的 PostgreSQL SELECT 查询，作为管道的起点
 */
Query * GenerateBaseTableQuery(text *databaseDatum, const StringView *collectionNameView,
							   pg_uuid_t *collectionUuid, const bson_value_t *indexHint,
							   AggregationPipelineBuildContext *context);

/*
 * GenerateBaseAgnosticQuery - 生成与表无关的查询
 *
 * 创建不依赖特定表的查询（如 $documents 阶段）
 */
Query * GenerateBaseAgnosticQuery(text *databaseDatum,
								  AggregationPipelineBuildContext *context);

/*
 * MakeSubQueryRte - 创建子查询的范围表条目
 *
 * 将子查询包装为范围表条目，以便在主查询中引用
 */
RangeTblEntry * MakeSubQueryRte(Query *subQuery, int stageNum, int pipelineDepth,
								const char *prefix, bool includeAllColumns);

/*
 * CanInlineLookupPipeline - 判断 $lookup 管道是否可以内联
 *
 * 检查 $lookup 的子管道是否足够简单，可以直接内联到主查询中
 * 而不需要作为独立的子查询执行
 */
bool CanInlineLookupPipeline(const bson_value_t *pipeline,
							 const StringView *lookupPath,
							 bool hasLet,
							 pgbson **inlinedPipeline,
							 pgbson **nonInlinedPipeline,
							 bool *pipelineIsValid);

/*
 * ParseCursorDocument - 解析游标文档
 *
 * 从 BSON 迭代器中提取游标相关参数到 QueryData 结构
 */
void ParseCursorDocument(bson_iter_t *iterator, QueryData *queryData);

/*
 * CreateNamespaceName - 创建命名空间名称
 *
 * 组合数据库名和集合名，生成完整的命名空间字符串（db.collection 格式）
 */
const char * CreateNamespaceName(text *databaseName,
								 const StringView *collectionName);

/* ========== 基础聚合阶段处理函数 ========== */

/*
 * HandleMatch - 处理 $match 阶段
 *
 * 将过滤条件转换为 PostgreSQL 的 WHERE 子句
 */
Query * HandleMatch(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);

/*
 * HandleSimpleProjectionStage - 处理简单投影阶段（$project/$set/$addFields/$unset）
 *
 * 将投影规范转换为 PostgreSQL 的 SELECT 列表表达式
 */
Query * HandleSimpleProjectionStage(const bson_value_t *existingValue, Query *query,
									AggregationPipelineBuildContext *context,
									const char *stageName, Oid functionOid,
									Oid (*functionOidWithLet)(void),
									Oid (*functionOidWithLetAndCollation)(void));

/*
 * HandleGroup - 处理 $group 阶段
 *
 * 将分组操作转换为 PostgreSQL 的 GROUP BY 和聚合函数
 */
Query * HandleGroup(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);

/* ========== 子管道相关聚合阶段 ========== */

/* Sub-Pipeline related aggregation stages */

/*
 * HandleFacet - 处理 $facet 阶段
 *
 * $facet 允许在单个管道中并行执行多个子管道
 * 将转换为 UNION ALL 的多个子查询
 */
Query * HandleFacet(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);

/*
 * HandleLookup - 处理 $lookup 阶段
 *
 * 执行左外连接，将其他集合的文档关联到当前文档
 * 转换为 PostgreSQL 的 LEFT JOIN
 */
Query * HandleLookup(const bson_value_t *existingValue, Query *query,
					 AggregationPipelineBuildContext *context);

/*
 * HandleLookupUnwind - 处理 $lookup + $unwind 组合阶段（优化）
 *
 * 将 $lookup 后紧跟 $unwind 的常见模式优化为单个操作
 */
Query * HandleLookupUnwind(const bson_value_t *existingValue, Query *query,
						   AggregationPipelineBuildContext *context);

/*
 * HandleGraphLookup - 处理 $graphLookup 阶段
 *
 * 递归图遍历，用于查询树形或图结构数据
 * 转换为递归 CTE（Common Table Expression）
 */
Query * HandleGraphLookup(const bson_value_t *existingValue, Query *query,
						  AggregationPipelineBuildContext *context);

/*
 * HandleDocumentsStage - 处理 $documents 阶段
 *
 * 从字面值创建文档流，不依赖任何集合
 * 转换为 VALUES 子句
 */
Query * HandleDocumentsStage(const bson_value_t *existingValue, Query *query,
							 AggregationPipelineBuildContext *context);

/*
 * HandleUnionWith - 处理 $unionWith 阶段
 *
 * 合并两个集合的结果
 * 转换为 PostgreSQL 的 UNION ALL
 */
Query * HandleUnionWith(const bson_value_t *existingValue, Query *query,
						AggregationPipelineBuildContext *context);

/*
 * HandleInternalInhibitOptimization - 处理内部禁止优化阶段
 *
 * 阻止后续阶段的优化转换
 * 用于测试和确保特定执行顺序
 */
Query * HandleInternalInhibitOptimization(const bson_value_t *existingValue, Query *query,
										  AggregationPipelineBuildContext *context);

/*
 * HandleInverseMatch - 处理 $inverseMatch 阶段（DocumentDB 扩展）
 *
 * 反向匹配，用于优化某些特定场景的查询
 */
Query * HandleInverseMatch(const bson_value_t *existingValue, Query *query,
						   AggregationPipelineBuildContext *context);

/* ========== 元数据查询阶段 ========== */

/* Metadata based query stages */

/*
 * HandleCollStats - 处理 $collStats 阶段
 *
 * 返回集合的统计信息（存储大小、文档数量、索引数量等）
 */
Query * HandleCollStats(const bson_value_t *existingValue, Query *query,
						AggregationPipelineBuildContext *context);

/*
 * HandleIndexStats - 处理 $indexStats 阶段
 *
 * 返回索引的使用统计信息
 */
Query * HandleIndexStats(const bson_value_t *existingValue, Query *query,
						 AggregationPipelineBuildContext *context);

/*
 * HandleCurrentOp - 处理 $currentOp 阶段
 *
 * 返回当前正在执行的操作信息
 */
Query * HandleCurrentOp(const bson_value_t *existingValue, Query *query,
						AggregationPipelineBuildContext *context);

/*
 * HandleChangeStream - 处理 $changeStream 阶段
 *
 * 返回集合的变更流，用于实时数据变更通知
 */
Query * HandleChangeStream(const bson_value_t *existingValue, Query *query,
						   AggregationPipelineBuildContext *context);

/* ========== 内联优化相关函数 ========== */

/*
 * CanInlineLookupStageLookup - 判断 $lookup 阶段是否可以内联
 *
 * 检查 $lookup 是否足够简单，可以转换为 JOIN 而非子查询
 */
bool CanInlineLookupStageLookup(const bson_value_t *lookupStage,
								const StringView *lookupPath,
								bool hasLet);

/*
 * CanInlineLookupWithUnwind - 判断 $lookup + $unwind 组合是否可以内联
 *
 * 检查这个常见的组合模式是否可以优化为单个操作
 */
bool CanInlineLookupWithUnwind(const bson_value_t *lookUpStageValue,
							   const bson_value_t *unwindStageValue,
							   bool *isPreserveNullAndEmptyArrays);

/* ========== 向量搜索相关阶段 ========== */

/* vector search related aggregation stages */

/*
 * HandleSearch - 处理 $search 阶段
 *
 * 全文搜索阶段（需要 Atlas Search 或类似服务）
 */
Query * HandleSearch(const bson_value_t *existingValue, Query *query,
					 AggregationPipelineBuildContext *context);

/* ========== 输出到集合相关阶段 ========== */

/* output to collection related aggregation pipeline */

/*
 * HandleMerge - 处理 $merge 阶段
 *
 * 将聚合结果合并到指定集合（支持增量更新）
 */
Query * HandleMerge(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);

/*
 * HandleOut - 处理 $out 阶段
 *
 * 将聚合结果写入到指定集合（替换原内容）
 */
Query * HandleOut(const bson_value_t *existingValue, Query *query,
				  AggregationPipelineBuildContext *context);

/* ========== 原生向量搜索相关阶段 ========== */

/* Native vector search related aggregation stages */

/*
 * HandleNativeVectorSearch - 处理原生向量搜索阶段
 *
 * 使用 pgvector 进行向量相似度搜索
 */
Query * HandleNativeVectorSearch(const bson_value_t *existingValue, Query *query,
								 AggregationPipelineBuildContext *context);

/* ========== 元数据查询生成器 ========== */

/* Metadata based query generators */

/*
 * GenerateConfigDatabaseQuery - 生成配置数据库查询
 *
 * 创建查询配置元数据的 SQL
 */
Query * GenerateConfigDatabaseQuery(AggregationPipelineBuildContext *context);

/*
 * IsPartitionByFieldsOnShardKey - 检查分区字段是否在分片键上
 *
 * 判断窗口函数的分区是否与分片键对齐
 */
bool IsPartitionByFieldsOnShardKey(const pgbson *partitionByFields,
								   const MongoCollection *collection);

/* ========== 其他辅助函数 ========== */

/*
 * GenerateMultiExpressionRepathExpression - 生成多表达式重路径表达式
 *
 * 用于重命名字段路径（如 $project 中的嵌套字段重命名）
 */
Expr * GenerateMultiExpressionRepathExpression(List *repathArgs,
											   bool overrideArrayInProjection);

/*
 * GetAggregationStageAtPosition - 获取指定位置的聚合阶段
 *
 * 从阶段列表中返回指定位置的阶段
 */
Stage GetAggregationStageAtPosition(const List *aggregationStages, int position);

/* ========== 辅助方法 ========== */

/* Helper methods */

/*
 * MakeTextConst - 创建文本常量节点
 *
 * 从 C 字符串创建 PostgreSQL 的 Const 节点，类型为 TEXTOID
 */
inline static Const *
MakeTextConst(const char *cstring, uint32_t stringLength)
{
	text *textValue = cstring_to_text_with_len(cstring, stringLength);	/* 转换为 PostgreSQL text 类型 */
	return makeConst(TEXTOID, -1, DEFAULT_COLLATION_OID, -1, PointerGetDatum(textValue),
					 false,	/* 不是 NULL */
					 false);	/* 不是通过引用传递 */
}


/*
 * MakeBsonConst - 创建 BSON 常量节点
 *
 * 从 pgbson 指针创建 PostgreSQL 的 Const 节点，类型为 bson
 */
inline static Const *
MakeBsonConst(pgbson *pgbson)
{
	return makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(pgbson), false,
					 false);
}


/*
 * MakeBoolValueConst - 创建布尔值常量节点
 *
 * Inline method for a bool const specifying the isNull attribute.
 * 为布尔常量创建常量节点，指定 isNull 属性
 */
inline static Node *
MakeBoolValueConst(bool value)
{
	bool isNull = false;	/* 值不为 NULL */
	return makeBoolConst(value, isNull);
}


/*
 * MakeFloat8Const - 创建浮点数常量节点
 *
 * 从 float8 值创建 PostgreSQL 的 Const 节点，类型为 FLOAT8OID
 */
inline static Const *
MakeFloat8Const(float8 floatValue)
{
	return makeConst(FLOAT8OID, -1, InvalidOid, sizeof(float8),
					 Float8GetDatum(floatValue), false, true);
}


/*
 * MakeBsonSetOpStatement - 创建 BSON 集合操作语句
 *
 * Helper function that creates a UNION ALL Set operation statement
 * that returns a single BSON field.
 * 创建返回单个 BSON 字段的 UNION ALL 集合操作语句
 *
 * 用于 $facet 和 $unionWith 等需要合并多个结果的场景
 */
inline static SetOperationStmt *
MakeBsonSetOpStatement(void)
{
	SetOperationStmt *setOpStatement = makeNode(SetOperationStmt);
	setOpStatement->all = true;	/* UNION ALL，保留所有行（包括重复） */
	setOpStatement->op = SETOP_UNION;	/* UNION 操作 */
	setOpStatement->colCollations = list_make1_oid(InvalidOid);	/* 列排序规则 */
	setOpStatement->colTypes = list_make1_oid(BsonTypeId());	/* 列类型为 BSON */
	setOpStatement->colTypmods = list_make1_int(-1);	/* 列类型修饰符 */
	return setOpStatement;
}


#endif /* BSON_AGGREGATION_PIPELINE_PRIVATE_H */
