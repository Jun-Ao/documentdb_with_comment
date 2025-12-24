/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_shape_operators.h
 *
 * 地理空间形状操作符的通用函数和类型声明
 *
 * 本文件定义了DocumentDB中地理空间形状操作符的核心数据结构和函数接口，
 * 包括：
 * - 形状操作符类型定义
 * - 查询阶段枚举
 * - 形状操作符状态和信息结构体
 * - 函数原型定义
 *
 *-------------------------------------------------------------------------
 */
#ifndef BSON_GEOSPATIAL_SHAPE_OPERATORS_H
#define BSON_GEOSPATIAL_SHAPE_OPERATORS_H

#include "postgres.h"

#include "io/bson_core.h"
#include "planner/mongo_query_operator.h"


/*
 * 地理空间形状操作符类型定义
 */
typedef enum GeospatialShapeOperator
{
	GeospatialShapeOperator_UNKNOWN = 0,         // 未知操作符
	GeospatialShapeOperator_POLYGON,             // 多边形操作符
	GeospatialShapeOperator_BOX,                 // 矩形框操作符
	GeospatialShapeOperator_CENTER,              // 中心点操作符
	GeospatialShapeOperator_GEOMETRY,            // 几何对象操作符
	GeospatialShapeOperator_CENTERSPHERE,       // 球面中心操作符

	/* 已弃用!! 保留在类型中以便可以忽略它 */
	GeospatialShapeOperator_UNIQUEDOCS,
} GeospatialShapeOperator;

/*
 * 查询阶段枚举定义
 * $centerSphere在不同阶段使用不同的PostGIS函数
 */
typedef enum QueryStage
{
	QueryStage_UNKNOWN = 0,   // 未知阶段
	QueryStage_RUNTIME,      // 运行时阶段
	QueryStage_INDEX,        // 索引阶段
} QueryStage;

/*
 * 形状操作符状态的父结构体，例如DollarCenterOperatorState
 * 对于派生类型，这应该是第一个成员
 */
typedef struct ShapeOperatorState
{ }ShapeOperatorState;

/*
 * 用于携带形状特定信息的结构体
 */
typedef struct ShapeOperatorInfo
{
	QueryStage queryStage;              // 查询阶段

	GeospatialShapeOperator op;         // 形状操作符类型

	/* 父查询操作符类型 */
	MongoQueryOperatorType queryOperatorType;

	/*
	 * $center和$centerSphere操作的半径
	 * 对于其他操作符可以设置为任何值
	 */
	ShapeOperatorState *opState;        // 操作符状态
} ShapeOperatorInfo;

/*
 * 获取形状操作符的函数原型
 */
typedef Datum (*BsonValueGetShapeDatum) (const bson_value_t *, ShapeOperatorInfo *);

/*
 * 形状操作符结构体
 * BsonValueGetShapeDatum接受特定的BSON值并返回对应的PostGIS几何/地理形状
 */
typedef struct ShapeOperator
{
	/* 形状操作符名称 */
	const char *shapeOperatorName;

	/* 枚举类型 */
	GeospatialShapeOperator op;

	/* 操作符是否具有球面特性 */
	bool isSpherical;

	/* 返回几何/地理PostGIS形状的函数 */
	BsonValueGetShapeDatum getShapeDatum;

	/*
	 * 确定是否应在索引下推时进行分割（segmentize）
	 * $box、$center和$centerSphere设置为false
	 */
	bool shouldSegmentize;
} ShapeOperator;

/*
 * $center和$centerSphere操作符的状态结构体
 */
typedef struct DollarCenterOperatorState
{
	ShapeOperatorState opState;         // 基础操作符状态

	/* 输入半径，$centerSphere使用弧度，$center使用2D单位 */
	double radius;

	/* 转换为米的半径 */
	double radiusInMeters;

	/* $centerSphere输入的补地理区域面积，当半径 > (pi/2)时 */
	Datum complimentArea;

	/* 检查半径是否为$center的无穷大或$centerSphere的 >= pi */
	bool isRadiusInfinite;
}DollarCenterOperatorState;

/* 根据形状值获取对应的形状操作符
 * 输入参数：
 *   - shapeValue: 形状的BSON值
 *   - shapePointsOut: 输出形状点数组
 */
const ShapeOperator * GetShapeOperatorByValue(const bson_value_t *shapeValue,
											  bson_value_t *shapePointsOut);


#endif
