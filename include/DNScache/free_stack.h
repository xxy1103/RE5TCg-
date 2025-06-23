#ifndef FREE_STACK_H
#define FREE_STACK_H

/**
 * @file free_stack.h
 * @brief 空闲条目栈管理模块
 * @author DNS Relay Team
 * @date 2025-06-23
 */

#include <stddef.h>

/**
 * @brief 空闲条目栈结构体
 */
typedef struct {
    int* stack;          // 空闲条目索引栈
    int top;             // 栈顶指针
    int capacity;        // 栈容量
} free_stack_t;

/**
 * @brief 初始化空闲条目栈
 * @param stack 栈结构体指针
 * @param capacity 栈容量
 * @return 成功返回0，失败返回-1
 */
int free_stack_init(free_stack_t* stack, int capacity);

/**
 * @brief 销毁空闲条目栈
 * @param stack 栈结构体指针
 */
void free_stack_destroy(free_stack_t* stack);

/**
 * @brief 从栈中弹出一个空闲条目索引
 * @param stack 栈结构体指针
 * @return 成功返回条目索引，栈为空返回-1
 */
int free_stack_pop(free_stack_t* stack);

/**
 * @brief 向栈中推入一个空闲条目索引
 * @param stack 栈结构体指针
 * @param index 要推入的条目索引
 * @return 成功返回0，栈满或参数错误返回-1
 */
int free_stack_push(free_stack_t* stack, int index);

/**
 * @brief 检查栈是否为空
 * @param stack 栈结构体指针
 * @return 空返回1，非空返回0，参数错误返回-1
 */
int free_stack_is_empty(free_stack_t* stack);

/**
 * @brief 检查栈是否已满
 * @param stack 栈结构体指针
 * @return 满返回1，非满返回0，参数错误返回-1
 */
int free_stack_is_full(free_stack_t* stack);

/**
 * @brief 获取栈中当前元素数量
 * @param stack 栈结构体指针
 * @return 返回栈中元素数量，参数错误返回-1
 */
int free_stack_size(free_stack_t* stack);

#endif // FREE_STACK_H
