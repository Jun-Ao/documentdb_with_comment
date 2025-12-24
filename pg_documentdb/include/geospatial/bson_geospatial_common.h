/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_common.h
 *
 * Common function declarations for method interacting between documentdb_api and
 * postgis extension to convert and process GeoSpatial Data
 * DocumentDB API 与 PostGIS 扩展交互处理地理空间数据的通用函数声明
 * 包含地理空间数据的验证、类型定义和状态管理
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GEOSPATIAL_COMMON_H
#define BSON_GEOSPATIAL_COMMON_H

#include "postgres.h"
#include "float.h"

#include "io/bson_core.h"
#include "geospatial/bson_geospatial_private.h"
#include "planner/mongo_query_operator.h"
#include "geospatial/bson_geospatial_shape_operators.h"
#include "metadata/metadata_cache.h"

/* 二维索引的默认最小和最大边界值（经度范围） */
#define DEFAULT_2D_INDEX_MIN_BOUND -180.0
#define DEFAULT_2D_INDEX_MAX_BOUND 180.0

/* 双精度浮点数相等性检查的实用宏 */
#define DOUBLE_EQUALS(a, b) (fabs((a) - (b)) < DBL_EPSILON)


/* Forward declaration of struct */
typedef struct ProcessCommonGeospatialState ProcessCommonGeospatialState;


/*================================*/
/* Data Types*/
/*================================*/

/* Forward declaration of struct */
typedef struct ProcessCommonGeospatialState ProcessCommonGeospatialState;

/*
 * 地理空间数据处理时的验证类型枚举
 * 定义了不同级别的地理空间数据验证策略
 */
typedef enum GeospatialValidationLevel
{
	GeospatialValidationLevel_Unknown = 0,  // 未知验证级别

	/*
	 * GeospatialValidationLevel_BloomFilter - 布隆过滤器级别验证
	 * 与 bson_validate_* 函数一起使用，检查文档路径中是否有潜在的地理空间值
	 * 此级别不抛出错误，只检查第一个潜在的地理空间值并立即返回
	 */
	GeospatialValidationLevel_BloomFilter,

	/*
	 * GeospatialValidationLevel_Runtime - 运行时验证级别
	 * 运行时运算符函数家族（如 geoIntersects 或 geoWithin）需要此验证级别
	 * 此级别不抛出错误，返回所有有效几何体
	 */
	GeospatialValidationLevel_Runtime,

	/*
	 * GeospatialValidationLevel_Index - 索引级别验证
	 * 用于强制执行地理空间索引词条生成验证
	 * 如果没有地理空间索引，文档可以插入；否则返回错误
	 * 此验证在处理任何无效几何体时抛出错误
	 */
	GeospatialValidationLevel_Index,
} GeospatialValidationLevel;


/*
 * 正在处理的地理空间数据类型枚举
 */
typedef enum GeospatialType
{
	GeospatialType_UNKNOWN = 0,  // 未知类型
	GeospatialType_Geometry,    // 几何类型（平面坐标系）
	GeospatialType_Geography,    // 地理类型（球面坐标系，考虑地球曲率）
} GeospatialType;

/*
 * 在 $geoWithin 和 $geoIntersects 运行时匹配中使用的 PostGIS 函数枚举
 * 这些函数用于执行地理空间查询的底层计算
 */
typedef enum PostgisFuncsForDollarGeo
{
	Geometry_Intersects = 0,      // 几何体相交检查
	Geography_Intersects,        // 地理体相交检查
	Geometry_Covers,             // 几何体覆盖检查
	Geography_Covers,             // 地理体覆盖检查
	Geometry_DWithin,            // 几何体距离内检查
	Geography_DWithin,           // 地理体距离内检查
	Geometry_IsValidDetail,      // 几何体有效性详细检查
	PostgisFuncsForDollarGeo_MAX  // 枚举最大值，用于边界检查
} PostgisFuncsForDollarGeo;


/*
 * 几何体/地理体的通用缓存状态结构体
 * 用于缓存预计算的几何体/地理体数据
 */
typedef struct CommonBsonGeospatialState
{
	/*
	 * 大地测量基准面是否为球面
	 * true: 使用球面模型（适用于大范围地理计算）
	 * false: 使用椭球面模型（更精确的地球模型）
	 */
	bool isSpherical;

	/*
	 * 为查询预计算的 PostGIS 几何体/地理体数据
	 * 存储在 Datum 类型中，用于提高查询性能
	 */
	Datum geoSpatialDatum;
} CommonBsonGeospatialState;

/* Signature for runtime function to get match result for $geoWithin and $geoIntersects */
typedef bool (*GeospatialQueryMatcherFunc)(const ProcessCommonGeospatialState *,
										   StringInfo);

/*
 * 运行时查询匹配器结构体
 * 用于基于 MongoDB 地理空间查询运算符比较文档中的几何体/地理体与查询结果
 */
typedef struct RuntimeQueryMatcherInfo
{
	/* 用于检查匹配的匹配器函数指针 */
	GeospatialQueryMatcherFunc matcherFunc;

	/* 运行时匹配函数的 FmgrInfo 存储数组 */
	FmgrInfo **runtimeFmgrStore;

	/* 用于运行时匹配的主要 PostGIS 函数 */
	PostgisFuncsForDollarGeo runtimePostgisFunc;

	/* 预计算的查询几何体/地理体数据 */
	Datum queryGeoDatum;

	/* 是否匹配成功的标志 */
	bool isMatched;
} RuntimeQueryMatcherInfo;


/*
 * 处理文档中地理空间数据的通用状态结构体
 * 包含输入变量和输出变量，用于地理空间数据处理的全过程
 */
typedef struct ProcessCommonGeospatialState
{
	/* ========== 输入变量 ============ */

	/* 正在处理的地理空间类型：几何体或地理体 */
	GeospatialType geospatialType;

	/* 需要解析几何体/地理体的验证级别 */
	GeospatialValidationLevel validationLevel;

	/* 运行时查询匹配器，仅用于运行时匹配，否则为 NULL */
	RuntimeQueryMatcherInfo runtimeMatcher;

	/* 错误上下文指针，用于在 ereports 中抛出错误 */
	GeospatialErrorContext *errorCtxt;

	/* 携带形状特定的信息，例如 $center 和 $centerSphere 的半径 */
	ShapeOperatorInfo *opInfo;

	/* ========== 输出变量 ============ */

	/* 生成的几何体/地理体的 WKB（Well-Known Binary）缓冲区 */
	StringInfo WKBBuffer;

	/* 是否处理过多键情况 */
	bool isMultiKeyContext;

	/* 找到的地理空间值总数 */
	uint32 total;

	/* 是否没有有效区域 */
	bool isEmpty;
} ProcessCommonGeospatialState;

/*
 * BsonIterGetLegacyGeometryPoints - 从 BSON 迭代器获取遗留几何体点
 * @documentIter: BSON 文档迭代器
 * @keyPathView: 键路径视图
 * @state: 处理状态
 * 从文档中提取传统的几何体点数据
 */
void BsonIterGetLegacyGeometryPoints(bson_iter_t *documentIter, const
									 StringView *keyPathView,
									 ProcessCommonGeospatialState *state);

/*
 * BsonIterGetGeographies - 从 BSON 迭代器获取地理体数据
 * @documentIter: BSON 文档迭代器
 * @keyPathView: 键路径视图
 * @state: 处理状态
 * 从文档中提取地理体数据
 */
void BsonIterGetGeographies(bson_iter_t *documentIter, const StringView *keyPathView,
							ProcessCommonGeospatialState *state);

/*
 * BsonIterValidateGeographies - 验证 BSON 迭代器中的地理体数据
 * @documentIter: BSON 文档迭代器
 * @keyPathView: 键路径视图
 * @state: 处理状态
 * 验证文档中的地理体数据有效性
 */
void BsonIterValidateGeographies(bson_iter_t *documentIter, const StringView *keyPathView,
								 ProcessCommonGeospatialState *state);

/*
 * BsonExtractGeometryStrict - 严格提取几何体
 * @document: BSON 文档指针
 * @pathView: 路径视图
 * 从文档中严格提取几何体数据，执行严格验证
 */
Datum BsonExtractGeometryStrict(const pgbson *document, const StringView *pathView);

/*
 * BsonExtractGeographyStrict - 严格提取地理体
 * @document: BSON 文档指针
 * @pathView: 路径视图
 * 从文档中严格提取地理体数据，执行严格验证
 */
Datum BsonExtractGeographyStrict(const pgbson *document, const StringView *pathView);

/*
 * BsonExtractGeometryRuntime - 运行时提取几何体
 * @document: BSON 文档指针
 * @pathView: 路径视图
 * 从文档中运行时提取几何体数据，适用于查询执行阶段
 */
Datum BsonExtractGeometryRuntime(const pgbson *document, const StringView *pathView);

/*
 * BsonExtractGeographyRuntime - 运行时提取地理体
 * @document: BSON 文档指针
 * @pathView: 路径视图
 * 从文档中运行时提取地理体数据，适用于查询执行阶段
 */
Datum BsonExtractGeographyRuntime(const pgbson *document, const StringView *pathView);


/*
 * Initialize ProcessCommonState with given set of values
 */
static inline void
InitProcessCommonGeospatialState(ProcessCommonGeospatialState *state,
								 GeospatialValidationLevel validationLevel,
								 GeospatialType type,
								 GeospatialErrorContext *errCtxt)
{
	memset(state, 0, sizeof(ProcessCommonGeospatialState));
	state->isEmpty = true;
	state->geospatialType = type;
	state->validationLevel = validationLevel;
	state->WKBBuffer = makeStringInfo();

	/*
	 * Error context while processing the data as geospatial data used in
	 * ereports to throw error where valid
	 */
	state->errorCtxt = errCtxt;
}


/*
 * Validates whether this is a geo-within query operator (for both variants)
 */
static inline bool
IsGeoWithinQueryOperator(MongoQueryOperatorType queryOperatorType)
{
	return queryOperatorType == QUERY_OPERATOR_GEOWITHIN ||
		   queryOperatorType == QUERY_OPERATOR_WITHIN;
}


#endif
