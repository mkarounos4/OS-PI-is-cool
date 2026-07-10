#pragma once

#include <stdbool.h>
#include "lib/syscall.h"

typedef void* ptr_t;
typedef void(*ptr_dtor_fn)(ptr_t);

typedef struct vec_st {
  ptr_t* data;
  size_t length;
  size_t capacity;
  ptr_dtor_fn ele_dtor_fn;
} Vec;

Vec vec_new(size_t initial_capacity, ptr_dtor_fn ele_dtor_fn);

#define vec_capacity(vec) ((vec)->capacity)

#define vec_len(vec) ((vec)->length)

#define vec_is_empty(vec) (vec_len((vec)) == 0)


ptr_t vec_get(Vec* self, size_t index);

void vec_set(Vec* self, size_t index, ptr_t new_ele);

void vec_push_back(Vec* self, ptr_t new_ele);

bool vec_pop_back(Vec* self, ptr_t *deleted_elem);

void vec_insert(Vec* self, size_t index, ptr_t new_ele);

void vec_erase(Vec* self, size_t index);

void vec_resize(Vec* self, size_t new_capacity);

void vec_clear(Vec* self);

void vec_destroy(Vec* self);
