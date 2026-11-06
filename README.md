# Group Chat (TCP, C)

Chat nhóm qua TCP: một server, nhiều client (tối đa 10) trong cùng mạng wifi/LAN.

## Build

```bash
gcc -std=c11 -Wall -Wextra -pthread server.c -o server
gcc -std=c11 -Wall -Wextra -pthread client.c -o client
```

> Chạy trên Linux/macOS/WSL (dùng API POSIX), không chạy trực tiếp trên CMD/PowerShell.

## Chạy server

```bash
./server <PORT>
# ví dụ
./server 8080
```

Server nghe trên `0.0.0.0:<PORT>`. Giữ cửa sổ này chạy.

### Server chạy trong WSL2 (Windows)

WSL2 nằm sau NAT nên máy khác không kết nối thẳng được. Cần chuyển tiếp cổng từ Windows vào WSL.

**1. Lấy IP của WSL** (trong Ubuntu):

```bash
hostname -I
```

**2. Mở PowerShell bằng quyền Admin**, thay `<WSL_IP>` bằng IP vừa lấy:

```powershell
netsh interface portproxy add v4tov4 listenport=8080 listenaddress=0.0.0.0 connectport=8080 connectaddress=<WSL_IP>
New-NetFirewallRule -DisplayName "WSL Group Chat 8080" -Direction Inbound -Protocol TCP -LocalPort 8080 -Action Allow
```

**3. Lấy IP wifi của Windows:** chạy `ipconfig`, tìm `IPv4 Address` ở mục *Wireless LAN adapter Wi-Fi* (dạng `192.168.x.x`). Đây là IP mà client sẽ dùng.

> IP của WSL đổi sau mỗi lần restart máy hoặc `wsl --shutdown`. Khi đó xóa rule cũ rồi tạo lại:
>
> ```powershell
> netsh interface portproxy delete v4tov4 listenport=8080 listenaddress=0.0.0.0
> ```
>
> Kiểm tra rule hiện có: `netsh interface portproxy show all`

### Server chạy trên Linux thường

Chỉ cần mở cổng nếu có firewall (ví dụ `sudo ufw allow 8080/tcp`) và dùng `hostname -I` để lấy IP.

## Chạy client

```bash
./client <SERVER_IP> <PORT> <USERNAME>
# ví dụ
./client 192.168.1.23 8080 trung
```

- `SERVER_IP`: IP wifi của máy chạy server (với WSL2 là IP **Windows**, không phải IP WSL).
- Username: 1-31 ký tự, không có xuống dòng.
- Gõ tin nhắn rồi Enter để gửi, `/quit` để thoát.
- Máy chạy server cũng có thể tự kết nối bằng `./client 127.0.0.1 8080 ten`.

## Kiểm tra nhanh kết nối

```bash
nc -vz <SERVER_IP> <PORT>                      # Linux/macOS
Test-NetConnection <SERVER_IP> -Port <PORT>    # Windows PowerShell
```

## Xử lý lỗi

| Lỗi | Nguyên nhân thường gặp |
|---|---|
| `Connection timed out` | Firewall chặn, chưa tạo portproxy, hoặc wifi bật client isolation (thử hotspot điện thoại) |
| `Connection refused` | Server chưa chạy, sai cổng, hoặc portproxy trỏ sai IP WSL (IP đã đổi) |
| `Invalid IPv4 address` | Gõ sai định dạng IP |
| `Server full` | Đã đủ 10 người |
