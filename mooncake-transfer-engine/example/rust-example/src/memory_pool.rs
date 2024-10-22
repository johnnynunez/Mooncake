use std::alloc::{alloc, dealloc, Layout};
use std::ptr::NonNull;

pub struct MemoryPool {
    ptr: NonNull<u8>,
    size: usize,
}

impl MemoryPool {
    pub fn new(size: usize) -> Self {
        let layout = Layout::from_size_align(size, 1).unwrap();
        let ptr = unsafe { alloc(layout) };
        MemoryPool {
            ptr: NonNull::new(ptr).expect("Failed to allocate memory"),
            size,
        }
    }

    pub fn offset(&self, offset: isize) -> *mut u8 {
        unsafe { self.ptr.as_ptr().offset(offset) }
    }
}

impl Drop for MemoryPool {
    fn drop(&mut self) {
        let layout = Layout::from_size_align(self.size, 1).unwrap();
        unsafe {
            dealloc(self.ptr.as_ptr(), layout);
        }
    }
}

unsafe impl Send for MemoryPool {}
unsafe impl Sync for MemoryPool {}
