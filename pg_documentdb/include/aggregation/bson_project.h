/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_project.h
 *
 * Common declarations of functions for handling bson projection.
 * 用于处理 BSON 投影（Projection）操作的函数公共声明
 *
 * 投影操作用于控制查询结果中返回哪些字段，支持包含（inclusion）和排除（exclusion）模式。
 * 这是 MongoDB 查询和聚合管道中非常重要的功能。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_PROJECT_H
#define BSON_PROJECT_H


/* 包含 BSON 核心数据结构和操作定义 */
#include "io/bson_core.h"
/* 包含投影路径树相关定义 */
#include "aggregation/bson_projection_tree.h"

/* 导出的 _id 字符串常量，用于快速识别 _id 字段 */
extern PGDLLIMPORT const StringView IdFieldStringView;

/* 前向声明：投影查询状态结构（无需暴露结构布局） */
typedef struct BsonProjectionQueryState BsonProjectionQueryState;
/* 前向声明：投影文档函数表结构 */
typedef struct BsonProjectDocumentFunctions BsonProjectDocumentFunctions;

/* 前向声明：文档投影状态结构 */
typedef struct ProjectDocumentState ProjectDocumentState;

/*
 * 函数指针类型：处理中间数组节点的回调函数
 *
 * 用于在投影过程中处理遇到数组字段时的特殊逻辑。
 *
 * 参数:
 *   node - 当前的中间路径节点
 *   state - 文档投影状态
 *   sourceValue - 源值的迭代器
 *
 * 返回值:
 *   true 表示已成功处理，false 表示需要继续默认处理
 */
typedef bool (*TryHandleIntermediateArrayNodeFunc)(const BsonIntermediatePathNode *node,
												   ProjectDocumentState *state,
												   bson_iter_t *sourceValue);

/*
 * 函数指针类型：初始化待处理投影状态的回调函数
 *
 * 用于某些需要延迟处理的投影场景，如 $elemMatch。
 *
 * 参数:
 *   totalPendingProjections - 待处理的投影数量
 *
 * 返回值:
 *   返回初始化的状态对象指针
 */
typedef void *(*InitializePendingProjectionStateFunc)(uint32_t totalPendingProjections);

/*
 * 函数指针类型：写入待处理投影的回调函数
 *
 * 将延迟处理的投影数据写入最终的写入器。
 *
 * 参数:
 *   writer - BSON 写入器
 *   pendingProjections - 待处理的投影数据
 */
typedef void (*WritePendingProjectionFunc)(pgbson_writer *writer,
										   void *pendingProjections);

/*
 * 函数指针类型：判断是否跳过未解析中间字段的回调函数
 *
 * 用于某些特殊投影场景，决定是否跳过中间数组字段。
 *
 * 参数:
 *   tree - 路径树节点
 *
 * 返回值:
 *   true 表示跳过，false 表示不跳过
 */
typedef bool (*SkipUnresolvedIntermediateFieldsFunc)(const
													 BsonIntermediatePathNode *tree);


/*
 * 投影操作上下文结构
 *
 * 存储投影操作所需的配置和状态信息。
 */
typedef struct BsonProjectionContext
{
	/* 是否强制投影 _id 字段（即使在排除模式中也会返回 _id） */
	bool forceProjectId;

	/* 是否允许同时使用包含和排除投影（默认不允许混合使用） */
	bool allowInclusionExclusion;

	/* 路径规范迭代器，用于遍历投影规范 */
	bson_iter_t *pathSpecIter;

	/* 位置投影的查询规范。当 `isFindProjection` 为 false 时，此值为 null */
	pgbson *querySpec;

	/* let 子句中的变量规范，用于在投影中引用变量 */
	const pgbson *variableSpec;

	/* 排序规则字符串，用于字符串比较时的排序规则 */
	const char *collationString;
} BsonProjectionContext;

/*
 * 投影文档函数表结构
 *
 * 定义投影操作过程中各个阶段的钩子函数，用于自定义投影行为。
 */
typedef struct BsonProjectDocumentFunctions
{
	/*
	 * 处理中间数组字段的函数指针
	 * 例如：$ 投影使用第一个中间数组字段，应用查询找到匹配的索引并进行投影
	 */
	TryHandleIntermediateArrayNodeFunc tryMoveArrayIteratorFunc;

	/*
	 * 初始化待处理投影状态的函数指针
	 * 用于某些投影需要延迟写入的场景
	 * 例如：$elemMatch 投影需要先找到匹配元素再写入
	 */
	InitializePendingProjectionStateFunc initializePendingProjectionFunc;

	/*
	 * 写入待处理投影的函数指针
	 * 用于将延迟处理的投影数据写入主写入器
	 */
	WritePendingProjectionFunc writePendingProjectionFunc;
} BsonProjectDocumentFunctions;

/*
 * 文档投影状态结构
 *
 * 存储单个文档投影过程中的状态信息。
 */
typedef struct ProjectDocumentState
{
	/* 进行投影的源文档 */
	pgbson *parentDocument;

	/* 当前投影的变量上下文，用于解析变量引用 */
	const ExpressionVariableContext *variableContext;

	/*
	 * 标记位置投影的查询是否已用于评估匹配索引
	 * 位置路径规范只能由最外层数组使用一次
	 * 例如：{a.b.c.d.$: 1} => 如果 `b` 和 `d` 都是数组字段，则位置投影应用于 `b`
	 */
	bool isPositionalAlreadyEvaluated;

	/* 是否是排除投影（true 表示排除模式） */
	bool hasExclusion;

	/* 可选的投影文档阶段函数钩子 */
	BsonProjectDocumentFunctions projectDocumentFuncs;

	/* 整个文档的待处理投影状态 */
	void *pendingProjectionState;

	/*
	 * 是否跳过中间数组字段的所有元素投影
	 * 例如：$geoNear 更新文档时，对于冲突的中间数组路径，会覆盖整个数组并转换为单个嵌套对象
	 *
	 * 示例：
	 *   文档 => {a : {b: [{c: 10}, {c: 20}]]}}
	 *   投影 => { a.b.c: 100 }
	 *   结果 => { a: {b: {c: 100} }
	 */
	bool skipIntermediateArrayFields;
} ProjectDocumentState;


/*
 * ReplaceRoot 和 Redact 阶段的缓存状态
 *
 * 存储这两个聚合阶段的表达式和变量上下文信息。
 */
typedef struct BsonReplaceRootRedactState
{
	/* 聚合表达式数据，定义如何替换或编辑文档 */
	AggregationExpressionData *expressionData;

	/* let 子句的变量上下文（如果有） */
	ExpressionVariableContext *variableContext;
} BsonReplaceRootRedactState;


/*
 * GetProjectionStateForBsonProject - 为 $project 阶段获取投影状态
 *
 * 根据投影规范构建投影查询状态，用于控制文档的输出字段。
 *
 * 参数:
 *   projectionSpecIter - 投影规范迭代器
 *   forceProjectId - 是否强制包含 _id 字段
 *   allowInclusionExclusion - 是否允许同时使用包含和排除
 *   variableSpec - 变量规范
 *
 * 返回值:
 *   投影查询状态常量指针
 *
 * $project 支持的操作:
 * - 字段包含/排除: { field: 1 } 或 { field: 0 }
 * - 字段重命名: { newField: "$oldField" }
 * - 表达式计算: { total: { $add: ["$price", "$tax"] } }
 * - 数组操作: { firstGrade: { $arrayElemAt: ["$grades", 0] } }
 */
const BsonProjectionQueryState * GetProjectionStateForBsonProject(
	bson_iter_t *projectionSpecIter, bool forceProjectId,
	bool
	allowInclusionExclusion,
	const pgbson *
	variableSpec);

/*
 * GetProjectionStateForBsonAddFields - 为 $addFields 阶段获取投影状态
 *
 * $addFields 是 $project 的特殊形式，它保留所有现有字段并添加新字段。
 *
 * 参数:
 *   projectionSpecIter - 投影规范迭代器
 *   variableSpec - 变量规范
 *
 * 返回值:
 *   投影查询状态常量指针
 *
 * $addFields 特点:
 * - 保留文档的所有原有字段
 * - 添加新字段或覆盖现有字段
 * - 支持使用表达式计算字段值
 */
const BsonProjectionQueryState * GetProjectionStateForBsonAddFields(
	bson_iter_t *projectionSpecIter, const bson_value_t *variableSpec);

/*
 * GetProjectionStateForBsonUnset - 为 $unset 阶段获取投影状态
 *
 * $unset 用于从文档中移除指定字段，等同于 { field: 0 } 投影。
 *
 * 参数:
 *   unsetValue - 要移除的字段列表或单个字段
 *   forceProjectId - 是否强制包含 _id 字段
 *
 * 返回值:
 *   投影查询状态常量指针
 *
 * $unset 示例:
 *   { $unset: ["field1", "field2"] }
 *   或 { $unset: "field1" }
 */
const BsonProjectionQueryState * GetProjectionStateForBsonUnset(const bson_value_t *
																unsetValue,
																bool forceProjectId);

/*
 * GetBsonValueForReplaceRoot - 获取 $replaceRoot 阶段的 BSON 值
 *
 * 从迭代器中提取 $replaceRoot 的替换值。
 *
 * 参数:
 *   replaceRootIterator - 替换根规范迭代器
 *   value - 输出参数，用于存储提取的值
 */
void GetBsonValueForReplaceRoot(bson_iter_t *replaceRootIterator, bson_value_t *value);

/*
 * ValidateReplaceRootElement - 验证 $replaceRoot 元素
 *
 * 验证替换根的值是否有效。
 *
 * 参数:
 *   value - 要验证的 BSON 值
 *
 * 验证规则:
 * - 必须是有效的 BSON 文档
 * - 不能包含无效的表达式
 */
void ValidateReplaceRootElement(const bson_value_t *value);

/*
 * PopulateReplaceRootExpressionDataFromSpec - 从规范填充 $replaceRoot 表达式数据
 *
 * 解析 $replaceRoot 规范，构建表达式数据结构。
 *
 * 参数:
 *   state - 替换根状态对象
 *   pathSpec - 路径规范（新根文档的表达式）
 *   variableSpec - 变量规范
 *   collationString - 排序规则字符串
 */
void PopulateReplaceRootExpressionDataFromSpec(BsonReplaceRootRedactState *state,
											   const bson_value_t *pathSpec,
											   pgbson *variableSpec,
											   const char *collationString);


/*
 * BuildRedactState - 构建 $redact 阶段的状态
 *
 * $redact 根据条件控制文档内容的可见性，用于实现字段级别的访问控制。
 *
 * 参数:
 *   redactState - 编辑状态对象（输出参数）
 *   redactValue - 编辑表达式，决定如何处理文档
 *   variableSpec - 变量规范
 *   collationString - 排序规则字符串
 *
 * $redact 工作原理:
 * - 使用 $$DESCEND 保留内容
 * - 使用 $$PRUNE 移除内容
 * - 使用 $$KEEP 保留当前文档
 */
void BuildRedactState(BsonReplaceRootRedactState *redactState, const
					  bson_value_t *redactValue, pgbson *variableSpec, const
					  char *collationString);

/*
 * ProjectDocumentWithState - 使用投影状态投影文档
 *
 * 根据投影状态对文档进行投影操作，返回包含指定字段的新文档。
 *
 * 参数:
 *   sourceDocument - 源文档
 *   state - 投影查询状态
 *
 * 返回值:
 *   投影后的新文档
 */
pgbson * ProjectDocumentWithState(pgbson *sourceDocument,
								  const BsonProjectionQueryState *state);

/*
 * ProjectReplaceRootDocument - 执行 $replaceRoot 操作
 *
 * 将文档替换为指定的值，通常用于将嵌套文档提升为根文档。
 *
 * 参数:
 *   document - 原始文档
 *   replaceRootExpression - 替换表达式
 *   variableContext - 变量上下文
 *   forceProjectId - 是否强制包含 _id 字段
 *
 * 返回值:
 *   替换根后的新文档
 *
 * 使用场景:
 * - 将嵌套文档提升为根: { $replaceRoot: { newRoot: "$contact" } }
 * - 使用表达式创建新根: { $replaceRoot: { newRoot: { name: "$firstName", ... } } }
 */
pgbson * ProjectReplaceRootDocument(pgbson *document,
									const AggregationExpressionData *replaceRootExpression,
									const ExpressionVariableContext *variableContext,
									bool forceProjectId);

/* projection writer functions - 投影写入器函数 */

/*
 * TraverseObjectAndAppendToWriter - 遍历对象并追加到写入器
 *
 * 递归遍历文档对象，根据投影规范将匹配的字段写入 BSON 写入器。
 *
 * 参数:
 *   parentIterator - 父对象的迭代器
 *   pathSpecTree - 投影规范路径树
 *   writer - BSON 写入器
 *   projectNonMatchingFields - 是否投影不匹配的字段
 *   projectDocState - 文档投影状态
 *   isInNestedArray - 是否在嵌套数组中
 */
void TraverseObjectAndAppendToWriter(bson_iter_t *parentIterator,
									 const BsonIntermediatePathNode *pathSpecTree,
									 pgbson_writer *writer,
									 bool projectNonMatchingFields,
									 ProjectDocumentState *projectDocState,
									 bool isInNestedArray);


/*
 * TryInlineProjection - 尝试内联投影优化
 *
 * 尝试将投影操作直接内联到查询表达式中，以提高性能。
 *
 * 参数:
 *   currentExpr - 当前表达式节点
 *   functionOid - 函数对象 ID
 *   projectValue - 投影值
 *
 * 返回值:
 *   true 表示成功内联，false 表示不能内联
 *
 * 优化原理:
 * - 如果投影只是简单的字段访问，可以直接在源文档上操作
 * - 避免创建中间文档，减少内存分配和复制
 */
bool TryInlineProjection(Node *currentExpr, Oid functionOid, const
						 bson_value_t *projectValue);

#endif
