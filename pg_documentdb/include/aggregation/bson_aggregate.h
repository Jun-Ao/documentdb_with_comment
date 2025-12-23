/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_aggregates.h
 *
 * BSON Aggregate headers
 * BSON 聚合相关头文件
 *
 * 此文件提供了用于处理需要对齐到8字节边界的变长数据结构（varlena）的工具类型和函数。
 * 在 PostgreSQL 中，varlena 是一种变长数据类型的存储格式，通常用于存储字符串、字节等。
 * 但某些结构体需要 8 字节对齐以保证性能和正确性，本文件提供的 MaxAlignedVarlena 就是为此目的。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_AGGREGATE_H
#define BSON_AGGREGATE_H

#include <postgres.h>
#if PG_VERSION_NUM >= 160000
#include <varatt.h>
#endif

/*
 * Varlena wrapper for MAXALIGN(8bytes) structs that are serialized as a varlena
 * MAXALIGN(8字节) 结构体的 varlena 包装器，这些结构体被序列化为 varlena 格式
 *
 * 用途说明：
 * PostgreSQL 中某些数据结构（特别是包含双精度浮点数或需要 SIMD 指令优化的结构）
 * 需要按照 8 字节边界对齐，以保证：
 * 1. CPU 访问效率（避免未对齐访问导致的性能下降）
 * 2. 某些平台上的正确性（某些架构不支持未对齐内存访问）
 *
 * 结构布局：
 * - vl_len_: PostgreSQL 标准的 varlena 头部，存储数据总大小（包含头部本身）
 * - pad: 4字节填充，确保后续的 state 数据从8字节边界开始
 * - state: 柔性数组成员，实际结构体数据紧随其后
 */
typedef struct pg_attribute_aligned (MAXIMUM_ALIGNOF) MaxAlignedVarlena
{
	int32 vl_len_; /* (DO NOT TOUCH) varlena header / varlena 头部（请勿修改） */
	int32 pad;     /* (UNUSED SPACE) 4 bytes to align following data at 8-byte boundary / 未使用的4字节空间，用于将后续数据对齐到8字节边界 */
	char state[FLEXIBLE_ARRAY_MEMBER]; /* raw struct bytes to follow / 原始结构体字节数据（柔性数组） */
} MaxAlignedVarlena;


/*
 * Allocate a MaxAlignedVarlena of structSize bytes without
 * zeroing the memory.
 * 分配一个指定大小的 MaxAlignedVarlena，但不将内存清零
 *
 * 参数说明：
 * - structSize: 需要存储的实际结构体大小（不包含 MaxAlignedVarlena 头部）
 *
 * 返回值：
 * - 返回指向新分配的 MaxAlignedVarlena 的指针
 *
 * 注意：
 * - 此函数使用 palloc() 而非 palloc0()，因此分配的内存内容未初始化
 * - 调用者需要确保后续会正确初始化所有字段
 * - 分配的总大小 = sizeof(MaxAlignedVarlena) + structSize
 */
static inline MaxAlignedVarlena *
AllocateMaxAlignedVarlena(Size structSize)
{
	Size total = sizeof(MaxAlignedVarlena) + structSize;	/* 计算总大小：头部 + 实际数据 */
	MaxAlignedVarlena *bytes = (MaxAlignedVarlena *) palloc(total);	/* 使用 PostgreSQL 的 palloc 分配内存 */
	SET_VARSIZE(bytes, total);	/* 设置 varlena 头部的大小字段 */
	return bytes;
}


/*
 * Allocate a MaxAlignedVarlena of structSize bytes with
 * zeroing the memory.
 * 分配一个指定大小的 MaxAlignedVarlena，并将内存清零
 *
 * 参数说明：
 * - structSize: 需要存储的实际结构体大小（不包含 MaxAlignedVarlena 头部）
 *
 * 返回值：
 * - 返回指向新分配的 MaxAlignedVarlena 的指针，内存已清零
 *
 * 注意：
 * - 此函数使用 palloc0()，确保分配的内存全部初始化为零
 * - 适用于需要确保内存干净的场景
 * - 分配的总大小 = sizeof(MaxAlignedVarlena) + structSize
 */
static inline MaxAlignedVarlena *
AllocateZeroedMaxAlignedVarlena(Size structSize)
{
	Size total = sizeof(MaxAlignedVarlena) + structSize;	/* 计算总大小：头部 + 实际数据 */
	MaxAlignedVarlena *bytes = (MaxAlignedVarlena *) palloc0(total);	/* 使用 palloc0 分配并清零内存 */
	SET_VARSIZE(bytes, total);	/* 设置 varlena 头部的大小字段 */
	return bytes;
}


/*
 * GetMaxAlignedVarlena - 将 bytea 指针转换为 MaxAlignedVarlena 指针
 *
 * 参数说明：
 * - bytes: 指向 bytea 类型数据的指针（PostgreSQL 的变长字节类型）
 *
 * 返回值：
 * - 返回转换为 MaxAlignedVarlena 类型的指针
 *
 * 注意：
 * - 此函数仅进行类型转换，不进行任何内存复制或验证
 * - 调用者必须确保传入的 bytes 确实指向一个有效的 MaxAlignedVarlena 结构
 * - 主要用于将 PostgreSQL 存储的 varlena 数据还原为 MaxAlignedVarlena 结构
 */
static inline MaxAlignedVarlena *
GetMaxAlignedVarlena(bytea *bytes)
{
	return (MaxAlignedVarlena *) bytes;
}


#endif /* BSON_AGGREGATE_H */
