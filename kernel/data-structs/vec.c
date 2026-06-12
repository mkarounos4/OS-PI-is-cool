#include "data-structs/vec.h"
#include "memory/malloc.h"
#include "uart/uart.h"
#include "traps/traps.h"

Vec vec_new(size_t initial_capacity, ptr_dtor_fn ele_dtor_fn) {
	ptr_t* data = malloc(sizeof(void*) * initial_capacity);
	if (data == NULL) {
		fatal_exception("Failed to allocate memory.");
	}
	return (Vec) {
		.data = data,
		.length = 0,
		.capacity = initial_capacity,
		.ele_dtor_fn = ele_dtor_fn
	};
}

ptr_t vec_get(Vec* self, size_t index) {
	if (index >= vec_len(self)) {
		fatal_exception("Get index must be less than length.");
	}

	return self->data[index];
}

void vec_set(Vec* self, size_t index, ptr_t new_ele) {
	if (index >= vec_len(self)) {
		fatal_exception("Index out of bounds.");
	}

	ptr_t old_data = self->data[index];
	if (self->ele_dtor_fn != NULL) {
		self->ele_dtor_fn(old_data);
	}
	self->data[index] = new_ele;
}

void vec_push_back(Vec* self, ptr_t new_ele) {
	if (vec_len(self) >= vec_capacity(self)) {
		vec_resize(self, vec_capacity(self) == 0 ? 1 : vec_capacity(self) * 2);
	}

	self->data[vec_len(self)] = new_ele;
	vec_len(self)++;
}

bool vec_pop_back(Vec* self, ptr_t *deleted_elem) {
	if (vec_is_empty(self)) {
        if (deleted_elem != NULL) {
            *deleted_elem = NULL;
        }
		return false;
	}
	
	vec_len(self)--;
    if (deleted_elem != NULL) {
        *deleted_elem = self->data[vec_len(self)];
    }
	return true;
}

void vec_insert(Vec* self, size_t index, ptr_t new_ele) {
	if (index == vec_len(self)) {
		vec_push_back(self, new_ele);
		return;
	}

	if (index > vec_len(self)) {
		fatal_exception("Insertion index out of bounds.");
	}

	if (vec_len(self) == vec_capacity(self)) {
		vec_resize(self, vec_capacity(self) == 0 ? 1 : vec_capacity(self) * 2);
	}
	
	ptr_t temp = self->data[index];
	self->data[index] = new_ele;
	for (size_t i = index+1; i < vec_len(self); i++) {
		ptr_t temp2 = self->data[i];
		self->data[i] = temp;
		temp = temp2;
	}
	self->data[vec_len(self)] = temp;
	vec_len(self)++;
}

void vec_erase(Vec* self, size_t index) {
	if (index >= vec_len(self)) {
		fatal_exception("Erase index out of bounds.");
	}

	if (self->ele_dtor_fn != NULL) {
		self->ele_dtor_fn(self->data[index]);
	}
	for (size_t i = index; i < vec_len(self)-1; i++) {
		self->data[i] = self->data[i+1];
	}
	vec_len(self)--;
}

void vec_resize(Vec* self, size_t new_capacity) {
	if (new_capacity < vec_len(self)) {
		fatal_exception("Resize capacity must be at least length.");
	}

	ptr_t* old_data = self->data;
	self->data = malloc(sizeof(ptr_t) * new_capacity);
	if (self->data == NULL) {
		fatal_exception("Failed to allocate memory.");
	}

	for (size_t i = 0; i < vec_len(self); i++) {
		self->data[i] = old_data[i]; 
	}
	free(old_data);
	vec_capacity(self) = new_capacity;
}

void vec_clear(Vec* self) {
	if (self->ele_dtor_fn != NULL) { 
		for (size_t i = 0; i < vec_len(self); i++) {
			self->ele_dtor_fn(self->data[i]);
		}
	}
	vec_len(self) = 0;
}

void vec_destroy(Vec* self) {
	vec_clear(self);
	free(self->data);
	self->data = NULL;
	vec_capacity(self) = 0;
	vec_len(self) = 0;
}
