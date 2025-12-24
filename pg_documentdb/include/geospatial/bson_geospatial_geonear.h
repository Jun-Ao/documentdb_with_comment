/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_geonear.h
 *
 * Common function declarations for method used for $geoNear aggregation stage
 * $geoNear 聚合阶段使用的相关函数声明
 * 实现地理空间近邻搜索功能，包括距离计算、索引选择和查询优化
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GEOSPATIAL_GEONEAR_H
#define BSON_GEOSPATIAL_GEONEAR_H

#include <postgres.h>
#include <nodes/pathnodes.h>

#include "io/bson_core.h"
#include "geospatial/bson_geospatial_private.h"
#include "opclass/bson_gin_index_mgmt.h"
#include "opclass/bson_index_support.h"
#include "geospatial/bson_geospatial_common.h"


/*
 * 地理空间近邻请求结构体
 * 表示一个 $geoNear 聚合阶段的完整请求配置
 */
typedef struct GeonearRequest
{
	/* 用于投影计算出的距离值的字段名 */
	char *distanceField;

	/* 用于投影文档中地理空间值的字段名 */
	char *includeLocs;

	/* 在此阶段要使用的地理空间索引键 */
	char *key;

	/* 计算出的距离乘以距离乘数器 */
	float8 distanceMultiplier;

	/* 最大距离，用于过滤文档
	 * 对于 2dsphere 索引单位为米
	 * 对于 2d 索引且 spherical 为 true 时单位为弧度 */
	float8 *maxDistance;

	/* 最小距离，用于过滤文档
	 * 对于 2dsphere 索引单位为米
	 * 对于 2d 索引且 spherical 为 true 时单位为弧度 */
	float8 *minDistance;

	/* 计算距离的参考点 */
	Point referencePoint;

	/* 判断点是 GeoJSON 点还是遗留点，有助于决定使用的索引类型 */
	bool isGeoJsonPoint;

	/* 阶段的额外查询过滤器 */
	bson_value_t query;

	/* 是否请求球面距离计算 */
	bool spherical;
} GeonearRequest;


/*
 * $geoNear 距离计算模式枚举
 * 定义了不同的距离计算方法
 */
typedef enum DistanceMode
{
	DistanceMode_Unknown = 0,  // 未知距离模式

	/*
	 * DistanceMode_Spherical - 基于地球椭球体的球面距离计算
	 * 考虑地球的曲率，适用于大范围距离计算
	 */
	DistanceMode_Spherical,

	/*
	 * DistanceMode_Cartesian - 二维笛卡尔距离计算
	 * 在平面上计算直线距离，适用于小范围距离计算
	 */
	DistanceMode_Cartesian,

	/*
	 * DistanceMode_Radians - 弧度距离模式
	 * 类似于球面距离，但以弧度表示相对于地球半径的距离
	 */
	DistanceMode_Radians,
} DistanceMode;


/*
 * 运行时距离计算函数的可缓存上下文结构体
 * 用于存储和重用距离计算相关的状态信息以提高性能
 */
typedef struct GeonearDistanceState
{
	/* 以下字段定义请参考 GeonearRequest 结构体 */
	StringView key;               // 地理空间索引键
	StringView distanceField;     // 距离字段名
	StringView includeLocs;       // 包含位置的字段名
	float8 distanceMultiplier;    // 距离乘数器
	Datum referencePoint;        // 参考点数据
	float8 *maxDistance;          // 最大距离指针
	float8 *minDistance;          // 最小距离指针

	/* PostGIS 运行时距离方法的 FmgrInfo
	 * 根据请求可以是球面或非球面距离计算函数
	 */
	FmgrInfo *distanceFnInfo;

	/* 距离计算模式 */
	DistanceMode mode;
} GeonearDistanceState;

/*
 * $geoNear 索引验证状态结构体
 * 存储验证级别和索引类型信息
 */
typedef struct GeonearIndexValidationState
{
	/* 验证级别：索引级别或运行时级别 */
	GeospatialValidationLevel validationLevel;

	/* 获取用于验证的索引类型
	 * 对于 2d 索引可能类型转换为 Bson2dGeometryPathOptions
	 */
	BsonGinIndexOptionsBase *options;
} GeonearIndexValidationState;

/*
 * ParseGeonearRequest - 解析 $geoNear 请求
 * @geoNearQuery: BSON 格式的 geoNear 查询
 * 解析并验证 geoNear 查询，返回结构化的请求对象
 */
GeonearRequest * ParseGeonearRequest(const pgbson *geoNearQuery);

/*
 * BuildGeoNearDistanceState - 构建 $geoNear 距离状态
 * @state: 距离状态指针
 * @geoNearQuery: BSON 格式的 geoNear 查询
 * @validationState: 索引验证状态
 * 根据查询构建距离计算的状态信息
 */
void BuildGeoNearDistanceState(GeonearDistanceState *state, const pgbson *geoNearQuery,
							   const GeonearIndexValidationState *validationState);

/*
 * BuildGeoNearRangeDistanceState - 构建 $geoNear 范围距离状态
 * @state: 距离状态指针
 * @geoNearQuery: BSON 格式的 geoNear 查询
 * 构建用于范围查询的距离计算状态
 */
void BuildGeoNearRangeDistanceState(GeonearDistanceState *state, const
									pgbson *geoNearQuery);

/*
 * GeonearDistanceFromDocument - 从文档计算 $geoNear 距离
 * @state: 距离状态
 * @document: BSON 文档
 * 根据文档中的地理位置计算到参考点的距离
 */
float8 GeonearDistanceFromDocument(const GeonearDistanceState *state, const
								   pgbson *document);

/*
 * ValidateQueryOperatorsForGeoNear - 验证 $geoNear 的查询运算符
 * @node: 语法树节点
 * @state: 验证状态
 * 验证查询中使用的地理空间运算符是否合法
 */
bool ValidateQueryOperatorsForGeoNear(Node *node, void *state);

/*
 * ConvertQueryToGeoNearQuery - 将查询转换为 $geoNear 查询
 * @operatorDocIterator: 运算符文档迭代器
 * @path: 路径
 * @mongoOperatorName: MongoDB 运算符名
 * 将普通查询转换为 geoNear 查询格式
 */
pgbson * ConvertQueryToGeoNearQuery(bson_iter_t *operatorDocIterator, const char *path,
									const char *mongoOperatorName);

/*
 * CreateExprForGeonearAndNearSphere - 为 $geoNear 和 $nearSphere 创建表达式
 * @queryDoc: 查询文档
 * @docExpr: 文档表达式
 * @request: geoNear 请求
 * @targetEntry: 目标条目输出
 * @sortClause: 排序子句输出
 * 创建地理空间查询的表达式树
 */
List * CreateExprForGeonearAndNearSphere(const pgbson *queryDoc, Expr *docExpr,
										 const GeonearRequest *request,
										 TargetEntry **targetEntry,
										 SortGroupClause **sortClause);

/*
 * GetGeonearSpecFromNearQuery - 从近邻查询获取 geoNear 规范
 * @operatorDocIterator: 运算符文档迭代器
 * @path: 路径
 * @mongoOperatorName: MongoDB 运算符名
 * 从 $near 查询中提取 geoNear 规范
 */
pgbson * GetGeonearSpecFromNearQuery(bson_iter_t *operatorDocIterator, const char *path,
									 const char *mongoOperatorName);

/*
 * CanGeonearQueryUseAlternateIndex - 检查 geoNear 查询是否可以使用替代索引
 * @geoNearOpExpr: geoNear 运算符表达式
 * @request: 请求输出指针
 * 判断是否可以使用备用索引优化查询
 */
bool CanGeonearQueryUseAlternateIndex(OpExpr *geoNearOpExpr,
									  GeonearRequest **request);

/*
 * GetAllGeoIndexesFromRelIndexList - 从关系索引列表获取所有地理索引
 * @indexlist: 索引列表
 * @_2dIndexList: 2D 索引列表输出
 * @_2dsphereIndexList: 2DSphere 索引列表输出
 * 从索引列表中筛选出所有地理空间索引
 */
void GetAllGeoIndexesFromRelIndexList(List *indexlist, List **_2dIndexList,
									  List **_2dsphereIndexList);

/*
 * CheckGeonearEmptyKeyCanUseIndex - 检查空键 geoNear 是否可以使用索引
 * @request: geoNear 请求
 * @_2dIndexList: 2D 索引列表
 * @_2dsphereIndexList: 2DSphere 索引列表
 * @useSphericalIndex: 是否使用球面索引输出
 * 检查没有指定键时的索引使用情况
 */
char * CheckGeonearEmptyKeyCanUseIndex(GeonearRequest *request, List *_2dIndexList,
									   List *_2dsphereIndexList, bool *useSphericalIndex);

/*
 * UpdateGeoNearQueryTreeToUseAlternateIndex - 更新查询树以使用替代索引
 * @root: 规划器根节点
 * @rel: 关系优化信息
 * @geoNearOpExpr: geoNear 运算符表达式
 * @key: 索引键
 * @useSphericalIndex: 是否使用球面索引
 * @isEmptyKey: 是否为空键
 * 修改查询计划以使用最优的地理空间索引
 */
void UpdateGeoNearQueryTreeToUseAlternateIndex(PlannerInfo *root, RelOptInfo *rel,
											   OpExpr *geoNearOpExpr, const char *key,
											   bool useSphericalIndex, bool isEmptyKey);

/*
 * TryFindGeoNearOpExpr - 尝试查找 geoNear 运算符表达式
 * @root: 规划器根节点
 * @context: 替换扩展函数上下文
 * 在查询中查找 geoNear 运算符表达式
 */
bool TryFindGeoNearOpExpr(PlannerInfo *root, ReplaceExtensionFunctionContext *context);

/*
 * EvaluateGeoNearConstExpression - 计算 geoNear 常量表达式
 * @geoNearSpecValue: geoNear 规范值
 * @variableExpr: 变量表达式
 * 评估并计算 geoNear 查询中的常量表达式
 */
pgbson * EvaluateGeoNearConstExpression(const bson_value_t *geoNearSpecValue,
										Expr *variableExpr);

/*
 * Is2dWithSphericalDistance - 检查是否为 2D 索引使用球面距离
 * @request: geoNear 请求
 * 检查在 2D 索引上是否使用了球面距离计算
 */
inline static bool
Is2dWithSphericalDistance(const GeonearRequest *request)
{
	return !request->isGeoJsonPoint && request->spherical;
}

/*
 * ConvertRadiansToMeters - 将弧度转换为米
 * @radians: 弧度值
 * 将弧度距离转换为米，使用地球半径进行计算
 */
inline static float8
ConvertRadiansToMeters(float8 radians)
{
	return radians * RADIUS_OF_EARTH_M;
}

/*
 * ConvertMetersToRadians - 将米转换为弧度
 * @meters: 米值
 * 将米距离转换为弧度，使用地球半径进行计算
 */
inline static float8
ConvertMetersToRadians(float8 meters)
{
	return meters / RADIUS_OF_EARTH_M;
}

/*
 * TargetListContainsGeonearOp - 检查排序子句是否包含 geoNear 运算符
 * @targetList: 目标列表
 * 检查查询的目标列表中是否包含 geoNear 距离运算符
 */
inline static bool
TargetListContainsGeonearOp(const List *targetList)
{
	if (!targetList)
	{
		return false;
	}

	TargetEntry *tle;
	ListCell *cell;
	foreach(cell, targetList)
	{
		tle = (TargetEntry *) lfirst(cell);

		if (tle->resjunk)
		{
			if (IsA(tle->expr, OpExpr))
			{
				OpExpr *expr = (OpExpr *) tle->expr;
				if (expr->opno == BsonGeonearDistanceOperatorId())
				{
					return true;
				}
			}
		}
	}

	return false;
}


/*
 * ThrowGeoNearNotAllowedInContextError - 抛出 geoNear 上下文不允许错误
 * 当 $geoNear、$near 和 $nearSphere 运算符在不允许的上下文中使用时抛出
 */
static inline void
pg_attribute_noreturn()
ThrowGeoNearNotAllowedInContextError()
{
	ereport(ERROR, (
				errcode(ERRCODE_DOCUMENTDB_LOCATION5626500),
				errmsg(
					"Operators $geoNear, $near, and $nearSphere cannot be used in this particular context")));
}

/*
 * ThrowGeoNearUnableToFindIndex - 抛出找不到 geoNear 索引错误
 * 当找不到用于 geoNear 查询的索引时抛出
 */
static inline void
pg_attribute_noreturn()
ThrowGeoNearUnableToFindIndex()
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNABLETOFINDINDEX),
					errmsg("unable to find index for $geoNear query")));
}

/*
 * ThrowNoGeoIndexesFound - 抛出没有找到地理索引错误
 * 当 $geoNear 运算符需要 2d 或 2dsphere 索引但不存在时抛出
 */
static inline void
pg_attribute_noreturn()
ThrowNoGeoIndexesFound()
{
	ereport(ERROR, (
				errcode(ERRCODE_DOCUMENTDB_INDEXNOTFOUND),
				errmsg(
					"The $geoNear operator needs either a 2d or 2dsphere index, but no such index exists")));
}

/*
 * ThrowAmbigousIndexesFound - 抛出歧义索引错误
 * @indexType: 索引类型
 * 当找到多个相同类型的索引，不确定应该使用哪个时抛出
 */
static inline void
pg_attribute_noreturn()
ThrowAmbigousIndexesFound(const char * indexType)
{
	ereport(ERROR, (
				errcode(ERRCODE_DOCUMENTDB_INDEXNOTFOUND),
				errmsg(
					"Multiple %s indexes found; uncertain which index should be applied for the $geoNear operator",
					indexType)));
}

#endif
