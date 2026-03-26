// utils_list.c
#include "utils_list.h"
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// 链表项内部结构
typedef struct _lean_utils_list_item_struct {
  void*                           data;      // 数据指针
  bool                            need_free; // 是否需要释放
  struct _lean_utils_list_item_struct* next;      // 下一个节点
} lean_utils_list_item_struct;

// 链表节点内部结构
typedef struct _lean_utils_list_node_struct {
  lean_utils_list_item_struct* head;         // 头指针
  lean_utils_list_item_struct* tail;         // 尾指针
  SemaphoreHandle_t       mutex;        // FreeRTOS 互斥锁
  bool                    mutex_enable; // 是否启用锁
} lean_utils_list_node_struct;

// 创建链表节点
lean_utils_list_node lean_utils_list_node_create(bool mutex_enable) {
  lean_utils_list_node_struct* node = (lean_utils_list_node_struct*)malloc(sizeof(lean_utils_list_node_struct));
  if (node == NULL) {
    return NULL;
  }

  node->head         = NULL;
  node->tail         = NULL;
  node->mutex_enable = mutex_enable;

  if (mutex_enable) {
    node->mutex = xSemaphoreCreateRecursiveMutex();
    if (node->mutex == NULL) {
      free(node);
      return NULL;
    }
  } else {
    node->mutex = NULL;
  }

  return (lean_utils_list_node)node;
}

// 获取链表开始节点
lean_utils_list_item lean_utils_list_item_begin_get(lean_utils_list_node node) {
  if (node == NULL) {
    return NULL;
  }
  lean_utils_list_node_struct* list_node = (lean_utils_list_node_struct*)node;
  return (lean_utils_list_item)list_node->head;
}

// 获取下一个节点
lean_utils_list_item lean_utils_list_item_next_get(lean_utils_list_item item) {
  if (item == NULL) {
    return NULL;
  }
  lean_utils_list_item_struct* list_item = (lean_utils_list_item_struct*)item;
  return (lean_utils_list_item)list_item->next;
}

// 获取节点数据
void* lean_utils_list_item_data_get(lean_utils_list_item item) {
  if (item == NULL) {
    return NULL;
  }
  lean_utils_list_item_struct* list_item = (lean_utils_list_item_struct*)item;
  return list_item->data;
}

// 添加项目
bool lean_utils_list_item_append(lean_utils_list_node node, bool need_free, void* data) {
  if (node == NULL || data == NULL) {
    return false;
  }

  lean_utils_list_node_struct* list_node = (lean_utils_list_node_struct*)node;
  lean_utils_list_item_struct* new_item  = (lean_utils_list_item_struct*)malloc(sizeof(lean_utils_list_item_struct));
  if (new_item == NULL) {
    return false;
  }

  new_item->data      = data;
  new_item->need_free = need_free;
  new_item->next      = NULL;

  lean_utils_list_mutex_lock(node);

  if (list_node->head == NULL) {
    list_node->head = new_item;
    list_node->tail = new_item;
  } else {
    list_node->tail->next = new_item;
    list_node->tail       = new_item;
  }

  lean_utils_list_mutex_unlock(node);
  return true;
}

// 删除项目（返回前驱节点）
lean_utils_list_item lean_utils_list_item_remove(lean_utils_list_node node, void* data) {
  if (node == NULL || data == NULL) {
    return NULL;
  }

  lean_utils_list_node_struct* list_node = (lean_utils_list_node_struct*)node;
  lean_utils_list_item_struct* current   = list_node->head;
  lean_utils_list_item_struct* prev      = NULL;

  lean_utils_list_mutex_lock(node);

  while (current != NULL) {
    if (current->data == data) {
      // 保存前驱节点作为返回值
      lean_utils_list_item prev_item = (lean_utils_list_item)prev;

      if (prev == NULL) {
        list_node->head = current->next;
      } else {
        prev->next = current->next;
      }

      if (current == list_node->tail) {
        list_node->tail = prev;
      }

      // 获取下一个节点用于继续遍历
      if (current->need_free && current->data != NULL) {
        free(current->data);
      }
      free(current);

      lean_utils_list_mutex_unlock(node);
      return prev_item; // 返回前驱节点
    }

    prev    = current;
    current = current->next;
  }

  lean_utils_list_mutex_unlock(node);
  return NULL;
}

// 节点锁
void lean_utils_list_mutex_lock(lean_utils_list_node node) {
  if (node == NULL) {
    return;
  }
  lean_utils_list_node_struct* list_node = (lean_utils_list_node_struct*)node;
  if (list_node->mutex_enable && list_node->mutex != NULL) {
    xSemaphoreTakeRecursive(list_node->mutex, portMAX_DELAY);
  }
}

// 节点解锁
void lean_utils_list_mutex_unlock(lean_utils_list_node node) {
  if (node == NULL) {
    return;
  }
  lean_utils_list_node_struct* list_node = (lean_utils_list_node_struct*)node;
  if (list_node->mutex_enable && list_node->mutex != NULL) {
    xSemaphoreGiveRecursive(list_node->mutex);
  }
}

// 释放节点
void lean_utils_list_node_relase(lean_utils_list_node node) {
  if (node == NULL) {
    return;
  }

  lean_utils_list_node_struct* list_node = (lean_utils_list_node_struct*)node;

  lean_utils_list_mutex_lock(node);

  lean_utils_list_item_struct* current = list_node->head;
  while (current != NULL) {
    lean_utils_list_item_struct* next = current->next;
    if (current->need_free && current->data != NULL) {
      free(current->data);
    }
    free(current);
    current = next;
  }

  lean_utils_list_mutex_unlock(node);

  if (list_node->mutex_enable && list_node->mutex != NULL) {
    vSemaphoreDelete(list_node->mutex);
  }

  free(list_node);
}