/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/parse_error.h
 *
 * 解析错误的异常处理函数
 * 定义了常见的解析错误处理和类型检查函数
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>

#include "io/bson_core.h"
#include "utils/documentdb_errors.h"


#ifndef PARSE_ERROR_H
#define PARSE_ERROR_H


/* 抛出顶级类型不匹配错误 */
static inline void
ThrowTopLevelTypeMismatchError(const char *fieldName, const char *fieldTypeName,
							   const char *expectedTypeName)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
					errmsg(
						"The BSON field '%s' has an incorrect type '%s'; it should be of type '%s'.",
						fieldName, fieldTypeName, expectedTypeName),
					errdetail_log(
						"The BSON field '%s' has an incorrect type '%s'; it should be of type '%s'.",
						fieldName, fieldTypeName, expectedTypeName)));
}


/*
 * 如果给定迭代器持有的值的类型与预期不匹配，则抛出错误
 */
static inline void
EnsureTopLevelFieldType(const char *fieldName, const bson_iter_t *iter,
						bson_type_t expectedType)
{
	bson_type_t fieldType = bson_iter_type(iter);
	if (fieldType != expectedType)
	{
		ThrowTopLevelTypeMismatchError(fieldName, BsonTypeName(fieldType),
									   BsonTypeName(expectedType));
	}
}


/*
 * 上述函数的值版本变体
 */
static inline void
EnsureTopLevelFieldValueType(const char *fieldName, const bson_value_t *value,
							 bson_type_t expectedType)
{
	if (value->value_type != expectedType)
	{
		ThrowTopLevelTypeMismatchError(fieldName, BsonTypeName(value->value_type),
									   BsonTypeName(expectedType));
	}
}


/*
 * 类似于 EnsureTopLevelFieldType，但如果 expectedType 不是 "null"，null 值也是允许的
 *
 * 这意味着：
 *  - 如果它持有的值类型匹配预期类型，则返回 true
 *  - 否则，如果迭代器持有 null 值则返回 false，否则抛出错误
 *
 * 主要用于当给定字段设置为 null 意味着使用该规范选项的默认设置时
 */
static inline bool
EnsureTopLevelFieldTypeNullOk(const char *fieldName, const bson_iter_t *iter,
							  bson_type_t expectedType)
{
	if (BSON_ITER_HOLDS_NULL(iter) && expectedType != BSON_TYPE_NULL)
	{
		return false;
	}

	EnsureTopLevelFieldType(fieldName, iter, expectedType);
	return true;
}


/*
 * 类似于 EnsureTopLevelFieldType，但如果 expectedType 不是 "null" 或 "undefined"，null 值也是允许的
 *
 * 这意味着：
 *  - 如果它持有的值类型匹配预期类型，则返回 true
 *  - 否则，如果迭代器持有 null 值则返回 false，否则抛出错误
 *
 * 主要用于当给定字段设置为 null 意味着使用该规范选项的默认设置时
 */
static inline bool
EnsureTopLevelFieldTypeNullOkUndefinedOK(const char *fieldName, const bson_iter_t *iter,
										 bson_type_t expectedType)
{
	if ((BSON_ITER_HOLDS_NULL(iter) && expectedType != BSON_TYPE_NULL) ||
		(BSON_ITER_HOLDS_UNDEFINED(iter) && expectedType != BSON_TYPE_UNDEFINED))
	{
		return false;
	}

	EnsureTopLevelFieldType(fieldName, iter, expectedType);
	return true;
}


/*
 * 如果给定迭代器持有的值无法解释为布尔值，则抛出错误
 */
static inline void
EnsureTopLevelFieldIsBooleanLike(const char *fieldName, const bson_iter_t *iter)
{
	if (!BsonTypeIsNumberOrBool(bson_iter_type(iter)))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"The BSON field '%s' has an incorrect type '%s'; it should be"
							" one of the following valid types: [bool, long, int, decimal, double]",
							fieldName, BsonIterTypeName(iter)),
						errdetail_log(
							"The BSON field '%s' has an incorrect type '%s'; it should be"
							" one of the following valid types: [bool, long, int, decimal, double]",
							fieldName, BsonIterTypeName(iter))));
	}
}


/*
 * 如果给定迭代器持有的值无法解释为数字，则抛出错误
 */
static inline void
EnsureTopLevelFieldIsNumberLike(const char *fieldName, const bson_value_t *value)
{
	if (!BsonTypeIsNumber(value->value_type))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"The BSON field '%s' has an incorrect type '%s'; it should be"
							" one of the following valid types: [int, decimal, double, long]",
							fieldName, BsonTypeName(value->value_type)),
						errdetail_log(
							"The BSON field '%s' has an incorrect type '%s'; it should be"
							" one of the following valid types: [int, decimal, double, long]",
							fieldName, BsonTypeName(value->value_type))));
	}
}


/*
 * 类似于 EnsureTopLevelFieldIsBooleanLike，但 null 值也是允许的
 *
 * 这意味着：
 *  - 如果它持有的值可以解释为布尔值，则返回 true
 *  - 否则，如果迭代器持有 null 值则返回 false，否则抛出错误
 *
 * 主要用于当给定字段设置为 null 意味着使用该规范选项的默认设置时
 */
static inline bool
EnsureTopLevelFieldIsBooleanLikeNullOk(const char *fieldName, const bson_iter_t *iter)
{
	if (BSON_ITER_HOLDS_NULL(iter))
	{
		return false;
	}

	EnsureTopLevelFieldIsBooleanLike(fieldName, iter);
	return true;
}


/* 抛出顶级字段缺失错误 */
static inline void
ThrowTopLevelMissingFieldError(const char *fieldName)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
					errmsg("The BSON field '%s' is required but was not provided",
						   fieldName)));
}


/* 带错误代码的顶级字段缺失错误 */
static inline void
ThrowTopLevelMissingFieldErrorWithCode(const char *fieldName, int code)
{
	ereport(ERROR, (errcode(code),
					errmsg("The BSON field '%s' is required but was not provided",
						   fieldName),
					errdetail_log("The BSON field '%s' is required but was not provided",
								  fieldName)));
}


/* 确保字符串值不以 $ 开头（MongoDB 操作符限制） */
static inline void
EnsureStringValueNotDollarPrefixed(const char *fieldValue, int fieldLength)
{
	if (fieldLength > 0 && fieldValue[0] == '$')
	{
		ereport(ERROR, (
					errcode(ERRCODE_DOCUMENTDB_LOCATION16410),
					errmsg(
						"FieldPath field names are not allowed to begin with the operators symbol '$'; consider using $getField or $setField instead.")));
	}
}


#endif
