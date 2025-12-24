/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_json_schema_tree.h
 *
 * BSON JSON模式树处理的结构体和函数通用声明
 *
 * 本文件定义了DocumentDB中BSON JSON模式树的核心数据结构，
 * 用于模式验证和约束检查，支持MongoDB的JSON Schema验证功能。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_JSON_SCHEMA_TREE_H
#define BSON_JSON_SCHEMA_TREE_H

#include "query/bson_compare.h"

#include "types/pcre_regex.h"
#include "query/bson_dollar_operators.h"

/* -------------------------------------------------------- */
/*                  数据类型定义                              */
/* -------------------------------------------------------- */

/* 模式节点结构体前向声明 */
typedef struct SchemaNode SchemaNode;

/* 模式字段节点结构体前向声明 */
typedef struct SchemaFieldNode SchemaFieldNode;

/* 模式关键字节点结构体前向声明 */
typedef struct SchemaKeywordNode SchemaKeywordNode;

/* BSON类型标志位枚举
 * 使用位标志来表示支持的BSON数据类型
 */
typedef enum BsonTypeFlags
{
	BsonTypeFlag_EOD = 1 << 0,              /* 文档结束标志 */
	BsonTypeFlag_DOUBLE = 1 << 1,          /* 双精度浮点数 */
	BsonTypeFlag_UTF8 = 1 << 2,             /* UTF8字符串 */
	BsonTypeFlag_DOCUMENT = 1 << 3,        /* 文档对象 */
	BsonTypeFlag_ARRAY = 1 << 4,            /* 数组 */
	BsonTypeFlag_BINARY = 1 << 5,          /* 二进制数据 */
	BsonTypeFlag_UNDEFINED = 1 << 6,       /* 未定义类型 */
	BsonTypeFlag_OID = 1 << 7,             /* 对象ID */
	BsonTypeFlag_BOOL = 1 << 8,            /* 布尔值 */
	BsonTypeFlag_DATE_TIME = 1 << 9,       /* 日期时间 */
	BsonTypeFlag_NULL = 1 << 10,           /* 空值 */
	BsonTypeFlag_REGEX = 1 << 11,          /* 正则表达式 */
	BsonTypeFlag_DBPOINTER = 1 << 12,      /* 数据库指针 */
	BsonTypeFlag_CODE = 1 << 13,           /* 代码 */
	BsonTypeFlag_SYMBOL = 1 << 14,         /* 符号 */
	BsonTypeFlag_CODEWSCOPE = 1 << 15,     /* 带作用域的代码 */
	BsonTypeFlag_INT32 = 1 << 16,          /* 32位整数 */
	BsonTypeFlag_TIMESTAMP = 1 << 17,      /* 时间戳 */
	BsonTypeFlag_INT64 = 1 << 18,          /* 64位整数 */
	BsonTypeFlag_DECIMAL128 = 1 << 19,     /* 128位十进制数 */
	BsonTypeFlag_MINKEY = 1 << 20,         /* 最小键 */
	BsonTypeFlag_MAXKEY = 1 << 21          /* 最大键 */
} BsonTypeFlags;

/* 对象验证类型枚举
 * 定义了JSON Schema中对象类型的验证规则
 */
typedef enum ObjectValidationTypes
{
	ObjectValidationTypes_MaxProperties = 1 << 0,      /* 最大属性数量 */
	ObjectValidationTypes_MinProperties = 1 << 1,      /* 最小属性数量 */
	ObjectValidationTypes_Required = 1 << 2,           /* 必需字段 */
	ObjectValidationTypes_Properties = 1 << 3,         /* 属性定义 */
	ObjectValidationTypes_PatternProperties = 1 << 4,  /* 模式属性 */
	ObjectValidationTypes_AdditionalPropertiesBool = 1 << 5,  /* 额外属性布尔标志 */
	ObjectValidationTypes_AdditionalPropertiesObject = 1 << 6, /* 额外属性对象 */
	ObjectValidationTypes_Dependency = 1 << 7,         /* 依赖关系 */
	ObjectValidationTypes_DependencyArray = 1 << 8,    /* 依赖数组 */
	ObjectValidationTypes_DependencyObject = 1 << 9,   /* 依赖对象 */
} ObjectValidationTypes;

typedef enum CommonValidationTypes
{
	CommonValidationTypes_Enum = 1 << 0,
	CommonValidationTypes_JsonType = 1 << 1,
	CommonValidationTypes_BsonType = 1 << 2,
	CommonValidationTypes_AllOf = 1 << 3,
	CommonValidationTypes_AnyOf = 1 << 4,
	CommonValidationTypes_OneOf = 1 << 5,
	CommonValidationTypes_Not = 1 << 6
} CommonValidationTypes;

typedef enum NumericValidationTypes
{
	NumericValidationTypes_MultipleOf = 1 << 0,
	NumericValidationTypes_Maximum = 1 << 1,
	NumericValidationTypes_ExclusiveMaximum = 1 << 2,
	NumericValidationTypes_Minimum = 1 << 3,
	NumericValidationTypes_ExclusiveMinimum = 1 << 4,
} NumericValidationTypes;

typedef enum StringValidationTypes
{
	StringValidationTypes_MaxLength = 1 << 0,
	StringValidationTypes_MinLength = 1 << 1,
	StringValidationTypes_Pattern = 1 << 2,
} StringValidationTypes;

typedef enum ArrayValidationTypes
{
	ArrayValidationTypes_MaxItems = 1 << 0,
	ArrayValidationTypes_MinItems = 1 << 1,
	ArrayValidationTypes_UniqueItems = 1 << 2,
	ArrayValidationTypes_ItemsObject = 1 << 3,
	ArrayValidationTypes_ItemsArray = 1 << 4,
	ArrayValidationTypes_AdditionalItemsBool = 1 << 5,
	ArrayValidationTypes_AdditionalItemsObject = 1 << 6,
} ArrayValidationTypes;

typedef enum BinaryValidationTypes
{
	BinaryValidationTypes_Encrypt = 1 << 0,
} BinaryValidationTypes;

typedef enum SchemaNodeType
{
	SchemaNodeType_Invalid = 0,
	SchemaNodeType_Field,
	SchemaNodeType_Root,
	SchemaNodeType_AdditionalProperties,
	SchemaNodeType_PatternProperties,
	SchemaNodeType_Dependencies,
	SchemaNodeType_Items,
	SchemaNodeType_AdditionalItems,
	SchemaNodeType_AllOf,
	SchemaNodeType_AnyOf,
	SchemaNodeType_OneOf,
	SchemaNodeType_Not,
} SchemaNodeType;


typedef struct ValidationsObject
{
	/* List of child field nodes */
	SchemaFieldNode *properties;

	/* Array of required field names */
	bson_value_t *required;
} ValidationsObject;

typedef struct ValidationsCommon
{
	BsonTypeFlags jsonTypes;
	BsonTypeFlags bsonTypes;
} ValidationsCommon;

typedef struct ValidationsNumeric
{
	bson_value_t *maximum;
	bool exclusiveMaximum;
	bson_value_t *minimum;
	bool exclusiveMinimum;
	bson_value_t *multipleOf;
} ValidationsNumeric;

typedef struct ValidationsString
{
	uint32_t maxLength;
	uint32_t minLength;
	RegexData *pattern;
} ValidationsString;

typedef struct ValidationsArray
{
	uint32_t maxItems;
	uint32_t minItems;
	bool uniqueItems;
	union
	{
		SchemaKeywordNode *itemsNode;

		/* List of dependency item schemas */
		SchemaKeywordNode *itemsArray;
	};
	union
	{
		bool additionalItemsBool;
		SchemaKeywordNode *additionalItemsNode;
	};
} ValidationsArray;

typedef struct Validations
{
	ValidationsObject *object;
	ValidationsCommon *common;
	ValidationsNumeric *numeric;
	ValidationsString *string;
	ValidationsArray *array;
}Validations;

typedef struct ValidationFlags
{
	uint16_t object;
	uint16_t common;
	uint16_t numeric;
	uint16_t string;
	uint16_t array;
	uint16_t binary;
}ValidationFlags;

struct SchemaNode
{
	SchemaNodeType nodeType;

	Validations validations;
	ValidationFlags validationFlags;

	/* link to next node of linked list (e.g. sibling node) */
	struct SchemaNode *next;
};

struct SchemaFieldNode
{
	SchemaNode base;

	/* This is the non-dotted field path at the current level */
	StringView field;
};

struct SchemaKeywordNode
{
	SchemaNode base;
	RegexData *fieldPattern;
};


typedef struct SchemaTreeState
{
	SchemaNode *rootNode;
}SchemaTreeState;

/* -------------------------------------------------------- */
/*                  Functions                               */
/* -------------------------------------------------------- */

void BuildSchemaTree(SchemaTreeState *treeState, bson_iter_t *schemaIter);
SchemaFieldNode * FindFieldNodeByName(const SchemaNode *parent, const
									  char *field);

#endif
