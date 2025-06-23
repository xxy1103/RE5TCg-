#include "DNScache/free_stack.h"
#include "debug/debug.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief 初始化空闲条目栈
 */
int free_stack_init(free_stack_t* stack, int capacity) {
    if (!stack || capacity <= 0) {
        log_error("空闲栈初始化参数错误");
        return -1;
    }
    
    stack->stack = (int*)malloc(capacity * sizeof(int));
    if (!stack->stack) {
        log_error("空闲栈内存分配失败，容量: %d", capacity);
        return -1;
    }
    
    stack->capacity = capacity;
    stack->top = capacity - 1;  // 栈顶指针，初始时栈满
    
    // 填充空闲栈（索引从0到capacity-1）
    for (int i = 0; i < capacity; i++) {
        stack->stack[i] = i;
    }
    
    log_debug("空闲栈初始化完成，容量: %d", capacity);
    return 0;
}

/**
 * @brief 销毁空闲条目栈
 */
void free_stack_destroy(free_stack_t* stack) {
    if (!stack) return;
    
    if (stack->stack) {
        free(stack->stack);
        stack->stack = NULL;
    }
    
    stack->capacity = 0;
    stack->top = -1;
    
    log_debug("空闲栈已销毁");
}

/**
 * @brief 从栈中弹出一个空闲条目索引
 */
int free_stack_pop(free_stack_t* stack) {
    if (!stack || !stack->stack) {
        log_error("空闲栈pop操作参数错误");
        return -1;
    }
    
    if (stack->top < 0) {
        log_debug("空闲栈为空，无法pop");
        return -1;
    }
    
    int index = stack->stack[stack->top];
    stack->top--;
    
    log_debug("从空闲栈弹出索引: %d, 剩余: %d", index, stack->top + 1);
    return index;
}

/**
 * @brief 向栈中推入一个空闲条目索引
 */
int free_stack_push(free_stack_t* stack, int index) {
    if (!stack || !stack->stack) {
        log_error("空闲栈push操作参数错误");
        return -1;
    }
    
    if (index < 0 || index >= stack->capacity) {
        log_error("空闲栈push索引超出范围: %d", index);
        return -1;
    }
    
    if (stack->top >= stack->capacity - 1) {
        log_error("空闲栈已满，无法push索引: %d", index);
        return -1;
    }
    
    stack->top++;
    stack->stack[stack->top] = index;
    
    log_debug("向空闲栈推入索引: %d, 当前数量: %d", index, stack->top + 1);
    return 0;
}

/**
 * @brief 检查栈是否为空
 */
int free_stack_is_empty(free_stack_t* stack) {
    if (!stack) return -1;
    return (stack->top < 0) ? 1 : 0;
}

/**
 * @brief 检查栈是否已满
 */
int free_stack_is_full(free_stack_t* stack) {
    if (!stack) return -1;
    return (stack->top >= stack->capacity - 1) ? 1 : 0;
}

/**
 * @brief 获取栈中当前元素数量
 */
int free_stack_size(free_stack_t* stack) {
    if (!stack) return -1;
    return stack->top + 1;
}
