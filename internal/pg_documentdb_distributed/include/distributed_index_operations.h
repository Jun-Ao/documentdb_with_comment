/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/distributed_index_operations.h
 *
 * The implementation for distributed index operations.
 *
 *-------------------------------------------------------------------------
 */
#ifndef DOCUMENTDB_DISTRIBUTED_INDEX_OPS_H
#define DOCUMENTDB_DISTRIBUTED_INDEX_OPS_H

/*
 * UpdateDistributedPostgresIndex - 更新分布式PostgreSQL索引
 *
 * 该函数用于在分布式环境中管理PostgreSQL索引的状态和配置。
 * 在DocumentDB与Citus集成的分布式架构中，索引需要在多个分片节点上
 * 保持一致性和正确性，该函数负责协调这种分布式索引管理。
 *
 * 参数说明：
 * @param collectionId: 集合的唯一标识符（64位无符号整数）
 *                      用于标识需要操作的DocumentDB集合
 * @param indexId: 索引的唯一标识符（32位整数）
 *                 用于标识需要更新的具体索引
 * @param operation: 要执行的操作类型（32位整数）
 *                   可能的操作包括：
 *                   - 创建索引操作
 *                   - 删除索引操作
 *                   - 索引状态更新操作
 *                   - 索引优化操作
 * @param value: 操作相关的布尔值参数
 *               含义取决于operation类型，可能用于：
 *               - 是否同步创建（true=同步，false=异步）
 *               - 是否在所有分片上执行
 *               - 是否强制重建索引
 *
 * 主要功能：
 * 1. 在分布式环境中创建索引，确保所有相关分片都创建对应的索引
 * 2. 同步索引状态，保持分布式集群中各节点的索引一致性
 * 3. 处理索引的删除和更新操作，避免数据不一致
 * 4. 支持同步和异步索引操作，适应不同的性能需求
 *
 * 分布式考虑：
 * - 需要考虑分片的位置化（colocation）策略
 * - 可能需要在多个节点上并行执行索引操作
 * - 需要处理网络分区和节点故障的情况
 * - 保证索引操作的原子性和一致性
 *
 * 返回值：void
 * 无返回值，但会修改分布式索引的状态
 *
 * 错误处理：
 * - 如果某个分片节点不可达，可能需要重试或记录错误
 * - 索引创建失败时需要清理已创建的部分索引
 * - 需要保证分布式事务的一致性
 */
void UpdateDistributedPostgresIndex(uint64_t collectionId, int indexId, int operation,
									bool value);

#endif
