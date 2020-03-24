#include "utils/circularbuffer.h"
#include <mach/mach_init.h>
#include <mach/vm_map.h>
#include <mach/vm_page_size.h>

void *CircularBufferBase::Allocate(size_t *size) {
  void *buffer = nullptr;

  // round up to page size
  *size = (*size + vm_page_size - 1) & ~(vm_page_size - 1);

  kern_return_t result = vm_allocate(mach_task_self(), (vm_address_t *)&buffer,
                                     *size * 2, VM_FLAGS_ANYWHERE);
  assert(result == KERN_SUCCESS);

  uint8_t *mirror = (uint8_t *)buffer + *size;
  result = vm_deallocate(mach_task_self(), (vm_address_t)mirror, *size);
  assert(result == KERN_SUCCESS);

  vm_prot_t cur_protection, max_protection;
  result = vm_remap(mach_task_self(), (vm_address_t *)&mirror, *size, 0, 0,
                    mach_task_self(), (vm_address_t)buffer, 0, &cur_protection,
                    &max_protection, VM_INHERIT_COPY);

  assert(result == KERN_SUCCESS);
  return buffer;
}

void CircularBufferBase::Deallocate(void *buffer, size_t size) {
  kern_return_t result =
      vm_deallocate(mach_task_self(), (vm_address_t)buffer, size * 2);
  assert(result == KERN_SUCCESS);
}