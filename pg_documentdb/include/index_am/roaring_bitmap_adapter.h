/*-------------------------------------------------------------------------
 * Copyright (c) 2025 documentdb.  All rights reserved.
 *
 * include/bitmap_utils/roaring_bitmap_adapter.h
 *
 * Roaring位图适配器接口
 *
 * 本文件定义了DocumentDB中Roaring位图功能的适配器接口，
 * 提供了高效的压缩位图操作，用于索引和查询优化。
 * Roaring位图是一种高效的压缩数据结构，特别适合处理稀疏位图。
 *
 *-------------------------------------------------------------------------
 */

 #ifndef ROARING_BITMAP_ADAPTER_H
 #define ROARING_BITMAP_ADAPTER_H

/*
 * 注册Roaring位图钩子函数
 * 此函数用于初始化和注册Roaring位图的相关函数钩子，
 * 使DocumentDB能够使用Roaring位图功能进行高效的位图操作
 */
void RegisterRoaringBitmapHooks(void);

 #endif
