/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_projection_tree.h
 *
 * Common declarations of functions for handling bson path trees for projection.
 * 用于处理投影操作中 BSON 路径树的函数公共声明
 *
 * 路径树是一种数据结构，用于表示和操作文档中的嵌套字段路径。
 * 在投影操作中，路径树决定了哪些字段应该被包含或排除。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_PROJECTION_TREE_H
#define BSON_PROJECTION_TREE_H

/* 包含 BSON 路径树相关定义 */
#include "aggregation/bson_tree.h"
/* 包含位置查询相关定义 */
#include "aggregation/bson_positional_query.h"
/* 包含字符串视图工具 */
#include "utils/string_view.h"


/*
 * 路径树构建函数表结构
 *
 * 定义在创建路径树过程中各个阶段的钩子函数，用于自定义路径树的构建行为。
 */
typedef struct BuildBsonPathTreeFunctions
{
	/* 创建子叶子节点的函数指针 */
	CreateLeafNodeFunc createLeafNodeFunc;

	/* 创建中间节点的函数指针 */
	CreateIntermediateNodeFunc createIntermediateNodeFunc;

	/* 预处理字段路径的函数指针，在路径添加到树之前进行转换 */
	StringView (*preprocessPathFunc)(const StringView *path, const
									 bson_value_t *pathSpecValue,
									 bool isOperator,
									 bool *isFindOperator,
									 void *state,
									 bool *skipInclusionExclusionValidation);

	/* 验证已存在节点的函数指针，在节点已存在时调用 */
	void (*validateAlreadyExistsNodeFunc)(void *state, const StringView *path,
										  BsonPathNode *node);

	/* 叶子节点创建后的后处理函数指针 */
	void (*postProcessLeafNodeFunc)(void *state, const StringView *path,
									BsonPathNode *node, bool *isExclusionIfNoInclusion,
									bool *hasFieldsForIntermediate);
} BuildBsonPathTreeFunctions;

/*
 * 路径树构建上下文结构
 *
 * 存储构建 BSON 路径树所需的配置和状态信息。
 */
typedef struct BuildBsonPathTreeContext
{
	/* IN: 是否允许同时使用包含和排除投影 */
	bool allowInclusionExclusion;

	/* IN: 是否跳过聚合表达式解析，将表达式视为常量（如构建通配符索引规范时） */
	bool skipParseAggregationExpressions;

	/* OUT: 规范中是否包含字段排除 */
	bool hasExclusion;

	/* OUT: 规范中是否包含字段包含 */
	bool hasInclusion;

	/* OUT: 树中是否包含非恒定聚合表达式的叶子节点 */
	bool hasAggregationExpressions;

	/* IN: 聚合表达式解析上下文 */
	ParseAggregationExpressionContext parseAggregationContext;

	/* INOUT: 路径树状态，用于特殊 find 查询操作符 */
	void *pathTreeState;

	/* 如果节点已存在则跳过（不覆盖） */
	bool skipIfAlreadyExists;

	/* IN: 构建路径树时处理中间阶段的函数表 */
	BuildBsonPathTreeFunctions *buildPathTreeFuncs;
} BuildBsonPathTreeContext;


/*
 * 默认路径树函数实现
 *
 * 提供投影操作的默认路径树遍历行为。
 * 这些函数可以被特定的投影场景（如 find 查询投影）覆盖。
 */
extern BuildBsonPathTreeFunctions DefaultPathTreeFuncs;

/*
 * 带上下文的叶子节点结构
 *
 * 用于 find 查询投影操作符的树叶子节点，包含额外的状态信息。
 */
typedef struct BsonLeafNodeWithContext
{
	/* 路径的基础节点 */
	BsonLeafPathNode base;

	/* 节点的附加状态（如操作符特定数据） */
	void *context;
} BsonLeafNodeWithContext;


/*
 * BuildBsonPathTree - 构建 BSON 路径树
 *
 * 根据路径规范构建路径树，用于表示投影操作的字段选择逻辑。
 *
 * 参数:
 *   pathSpecification - 路径规范迭代器
 *   context - 路径树构建上下文
 *   forceLeafExpression - 是否强制将叶子节点作为表达式处理
 *   hasFields - 输出参数，指示是否有字段被添加
 *
 * 返回值:
 *   构建的路径树根节点
 *
 * 路径树结构示例:
 *   投影: { "a.b": 1, "a.c": 1 }
 *   树结构:
 *     root
 *       └── a (intermediate)
 *           ├── b (leaf)
 *           └── c (leaf)
 */
BsonIntermediatePathNode * BuildBsonPathTree(bson_iter_t *pathSpecification,
											 BuildBsonPathTreeContext *context,
											 bool forceLeafExpression,
											 bool *hasFields);

/*
 * MergeBsonPathTree - 合并 BSON 路径树
 *
 * 将新的路径规范合并到现有的路径树中。
 *
 * 参数:
 *   root - 现有路径树的根节点
 *   pathSpecification - 要合并的路径规范迭代器
 *   context - 路径树构建上下文
 *   forceLeafExpression - 是否强制将叶子节点作为表达式处理
 *   hasFields - 输出参数，指示是否有新字段被添加
 *
 * 合并策略:
 * - 如果路径已存在，根据 skipIfAlreadyExists 决定是否覆盖
 * - 如果路径不存在，添加新的分支
 * - 处理包含/排除冲突
 */
void MergeBsonPathTree(BsonIntermediatePathNode *root,
					   bson_iter_t *pathSpecification,
					   BuildBsonPathTreeContext *context,
					   bool forceLeafExpression,
					   bool *hasFields);

/*
 * CastAsBsonLeafNodeWithContext - 将节点转换为带上下文的叶子节点
 *
 * 辅助函数，用于安全地将路径节点转换为带上下文的叶子节点。
 *
 * 参数:
 *   toCast - 要转换的路径节点
 *
 * 返回值:
 *   转换后的带上下文叶子节点指针
 *
 * 断言:
 *   节点类型必须是叶子节点类型
 */
inline static const BsonLeafNodeWithContext *
CastAsBsonLeafNodeWithContext(const BsonPathNode *toCast)
{
	Assert(NodeType_IsLeaf(toCast->nodeType));
	return (const BsonLeafNodeWithContext *) toCast;
}


#endif
