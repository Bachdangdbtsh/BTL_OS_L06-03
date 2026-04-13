// Cap nhat du lieu moi nhat:
git checkout main
git pull origin main

// Chuyen sang 1 trong 3 nhanh phu trach cua rieng minh
git checkout -b feature/scheduler
git checkout -b feature/synchronization
git checkout -b feature/memory_management

// Luu lai cong viec sau khi hoan thanh
git add .
git commit -m <"Thong diep muon nhan gui">
git push origin scheduler
git push origin synchronization
git push origin memory_management
