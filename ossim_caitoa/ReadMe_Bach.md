# ossim — Memory Management Implementation

**Course:** CO2018 — Operating Systems  
**Module:** Memory Management (`libmem.c`, `sys_mem.c`)

---

## Table of Contents

1. [Kiến trúc tổng quan](#1-kiến-trúc-tổng-quan)
2. [libmem.c — User & Kernel Memory Library](#2-libmemc--user--kernel-memory-library)
   - [Helper functions](#21-helper-functions)
   - [User memory — alloc & free](#22-user-memory--alloc--free)
   - [User memory — wrappers](#23-user-memory--wrappers)
   - [Page-level read/write](#24-page-level-readwrite)
   - [Read/Write wrappers](#25-readwrite-wrappers)
   - [Kernel memory — slab allocator](#26-kernel-memory--slab-allocator)
   - [Kernel memory — cache pool](#27-kernel-memory--cache-pool)
   - [Kernel/user boundary copy](#28-kerneluser-boundary-copy)
   - [Low-level memory access](#29-low-level-memory-access)
   - [Memory management helpers](#210-memory-management-helpers)
3. [sys_mem.c — Syscall Handler](#3-sys_memc--syscall-handler)
4. [Luồng gọi hàm tổng quát](#4-luồng-gọi-hàm-tổng-quát)

---

## 1. Kiến trúc tổng quan

```
User process instruction
        │
        ▼
  liballoc / libfree / libread / libwrite          ← wrapper layer
        │
        ▼
  __alloc / __free / __read / __write              ← core logic
        │                    │
        │                    ▼
        │            pg_getval / pg_setval          ← virtual → physical
        │                    │
        │                    ▼
        │             pg_getpage                    ← page fault / swap handler
        │                    │
        ▼                    ▼
  _syscall(17, SYSMEM_INC_OP)          _syscall(17, SYSMEM_IO_READ/WRITE)
        │                                           │
        └─────────────────┬─────────────────────────┘
                          ▼
                  __sys_memmap                      ← kernel syscall dispatcher
                          │
              ┌───────────┼───────────┐
              ▼           ▼           ▼
        inc_vma_limit  MEMPHY_read  __mm_swap_page
```

**Hai không gian bộ nhớ hoàn toàn độc lập:**

| Không gian | Page table | Quản lý bởi |
|---|---|---|
| User space | `caller->mm->pgd` | `__alloc`, `pg_getpage`, `libmem.c` |
| Kernel space | `krnl->krnl_pgd` (32-bit) hoặc `krnl->krnl_pt` (64-bit) | `__kmalloc`, `__read/write_kernel_mem` |

---

## 2. libmem.c — User & Kernel Memory Library

### 2.1 Helper functions

---

#### `enlist_vm_freerg_list`

```c
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
```

Thêm một vùng nhớ vừa được giải phóng vào đầu danh sách `vm_freerg_list` của VMA đầu tiên (`mm->mmap`). Danh sách này là free-list dùng cho các lần `__alloc` tiếp theo.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `mm` | `struct mm_struct *` | Memory management struct của process |
| `rg_elmt` | `struct vm_rg_struct *` | Node vùng nhớ cần đưa vào free-list |

**Trả về:** `0` nếu thành công, `-1` nếu `rg_start >= rg_end` (vùng không hợp lệ).

**Lưu ý:** Không kiểm tra trùng lặp — caller có trách nhiệm đảm bảo không enlist cùng vùng hai lần.

---

#### `get_symrg_byid`

```c
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
```

Trả về con trỏ đến entry `rgid` trong bảng symbol region (`mm->symrgtbl`). Bảng này lưu ánh xạ giữa chỉ số thanh ghi (`reg_index`) và vùng nhớ ảo đã cấp phát.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `mm` | `struct mm_struct *` | Memory management struct |
| `rgid` | `int` | Chỉ số vùng nhớ (0 đến `PAGING_MAX_SYMTBL_SZ - 1`) |

**Trả về:** Con trỏ `vm_rg_struct` tương ứng, hoặc `NULL` nếu `rgid` ngoài giới hạn.

---

### 2.2 User memory — alloc & free

---

#### `__alloc`

```c
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
```

Hàm cấp phát vùng nhớ ảo cốt lõi cho user process. Thực hiện theo hai đường:

**Đường 1 — Reuse freerg_list:** Nếu `get_free_vmrg_area` tìm được vùng trống đủ lớn trong `vm_freerg_list`, ghi địa chỉ vào `symrgtbl[rgid]` và trả về ngay.

**Đường 2 — Mở rộng VMA:** Nếu free-list không đủ, gọi `_syscall(17, SYSMEM_INC_OP)` để tăng giới hạn VMA, sau đó dùng `sbrk` cũ làm địa chỉ bắt đầu.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process đang thực thi lệnh alloc |
| `vmaid` | `int` | ID của VM area (thường là `0` cho heap) |
| `rgid` | `int` | Chỉ số thanh ghi lưu địa chỉ trả về |
| `size` | `addr_t` | Số byte cần cấp phát |
| `alloc_addr` | `addr_t *` | Output: địa chỉ ảo bắt đầu của vùng được cấp phát |

**Trả về:** `0` nếu thành công, `-1` nếu thất bại (VMA NULL, rgid không hợp lệ, rgid đã được dùng, syscall lỗi).

**Cơ chế bảo vệ:** Dùng `pthread_mutex_lock(&mmvm_lock)` để tránh race condition khi nhiều process cùng alloc.

---

#### `__free`

```c
int __free(struct pcb_t *caller, int vmaid, int rgid)
```

Giải phóng vùng nhớ tương ứng với `rgid` bằng cách đưa nó vào `vm_freerg_list`. **Không giải phóng frame vật lý** — frame chỉ được trả về khi process kết thúc qua `free_pcb_memph`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process thực thi lệnh free |
| `vmaid` | `int` | ID của VM area (không dùng trực tiếp, dùng qua `get_symrg_byid`) |
| `rgid` | `int` | Chỉ số thanh ghi của vùng nhớ cần giải phóng |

**Trả về:** `0` nếu thành công, `-1` nếu `rgid` không hợp lệ, vùng đã được free trước đó (rg_start == rg_end == 0), hoặc `malloc` thất bại.

**Hành vi sau free:** Entry `symrgtbl[rgid]` bị reset về `{0, 0}` để đánh dấu "không được cấp phát". Node mới được chèn vào đầu `vm_freerg_list`.

---

### 2.3 User memory — wrappers

---

#### `liballoc`

```c
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
```

Wrapper công khai của `__alloc`, gọi với `vmaid = 0` (heap mặc định). In thông tin debug nếu `IODUMP` được định nghĩa.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `proc` | `struct pcb_t *` | Process thực thi |
| `size` | `addr_t` | Số byte cần cấp phát |
| `reg_index` | `uint32_t` | Chỉ số thanh ghi nhận địa chỉ kết quả |

**Trả về:** `0` nếu thành công, `-1` nếu thất bại.

---

#### `libfree`

```c
int libfree(struct pcb_t *proc, uint32_t reg_index)
```

Wrapper công khai của `__free`, gọi với `vmaid = 0`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `proc` | `struct pcb_t *` | Process thực thi |
| `reg_index` | `uint32_t` | Chỉ số thanh ghi của vùng nhớ cần giải phóng |

**Trả về:** `0` nếu thành công, `-1` nếu thất bại.

---

### 2.4 Page-level read/write

---

#### `pg_getpage`

```c
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
```

Đảm bảo page `pgn` đang nằm trong RAM và trả về FPN tương ứng. Nếu page đang ở swap, thực hiện toàn bộ quy trình **swap-in**:

1. Gọi `find_victim_page` tìm page FIFO lâu nhất để đuổi ra.
2. Lấy frame trống trên swap (`MEMPHY_get_freefp` trên `active_mswp`).
3. Gọi syscall `SYSMEM_SWP_OP` để copy frame victim từ RAM sang swap.
4. Gọi `pte_set_swap` cập nhật PTE của victim page (đánh dấu swapped).
5. Copy page cần thiết từ swap vào frame vừa giải phóng (`__swap_cp_page`).
6. Trả frame swap vừa dùng về free-list của swap (`MEMPHY_put_freefp`).
7. Gọi `pte_set_fpn` cập nhật PTE của target page (đánh dấu present).
8. Enlist page vào FIFO queue (`enlist_pgn_node`).

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `mm` | `struct mm_struct *` | Memory struct (không dùng trực tiếp, truyền theo convention) |
| `pgn` | `int` | Số trang ảo cần truy cập |
| `fpn` | `int *` | Output: số frame vật lý trong RAM |
| `caller` | `struct pcb_t *` | Process đang truy cập — dùng để gọi syscall |

**Trả về:** `0` nếu thành công, `-1` nếu không tìm được victim hoặc swap đầy.

---

#### `pg_getval`

```c
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
```

Đọc 1 byte tại địa chỉ ảo `addr`. Gọi `pg_getpage` để đảm bảo page có mặt trong RAM, tính địa chỉ vật lý, rồi gọi syscall `SYSMEM_IO_READ` để đọc từ `mram`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `mm` | `struct mm_struct *` | Memory struct của process |
| `addr` | `int` | Địa chỉ ảo cần đọc |
| `data` | `BYTE *` | Output: byte được đọc ra |
| `caller` | `struct pcb_t *` | Process đang đọc |

**Trả về:** `0` nếu thành công, `-1` nếu page access thất bại.

**Tính địa chỉ vật lý:** `phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + offset_trong_page`

---

#### `pg_setval`

```c
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
```

Ghi 1 byte `value` vào địa chỉ ảo `addr`. Logic tương tự `pg_getval` nhưng dùng syscall `SYSMEM_IO_WRITE`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `mm` | `struct mm_struct *` | Memory struct của process |
| `addr` | `int` | Địa chỉ ảo đích |
| `value` | `BYTE` | Byte cần ghi |
| `caller` | `struct pcb_t *` | Process đang ghi |

**Trả về:** `0` nếu thành công, `-1` nếu page access thất bại.

---

### 2.5 Read/Write wrappers

---

#### `__read`

```c
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
```

Đọc 1 byte tại `offset` trong vùng nhớ `rgid`. Kiểm tra địa chỉ hợp lệ rồi chuyển sang địa chỉ ảo tuyệt đối (`rg_start + offset`) và gọi `pg_getval`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process đang đọc |
| `vmaid` | `int` | ID của VM area |
| `rgid` | `int` | Chỉ số vùng nhớ trong `symrgtbl` |
| `offset` | `addr_t` | Byte offset trong vùng nhớ |
| `data` | `BYTE *` | Output: byte đọc ra |

**Trả về:** `0` nếu thành công, `-1` nếu vùng nhớ không hợp lệ hoặc offset vượt biên.

---

#### `libread`

```c
int libread(struct pcb_t *proc, uint32_t source, addr_t offset, uint32_t *destination)
```

Wrapper công khai của `__read`. Kết quả được ghi vào `*destination` (kiểu `uint32_t` để tương thích với thanh ghi CPU).

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `proc` | `struct pcb_t *` | Process thực thi |
| `source` | `uint32_t` | Chỉ số thanh ghi nguồn (`rgid`) |
| `offset` | `addr_t` | Byte offset trong vùng nhớ |
| `destination` | `uint32_t *` | Output: nơi nhận giá trị đọc được |

**Trả về:** `0` nếu thành công, `-1` nếu thất bại.

---

#### `__write`

```c
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
```

Ghi 1 byte `value` vào `offset` trong vùng nhớ `rgid`. Kiểm tra biên rồi gọi `pg_setval`. Dùng `mmvm_lock` để thread-safe.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process đang ghi |
| `vmaid` | `int` | ID của VM area |
| `rgid` | `int` | Chỉ số vùng nhớ đích |
| `offset` | `addr_t` | Byte offset trong vùng nhớ |
| `value` | `BYTE` | Giá trị byte cần ghi |

**Trả về:** `0` nếu thành công, `-1` nếu vùng nhớ không hợp lệ hoặc offset vượt biên.

---

#### `libwrite`

```c
int libwrite(struct pcb_t *proc, BYTE data, uint32_t destination, addr_t offset)
```

Wrapper công khai của `__write`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `proc` | `struct pcb_t *` | Process thực thi |
| `data` | `BYTE` | Byte cần ghi |
| `destination` | `uint32_t` | Chỉ số thanh ghi đích (`rgid`) |
| `offset` | `addr_t` | Byte offset trong vùng nhớ |

**Trả về:** `0` nếu thành công, `-1` nếu thất bại.

---

### 2.6 Kernel memory — slab allocator

---

#### `libkmem_malloc`

```c
int libkmem_malloc(struct pcb_t *caller, uint32_t size, uint32_t reg_index)
```

Wrapper cấp phát bộ nhớ kernel. Kiểm tra đầu vào hợp lệ rồi gọi `__kmalloc`, lưu địa chỉ kết quả vào `caller->regs[reg_index]`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process yêu cầu cấp phát kernel memory |
| `size` | `uint32_t` | Số byte cần cấp phát |
| `reg_index` | `uint32_t` | Chỉ số thanh ghi nhận địa chỉ (phải < 10) |

**Trả về:** `0` nếu thành công, `-1` nếu `caller` NULL, `size == 0`, `reg_index >= 10`, hoặc `__kmalloc` thất bại.

---

#### `__kmalloc`

```c
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
```

Cấp phát vùng nhớ trên kernel page table (`krnl->mm`). Khác với `__alloc` (dùng `caller->mm`), hàm này vận hành trên `caller->krnl->mm`. Sau khi xác định địa chỉ ảo, cấp phát frame vật lý từ `krnl->mram` và ghi PTE trực tiếp qua `pte_set_entry` + `init_pte`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process yêu cầu |
| `vmaid` | `int` | ID của kernel VM area (`-1` để dùng krnl_pgd) |
| `rgid` | `int` | Chỉ số vùng nhớ trong kernel `symrgtbl` |
| `size` | `addr_t` | Số byte cần cấp phát |
| `alloc_addr` | `addr_t *` | Output: địa chỉ ảo trong kernel space |

**Trả về:** `0` nếu thành công, `-1` nếu RAM cạn hoặc VMA không hợp lệ.

**Điểm khác biệt quan trọng so với `__alloc`:**

| | `__alloc` | `__kmalloc` |
|---|---|---|
| Page table | `caller->mm->pgd` | `krnl->krnl_pgd` / `krnl->krnl_pt` |
| symrgtbl | `caller->mm->symrgtbl` | `krnl->mm->symrgtbl` |
| Mục đích | Heap user process | Buffer kernel nội bộ |

---

### 2.7 Kernel memory — cache pool

---

#### `libkmem_cache_pool_create`

```c
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
```

Tạo một cache pool mới với slot size cố định. Cấp phát backing storage qua `__kmalloc`, khởi tạo `kcache_pool_struct` và chèn vào đầu danh sách liên kết `mm->kcpooltbl`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process yêu cầu tạo pool |
| `size` | `uint32_t` | Tổng kích thước backing memory của pool |
| `align` | `uint32_t` | Kích thước alignment của mỗi slot (phải ≤ `size`) |
| `cache_pool_id` | `uint32_t` | ID định danh pool (dùng để tìm kiếm sau) |

**Trả về:** `0` nếu thành công, `-1` nếu tham số không hợp lệ, `krnl_rgid` âm, `__kmalloc` thất bại, hoặc `malloc` struct thất bại.

**Cách tính `krnl_rgid`:** `PAGING_MAX_SYMTBL_SZ - 1 - cache_pool_id` — dành riêng các slot cuối của symrgtbl cho kernel cache, tránh đụng độ với slot của user.

---

#### `libkmem_cache_alloc`

```c
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
```

Cấp phát 1 slot từ cache pool đã tạo. Tìm pool theo `cache_pool_id` trong `kcpooltbl`, rồi gọi `__kmem_cache_alloc` để lấy địa chỉ. Kết quả lưu vào `proc->regs[reg_index]`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `proc` | `struct pcb_t *` | Process yêu cầu cấp phát slot |
| `cache_pool_id` | `uint32_t` | ID của pool cần lấy slot |
| `reg_index` | `uint32_t` | Chỉ số thanh ghi nhận địa chỉ slot |

**Trả về:** `0` nếu thành công, `-1` nếu pool không tồn tại, `reg_index` vượt giới hạn, hoặc `__kmem_cache_alloc` thất bại.

---

#### `__kmem_cache_alloc`

```c
addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
```

Logic cấp phát slot từ cache pool. Tính kích thước slot có alignment, ưu tiên dùng `vm_freerg_list`, nếu không đủ thì gọi syscall `SYSMEM_INC_OP` mở rộng VMA.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process yêu cầu |
| `vmaid` | `int` | ID VM area (`-1` cho kernel page table) |
| `rgid` | `int` | Chỉ số vùng nhớ trong `symrgtbl` |
| `cache_pool_id` | `int` | ID của pool cần tìm |
| `alloc_addr` | `addr_t *` | Output: địa chỉ ảo của slot được cấp phát |

**Trả về:** `0` nếu thành công, `-1` nếu pool NULL hoặc VMA NULL.

**Tính slot size có alignment (MM64):**
```c
slot_sz = ((slot_sz + align - 1) / align) * align;
```

---

### 2.8 Kernel/user boundary copy

---

#### `libkmem_copy_from_user`

```c
int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
```

Copy `size` byte từ user space vào kernel space, byte by byte. Thực hiện kiểm tra biên cả nguồn lẫn đích trước khi copy.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process đang thực hiện copy |
| `source` | `uint32_t` | `rgid` của vùng **user** space (nguồn dữ liệu) |
| `destination` | `uint32_t` | `rgid` của vùng **kernel** space (đích nhận dữ liệu) |
| `offset` | `uint32_t` | Byte offset bắt đầu đọc trong vùng source |
| `size` | `uint32_t` | Số byte cần copy |

**Trả về:** `0` nếu thành công, `-1` nếu vùng nhớ không tồn tại, `offset + size` vượt biên nguồn, `size` vượt biên đích, hoặc lỗi đọc/ghi từng byte.

**Luồng copy:** `__read_user_mem` (đọc từ user) → `__write_kernel_mem` (ghi vào kernel).

---

#### `libkmem_copy_to_user`

```c
int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
```

Copy `size` byte từ kernel space vào user space, byte by byte. Chiều ngược lại của `libkmem_copy_from_user`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process đang thực hiện copy |
| `source` | `uint32_t` | `rgid` của vùng **kernel** space (nguồn dữ liệu) |
| `destination` | `uint32_t` | `rgid` của vùng **user** space (đích nhận dữ liệu) |
| `offset` | `uint32_t` | Byte offset bắt đầu đọc trong vùng source |
| `size` | `uint32_t` | Số byte cần copy |

**Trả về:** `0` nếu thành công, `-1` nếu vùng nhớ không tồn tại, `offset + size` vượt biên, hoặc lỗi đọc/ghi.

**Luồng copy:** `__read_kernel_mem` (đọc từ kernel) → `__write_user_mem` (ghi vào user).

---

### 2.9 Low-level memory access

---

#### `__read_kernel_mem`

```c
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
```

Đọc 1 byte từ kernel memory region. Tra cứu PTE trong `krnl->krnl_pt` (MM64) hoặc `krnl->krnl_pgd` (32-bit), tính địa chỉ vật lý và gọi `MEMPHY_read` trực tiếp (không qua syscall).

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process thực thi |
| `vmaid` | `int` | ID VM area (`-1` cho kernel — không dùng trực tiếp trong hàm) |
| `rgid` | `int` | Chỉ số vùng nhớ trong **kernel** `symrgtbl` (`krnl->mm->symrgtbl`) |
| `offset` | `addr_t` | Byte offset trong vùng nhớ |
| `data` | `BYTE *` | Output: byte đọc ra |

**Trả về:** `0` nếu thành công, `-1` nếu region không tồn tại hoặc page not present.

**MM64 — cách tra PTE:**
```c
get_pd_from_address(virtual_addr, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);
pte = krnl->krnl_pt[pt_idx];   // flat array indexed bằng pt_idx
```

---

#### `__write_kernel_mem`

```c
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
```

Ghi 1 byte vào kernel memory region. Logic tra cứu PTE giống `__read_kernel_mem`, sau đó gọi `MEMPHY_write`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process thực thi |
| `vmaid` | `int` | ID VM area (kernel convention: `-1`) |
| `rgid` | `int` | Chỉ số vùng nhớ trong `krnl->mm->symrgtbl` |
| `offset` | `addr_t` | Byte offset trong vùng nhớ |
| `value` | `BYTE` | Byte cần ghi |

**Trả về:** `0` nếu thành công, `-1` nếu region không tồn tại hoặc page not present.

---

#### `__read_user_mem`

```c
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
```

Đọc 1 byte từ user memory region qua `caller->mm`. Kiểm tra biên rồi gọi `pg_getval` (có swap-in nếu cần). Dùng `mmvm_lock`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process thực thi |
| `vmaid` | `int` | ID VM area (user convention: `0`) |
| `rgid` | `int` | Chỉ số vùng nhớ trong `caller->mm->symrgtbl` |
| `offset` | `addr_t` | Byte offset trong vùng nhớ |
| `data` | `BYTE *` | Output: byte đọc ra |

**Trả về:** `0` nếu thành công, `-1` nếu region NULL, chưa cấp phát, offset vượt biên, hoặc `pg_getval` thất bại.

---

#### `__write_user_mem`

```c
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
```

Ghi 1 byte vào user memory region qua `caller->mm`. Kiểm tra biên rồi gọi `pg_setval`. Dùng `mmvm_lock`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process thực thi |
| `vmaid` | `int` | ID VM area (user convention: `0`) |
| `rgid` | `int` | Chỉ số vùng nhớ trong `caller->mm->symrgtbl` |
| `offset` | `addr_t` | Byte offset trong vùng nhớ |
| `value` | `BYTE` | Byte cần ghi |

**Trả về:** `0` nếu thành công, `-1` nếu region NULL, chưa cấp phát, hoặc offset vượt biên.

---

### 2.10 Memory management helpers

---

#### `free_pcb_memph`

```c
int free_pcb_memph(struct pcb_t *caller)
```

Giải phóng toàn bộ frame vật lý của một process khi process kết thúc. Duyệt qua tất cả `PAGING_MAX_PGN` entry trong `caller->mm->pgd`:
- Nếu page **present**: trả frame về `mram` free-list.
- Nếu page **swapped**: trả frame về `active_mswp` free-list.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process đang kết thúc |

**Trả về:** `0`.

**Lưu ý:** Đây là hàm duy nhất thực sự giải phóng frame vật lý. `__free` chỉ cập nhật virtual address space.

---

#### `find_victim_page`

```c
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
```

Tìm trang cần đuổi ra swap theo chính sách **FIFO**: trả về page nằm ở **cuối** danh sách `fifo_pgn` (tức là page được thêm vào lâu nhất — `enlist_pgn_node` chèn vào đầu).

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `mm` | `struct mm_struct *` | Memory struct chứa `fifo_pgn` |
| `retpgn` | `addr_t *` | Output: page number của victim |

**Trả về:** `0` nếu thành công, `-1` nếu FIFO list rỗng hoặc chỉ có đúng 1 phần tử (không đủ để evict — giữ lại để process có thể tiếp tục hoạt động).

**Lý do kiểm tra `pg->pg_next == NULL`:** Nếu chỉ có 1 page trong RAM, evict nó sẽ khiến process không còn frame nào để hoạt động, dẫn đến deadlock.

---

#### `get_free_vmrg_area`

```c
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
```

Tìm kiếm vùng trống đủ `size` byte trong `vm_freerg_list` của VMA. Nếu tìm được, cập nhật node freerg (thu hẹp hoặc xóa) và trả về vùng tìm được qua `newrg`.

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process yêu cầu |
| `vmaid` | `int` | ID VM area cần tìm |
| `size` | `int` | Số byte cần tìm |
| `newrg` | `struct vm_rg_struct *` | Output: vùng được tìm thấy (`rg_start`, `rg_end`) |

**Trả về:** `0` nếu tìm được, `-1` nếu free-list rỗng hoặc không có vùng đủ lớn.

**Hành vi khi dùng hết vùng free:** Clone node tiếp theo vào node hiện tại rồi free node tiếp theo (in-place replacement để tránh phá vỡ con trỏ đầu danh sách).

---

## 3. sys_mem.c — Syscall Handler

---

#### `__sys_memmap`

```c
int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
```

Dispatcher syscall số 17 (`memmap`). Nhận yêu cầu từ user process thông qua `_syscall`, tìm đúng PCB từ `krnl->running_list` theo `pid`, rồi điều phối sang handler tương ứng dựa trên `regs->a1` (operation code).

| Tham số | Kiểu | Ý nghĩa |
|---|---|---|
| `krnl` | `struct krnl_t *` | Kernel struct chứa running_list, mram, mswp |
| `pid` | `uint32_t` | PID của process đang gọi syscall |
| `regs` | `struct sc_regs *` | Thanh ghi syscall: `a1`=opcode, `a2`/`a3`=tham số |

**Trả về:** `0` nếu thành công, `-1` nếu không tìm thấy process với `pid` trong `running_list`.

**Tại sao phải traverse `running_list` thay vì `malloc` PCB?**

Code gốc tạo một PCB giả bằng `malloc` rỗng, khiến tất cả các hàm con nhận được struct không có dữ liệu thật. Cách đúng là duyệt `running_list->proc[]` để lấy con trỏ đến PCB thật của process đang chạy.

```c
// ❌ SAI — code gốc
struct pcb_t *caller = malloc(sizeof(struct pcb_t));  // struct rỗng, không có mm, krnl thật

// ✅ ĐÚNG — traverse running_list
for (int i = 0; i < running_list->size; i++) {
    if (running_list->proc[i]->pid == pid) {
        caller = running_list->proc[i];
        break;
    }
}
```

**Bảng operation codes (`regs->a1`):**

| Opcode | Tên hằng số | Handler được gọi | Ý nghĩa |
|---|---|---|---|
| `SYSMEM_MAP_OP` | — | `vmap_pgd_memset(caller, a2, a3)` | Memset page table range |
| `SYSMEM_INC_OP` | — | `inc_vma_limit(caller, a2, a3)` | Tăng giới hạn VMA (tương tự `sbrk`) |
| `SYSMEM_SWP_OP` | — | `__mm_swap_page(caller, a2, a3)` | Swap frame giữa RAM và SWAP |
| `SYSMEM_IO_READ` | — | `MEMPHY_read(mram, a2, &value)` | Đọc 1 byte từ địa chỉ vật lý, kết quả trả về qua `a3` |
| `SYSMEM_IO_WRITE` | — | `MEMPHY_write(mram, a2, a3)` | Ghi 1 byte vào địa chỉ vật lý |

**Tham số syscall cho từng operation:**

| Operation | `regs->a2` | `regs->a3` |
|---|---|---|
| `SYSMEM_INC_OP` | `vmaid` | `size` cần tăng |
| `SYSMEM_SWP_OP` | `vicfpn` (frame RAM cần đuổi) | `swpfpn` (frame SWAP đích) |
| `SYSMEM_IO_READ` | địa chỉ vật lý | (output) giá trị đọc được |
| `SYSMEM_IO_WRITE` | địa chỉ vật lý | byte cần ghi |

---

## 4. Luồng gọi hàm tổng quát

### Luồng `alloc`

```
liballoc(proc, size, reg_index)
  └── __alloc(caller, vmaid=0, rgid, size, &addr)
        ├── get_vma_by_num(caller->mm, 0)          # lấy VMA
        ├── get_free_vmrg_area(...)                 # tìm trong freerg_list
        │     └── [thành công] → ghi symrgtbl, return
        └── [thất bại] → _syscall(17, SYSMEM_INC_OP)
              └── __sys_memmap → inc_vma_limit(caller, vmaid, size)
```

### Luồng `write`

```
libwrite(proc, data, destination, offset)
  └── __write(caller, vmaid=0, rgid, offset, value)
        └── pg_setval(mm, rg_start+offset, value, caller)
              └── pg_getpage(mm, pgn, &fpn, caller)
                    ├── pte_get_entry(caller, pgn)   # kiểm tra present bit
                    ├── [page not present] → swap procedure:
                    │     ├── find_victim_page(mm, &vicpgn)
                    │     ├── MEMPHY_get_freefp(active_mswp, &swpfpn)
                    │     ├── _syscall(17, SYSMEM_SWP_OP)  # copy victim → swap
                    │     ├── pte_set_swap(caller, vicpgn, ...)
                    │     ├── __swap_cp_page(mswp, tgtswpfpn, mram, tgtfpn)
                    │     ├── MEMPHY_put_freefp(mswp, tgtswpfpn)
                    │     └── pte_set_fpn(caller, pgn, tgtfpn)
                    └── _syscall(17, SYSMEM_IO_WRITE)  # ghi byte vật lý
```

### Luồng `copy_from_user`

```
libkmem_copy_from_user(caller, source, destination, offset, size)
  └── [validate src_rg, dst_rg]
  └── for i in 0..size:
        ├── __read_user_mem(caller, 0, source, offset+i, &byte)
        │     └── pg_getval(caller->mm, ...) → _syscall(SYSMEM_IO_READ)
        └── __write_kernel_mem(caller, -1, destination, i, byte)
              └── krnl->krnl_pt[pt_idx] → MEMPHY_write(krnl->mram, phyaddr, byte)
```
