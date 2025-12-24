/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/index_am/index_am_utils.h
 *
 * RUM特定辅助函数的通用声明
 *
 * 本文件定义了DocumentDB中索引访问方法的通用辅助函数和工具，
 * 提供了索引访问方法的查询、验证和优化功能。
 *
 *-------------------------------------------------------------------------
 */

#ifndef INDEX_AM_UTILS_H
#define INDEX_AM_UTILS_H

#include <postgres.h>
#include <utils/rel.h>
#include "metadata/metadata_cache.h"
#include "utils/version_utils.h"
#include "index_am/index_am_exports.h"

/* 最大替代索引访问方法数量 */
#define MAX_ALTERNATE_INDEX_AMS 5


/*
 * 设置动态索引访问方法OID并获取计数
 * 输入参数：
 *   - indexAmArray: 索引访问方法数组
 *   - indexAmArraySize: 索引访问方法数组大小
 * 返回值：成功设置的索引访问方法数量
 */
int SetDynamicIndexAmOidsAndGetCount(Datum *indexAmArray, int32_t indexAmArraySize);

/*
 * 根据名称获取索引访问方法条目
 * 输入参数：
 *   - index_am_name: 索引访问方法名称
 * 返回值：对应的BSON索引访问方法条目指针
 */
const BsonIndexAmEntry * GetBsonIndexAmByIndexAmName(const char *index_am_name);

/*
 * 检查索引访问方法是否用于索引BSON数据
 * （与TEXT、Vector、Points等索引相对）
 * 由枚举MongoIndexKind_Regular指示
 * 输入参数：
 *   - indexAm: 索引访问方法OID
 * 返回值：是否为常规BSON索引访问方法
 */
bool IsBsonRegularIndexAm(Oid indexAm);

/*
 * 检查BSON索引访问方法是否需要范围优化
 * 输入参数：
 *   - indexAm: 索引访问方法OID
 *   - opFamilyOid: 操作符族OID
 * 返回值：是否需要范围优化
 */
bool BsonIndexAmRequiresRangeOptimization(Oid indexAm, Oid opFamilyOid);

/*
 * 检查索引关系是否通过复合索引操作符类创建
 * 输入参数：
 *   - indexRelation: 索引关系
 * 返回值：是否为复合索引操作符类
 */
bool IsCompositeOpClass(Relation indexRelation);

/*
 * 检查操作符族OID是否为复合操作符族
 * 输入参数：
 *   - relam: 关系访问方法OID
 *   - opFamilyOid: 操作符族OID
 * 返回值：是否为复合操作符族
 */
bool IsCompositeOpFamilyOid(Oid relam, Oid opFamilyOid);

/*
 * 检查操作符族OID是否为支持并行的复合操作符族
 * 输入参数：
 *   - relam: 关系访问方法OID
 *   - opFamilyOid: 操作符族OID
 * 返回值：是否为支持并行的复合操作符族
 */
bool IsCompositeOpFamilyOidWithParallelSupport(Oid relam, Oid opFamilyOid);

/*
 * 检查操作符族OID是否指向单路径操作符族
 * 输入参数：
 *   - relam: 关系访问方法OID
 *   - opFamilyOid: 操作符族OID
 * 返回值：是否为单路径操作符族
 */
bool IsSinglePathOpFamilyOid(Oid relam, Oid opFamilyOid);

/*
 * 检查操作符族OID是否指向文本路径操作符族
 * 输入参数：
 *   - relam: 关系访问方法OID
 *   - opFamilyOid: 操作符族OID
 * 返回值：是否为文本路径操作符族
 */
bool IsTextPathOpFamilyOid(Oid relam, Oid opFamilyOid);

/*
 * 获取文本路径操作符族OID
 * 输入参数：
 *   - relam: 关系访问方法OID
 * 返回值：文本路径操作符族OID
 */
Oid GetTextPathOpFamilyOid(Oid relam);

/*
 * 检查操作符族OID是否为唯一性检查操作符族
 * 输入参数：
 *   - relam: 关系访问方法OID
 *   - opFamilyOid: 操作符族OID
 * 返回值：是否为唯一性检查操作符族
 */
bool IsUniqueCheckOpFamilyOid(Oid relam, Oid opFamilyOid);

/*
 * 检查操作符族OID是否为哈希路径操作符族
 * 输入参数：
 *   - relam: 关系访问方法OID
 *   - opFamilyOid: 操作符族OID
 * 返回值：是否为哈希路径操作符族
 */
bool IsHashedPathOpFamilyOid(Oid relam, Oid opFamilyOid);

/*
 * 检查操作符类是否支持排序
 * 输入参数：
 *   - indexAm: 索引访问方法OID
 *   - IndexPathOpFamilyAm: 索引路径操作符族访问方法OID
 * 返回值：是否支持排序
 */
bool IsOrderBySupportedOnOpClass(Oid indexAm, Oid IndexPathOpFamilyAm);

/*
 * 根据关系访问方法获取多键状态函数
 * 输入参数：
 *   - relam: 关系访问方法OID
 * 返回值：多键状态函数指针
 */
GetMultikeyStatusFunc GetMultiKeyStatusByRelAm(Oid relam);

/*
 * 获取索引是否支持反向扫描
 * 输入参数：
 *   - relam: 关系访问方法OID
 *   - indexCanOrder: 输出参数，索引是否支持排序
 * 返回值：是否支持反向扫描
 */
bool GetIndexSupportsBackwardsScan(Oid relam, bool *indexCanOrder);

/*
 * 获取索引访问方法是否支持仅索引扫描
 * 输入参数：
 *   - indexAm: 索引访问方法OID
 *   - opFamilyOid: 操作符族OID
 *   - getMultiKeyStatus: 输出参数，多键状态函数
 *   - getTruncationStatus: 输出参数，截断状态函数
 * 返回值：是否支持仅索引扫描
 */
bool GetIndexAmSupportsIndexOnlyScan(Oid indexAm, Oid opFamilyOid,
									 GetMultikeyStatusFunc *getMultiKeyStatus,
									 GetTruncationStatusFunc *getTruncationStatus);

/*
 * 尝试根据索引访问方法添加解释信息
 * 输入参数：
 *   - scan: 索引扫描描述符
 *   - es: 解释状态结构体
 */
void TryExplainByIndexAm(struct IndexScanDescData *scan, struct ExplainState *es);

#endif
