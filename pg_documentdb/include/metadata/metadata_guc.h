/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/metadata/metadata_guc.h
 *
 * DocumentDB扩展的GUC（Grand Unified Configuration）变量声明。
 * 本文件定义了DocumentDB扩展使用的全局配置变量，包括集合ID、
 * 索引ID等自增ID的管理。
 *
 *-------------------------------------------------------------------------
 */
#ifndef METADATA_GUC_H
#define METADATA_GUC_H

#define NEXT_COLLECTION_ID_UNSET 0  /* 未设置的下一个集合ID常量值 */
extern int NextCollectionId;  /* 下一个集合ID - 全局自增集合ID计数器 */

#define NEXT_COLLECTION_INDEX_ID_UNSET 0  /* 未设置的下一个集合索引ID常量值 */
extern int NextCollectionIndexId;  /* 下一个集合索引ID - 全局自增索引ID计数器 */

#endif
