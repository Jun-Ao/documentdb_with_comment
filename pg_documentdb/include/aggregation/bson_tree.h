/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_tree.h
 *
 * Common declarations of functions for handling bson path trees.
 * 用于处理 BSON 路径树的函数公共声明
 *
 * 路径树是 DocumentDB 中用于表示和操作嵌套 BSON 文档的核心数据结构。
 * 它支持点号路径（如 "a.b.c"）的解析、遍历和修改操作。
 *
 *-------------------------------------------------------------------------
 */
#include "aggregation/bson_tree_common.h"
#include "utils/documentdb_errors.h"
#include "operators/bson_expression.h"

#ifndef BSON_TREE_H
#define BSON_TREE_H


/* 数据类型的前向声明 */
struct BsonPathNode;
struct BsonIntermediatePathNode;
struct BsonLeafPathNode;
struct BsonLeafArrayWithFieldPathNode;


/*
 * 基础路径节点结构
 *
 * 所有路径节点类型都从此结构派生。
 * 对于派生类型，此结构必须是第一个字段（C 语言多态模式）。
 */
typedef struct BsonPathNode
{
	/* 节点类型
	 * 指示此节点是路径包含/排除/字段，还是通向字段路径的树中间节点
	 */
	const NodeType nodeType;

	/* 投影的当前字段路径
	 * 这是投影树当前级别的非点号字段路径。
	 *
	 * 示例：对于 {"a": {"b.c": {"d.e.f": 1}}}，
	 * 节点 "d"、"e"、"f" 的 field 值分别为 "d"、"e"、"f"。
	 */
	const StringView field;

	/* 父节点指针 */
	struct BsonIntermediatePathNode *const parent;

	/* 树中的兄弟节点指针（形成链表） */
	struct BsonPathNode *next;
} BsonPathNode;


/*
 * 子节点数据结构
 *
 * 存储具有子节点的节点类型的子节点信息。
 * 目前用于 LeafWithArray 和 Intermediate 节点。
 *
 * 注意：这是一个内部结构，不应该直接访问！
 */
typedef struct ChildNodeData
{
	/* 此节点的子节点数量 */
	uint32_t numChildren;

	/* 子节点链表
	 * children 指向中间节点的尾部（哨兵节点）。
	 * 给定树 "a": { "b": 1, "c": 1 }，children 指向 b，
	 * b.next 指向 c。
	 * 第一个子节点是 tree->children->next。
	 *
	 * 警告：
	 * - 不要直接枚举子节点
	 * - 不要直接添加子节点
	 * - 请使用 foreach_child 宏
	 */
	BsonPathNode *children;
} ChildNodeData;

/*
 * 中间路径节点结构
 *
 * 表示路径树中的中间节点，对应嵌套文档的中间层级。
 */
typedef struct BsonIntermediatePathNode
{
	/* 基础节点 */
	BsonPathNode baseNode;

	/* 子节点中是否至少包含 1 个表达式字段 */
	bool hasExpressionFieldsInChildren;

	/* 中间节点的子节点数据
	 * 不要直接访问此字段。
	 * 使用 foreach_child 宏来枚举子节点
	 */
	ChildNodeData childData;
} BsonIntermediatePathNode;


/*
 * 叶子路径节点结构
 *
 * 表示路径树中的叶子节点，对应实际的字段值或表达式。
 */
typedef struct BsonLeafPathNode
{
	/* 基础节点 */
	BsonPathNode baseNode;

	/* 字段数据
	 * 可以是常量及其值，或者是操作符、字段表达式等。
	 * 例如：{ field: 1 } 中的 1，或 { field: { $add: [1, 2] } } 中的表达式。
	 */
	AggregationExpressionData fieldData;
} BsonLeafPathNode;


/*
 * 带数组的叶子路径节点结构
 *
 * 表示路径树中对应数组字段的叶子节点。
 * 此节点类型可以有子节点，用于表示数组元素的路径。
 */
typedef struct BsonLeafArrayWithFieldPathNode
{
	/* 基础叶子节点数据 */
	BsonLeafPathNode leafData;

	/* 数组子节点的数据
	 * 不要直接访问此字段。
	 * 使用 foreach_array_child 宏来枚举数组子节点
	 */
	ChildNodeData arrayChild;
} BsonLeafArrayWithFieldPathNode;


/*
 * 创建叶子节点的函数指针类型
 *
 * 定义创建叶子节点的回调函数类型，用于自定义节点创建行为。
 */
typedef BsonLeafPathNode *(*CreateLeafNodeFunc)(const StringView *path, const
												char *relativePath, void *state);


/*
 * 创建中间节点的函数指针类型
 *
 * 定义创建中间节点的回调函数类型，用于自定义节点创建行为。
 */
typedef BsonIntermediatePathNode *(*CreateIntermediateNodeFunc)(const StringView *path,
																const char *relativePath,
																void *state);


const BsonLeafPathNode * TraverseDottedPathAndGetOrAddLeafFieldNode(const
																	StringView *path,
																	const bson_value_t *
																	value,
																	BsonIntermediatePathNode
																	*tree,
																	CreateLeafNodeFunc
																	createFunc,
																	bool
																	treatLeafDataAsConstant,
																	bool *nodeCreated,
																	ParseAggregationExpressionContext
																	*parseContext);

const BsonLeafPathNode * TraverseDottedPathAndAddLeafFieldNode(const
															   StringView *path,
															   const bson_value_t *
															   value,
															   BsonIntermediatePathNode
															   *tree,
															   CreateLeafNodeFunc
															   createFunc,
															   bool
															   treatLeafDataAsConstant,
															   ParseAggregationExpressionContext
															   *parseContext);

const BsonLeafPathNode * TraverseDottedPathAndAddLeafValueNode(const StringView *path,
															   const bson_value_t *value,
															   BsonIntermediatePathNode *
															   tree,
															   CreateLeafNodeFunc
															   createFunc,
															   CreateIntermediateNodeFunc
															   intermediateFunc,
															   bool
															   treatLeafDataAsConstant,
															   ParseAggregationExpressionContext
															   *parseContext);

BsonLeafArrayWithFieldPathNode * TraverseDottedPathAndAddLeafArrayNode(const
																	   StringView *path,
																	   BsonIntermediatePathNode
																	   *tree,
																	   CreateIntermediateNodeFunc
																	   intermediateFunc,
																	   bool
																	   treatLeafDataAsConstant);

const BsonPathNode * TraverseDottedPathAndGetOrAddField(const StringView *path,
														const bson_value_t *value,
														BsonIntermediatePathNode *tree,
														CreateIntermediateNodeFunc
														createIntermediateFunc,
														CreateLeafNodeFunc createLeafFunc,
														bool treatLeafNodeAsConstant,
														void *nodeCreationState,
														bool *nodeCreated,
														ParseAggregationExpressionContext
														*parseContext);
const BsonPathNode * TraverseDottedPathAndGetOrAddValue(const StringView *path,
														const bson_value_t *value,
														BsonIntermediatePathNode *tree,
														CreateIntermediateNodeFunc
														createIntermediateFunc,
														CreateLeafNodeFunc createLeafFunc,
														bool treatLeafNodeAsConstant,
														void *nodeCreationState,
														bool *nodeCreated,
														ParseAggregationExpressionContext
														*parseContext);

BsonLeafPathNode * BsonDefaultCreateLeafNode(const StringView *fieldPath, const
											 char *relativePath, void *state);
BsonIntermediatePathNode * BsonDefaultCreateIntermediateNode(const StringView *fieldPath,
															 const char *relativePath,
															 void *state);

void ResetNodeWithField(const BsonLeafPathNode *baseLeafNode, const char *relativePath,
						const bson_value_t *value, CreateLeafNodeFunc createFunc,
						bool treatLeafDataAsConstant,
						ParseAggregationExpressionContext *parseContext);
void ResetNodeWithValue(const BsonLeafPathNode *baseLeafNode, const char *relativePath,
						const bson_value_t *value, CreateLeafNodeFunc createFunc,
						bool treatLeafDataAsConstant,
						ParseAggregationExpressionContext *parseContext);

/*
 * FreeTree - 释放路径树
 *
 * 递归释放路径树的所有节点和关联的内存。
 */
void FreeTree(BsonIntermediatePathNode *root);

/*
 * ResetNodeWithFieldAndState - 使用字段和状态重置节点
 *
 * 将叶子节点重置为包含指定字段的状态，并传递自定义状态。
 */
const BsonPathNode * ResetNodeWithFieldAndState(const BsonLeafPathNode *baseLeafNode,
												const char *relativePath,
												const bson_value_t *value,
												CreateLeafNodeFunc createFunc,
												bool treatLeafDataAsConstant,
												ParseAggregationExpressionContext *
												parseContext,
												void *leafState);
const BsonPathNode * ResetNodeWithValueAndState(const BsonLeafPathNode *baseLeafNode,
												const char *relativePath,
												const bson_value_t *value,
												CreateLeafNodeFunc createFunc,
												bool treatLeafDataAsConstant,
												ParseAggregationExpressionContext *
												parseContext,
												void *leafState);

const BsonLeafPathNode * AddValueNodeToLeafArrayWithField(
	BsonLeafArrayWithFieldPathNode *leafArrayField,
	const char *relativePath,
	int index,
	const bson_value_t *leafValue,
	CreateLeafNodeFunc createFunc,
	bool treatLeafDataAsConstant,
	ParseAggregationExpressionContext
	*parseContext);

void BuildTreeFromPgbson(BsonIntermediatePathNode *tree, pgbson *document,
						 ParseAggregationExpressionContext *parseContext);

/*
 * Helper function to create the Root node of a Bson Path Tree.
 * 辅助函数：创建 BSON 路径树的根节点
 */
inline static BsonIntermediatePathNode *
MakeRootNode()
{
	return palloc0(sizeof(BsonIntermediatePathNode));
}


/*
 * IsIntermediateNode - 判断节点是否为中间节点
 *
 * Evaluates to true when the BsonPathNode represents an intermediate path node.
 */
inline static bool
IsIntermediateNode(const BsonPathNode *node)
{
	return node->nodeType == NodeType_Intermediate;
}


/*
 * IsIntermediateNodeWithField - 判断是否为包含字段的中间节点
 *
 * Returns true if the BsonPathNode is an intermediate node
 * and has a field in its children.
 */
inline static bool
IsIntermediateNodeWithField(const BsonPathNode *node)
{
	return IsIntermediateNode(node) &&
		   ((const BsonIntermediatePathNode *) node)->
		   hasExpressionFieldsInChildren;
}


/*
 * IntermediateNodeHasChildren - 判断中间节点是否有子节点
 *
 * Returns true if the BsonPathNode is an intermediate node
 * and has at least 1 child node.
 */
inline static bool
IntermediateNodeHasChildren(const BsonIntermediatePathNode *intermediateNode)
{
	return intermediateNode->childData.numChildren > 0;
}


/*
 * CastAsIntermediateNode - 将节点转换为中间节点
 *
 * Convenience cast functions to get specific node types
 */
inline static const BsonIntermediatePathNode *
CastAsIntermediateNode(const BsonPathNode *toCast)
{
	Assert(IsIntermediateNode(toCast));
	return (const BsonIntermediatePathNode *) toCast;
}


/*
 * CastAsLeafNode - 将节点转换为叶子节点
 */
inline static const BsonLeafPathNode *
CastAsLeafNode(const BsonPathNode *toCast)
{
	Assert(NodeType_IsLeaf(toCast->nodeType));
	return (const BsonLeafPathNode *) toCast;
}


/*
 * CastAsLeafArrayFieldNode - 将节点转换为带数组的叶子节点
 */
inline static const BsonLeafArrayWithFieldPathNode *
CastAsLeafArrayFieldNode(const BsonPathNode *toCast)
{
	Assert(toCast->nodeType == NodeType_LeafWithArrayField);
	return (const BsonLeafArrayWithFieldPathNode *) toCast;
}


/*
 * foreach_child_common - 通用的子节点枚举宏
 *
 * 用于遍历中间节点的所有子节点。
 * 这是一个底层宏，通常应该使用 foreach_child 或 foreach_array_child。
 *
 * 使用示例:
 *   foreach_child_common(child, parent, childData, const BsonPathNode *) {
 *       // 处理 child
 *   }
 */
#define foreach_child_common(node, parent, childAccessor, castFunc) node = \
	parent->childAccessor.children == NULL ? NULL : \
	(castFunc) (parent->childAccessor.children->next); \
	for (uint32_t _doNotUseMacroTreeCounter = 0; \
		 node != NULL && (_doNotUseMacroTreeCounter < parent->childAccessor.numChildren); \
		 _doNotUseMacroTreeCounter++, node = (castFunc) (((BsonPathNode *) node)->next))

/*
 * Macros that help enumerate intermediate node's children.
 * 用于枚举中间节点子节点的宏
 */
#ifdef BSON_TREE_PRIVATE
/* 私有版本：用于内部实现，使用非 const 指针 */
#define foreach_child(node, parent) foreach_child_common(node, parent, childData, \
														 BsonPathNode *)
#define foreach_array_child(node, parent) foreach_child_common(node, parent, arrayChild, \
															   BsonLeafPathNode *)
#else
/* 公共版本：用于外部 API，使用 const 指针 */
#define foreach_child(node, parent) foreach_child_common(node, parent, childData, const \
														 BsonPathNode *)
#define foreach_array_child(node, parent) foreach_child_common(node, parent, arrayChild, \
															   const BsonLeafPathNode *)
#endif

/* foreach_child 使用示例:
 *
 * BsonIntermediatePathNode *parent = ...;
 * const BsonPathNode *child;
 * foreach_child(child, parent) {
 *     // 处理每个子节点
 *     printf("Field: %s\n", child->field.string);
 * }
 *
 * foreach_array_child 使用示例:
 *
 * BsonLeafArrayWithFieldPathNode *arrayParent = ...;
 * const BsonLeafPathNode *arrayChild;
 * foreach_array_child(arrayChild, arrayParent) {
 *     // 处理数组中的每个元素节点
 * }
 */

#endif
