/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_private.h
 *
 * 地理空间功能的私有数据结构和函数声明
 *
 * 本文件定义了DocumentDB中地理空间功能的内部实现，包括：
 * - 地理空间数据类型和常量定义
 * - WKB（Well-Known Binary）格式处理
 * - GeoJSON解析状态
 * - PostGIS集成接口
 * - 错误处理机制
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GEOSPATIAL_PRIVATE_H
#define BSON_GEOSPATIAL_PRIVATE_H

#include <postgres.h>
#include <utils/builtins.h>
#include <lib/stringinfo.h>

#include "io/bson_core.h"
#include "metadata/metadata_cache.h"
#include "utils/documentdb_errors.h"


/*
 * 默认的地理空间参考系统ID（SRID）
 * WGS84坐标系在PostGIS中使用4326作为SRID（空间参考ID）
 * 用于将地理数据作为WGS84坐标系处理
 */
#define DEFAULT_GEO_SRID 4326

#define GeometryParseFlag uint32


/* 根据字节序定义WKB字节顺序
 * WKB（Well-Known Binary）格式使用特定的字节标识符表示字节序
 * 1表示小端字节序（Little Endian），0表示大端字节序（Big Endian）
 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
# define WKB_BYTE_ORDER (unsigned char) 1  // 小端字节序
#elif __BYTE_ORDER == __BIG_ENDIAN
# define WKB_BYTE_ORDER = (unsigned char) 0  // 大端字节序
#endif

/*
 * PostGIS扩展WKB（EWKB）格式的SRID标志位
 * 在PostGIS的EWKB格式中，type字段通常填充了关于SRID是否包含在缓冲区中的元数据
 * 此标志位用于标识几何对象包含SRID信息
 * 更多信息参考PostGIS源码：liblwgeom/liblwgeom.h
 */
#define POSTGIS_EWKB_SRID_FLAG 0x20000000

/*
 * PostGIS EWKB SRID标志位的取反掩码
 * WKBBufferGetByteaWithSRID函数在从WKB缓冲区提取bytea时，会将SRID标志位嵌入type字段
 * 此掩码用于移除嵌入的标志位，从bytea中获取原始类型
 */
#define POSTGIS_EWKB_SRID_NEGATE_FLAG 0xDFFFFFFF

/*
 * 地理空间有效性错误处理的宏定义
 * 用于处理所有地理空间的有效性错误情况，应在抛出错误之前放置此宏
 * 确保在应该忽略错误的情况下不会抛出错误
 *
 * 此宏的设计效率很高，即使在错误不会抛出的情况下也不会为错误消息分配空间
 *
 * 使用方式：
 * bool shouldThrowError = true / false;
 * if (some error condition )
 * {
 *      RETURN_FALSE_IF_ERROR_NOT_EXPECTED(shouldThrowError, (
 *          errcode(ERRCODE_DOCUMENTDB_BADVALUE),
 *          errmsg("Error"),
 *          errdetail_log("PII Safe error")
 *      ));
 * }
 */
#define RETURN_FALSE_IF_ERROR_NOT_EXPECTED(shouldThrow, errCodeFormat) \
	if (!shouldThrow) { return false; } \
	else \
	{ \
		ereport(ERROR, errCodeFormat); \
	}


#define EMPTY_GEO_ERROR_PREFIX ""  // 空地理空间错误前缀

/* 获取地理空间错误代码 */
#define GEO_ERROR_CODE(errorCtxt) (errorCtxt ? errorCtxt->errCode : \
								   ERRCODE_DOCUMENTDB_BADVALUE)
/* 获取地理空间错误前缀 */
#define GEO_ERROR_PREFIX(errorCtxt) (errorCtxt && errorCtxt->errPrefix ? \
									 errorCtxt->errPrefix(errorCtxt->document) : \
									 EMPTY_GEO_ERROR_PREFIX)
/* 获取地理空间错误提示前缀 */
#define GEO_HINT_PREFIX(errorCtxt) (errorCtxt && errorCtxt->hintPrefix ? \
									errorCtxt->hintPrefix(errorCtxt->document) : \
									EMPTY_GEO_ERROR_PREFIX)

/* WKB格式中各部分的字节大小定义 */
#define WKB_BYTE_SIZE_ORDER 1     // WKB中字节序的字节大小（1字节）
#define WKB_BYTE_SIZE_TYPE 4      // WKB中几何类型的字节大小（4字节）
#define WKB_BYTE_SIZE_NUM WKB_BYTE_SIZE_TYPE  // WKB中组件数量的字节大小
#define WKB_BYTE_SIZE_SRID WKB_BYTE_SIZE_TYPE  // WKB中SRID的字节大小
#define WKB_BYTE_SIZE_POINT 16     // WKB中点值的字节大小（包括x和y坐标）

/*
 * 地球半径（米）- 根据NASA文档的球体计算值
 * 参考：https://nssdc.gsfc.nasa.gov/planetary/factsheet/earthfact.html
 */
static const float8 RADIUS_OF_EARTH_M = 6378.137 * 1000;

/*
 * 地球椭球体半径（米）- 用于球体计算的椭球体半径
 * 地球不是一个完美的球体，这是一个基于椭球体到球体转换模型的良好近似值
 * 更多信息：https://en.wikipedia.org/wiki/Earth_ellipsoid
 */
static const float8 RADIUS_OF_ELLIPSOIDAL_EARTH_M = 6371008.7714150595;


/*
 * GeoJSON类型定义，基于GeoJSON标准 (RFC 7946)
 * https://datatracker.ietf.org/doc/html/rfc7946
 *
 * 注意：这些类型设计为位标志（bitmask flags），可以组合使用
 */
typedef enum GeoJsonType
{
	GeoJsonType_UNKNOWN = 0x0,               // 未知类型
	GeoJsonType_POINT = 0x1,                 // 点
	GeoJsonType_LINESTRING = 0x2,            // 线
	GeoJsonType_POLYGON = 0x4,               // 多边形
	GeoJsonType_MULTIPOINT = 0x8,            //多点
	GeoJsonType_MULTILINESTRING = 0x10,      // 多线
	GeoJsonType_MULTIPOLYGON = 0x20,         // 多边形
	GeoJsonType_GEOMETRYCOLLECTION = 0x40,   // 几何集合

	/* 自定义类型，用于选择所有类型 */
	GeoJsonType_ALL = 0xFF
} GeoJsonType;


/*
 * WKB（Well-Known Binary）几何类型定义
 * 用于表示常见的几何对象类型
 */
typedef enum WKBGeometryType
{
	WKBGeometryType_Invalid = 0x0,           // 无效类型
	WKBGeometryType_Point = 0x1,             // 点
	WKBGeometryType_LineString = 0x2,        // 线
	WKBGeometryType_Polygon = 0x3,           // 多边形
	WKBGeometryType_MultiPoint = 0x4,        // 多点
	WKBGeometryType_MultiLineString = 0x5,   // 多线
	WKBGeometryType_MultiPolygon = 0x6,     // 多多边形
	WKBGeometryType_GeometryCollection = 0x7 // 几何集合
} WKBGeometryType;


/* 地理空间错误前缀回调函数类型 */
typedef const char *(*GeospatialErrorPrefixFunc)(const pgbson *);
/* 地理空间错误提示前缀回调函数类型 */
typedef const char *(*GeospatialErrorHintPrefixFunc)(const pgbson *);

/*
 * 地理空间错误上下文结构体，用于错误报告
 */
typedef struct GeospatialErrorContext
{
	/*
	 * 报告错误时使用的文档引用，传递给errPrefix和hintPrefix回调函数
	 * 调用者可以决定是否忽略文档或提取元数据（如_id）插入到前缀和提示前缀中
	 */
	const pgbson *document;

	/* 要抛出的期望错误代码 */
	int64 errCode;

	/* 错误前缀，用于追加到错误消息前，这是一个回调函数，只有在有效错误情况下才会调用
	 *
	 * 这有助于我们在不需要时不创建字符串并避免不必要的文档遍历
	 * 例如：在地理空间情况下，我们不希望遍历文档获取_id并将其作为errPrefix的一部分发送，
	 * 如果所有情况都有效，这可能不会被使用
	 */
	GeospatialErrorPrefixFunc errPrefix;

	/* 错误提示前缀，与errPrefix相同，这也是一个回调函数
	 * 返回的前缀中绝不能包含个人身份信息（PII）
	 */
	GeospatialErrorHintPrefixFunc hintPrefix;
} GeospatialErrorContext;


/*
 * 解析BSON值为GeoJSON数据时的通用状态结构体
 */
typedef struct GeoJsonParseState
{
	/*=============== 输入变量 ===================*/

	/*
	 * 用于在无效时立即抛出错误或通知错误是否存在
	 */
	bool shouldThrowValidityError;

	/*
	 * 期望解析的GeoJSON类型
	 */
	GeoJsonType expectedType;

	/*
	 * 抛出错误时使用的错误上下文
	 */
	GeospatialErrorContext *errorCtxt;

	/*=============== 输出变量 =================*/

	/* 解析过程中发现的GeoJSON类型 */
	GeoJsonType type;

	/* GeoJSON中给定的CRS（Coordinate Reference System）名称 */
	const char *crs;

	/* 多边形中的环数量，目前用于对带孔的多边形抛出$geoWithin错误
	 * 如果geoJSON是多多边形，则包含最大环数
	 */
	int32 numOfRingsInPolygon;

	/*================ 输入输出变量 =================*/

	/*
	 * WKB缓冲区，用于构建和存储WKB格式的地理数据
	 */
	StringInfo buffer;
} GeoJsonParseState;


/*
 * 基本点结构体，用于存储所有几何类型的x和y坐标
 * 提供PostGIS支持
 */
typedef struct Point
{
	float8 x;  // x坐标
	float8 y;  // y坐标
} Point;


/*
 * 解析几何体时的标志位定义
 */
typedef enum ParseFlags
{
	/* 无标志 */
	ParseFlag_None = 0x0,

	/* 传统点格式标志 */
	ParseFlag_Legacy = 0x2,

	/* 传统点格式，解析时不会尝试抛出错误 */
	ParseFlag_Legacy_NoError = 0x4,

	/* 仅解析GeoJSON点格式 */
	ParseFlag_GeoJSON_Point = 0x8,

	/* 解析任何GeoJSON类型 */
	ParseFlag_GeoJSON_All = 0x10,
} ParseFlags;

/* 将BSON值解析为点坐标
 * 输入参数：
 *   - value: BSON值指针
 *   - throwError: 是否抛出错误
 *   - errCtxt: 错误上下文
 *   - outPoint: 输出的点结构
 */
bool ParseBsonValueAsPoint(const bson_value_t *value,
						   bool throwError,
						   GeospatialErrorContext *errCtxt,
						   Point *outPoint);

/* 将BSON值解析为带边界的点坐标
 * 输入参数：
 *   - value: BSON值指针
 *   - throwError: 是否抛出错误
 *   - errCtxt: 错误上下文
 *   - outPoint: 输出的点结构
 */
bool ParseBsonValueAsPointWithBounds(const bson_value_t *value,
									 bool throwError,
									 GeospatialErrorContext *errCtxt,
									 Point *outPoint);

/*********缓冲区写入函数********/

/* 从BSON值获取WKB格式的几何数据
 * 输入参数：
 *   - value: BSON值指针
 *   - parseFlag: 几何解析标志
 *   - parseState: GeoJSON解析状态
 */
bool BsonValueGetGeometryWKB(const bson_value_t *value,
							 const GeometryParseFlag parseFlag,
							 GeoJsonParseState *parseState);


/*
 * 从扩展WKB（包含SRID）获取几何对象
 * 返回PostGIS几何类型的Datum值
 */
static inline Datum
GetGeometryFromWKB(const bytea *wkbBuffer)
{
	return OidFunctionCall1(PostgisGeometryFromEWKBFunctionId(),
							PointerGetDatum(wkbBuffer));
}


/*
 * 从扩展WKB（包含SRID）获取地理对象
 * 返回PostGIS地理类型的Datum值
 */
static inline Datum
GetGeographyFromWKB(const bytea *wkbBuffer)
{
	return OidFunctionCall1(PostgisGeographyFromWKBFunctionId(),
							PointerGetDatum(wkbBuffer));
}


/*
 * 判断WKB几何类型是否为集合或多类型
 * 集合类型包括GeometryCollection、MultiPolygon、MultiPoint、MultiLineString
 */
static inline bool
IsWKBCollectionType(WKBGeometryType type)
{
	return type == WKBGeometryType_GeometryCollection ||
		   type == WKBGeometryType_MultiPolygon ||
		   type == WKBGeometryType_MultiPoint ||
		   type == WKBGeometryType_MultiLineString;
}


/*
 * 向缓冲区追加4个空字节，返回4字节空间的起始位置
 * 该函数用于在未知多组件长度时预留空间，稍后填充实际值
 * 长度为4字节，先跳过4字节，稍后填充
 *
 * 示例：
 * 当前缓冲区 => 0x11110011
 * 跳过4字节后
 * 缓冲区 => 0x1111001100000000
 *                    ^
 *                    | => 这是返回的4字节空间起始位置
 */
static inline int32
WKBBufferAppend4EmptyBytesForNums(StringInfo buffer)
{
	int32 num = 0;
	int32 currentLength = buffer->len;
	appendBinaryStringInfoNT(buffer, (char *) &num, WKB_BYTE_SIZE_NUM);
	return currentLength;
}


/*
 * 向WKB缓冲区写入几何对象的头部信息
 * 头部信息包括：字节序和几何类型，共5字节（1字节序+4字节类型）
 *
 * 例如：
 * 0x01 01000000 => 表示小端字节序的点
 * 0x00 00000001 => 表示大端字节序的点
 */
static inline void
WriteHeaderToWKBBuffer(StringInfo buffer, const WKBGeometryType type)
{
	char endianess = (char) WKB_BYTE_ORDER;
	appendBinaryStringInfoNT(buffer, (char *) &endianess, WKB_BYTE_SIZE_ORDER);
	appendBinaryStringInfoNT(buffer, (char *) &type, WKB_BYTE_SIZE_TYPE);
}


/*
 * 向WKB缓冲区写入简单点数据
 * 点包含两个双精度浮点数，表示x和y坐标
 */
static inline void
WritePointToWKBBuffer(StringInfo buffer, const Point *point)
{
	appendBinaryStringInfo(buffer, (char *) point, WKB_BYTE_SIZE_POINT);
}


/*
 * 在缓冲区的相对位置写入组件数量（num）
 * num可以表示：多中的点数、多边形的环数、几何集合的几何对象数量等
 */
static inline void
WriteNumToWKBBufferAtPosition(StringInfo buffer, int32 relativePosition, int32 num)
{
	Assert(buffer->len > relativePosition + WKB_BYTE_SIZE_NUM);
	memcpy(buffer->data + relativePosition, (void *) &num, WKB_BYTE_SIZE_NUM);
}


/*
 * 向缓冲区写入组件数量（num）
 * num可以表示：多中的点数、多边形的环数、几何集合的几何对象数量等
 */
static inline void
WriteNumToWKBBuffer(StringInfo buffer, int32 num)
{
	appendBinaryStringInfoNT(buffer, (char *) &num, WKB_BYTE_SIZE_NUM);
}


/*
 * 将StringInfo缓冲区追加到WKB缓冲区
 */
static inline void
WriteStringInfoBufferToWKBBuffer(StringInfo wkbBuffer, StringInfo bufferToAppend)
{
	appendBinaryStringInfoNT(wkbBuffer, (char *) bufferToAppend->data,
							 bufferToAppend->len);
}


/*
 * 按长度将缓冲区追加到WKB缓冲区
 */
static inline void
WriteBufferWithLengthToWKBBuffer(StringInfo wkbBuffer, const char *bufferStart, int32
								 length)
{
	appendBinaryStringInfoNT(wkbBuffer, bufferStart, length);
}


/*
 * 从偏移量开始将StringInfo缓冲区追加到WKB缓冲区
 */
static inline void
WriteStringInfoBufferToWKBBufferWithOffset(StringInfo wkbBuffer, StringInfo
										   bufferToAppend, Size offset)
{
	appendBinaryStringInfoNT(wkbBuffer, (char *) bufferToAppend->data + offset,
							 bufferToAppend->len - offset);
}


/*
 * 深度释放StringInfo格式的WKB缓冲区
 * resetStringInfo()只重置数据指针为NULL，不清理palloc分配的内存
 */
static inline void
DeepFreeWKB(StringInfo wkbBuffer)
{
	if (wkbBuffer->data != NULL)
	{
		pfree(wkbBuffer->data);
	}
	pfree(wkbBuffer);
}


/*
 * WKBBufferGetByteaWithSRID将单个几何/地理对象的WKB转换为带SRID的扩展WKB
 * 输入WKB格式为：
 * <1字节序> <4字节类型> <几何点数据的任意字节>
 *
 * 此函数将WKB转换为嵌入SRID的扩展WKB
 * 函数返回的bytea格式为：
 * <1字节序> <4字节修改后的类型> <4字节SRID> <几何点数据的任意字节>
 */
static inline bytea *
WKBBufferGetByteaWithSRID(StringInfo wkbBuffer)
{
	/* bytea结构 => 长度(varlena头部) +  */
	Size size = wkbBuffer->len + VARHDRSZ + WKB_BYTE_SIZE_SRID;
	bytea *result = (bytea *) palloc0(size);

	/* 写入大小 */
	SET_VARSIZE(result, size);
	uint8 *wkbData = (uint8 *) VARDATA_ANY(result);

	/* 首先复制字节序 */
	memcpy(wkbData, wkbBuffer->data, WKB_BYTE_SIZE_ORDER);

	/* 在类型中嵌入SRID标志位 */
	uint32 type = 0;
	memcpy(&type, (wkbBuffer->data + WKB_BYTE_SIZE_ORDER), sizeof(uint32));
	type = type | POSTGIS_EWKB_SRID_FLAG;
	memcpy((wkbData + WKB_BYTE_SIZE_ORDER), (uint8 *) &type, WKB_BYTE_SIZE_TYPE);

	/* 在类型后插入SRID */
	uint32 srid = DEFAULT_GEO_SRID;
	uint32 sridPos = WKB_BYTE_SIZE_ORDER + WKB_BYTE_SIZE_TYPE;
	memcpy((wkbData + sridPos), (uint8 *) &srid, WKB_BYTE_SIZE_SRID);

	 /*复制排除字节序和类型的数据并将其插入到SRID之后 */
	uint32 dataPos = sridPos + WKB_BYTE_SIZE_SRID;
	uint32 endianAndTypeLen = WKB_BYTE_SIZE_ORDER + WKB_BYTE_SIZE_TYPE;
	memcpy((wkbData + dataPos), (wkbBuffer->data + endianAndTypeLen),
		   (wkbBuffer->len - endianAndTypeLen));

	return result;
}


/*
 * WKBBufferGetCollectionByteaWithSRID将多个几何/地理对象的WKB转换为带SRID的集合类型
 * 输入WKB格式为：
 * [<1字节序> <4字节类型> <几何点数据的任意字节> ... ]
 *
 * 此函数创建一个新的收集类型`collectType`并嵌入SRID，返回的bytea格式为：
 * <1字节序> <4字节收集类型> <4字节SRID> <wkbBuffer（多个值的缓冲区）>
 */
static inline bytea *
WKBBufferGetCollectionByteaWithSRID(StringInfo wkbBuffer, WKBGeometryType collectType,
									int32 totalNum)
{
	/* bytea结构 => (varlena头部) + 字节序 + 类型 + SRID + 收集数量 + 数据 */
	Size size = VARHDRSZ + WKB_BYTE_SIZE_ORDER + WKB_BYTE_SIZE_TYPE +
				WKB_BYTE_SIZE_SRID + WKB_BYTE_SIZE_NUM + wkbBuffer->len;
	bytea *result = (bytea *) palloc0(size);

	/* 写入大小 */
	SET_VARSIZE(result, size);
	uint8 *wkbData = (uint8 *) VARDATA_ANY(result);

	/* 首先写入字节序 */
	memcpy(wkbData, wkbBuffer->data, WKB_BYTE_SIZE_ORDER);

	/* 在类型中嵌入SRID标志位 */
	int32 type = collectType | POSTGIS_EWKB_SRID_FLAG;
	memcpy((wkbData + WKB_BYTE_SIZE_ORDER), (uint8 *) &type, WKB_BYTE_SIZE_TYPE);

	/* 在类型后插入SRID */
	uint32 srid = DEFAULT_GEO_SRID;
	uint32 sridPos = WKB_BYTE_SIZE_ORDER + WKB_BYTE_SIZE_TYPE;
	memcpy((wkbData + sridPos), (uint8 *) &srid, WKB_BYTE_SIZE_SRID);

	/* 要复制的项目数量 */
	uint32 totalPos = sridPos + WKB_BYTE_SIZE_TYPE;
	memcpy((wkbData + totalPos), (uint8 *) &totalNum, WKB_BYTE_SIZE_SRID);

	/*
	 * 完全复制数据，对于Multipoint和GeometryCollection等集合，
	 * 每个单独的条目都有自己的字节序和类型
	 */
	uint32 dataPos = totalPos + WKB_BYTE_SIZE_NUM;
	memcpy((wkbData + dataPos), wkbBuffer->data, wkbBuffer->len);

	return result;
}


#endif
