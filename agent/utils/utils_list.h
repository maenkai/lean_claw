#pragma once
#include "stdbool.h"
typedef void* lean_utils_list_node;
typedef void* lean_utils_list_item;

/**
 * @brief 创建一个链表节点
 *
 * @param mutex_enable
 * @return lean_utils_list_node
 */
lean_utils_list_node lean_utils_list_node_create(bool mutex_enable);

/**
 * @brief 迭代链表
 * @example
 *    lean_utils_list_foreach(node, item, {
 *       char *string = lean_utils_list_item_data_get(node,item);
 *       printf("%s\n", item);
 *       item = lean_utils_list_delete(item, item, string);
 *    }
 *    );
 */
#define lean_utils_list_foreach(node, item, _foreach_doing)                                                                            \
  {                                                                                                                                    \
    lean_utils_list_mutex_lock(node);                                                                                                  \
    for (lean_utils_list_item item = lean_utils_list_item_begin_get(node); item != NULL; item = lean_utils_list_item_next_get(item)) { \
      _foreach_doing;                                                                                                                  \
    }                                                                                                                                  \
    lean_utils_list_mutex_unlock(node);                                                                                                \
  }

/**
 * @brief 获取链表节点的开始节点
 *
 * @param node
 * @return lean_utils_list_item
 */
lean_utils_list_item lean_utils_list_item_begin_get(lean_utils_list_node node);

/**
 * @brief 获取链表节节点的下一个节点
 *
 * @param item
 * @return lean_utils_list_item
 */
lean_utils_list_item lean_utils_list_item_next_get(lean_utils_list_item item);

/**
 * @brief 从链表节点获取数据
 *
 * @param item
 * @return void*
 */
void* lean_utils_list_item_data_get(lean_utils_list_item item);

/**
 * @brief 添加一个项目
 *
 * @param node
 * @param begin_item 从该项目开始遍历
 * @param need_free 标记成free,那么当在release或者remove时会进行free
 * @param data
 */
bool lean_utils_list_item_append(lean_utils_list_node node, bool need_free, void* data);

/**
 * @brief 删除对应的数据
 *
 * @param node  链表节点
 * @param begin_item 从该项目开始遍历
 * @param data 被删除的data
 * @return lean_utils_list_item 返回上一个节点
 */
lean_utils_list_item lean_utils_list_item_remove(lean_utils_list_node node, void* data);

/**
 * @brief 节点锁
 *
 * @param node
 */
void lean_utils_list_mutex_lock(lean_utils_list_node node);
void lean_utils_list_mutex_unlock(lean_utils_list_node node);

/**
 * @brief 释放节点
 *
 * @param node
 */
void lean_utils_list_node_relase(lean_utils_list_node node);