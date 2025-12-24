/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geojson_utils.h
 *
 * Definitions for utilities to work with GeoJSON Data type
 * GeoJSON 数据类型相关的工具函数定义
 * 支持地理空间数据的解析和处理，包括各种坐标系定义
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GEOJSON_UTILS_H
#define BSON_GEOJSON_UTILS_H

#include <postgres.h>

#include "io/bson_core.h"
#include "geospatial/bson_geospatial_private.h"

/*
 * GeoJSON 坐标参考系统（CRS）定义
 * GEOJSON_CRS_BIGPOLYGON - MongoDB 大多边形 CRS，使用严格缠绕规则的 EPSG:4326
 * GEOJSON_CRS_EPSG_4326 - 标准 EPSG:4326 WGS84 坐标系
 * GEOJSON_CRS_84 - OGC 标准的 CRS84 坐标系，使用经纬度坐标
 */
#define GEOJSON_CRS_BIGPOLYGON "urn:x-mongodb:crs:strictwinding:EPSG:4326"
#define GEOJSON_CRS_EPSG_4326 "EPSG:4326"
#define GEOJSON_CRS_84 "urn:ogc:def:crs:OGC:1.3:CRS84"

/*
 * ParseValueAsGeoJSON - 将 BSON 值解析为 GeoJSON 格式
 * @value: BSON 值指针
 * @parseState: GeoJSON 解析状态指针
 * 解析 BSON 格式的地理空间数据为内部 GeoJSON 表示
 * 返回值：解析成功返回 true，否则返回 false
 */
bool ParseValueAsGeoJSON(const bson_value_t *value,
						 GeoJsonParseState *parseState);


#endif
