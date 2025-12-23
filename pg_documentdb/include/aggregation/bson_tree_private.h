/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_tree_private.h
 *
 * Private declarations functions used for creating the Bson trees
 * 用于创建 BSON 树的私有声明函数
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_TREE_PRIVATE
// 如果未定义 BSON_TREE_PRIVATE，则报错
// 提示开发者应该导入 bson_tree.h 或 bson_projection_tree.h 而不是直接导入此文件
#error Do not import this header file. Import bson_tree.h / bson_projection_tree.h instead
// 不要直接导入此头文件。请改为导入 bson_tree.h / bson_projection_tree.h
#endif

#ifndef BSON_TREE_PRIVATE_H
#define BSON_TREE_PRIVATE_H

#include "aggregation/bson_tree.h"
#include "aggregation/bson_tree_common.h"
#include "utils/error_utils.h"
#include "utils/string_view.h"


// 获取或添加子节点到指定的中间节点树中
// 参数：
//   tree - 父中间节点
//   relativePath - 相对路径字符串
//   fieldPath - 字段路径的字符串视图
//   createFunc - 创建叶子节点的回调函数
//   createIntermediateNodeFunc - 创建中间节点的回调函数
//   childNodeType - 子节点的类型
//   replaceExistingNodes - 是否替换已存在的节点
//   nodeCreationState - 节点创建状态数据
//   alreadyExists - 输出参数，表示节点是否已存在
// 返回：子节点指针
BsonPathNode * GetOrAddChildNode(BsonIntermediatePathNode *tree,
								 const char *relativePath,
								 const StringView *fieldPath,
								 CreateLeafNodeFunc createFunc,
								 CreateIntermediateNodeFunc
								 createIntermediateNodeFunc,
								 NodeType childNodeType,
								 bool replaceExistingNodes,
								 void *nodeCreationState,
								 bool *alreadyExists);

// 确保字段路径字符串有效
// 如果字段路径无效，会抛出错误
void EnsureValidFieldPathString(const StringView *fieldPath);

// 遍历点号分隔的路径并获取对应的节点
// 参数：
//   path - 要遍历的路径（点号分隔，如 "a.b.c"）
//   tree - 起始中间节点树
//   hasFieldForIntermediateNode - 中间节点是否包含字段信息
//   createFunc - 创建叶子节点的回调函数
//   createIntermediateNodeFunc - 创建中间节点的回调函数
//   leafNodeType - 叶子节点类型
//   replaceExistingNodes - 是否替换已存在的节点
//   nodeCreationState - 节点创建状态数据
//   alreadyExists - 输出参数，表示节点是否已存在
// 返回：找到或创建的节点指针
BsonPathNode * TraverseDottedPathAndGetNode(const StringView *path,
											BsonIntermediatePathNode *tree,
											bool hasFieldForIntermediateNode,
											CreateLeafNodeFunc createFunc,
											CreateIntermediateNodeFunc
											createIntermediateNodeFunc,
											NodeType leafNodeType,
											bool replaceExistingNodes,
											void *nodeCreationState,
											bool *alreadyExists);

// 验证并设置叶子节点数据
// 参数：
//   childNode - 要设置的叶子节点
//   value - BSON 值
//   relativePath - 相对路径
//   treatAsConstantExpression - 是否作为常量表达式处理
//   parseContext - 解析上下文
// 返回：成功返回 true，失败返回 false
bool ValidateAndSetLeafNodeData(BsonPathNode *childNode, const bson_value_t *value,
								const StringView *relativePath,
								bool treatAsConstantExpression,
								ParseAggregationExpressionContext *parseContext);

// 尝试设置中间节点数据
// 参数：
//   node - 要设置的节点
//   relativePath - 相对路径
//   hasFields - 是否有字段信息
// 返回：成功返回 true，失败返回 false
bool TrySetIntermediateNodeData(BsonPathNode *node, const StringView *relativePath,
								bool hasFields);

// 添加子节点到树中
// 参数：
//   childData - 子节点数据
//   childNode - 要添加的子节点
void AddChildToTree(ChildNodeData *childData, BsonPathNode *childNode);

// 在节点核心中替换树
// 用于节点替换操作，将旧节点替换为新节点
// 参数：
//   previousNode - 前一个节点
//   baseNode - 基础节点
//   newNode - 新节点
void ReplaceTreeInNodeCore(BsonPathNode *previousNode, BsonPathNode *baseNode,
						   BsonPathNode *newNode);

// 确定子节点类型
// 根据 BSON 值和选项确定应该创建的节点类型
// 参数：
//   value - BSON 值
//   forceLeafExpression - 是否强制为叶子表达式
// 返回：确定的节点类型
NodeType DetermineChildNodeType(const bson_value_t *value,
								bool forceLeafExpression);

/*
 * Given a Node pointed to by childNode, initializes the base data
 * for the base PathNode.
 * 给定由 childNode 指向的节点，初始化基础 PathNode 的基础数据。
 */
inline static void
SetBasePathNodeData(BsonPathNode *childNode, NodeType finalNodeType,
					const StringView *fieldPath, BsonIntermediatePathNode *tree)
{
	// 创建基础节点结构，初始化节点的核心属性
	BsonPathNode baseNode =
	{
		.nodeType = finalNodeType,     // 节点类型（叶子节点或中间节点）
		.field = *fieldPath,           // 字段路径（如 "a.b.c"）
		.parent = tree,                // 父节点指针
		.next = NULL                   // 下一个兄弟节点（用于链表）
	};

	// 将初始化好的基础节点数据复制到目标节点
	memcpy(childNode, &baseNode, sizeof(BsonPathNode));
}


/*
 * Helper method that throws the Path collision error on intermediate node mismatch.
 * 辅助方法，当中间节点不匹配时抛出路径冲突错误。
 * 当尝试创建的节点与现有节点类型不匹配时（例如一个是中间节点，另一个是叶子节点），
 * 会调用此函数报告路径冲突。
 */
inline static void
pg_attribute_noreturn()  // 告诉编译器此函数不会返回（会抛出错误）
ThrowErrorOnIntermediateMismatch(BsonPathNode * node, const StringView * relativePath)
{
	// 默认错误码：路径冲突错误（Location31250）
	int errorCode = ERRCODE_DOCUMENTDB_LOCATION31250;

	// 创建错误消息字符串缓冲区
	StringInfo errorMessageStr = makeStringInfo();

	// 构建基础错误消息：检测到路径冲突
	appendStringInfo(errorMessageStr, "Collision detected in specified path %.*s",
					 relativePath->length,
					 relativePath->string);

	// 如果节点字段长度小于相对路径长度，说明是部分路径冲突
	// 例如：已有 "a.b" 节点，尝试添加 "a.b.c"
	if (node->field.length < relativePath->length)
	{
		// 更改错误码为部分路径冲突（Location31249）
		errorCode = ERRCODE_DOCUMENTDB_LOCATION31249;

		// 提取剩余的路径部分（从当前节点长度+1开始，跳过点号）
		StringView substring = StringViewSubstring(relativePath,
												   node->field.length +
												   1);

		// 将剩余路径追加到错误消息
		appendStringInfo(errorMessageStr, " remaining portion %.*s",
						 substring.length, substring.string);
	}

	// 抛出 PostgreSQL 错误，使用构建的错误码和消息
	ereport(ERROR, (errcode(errorCode),
					errmsg("%s", errorMessageStr->data)));
}

#endif
