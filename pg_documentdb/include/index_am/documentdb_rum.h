/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/index_am/documentdb_rum.h
 *
 * RUM特定辅助函数的通用声明
 *
 * 本文件定义了DocumentDB中RUM（Ranking UMn inverted index）索引访问方法的
 * 核心函数接口和数据结构，用于支持全文检索和带排序的索引操作。
 *
 *-------------------------------------------------------------------------
 */

#ifndef DOCUMENTDB_RUM_H
#define DOCUMENTDB_RUM_H

#include <fmgr.h>
#include <access/amapi.h>
#include <nodes/pathnodes.h>
#include "index_am/index_am_exports.h"

/* 创建索引数组追踪器状态的函数类型 */
typedef void *(*CreateIndexArrayTrackerState)(void);
/* 向索引追踪器添加项目的函数类型，返回是否为新项目 */
typedef bool (*IndexArrayTrackerAdd)(void *state, ItemPointer item);
/* 释放索引数组追踪器状态的函数类型 */
typedef void (*FreeIndexArrayTrackerState)(void *);
/* 更新多键状态的函数类型 */
typedef void (*UpdateMultikeyStatusFunc)(Relation index);

/*
 * RUM索引数组状态函数适配器结构体
 * 提供函数指针以允许在索引扫描中管理索引数组状态时的扩展性
 * 当前接口要求提供一种用于去重索引扫描中数组条目的抽象
 */
typedef struct RumIndexArrayStateFuncs
{
	/* 创建不透明状态以管理此特定索引扫描中的条目 */
	CreateIndexArrayTrackerState createState;

	/* 向索引扫描添加项目，返回是否为新项目或现有项目 */
	IndexArrayTrackerAdd addItem;

	/* 释放用于添加项目的临时状态 */
	FreeIndexArrayTrackerState freeState;
} RumIndexArrayStateFuncs;


/* RUM库加载到进程中的方式 */
typedef enum RumLibraryLoadOptions
{
	/* 不应用自定义 - 加载默认的RUM库 */
	RumLibraryLoadOption_None = 0,

	/* 优先加载自定义的documentdb_rum（如果可用），否则回退 */
	RumLibraryLoadOption_PreferDocumentDBRum = 1,

	/* 要求使用自定义的documentdb_rum */
	RumLibraryLoadOption_RequireDocumentDBRum = 2,
} RumLibraryLoadOptions;


/* 检查索引扫描中是否可以排序的函数类型 */
typedef bool (*CanOrderInIndexScan)(IndexScanDesc scan);

/* 全局变量：RUM库加载选项 */
extern RumLibraryLoadOptions DocumentDBRumLibraryLoadOption;

/* 加载RUM库运行时函数 */
void LoadRumRoutine(void);

/*
 * 扩展RUM索引扫描开始核心函数
 * 输入参数：
 *   - rel: 索引关系
 *   - nkeys: 键数量
 *   - norderbys: 排序键数量
 *   - coreRoutine: 核心索引访问方法例程
 */
IndexScanDesc extension_rumbeginscan_core(Relation rel, int nkeys, int norderbys,
										  IndexAmRoutine *coreRoutine);
/*
 * 扩展RUM索引扫描结束核心函数
 * 输入参数：
 *   - scan: 索引扫描描述符
 *   - coreRoutine: 核心索引访问方法例程
 */
void extension_rumendscan_core(IndexScanDesc scan, IndexAmRoutine *coreRoutine);
/*
 * 扩展RUM索引扫描重新开始核心函数
 * 输入参数：
 *   - scan: 索引扫描描述符
 *   - scankey: 扫描键
 *   - nscankeys: 扫描键数量
 *   - orderbys: 排序键
 *   - norderbys: 排序键数量
 *   - coreRoutine: 核心索引访问方法例程
 *   - multiKeyStatusFunc: 多键状态获取函数
 *   - indexScanOrderedFunc: 索引扫描排序检查函数
 */
void extension_rumrescan_core(IndexScanDesc scan, ScanKey scankey, int nscankeys,
							  ScanKey orderbys, int norderbys,
							  IndexAmRoutine *coreRoutine,
							  GetMultikeyStatusFunc multiKeyStatusFunc,
							  CanOrderInIndexScan indexScanOrderedFunc);
/*
 * 扩展RUM获取位图核心函数
 * 输入参数：
 *   - scan: 索引扫描描述符
 *   - tbm: TID位图
 *   - coreRoutine: 核心索引访问方法例程
 * 返回值：匹配的元组数量
 */
int64 extension_rumgetbitmap_core(IndexScanDesc scan, TIDBitmap *tbm,
								  IndexAmRoutine *coreRoutine);
/*
 * 扩展RUM获取元组核心函数
 * 输入参数：
 *   - scan: 索引扫描描述符
 *   - direction: 扫描方向
 *   - coreRoutine: 核心索引访问方法例程
 * 返回值：是否找到元组
 */
bool extension_rumgettuple_core(IndexScanDesc scan, ScanDirection direction,
								IndexAmRoutine *coreRoutine);


/*
 * 扩展RUM成本估计核心函数
 * 输入参数：
 *   - root: 查询规划器信息
 *   - path: 索引路径
 *   - loop_count: 循环计数
 *   - indexStartupCost: 索引启动成本（输出）
 *   - indexTotalCost: 索引总成本（输出）
 *   - indexSelectivity: 索引选择性（输出）
 *   - indexCorrelation: 索引相关性（输出）
 *   - indexPages: 索引页面数（输出）
 *   - coreRoutine: 核心索引访问方法例程
 *   - forceIndexPushdownCostToZero: 强制索引下推成本为零标志
 */
void extension_rumcostestimate_core(PlannerInfo *root, IndexPath *path, double
									loop_count,
									Cost *indexStartupCost, Cost *indexTotalCost,
									Selectivity *indexSelectivity,
									double *indexCorrelation,
									double *indexPages, IndexAmRoutine *coreRoutine,
									bool forceIndexPushdownCostToZero);

/*
 * 扩展RUM构建索引核心函数
 * 输入参数：
 *   - heapRelation: 堆表关系
 *   - indexRelation: 索引关系
 *   - indexInfo: 索引信息
 *   - coreRoutine: 核心索引访问方法例程
 *   - updateMultikeyStatus: 更新多键状态的函数
 *   - amCanBuildParallel: 是否支持并行构建标志
 * 返回值：索引构建结果
 */
IndexBuildResult * extension_rumbuild_core(Relation heapRelation, Relation indexRelation,
										   struct IndexInfo *indexInfo,
										   IndexAmRoutine *coreRoutine,
										   UpdateMultikeyStatusFunc updateMultikeyStatus,
										   bool amCanBuildParallel);

/*
 * 扩展RUM插入核心函数
 * 输入参数：
 *   - indexRelation: 索引关系
 *   - values: 要插入的值数组
 *   - isnull: 空值标志数组
 *   - heap_tid: 堆表元组标识符
 *   - heapRelation: 堆表关系
 *   - checkUnique: 唯一性检查类型
 *   - indexUnchanged: 索引是否未改变标志
 *   - indexInfo: 索引信息
 *   - coreRoutine: 核心索引访问方法例程
 *   - updateMultikeyStatus: 更新多键状态的函数
 * 返回值：插入是否成功
 */
bool extension_ruminsert_core(Relation indexRelation,
							  Datum *values,
							  bool *isnull,
							  ItemPointer heap_tid,
							  Relation heapRelation,
							  IndexUniqueCheck checkUnique,
							  bool indexUnchanged,
							  struct IndexInfo *indexInfo,
							  IndexAmRoutine *coreRoutine,
							  UpdateMultikeyStatusFunc updateMultikeyStatus);

/*
 * 获取RUM索引截断状态
 * 输入参数：
 *   - indexRelation: 索引关系
 * 返回值：索引是否可以被截断
 */
bool RumGetTruncationStatus(Relation indexRelation);

struct ExplainState;

/*
 * 解释复合索引扫描
 * 输入参数：
 *   - scan: 索引扫描描述符
 *   - es: 解释状态结构体
 */
void ExplainCompositeScan(IndexScanDesc scan, struct ExplainState *es);

/*
 * 解释常规索引扫描
 * 输入参数：
 *   - scan: 索引扫描描述符
 *   - es: 解释状态结构体
 */
void ExplainRegularIndexScan(IndexScanDesc scan, struct ExplainState *es);
#endif
