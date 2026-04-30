# ossim — Virtual Memory Implementation

**Course:** CO2018 — Operating Systems  
**Module:** Paging-based Memory Management (`mm-vm.c`)

---

## Table of Contents

1. [Kiến trúc tổng quan](#1-kiến-trúc-tổng-quan)
2. [Chi tiết mm-vm.c](#2-chi-tiết-các-hàm-trong-mm-vmc)
   - [get_vma_by_num](#21-hàm-get_vma_by_num)
   - [__mm_swap_page](#22-hàm-__mm_swap_page)
   - [get_vm_area_node_at_brk](#23-hàm-get_vm_area_node_at_brk)
   - [validate_overlap_vm_area](#24-hàm-validate_overlap_vm_area)
   - [inc_vma_limit](#25-hàm-inc_vma_limit)
3. [Luồng gọi hàm tổng quát](#3-luồng-gọi-hàm-tổng-quát)
4. [Cơ chế Đồng bộ & An toàn](#4-cơ-chế-đồng-bộ--an-toàn)

---

## 1. Kiến trúc tổng quan

`mm-vm.c` đóng vai trò là tầng Virtual Memory trung gian, kết nối giữa thư viện cấp phát bộ nhớ (`libmem.c`) và tầng điều khiển phần cứng và bảng trang (`mm.c`, `mm-memphy.c`)

**Cấu trúc dữ liệu chính:**
*   `struct vm_area_struct` (VMA): Quản lý ranh giới các phân vùng bộ nhớ liên tục
*   `struct vm_rg_struct` (Region): Quản lý chi tiết các vùng đã cấp phát và vùng trống (`vm_freerg_list`) bên trong từng VMA.


Nhiệm vụ chính: xác thực sự hợp lệ của địa chỉ, cấp phát thêm không gian ảo khi process yêu cầu (nâng ranh giới `sbrk`), và điều phối quá trình swap trang.

---

## 2. Chi tiết mm-vm.c

### 2.1. Hàm `get_vma_by_num`

Tìm kiếm và lấy ra cấu trúc quản lý vùng nhớ ảo (`vm_area_struct`) cụ thể dựa trên số định danh ID phân vùng (`vmaid`).

| Tham số | Kiểu dữ liệu | Ý nghĩa |
|---|---|---|
| `mm` | `struct mm_struct *` | Con trỏ tới bộ quản lý bộ nhớ của process. |
| `vmaid` | `int` | ID của phân vùng ảo cần truy xuất. |

**Trả về:** Con trỏ `struct vm_area_struct *` trỏ tới phân vùng nếu tìm thấy, hoặc `NULL` nếu thất bại.  
**Cách triển khai:** Duyệt qua danh sách liên kết các VMA (`mm->mmap`) bằng vòng lặp để đối chiếu `vm_id` với `vmaid` yêu cầu.

---

### 2.2. Hàm `__mm_swap_page`

Thực hiện điều phối việc swap dữ liệu của một frame từ RAM sang thiết bị nhớ hoán đổi (SWAP) để giải phóng RAM.

| Tham số | Kiểu dữ liệu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process đang thực thi. |
| `vicfpn` | `addr_t` | Số hiệu Victim Frame trên RAM. |
| `swpfpn` | `addr_t` | Số hiệu Frame trống trên ổ SWAP đích. |

**Trả về:** `0` nếu thành công.  
**Cách triển khai:** Đóng vai trò là hàm bao (wrapper) ở tầng Virtual Memory, nó gọi trực tiếp hàm cấp thấp `__swap_cp_page` của `mm.c` để thực thi sao chép vật lý giữa thiết bị `mram` và `active_mswp`.

---

### 2.3. Hàm `get_vm_area_node_at_brk`

Khởi tạo một nút Region mới ngay tại vị trí biên `sbrk` hiện tại của một VMA cụ thể.

| Tham số | Kiểu dữ liệu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process yêu cầu. |
| `vmaid` | `int` | ID của VMA. |
| `size` | `int` | Kích thước phân vùng muốn lấy. |

**Trả về:** Con trỏ `struct vm_rg_struct *` chứa vùng được tạo mới.  
**Cách triển khai:** Gọi `get_vma_by_num` lấy VMA. Cấp phát động một `vm_rg_struct`, gán `rg_start` bằng `sbrk` của VMA hiện tại và `rg_end` bằng `sbrk + size`.

---

### 2.4. Hàm `validate_overlap_vm_area`

Kiểm tra xem vùng địa chỉ không gian ảo dự kiến cấp phát có bị overlap với bất kỳ vùng ảo nào đang có sẵn trong process hay không.

| Tham số | Kiểu dữ liệu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process yêu cầu kiểm tra. |
| `vmaid` | `int` | ID phân vùng dự kiến. |
| `vmastart` | `addr_t` | Địa chỉ ảo bắt đầu. |
| `vmaend` | `addr_t` | Địa chỉ ảo kết thúc. |

**Trả về:** `0` nếu vùng nhớ hợp lệ (không chồng lấn), `-1` nếu phát hiện đụng độ hoặc tham số sai (`vmastart >= vmaend`).  
**Cách triển khai:** Sử dụng vòng lặp để duyệt qua toàn bộ VMA có trong `caller->krnl->mm->mmap`. Đối với các vùng khác với `vmaid` hiện tại, hàm dùng Macro `OVERLAP` (trong `mm.h`) để xác thực chéo.

---

### 2.5. Hàm `inc_vma_limit`

Tăng ranh giới không gian khả dụng của một VMA (tăng `sbrk`) để tạo thêm vùng nhớ ảo khi các vùng trống hiện tại đã cạn kiệt.

| Tham số | Kiểu dữ liệu | Ý nghĩa |
|---|---|---|
| `caller` | `struct pcb_t *` | Process gọi cấp phát. |
| `vmaid` | `int` | ID VMA cần mở rộng. |
| `inc_sz` | `addr_t` | Kích thước dung lượng cần cơi nới thêm. |

**Trả về:** `0` nếu mở rộng thành công, `-1` nếu vùng nhớ bị chồng chéo hoặc thất bại ở bước ánh xạ RAM.  
**Lưu ý:** Cần đảm bảo vùng nhớ dự kiến không đụng độ thông qua `validate_overlap_vm_area`. Tiếp theo, cập nhật giá trị `sbrk` mới và liên kết với việc ánh xạ vùng ảo này tới Frame vật lý thông qua `vm_map_ram`.

---

## 3. Luồng gọi hàm tổng quát

**Luồng mở rộng giới hạn VMA:**
*   Khi `libmem.c` gọi hàm `__alloc` hoặc `__kmalloc` mà không tìm thấy region trống đủ lớn, nó sẽ gọi hệ thống qua `_syscall(17, SYSMEM_INC_OP)`.
*   Hàm `sys_memmap` (trong `sys_mem.c`) tiếp nhận ngắt, phân tích cờ `SYSMEM_INC_OP` và chuyển tiếp đến hàm `inc_vma_limit` của `mm-vm.c`.
*   Tại đây, `inc_vma_limit` sẽ kiểm tra tính hợp lệ bằng `validate_overlap_vm_area` trước khi chấp nhận cung cấp thêm giới hạn bộ nhớ ảo.

**Luồng Page Swapping:**
*   Quá trình đọc/ghi bộ nhớ (`pg_getval`, `pg_setval`) thông qua hàm `pg_getpage` (`libmem.c`) nếu phát hiện Page Fault và nhận thấy hệ thống hết Frame RAM trống, nó sẽ đi tìm nạn nhân (victim page).
*   Sau đó nó kích hoạt lời gọi hệ thống `_syscall(17, SYSMEM_SWP_OP)`.
*   Handler của system call này gọi trực tiếp hàm `__mm_swap_page` trong `mm-vm.c`.
*   Hàm này tiếp tục gọi hàm vật lý `__swap_cp_page` (`mm.c`) để di chuyển dữ liệu thực sự giữa RAM và SWAP.
