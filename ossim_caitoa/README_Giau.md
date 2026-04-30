# ossim — Scheduling & Queue Management Implementation

**Course:** CO2018 — Operating Systems  
**Module:** Scheduler Core (`sched.c`) & Queue Utilities (`queue.c`)

---

## 📖 Table of Contents
1. [Kiến trúc hệ thống MLQ](#1-kiến-trúc-hệ-thống-mlq)
2. [Chi tiết Queue Management (`queue.c`)](#2-chi-tiết-queue-management-queuec)
3. [Chi tiết Scheduler Core (`sched.c`)](#3-chi-tiết-scheduler-core-schedc)
4. [Cơ chế Đồng bộ & An toàn](#4-cơ-chế-đồng-bộ--an-toàn)

---

## 1. Kiến trúc hệ thống MLQ

Hệ thống sử dụng giải thuật **Multi-Level Queue (MLQ)** để quản lý tiến trình dựa trên độ ưu tiên. Mỗi mức ưu tiên có một hàng đợi riêng và một định mức thời gian thực thi (slot) để tránh tình trạng độc chiếm CPU.

### Cấu trúc dữ liệu chính:
| Thành phần | Kiểu dữ liệu | Vai trò |
| :--- | :--- | :--- |
| `mlq_ready_queue` | `struct queue_t[MAX_PRIO]` | Mảng 140 hàng đợi tương ứng với 140 mức ưu tiên (0-139). |
| `slot` | `int[MAX_PRIO]` | Mảng lưu số lượng slot thực thi còn lại cho mỗi mức ưu tiên. |
| `running_list` | `struct queue_t` | Danh sách các tiến trình đang trong trạng thái thực thi. |
| `queue_lock` | `pthread_mutex_t` | Khóa đảm bảo an toàn đa luồng khi truy cập tài nguyên chung. |

---

## 2. Chi tiết Queue Management (`queue.c`)

Các hàm bổ trợ để thao tác trực tiếp trên cấu trúc dữ liệu hàng đợi:

*   **`enqueue`**: Thêm một tiến trình vào cuối mảng `proc` của hàng đợi.
*   **`dequeue`**: Lấy tiến trình ở đầu hàng đợi (`index 0`) và thực hiện **Array Shifting** để dịch chuyển các phần tử phía sau lên trước.
*   **`empty`**: Kiểm tra xem hàng đợi có trống hay không.
*   **`purgequeue`**: Tìm và xóa một tiến trình cụ thể khỏi bất kỳ vị trí nào trong hàng đợi, sau đó dồn mảng lại.

---

## 3. Chi tiết Scheduler Core (`sched.c`)

### 3.1 Khởi tạo (`init_scheduler`)
Hàm thực hiện xóa sạch các hàng đợi và thiết lập giá trị slot ban đầu cho mỗi mức ưu tiên theo quy tắc: `slot[i] = MAX_PRIO - i`.

### 3.2 Lấy tiến trình (`get_mlq_proc`)
Đây là logic điều phối cốt lõi của hệ thống:
1.  **Duyệt ưu tiên**: Tìm từ hàng đợi mức 0 đến `MAX_PRIO - 1`.
2.  **Kiểm tra Slot**: Lấy tiến trình nếu hàng đợi không trống và `slot[i] > 0`.
3.  **Cập nhật**: Giảm `slot[i]` sau khi lấy tiến trình và đưa tiến trình vào `running_list`.
4.  **Reset Slot**: Nếu tất cả hàng đợi còn tiến trình nhưng đều hết slot, hệ thống sẽ reset lại mảng `slot` về giá trị ban đầu để bắt đầu chu kỳ mới.

### 3.3 Điều phối trạng thái (`put_mlq_proc` & `add_mlq_proc`)
*   **`add_mlq_proc`**: Nạp một tiến trình mới vào hàng đợi sẵn sàng dựa trên `prio` của nó.
*   **`put_mlq_proc`**: Khi tiến trình quay lại trạng thái sẵn sàng, hàm sẽ `purgequeue` khỏi `running_list` trước khi đưa về `mlq_ready_queue`.

---

## 4. Cơ chế Đồng bộ & An toàn

Vì các hàm lập lịch được gọi bởi nhiều CPU ảo đồng thời, toàn bộ các vùng tới hạn (Critical Section) trong `sched.c` đều được bảo vệ bởi **Mutex**:
```c
pthread_mutex_lock(&queue_lock);
/* Thực hiện thao tác dequeue/enqueue/purge */
pthread_mutex_unlock(&queue_lock);