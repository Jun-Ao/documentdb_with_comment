/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/metadata/relation_utils.h
 *
 * 关系对象实用工具函数。
 * 本文件定义了操作类似关系对象的实用工具函数，主要用于处理
 * PostgreSQL表、序列等数据库对象的操作。
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>

#ifndef RELATION_UTILS_H
#define RELATION_UTILS_H

Datum SequenceGetNextValAsUser(Oid sequenceId, Oid userId);  /* 以指定用户身份获取序列的下一个值 - 序列值获取 */

#endif
