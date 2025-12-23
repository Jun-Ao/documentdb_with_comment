/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_aggregation_pipeline.h
 *
 * Exports for the bson_aggregation_pipeline definition
 * BSON 聚合管道定义的导出接口
 *
 * 此文件定义了 DocumentDB 聚合管道相关的公共接口和数据结构，包括：
 * 1. 查询游标类型（QueryCursorType）：定义不同类型的查询游标
 * 2. 查询数据结构（QueryData）：封装查询请求的元数据
 * 3. 各种查询生成函数：将 MongoDB 查询转换为 PostgreSQL 查询树
 *
 * 聚合管道是 MongoDB 的核心功能，DocumentDB 通过将这些管道阶段转换为
 * PostgreSQL 的查询树来实现兼容性。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_AGGREGATION_PIPELINE_H
#define BSON_AGGREGATION_PIPELINE_H

#include <nodes/params.h>

#include "operators/bson_expression.h"
#include "utils/documentdb_errors.h"

/*
 * QueryCursorType - 查询游标类型枚举
 *
 * 此枚举定义了不同类型的查询游标，每种类型对应不同的查询执行策略：
 * - 流式查询：结果可以逐批返回，适合大量数据
 * - 单批次查询：所有结果一次性返回
 * - 点读查询：通过主键精确读取单条记录
 * - 可追加游标：用于监听集合变更的查询
 * - 持久游标：默认类型，游标状态持久化存储
 */
typedef enum QueryCursorType
{
	QueryCursorType_Unspecified = 0,	/* 未指定类型，将根据查询特征自动推断 */

	/*
	 * Whether or not the query can be done as a streamable query.
	 * 流式游标：查询结果可以分批返回，无需一次性加载所有数据到内存
	 *
	 * 特点：
	 * - 适合返回大量结果的查询
	 * - 客户端可以通过 getMore 命令分批获取数据
	 * - 降低内存消耗，提高响应速度
	 */
	QueryCursorType_Streamable = 1,

	/*
	 * Indicates whether this is processed as a single batch query.
	 * 单批次游标：所有结果在一次查询中全部返回
	 *
	 * 特点：
	 * - 适合返回少量结果的查询
	 * - 无需维护游标状态
	 * - 查询完成后立即释放资源
	 */
	QueryCursorType_SingleBatch,

	/*
	 * The cursor plan is a point read.
	 * 点读游标：通过 _id 字段精确查找单条文档
	 *
	 * 特点：
	 * - 查询条件仅为 _id 字段的等值查询
	 * - 性能最优，直接通过主键索引定位
	 * - 常见于 findById 类型的查询
	 */
	QueryCursorType_PointRead,

	/*
	 * Whether or not the query can be done as a tailable query.
	 * 可追加游标：用于监听 capped collection 的追加数据
	 *
	 * 特点：
	 * - 类似于 tail -f 命令，持续监听新数据
	 * - 通常用于变更流或日志监听场景
	 * - 即使没有数据，游标也不会立即关闭
	 */
	QueryCursorType_Tailable,

	/*
	 * By default all queries are persistent cursors.
	 * 持久游标：默认类型，游标状态持久化存储
	 *
	 * 特点：
	 * - 游标状态保存在服务端
	 * - 支持通过 cursorId 进行多次 getMore 操作
	 * - 有超时机制，默认 10 分钟无活动自动关闭
	 */
	QueryCursorType_Persistent,
} QueryCursorType;

/*
 * QueryData - 查询元数据结构
 *
 * 此结构封装了从查询请求中提取的所有元数据，用于游标管理和查询执行：
 * - 跟踪查询请求的参数（如批大小、命名空间）
 * - 管理游标状态和分页信息
 * - 存储时间系统变量（用于支持 $$NOW 等特殊变量）
 * - 决定使用何种类型的游标来处理请求
 *
 * 使用场景：
 * 1. 查询规划阶段：确定最优的执行策略
 * 2. 游标管理阶段：维护游标状态和分页信息
 * 3. getMore 操作：恢复上一次查询的上下文
 */
typedef struct
{
	/*
	 * The parameter number used for the cursor
	 * continuation (if it is a streaming cursor)
	 * 游标续传参数的编号（用于流式游标）
	 *
	 * 说明：
	 * - 在 PostgreSQL 参数化查询中，每个参数都有一个编号
	 * - 此编号指向存储游标状态的参数
	 * - 用于 getMore 操作时恢复游标的执行位置
	 */
	int cursorStateParamNumber;

	/* Optional cursor state const param / 可选的游标状态常量参数 */
	pgbson *cursorStateConst;	/* 存储游标当前状态的 BSON 文档（如扫描位置、排序键等） */

	/*
	 * The namespaceName associated with the query.
	 * 查询关联的命名空间名称（格式：database.collection）
	 *
	 * 说明：
	 * - 命名空间是 MongoDB 中标识数据库和集合的组合
	 * - 格式为 "数据库名.集合名"，如 "testDB.users"
	 * - 用于确定查询操作的目标集合
	 */
	const char *namespaceName;

	QueryCursorType cursorKind;	/* 游标类型，决定查询执行策略 */

	/*
	 * The requested batchSize in the query request.
	 * 查询请求中指定的批大小
	 *
	 * 说明：
	 * - 每批次返回的文档数量限制
	 * - 0 表示使用默认值（通常为 101）
	 * - 负值表示没有限制，返回所有结果
	 */
	int32_t batchSize;

	/*
	 * The time system variables ($$NOW, $$CLUSTER_TIME).
	 * 时间系统变量（支持 MongoDB 的 $$NOW 和 $$CLUSTER_TIME 特殊变量）
	 *
	 * 说明：
	 * - $$NOW: 当前时间戳，用于聚合管道中的时间计算
	 * - $$CLUSTER_TIME: 集群时间，用于分布式事务和一致性保证
	 * - 这些变量在查询执行时会被替换为实际值
	 */
	TimeSystemVariables timeSystemVariables;
} QueryData;


/* ========== 查询生成函数声明 ========== */

/*
 * GenerateFindQuery - 生成 find 查询的 PostgreSQL 查询树
 *
 * 参数说明：
 * - database: 数据库名称
 * - findSpec: MongoDB find 命令的 BSON 规范（包含 filter, projection, sort 等）
 * - queryData: 查询元数据（游标类型、批大小等）
 * - addCursorParams: 是否添加游标参数（用于分页）
 * - setStatementTimeout: 是否设置语句超时
 *
 * 返回值：
 * - 返回 PostgreSQL 的 Query 结构指针，可直接用于执行
 *
 * 说明：
 * - 将 MongoDB 的 find 命令转换为 PostgreSQL 的 SELECT 查询
 * - filter 转换为 WHERE 子句
 * - projection 转换为 SELECT 列表
 * - sort 转换为 ORDER BY 子句
 * - limit/skip 转换为 LIMIT/OFFSET 子句
 */
Query * GenerateFindQuery(text *database, pgbson *findSpec, QueryData *queryData,
						  bool addCursorParams, bool setStatementTimeout);

/*
 * GenerateGetMoreQuery - 生成 getMore 查询的 PostgreSQL 查询树
 *
 * 参数说明：
 * - database: 数据库名称
 * - getMoreSpec: MongoDB getMore 命令的 BSON 规范
 * - continuationSpec: 游标续传信息（上次查询的结束位置）
 * - queryData: 查询元数据
 * - addCursorParams: 是否添加游标参数
 * - setStatementTimeout: 是否设置语句超时
 *
 * 返回值：
 * - 返回 PostgreSQL 的 Query 结构指针
 *
 * 说明：
 * - getMore 用于获取游标的下一批数据
 * - 需要恢复上次查询的上下文（扫描位置、排序状态等）
 * - 支持聚合游标和普通查询游标
 */
Query * GenerateGetMoreQuery(text *database, pgbson *getMoreSpec,
							 pgbson *continuationSpec,
							 QueryData *queryData, bool addCursorParams, bool
							 setStatementTimeout);

/*
 * BuildAggregationCursorGetMoreQuery - 构建聚合游标的 getMore 查询
 *
 * 参数说明：
 * - database: 数据库名称
 * - getMoreSpec: MongoDB getMore 命令的 BSON 规范
 * - continuationSpec: 游标续传信息
 *
 * 返回值：
 * - 返回 PostgreSQL 的 Query 结构指针
 *
 * 说明：
 * - 专门用于聚合管道游标的 getMore 操作
 * - 聚合游标比普通查询游标更复杂，需要维护更多状态
 */
Query * BuildAggregationCursorGetMoreQuery(text *database, pgbson *getMoreSpec,
										   pgbson *continuationSpec);

/*
 * GenerateCountQuery - 生成 count 查询的 PostgreSQL 查询树
 *
 * 参数说明：
 * - database: 数据库名称
 * - countSpec: MongoDB count 命令的 BSON 规范
 * - setStatementTimeout: 是否设置语句超时
 *
 * 返回值：
 * - 返回 PostgreSQL 的 Query 结构指针
 *
 * 说明：
 * - 将 count 命令转换为 SELECT COUNT(*) 查询
 * - 支持查询条件过滤
 */
Query * GenerateCountQuery(text *database, pgbson *countSpec, bool setStatementTimeout);

/*
 * GenerateDistinctQuery - 生成 distinct 查询的 PostgreSQL 查询树
 *
 * 参数说明：
 * - database: 数据库名称
 * - distinctSpec: MongoDB distinct 命令的 BSON 规范
 * - setStatementTimeout: 是否设置语句超时
 *
 * 返回值：
 * - 返回 PostgreSQL 的 Query 结构指针
 *
 * 说明：
 * - 将 distinct 命令转换为 SELECT DISTINCT 查询
 * - 返回指定字段的唯一值列表
 */
Query * GenerateDistinctQuery(text *database, pgbson *distinctSpec, bool
							  setStatementTimeout);

/*
 * GenerateListCollectionsQuery - 生成 listCollections 查询的 PostgreSQL 查询树
 *
 * 参数说明：
 * - database: 数据库名称
 * - listCollectionsSpec: listCollections 命令的 BSON 规范
 * - queryData: 查询元数据
 * - addCursorParams: 是否添加游标参数
 * - setStatementTimeout: 是否设置语句超时
 *
 * 返回值：
 * - 返回 PostgreSQL 的 Query 结构指针
 *
 * 说明：
 * - 从系统元数据表查询集合列表
 * - 支持过滤条件（如按名称过滤）
 */
Query * GenerateListCollectionsQuery(text *database, pgbson *listCollectionsSpec,
									 QueryData *queryData,
									 bool addCursorParams, bool setStatementTimeout);

/*
 * GenerateListIndexesQuery - 生成 listIndexes 查询的 PostgreSQL 查询树
 *
 * 参数说明：
 * - database: 数据库名称
 * - listIndexesSpec: listIndexes 命令的 BSON 规范
 * - queryData: 查询元数据
 * - addCursorParams: 是否添加游标参数
 * - setStatementTimeout: 是否设置语句超时
 *
 * 返回值：
 * - 返回 PostgreSQL 的 Query 结构指针
 *
 * 说明：
 * - 从系统元数据表查询索引列表
 * - 返回索引的完整定义信息
 */
Query * GenerateListIndexesQuery(text *database, pgbson *listIndexesSpec,
								 QueryData *queryData,
								 bool addCursorParams, bool setStatementTimeout);

/*
 * GenerateAggregationQuery - 生成聚合管道查询的 PostgreSQL 查询树
 *
 * 参数说明：
 * - database: 数据库名称
 * - aggregationSpec: MongoDB aggregate 命令的 BSON 规范（包含 pipeline 数组）
 * - queryData: 查询元数据
 * - addCursorParams: 是否添加游标参数
 * - setStatementTimeout: 是否设置语句超时
 *
 * 返回值：
 * - 返回 PostgreSQL 的 Query 结构指针
 *
 * 说明：
 * - 核心函数：将 MongoDB 聚合管道转换为 PostgreSQL 查询
 * - 支持 $match, $group, $project, $lookup, $unwind 等各种阶段
 * - 每个阶段会被转换为相应的 SQL 操作（WHERE, GROUP BY, JOIN 等）
 * - 复杂管道会被转换为嵌套子查询
 */
Query * GenerateAggregationQuery(text *database, pgbson *aggregationSpec,
								 QueryData *queryData, bool addCursorParams,
								 bool setStatementTimeout);

/* ========== 辅助函数声明 ========== */

/*
 * ParseGetMore - 解析 getMore 命令并返回游标 ID
 *
 * 参数说明：
 * - databaseName: 输出参数，返回数据库名称
 * - getMoreSpec: getMore 命令的 BSON 规范
 * - queryData: 输出参数，返回查询元数据
 * - setStatementTimeout: 是否设置语句超时
 *
 * 返回值：
 * - 返回游标 ID（用于标识要继续的游标）
 *
 * 说明：
 * - getMore 命令必须指定游标 ID
 * - 从游标存储中恢复游标状态
 */
int64_t ParseGetMore(text **databaseName, pgbson *getMoreSpec, QueryData *queryData, bool
					 setStatementTimeout);

/*
 * ValidateAggregationPipeline - 验证聚合管道的合法性
 *
 * 参数说明：
 * - databaseDatum: 数据库名称
 * - baseCollection: 基础集合名称
 * - pipelineValue: 聚合管道的 BSON 数组
 *
 * 说明：
 * - 在执行前检查管道是否有效
 * - 验证阶段顺序、参数类型等
 * - 不合法的管道会抛出错误
 */
void ValidateAggregationPipeline(text *databaseDatum, const StringView *baseCollection,
								 const bson_value_t *pipelineValue);

/*
 * LookupExtractCollectionAndPipeline - 从 $lookup 阶段提取集合和管道
 *
 * 参数说明：
 * - lookupValue: $lookup 阶段的 BSON 值
 * - collection: 输出参数，返回要关联的集合名称
 * - pipeline: 输出参数，返回关联的管道（如果有）
 *
 * 说明：
 * - $lookup 用于执行左外连接
 * - 支持简单的关联（通过 localField/foreignField）
 * - 也支持使用子管道进行复杂关联
 */
void LookupExtractCollectionAndPipeline(const bson_value_t *lookupValue,
										StringView *collection, bson_value_t *pipeline);

/*
 * GraphLookupExtractCollection - 从 $graphLookup 阶段提取集合名称
 *
 * 参数说明：
 * - lookupValue: $graphLookup 阶段的 BSON 值
 * - collection: 输出参数，返回要遍历的集合名称
 *
 * 说明：
 * - $graphLookup 用于递归遍历图结构
 * - 常用于社交网络、组织架构等场景
 */
void GraphLookupExtractCollection(const bson_value_t *lookupValue,
								  StringView *collection);

/*
 * ParseUnionWith - 解析 $unionWith 阶段
 *
 * 参数说明：
 * - existingValue: $unionWith 阶段的 BSON 值
 * - collectionFrom: 输出参数，返回要合并的集合名称
 * - pipeline: 输出参数，返回可选的管道
 *
 * 说明：
 * - $unionWith 用于合并多个集合的结果
 * - 类似于 SQL 的 UNION ALL
 */
void ParseUnionWith(const bson_value_t *existingValue, StringView *collectionFrom,
					bson_value_t *pipeline);

/*
 * ParseInputDocumentForTopAndBottom - 解析 $top/$bottom 阶段的输入文档
 *
 * 参数说明：
 * - inputDocument: 输入文档
 * - input: 输出参数，返回输入表达式
 * - elementsToFetch: 输出参数，返回要获取的元素数量
 * - sortSpec: 输出参数，返回排序规范
 * - opName: 操作符名称（用于错误消息）
 *
 * 说明：
 * - $top 返回排序后的前 N 个元素
 * - $bottom 返回排序后的后 N 个元素
 */
void ParseInputDocumentForTopAndBottom(const bson_value_t *inputDocument,
									   bson_value_t *input,
									   bson_value_t *elementsToFetch,
									   bson_value_t *sortSpec, const char *opName);

/*
 * ParseInputDocumentForMedianAndPercentile - 解析 $median/$percentile 阶段的输入文档
 *
 * 参数说明：
 * - inputDocument: 输入文档
 * - input: 输出参数，返回输入表达式
 * - p: 输出参数，返回百分位数
 * - method: 输出参数，返回计算方法
 * - isMedianOp: 是否为 $median 操作
 *
 * 说明：
 * - $median 计算中位数
 * - $percentile 计算指定的百分位数
 */
void ParseInputDocumentForMedianAndPercentile(const bson_value_t *inputDocument,
											  bson_value_t *input, bson_value_t *p,
											  bson_value_t *method, bool isMedianOp);

/*
 * ValidateElementForNGroupAccumulators - 验证 N 元组分组累加器的元素
 *
 * 参数说明：
 * - elementsToFetch: 要获取的元素
 * - opName: 操作符名称（用于错误消息）
 *
 * 说明：
 * - 某些累加器（如 $topN）需要获取多个元素
 * - 此函数验证参数的合法性
 */
void ValidateElementForNGroupAccumulators(bson_value_t *elementsToFetch, const
										  char *opName);

/*
 * ParseInputForNGroupAccumulators - 解析 N 元组分组累加器的输入
 *
 * 参数说明：
 * - inputDocument: 输入文档
 * - input: 输出参数，返回输入表达式
 * - elementsToFetch: 输出参数，返回要获取的元素数量
 * - opName: 操作符名称
 *
 * 说明：
 * - 解析 $topN、$bottomN、$firstN、$lastN 等操作符的参数
 */
void ParseInputForNGroupAccumulators(const bson_value_t *inputDocument,
									 bson_value_t *input,
									 bson_value_t *elementsToFetch,
									 const char *opName);

/* ========== 全局变量 ========== */

/*
 * DefaultCursorFirstPageBatchSize - 游标第一页的默认批大小
 *
 * 说明：
 * - 首次查询时，如果没有指定 batchSize，使用此默认值
 * - 通常设置为 101（MongoDB 的默认值）
 * - 第一页返回更多数据可以提高用户体验
 */
extern int DefaultCursorFirstPageBatchSize;

/*
 * GenerateFirstPageQueryData - 生成首页查询的 QueryData 结构
 *
 * 返回值：
 * - 返回初始化为默认值的 QueryData 结构
 *
 * 说明：
 * - 用于创建首次查询的查询元数据
 * - batchSize 设置为默认值
 * - 其他字段初始化为零或空值
 */
/* Generates a base QueryData used for the first page */
inline static QueryData
GenerateFirstPageQueryData(void)
{
	QueryData queryData = { 0 };
	queryData.batchSize = DefaultCursorFirstPageBatchSize;
	return queryData;
}


#endif /* BSON_AGGREGATION_PIPELINE_H */
