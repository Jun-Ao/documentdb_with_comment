/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/distribution/cluster_versioning.c
 *
 * Utilities that Provide extension functions to handle version upgrade
 * scenarios for the current extension.
 *-------------------------------------------------------------------------
 * 集群版本管理工具
 *
 * 本文件提供用于处理扩展版本升级场景的工具函数。
 *
 * 主要功能：
 * 1. 版本缓存失效：在升级后清除所有进程的版本缓存
 * 2. 版本查询：获取当前缓存的集群版本（用于调试和测试）
 *
 * 核心概念：
 * - 版本缓存：每个进程维护的集群版本信息缓存
 * - 缓存失效：在版本升级后通知所有进程更新其缓存
 * - 元数据缓存：集合和索引的元数据缓存
 *-------------------------------------------------------------------------
 */

#include "utils/version_utils.h"
#include "metadata/metadata_cache.h"
#include <utils/builtins.h>
#include "utils/query_utils.h"
#include "executor/spi.h"
#include "utils/inval.h"
#include "utils/version_utils_private.h"

PG_FUNCTION_INFO_V1(invalidate_cluster_version);
PG_FUNCTION_INFO_V1(get_current_cached_cluster_version);

/*
 * Invalidates the version cache and the metadata cache.
 * -----
 * 使集群版本缓存和元数据缓存失效
 *
 * 此函数在集群版本升级后调用，负责：
 * 1. 清除当前进程的版本缓存
 * 2. 清除集合元数据缓存
 * 3. 触发关系缓存失效，确保所有进程获取最新的元数据
 *
 * 通常由 cluster_data 表的触发器在数据更新时自动调用。
 * 也可以手动调用来强制刷新缓存。
 *
 * 注意：此函数只影响当前进程的缓存。要使所有进程的缓存失效，
 *       需要通过触发器或消息通知机制。
 */
Datum
invalidate_cluster_version(PG_FUNCTION_ARGS)
{
	InvalidateVersionCache();

	/* Also invalidate the metadata_cache */
	/* 同时清除元数据缓存 */
	InvalidateCollectionsCache();
	CacheInvalidateRelcacheAll();
	PG_RETURN_VOID();
}


/*
 * Returns the text version of the cluster version
 * Used for debugging and testing purposes.
 * -----
 * 获取当前缓存的集群版本
 *
 * 返回当前进程缓存的集群版本字符串。
 * 主要用于调试和测试，验证版本缓存是否正确。
 *
 * 返回格式：文本字符串（如 "1.2.3"）
 *
 * 使用场景：
 * - 调试版本升级问题
 * - 验证缓存失效是否生效
 * - 监控集群版本状态
 */
Datum
get_current_cached_cluster_version(PG_FUNCTION_ARGS)
{
	const char *versionString = GetCurrentVersionForLogging();
	PG_RETURN_DATUM(CStringGetTextDatum(versionString));
}
