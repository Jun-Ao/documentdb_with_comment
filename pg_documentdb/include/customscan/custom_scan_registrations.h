/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/customscan/custom_scan_registrations.h
 *
 *  Implementation of a custom scan plan.
 *  自定义扫描计划的注册函数声明，用于注册各种扫描节点
 *
 *-------------------------------------------------------------------------
 */

#ifndef CUSTOM_SCAN_REGISTRATION_H
#define CUSTOM_SCAN_REGISTRATION_H

/*
 * RegisterScanNodes - 注册自定义扫描节点
 * 注册 DocumentDB 的所有自定义扫描节点，使查询规划器能够识别和使用这些节点
 */
void RegisterScanNodes(void);

/*
 * RegisterQueryScanNodes - 注册查询扫描节点
 * 专门注册用于查询优化的自定义扫描节点，包括文本搜索和向量搜索扫描节点
 */
void RegisterQueryScanNodes(void);

/*
 * RegisterExplainScanNodes - 注册解释扫描节点
 * 注册用于 EXPLAIN 功能的自定义扫描节点，支持查看查询计划的执行细节
 */
void RegisterExplainScanNodes(void);

#endif
