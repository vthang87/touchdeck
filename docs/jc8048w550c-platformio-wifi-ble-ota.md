# Tài liệu triển khai cơ bản cho JC8048W550C bằng PlatformIO

## 1. Mục tiêu

Xây dựng firmware nền tảng cho bo **JC8048W550C** sử dụng **ESP32-S3** và PlatformIO, tập trung vào ba chức năng chính:

- Kết nối Wi-Fi.
- Kết nối Bluetooth Low Energy.
- Cập nhật firmware qua OTA.

Giai đoạn này chưa triển khai giao diện LCD, cảm ứng GT911 hoặc LVGL. Mục tiêu là kiểm tra độ ổn định của kết nối và quy trình cập nhật firmware trước khi tích hợp phần giao diện Stream Deck.

---

## 2. Phạm vi MVP

Firmware cơ bản cần đáp ứng:

1. Khởi động ESP32-S3 ổn định.
2. Đọc cấu hình Wi-Fi đã lưu.
3. Tự động kết nối Wi-Fi khi bật nguồn.
4. Tạo Access Point cấu hình nếu chưa có Wi-Fi hoặc kết nối thất bại.
5. Phát Bluetooth Low Energy để Mac có thể phát hiện thiết bị.
6. Cho phép gửi và nhận dữ liệu cơ bản qua BLE GATT.
7. Hỗ trợ cập nhật firmware qua mạng nội bộ bằng OTA.
8. Lưu cấu hình trong bộ nhớ NVS.
9. Có log qua Serial để kiểm tra trạng thái.
10. Có cơ chế khởi động lại an toàn khi OTA thất bại hoặc firmware lỗi.

---

## 3. Phần cứng mục tiêu

### Bo mạch

- Model: JC8048W550C.
- MCU: ESP32-S3.
- Flash: thường là 16 MB.
- PSRAM: thường là 8 MB.
- Wi-Fi: 2.4 GHz.
- Bluetooth: BLE.
- Màn hình: 5 inch, 800 × 480.
- Cảm ứng: điện dung, thường dùng GT911.
- Cổng nạp: USB-C.
- Mua sản phẩm: [AliExpress — JC8048W550C](https://aliexpress.com/item/1005006715794302.html)

Cần xác nhận đúng phiên bản bo trước khi cấu hình PlatformIO vì một số lô sản phẩm có thể khác dung lượng Flash, PSRAM hoặc chân ngoại vi.

---

## 4. Công nghệ sử dụng

### Firmware

- PlatformIO.
- Arduino Framework cho ESP32.
- WiFi của Arduino-ESP32.
- Preferences hoặc NVS.
- NimBLE-Arduino.
- ArduinoOTA.
- WebServer hoặc AsyncWebServer cho trang cấu hình.
- LittleFS cho file cấu hình mở rộng nếu cần.

### Công cụ phát triển

- Visual Studio Code.
- PlatformIO IDE Extension.
- Serial Monitor.
- Git.
- Máy Mac dùng để build, upload và test Bluetooth.

---

## 5. Kiến trúc firmware

```text
jc8048w550-streamdeck/
├── platformio.ini
├── README.md
├── include/
│   ├── app_config.h
│   ├── board_config.h
│   └── version.h
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── app_manager.cpp
│   │   └── app_manager.h
│   ├── wifi/
│   │   ├── wifi_manager.cpp
│   │   └── wifi_manager.h
│   ├── ble/
│   │   ├── ble_manager.cpp
│   │   └── ble_manager.h
│   ├── ota/
│   │   ├── ota_manager.cpp
│   │   └── ota_manager.h
│   ├── storage/
│   │   ├── settings_store.cpp
│   │   └── settings_store.h
│   ├── portal/
│   │   ├── config_portal.cpp
│   │   └── config_portal.h
│   └── system/
│       ├── system_status.cpp
│       └── system_status.h
├── data/
│   └── config-portal/
└── docs/
    ├── ble-protocol.md
    ├── ota-process.md
    └── provisioning.md
```

---

## 6. Luồng khởi động hệ thống

```text
Bật nguồn
   │
   ▼
Khởi tạo Serial
   │
   ▼
Khởi tạo NVS / Preferences
   │
   ▼
Đọc cấu hình thiết bị
   │
   ▼
Có thông tin Wi-Fi?
   ├── Không ──► Mở Access Point cấu hình
   │
   └── Có
        │
        ▼
   Kết nối Wi-Fi
        │
        ├── Thành công
        │      ├── Khởi tạo OTA
        │      ├── Khởi tạo BLE
        │      └── Chạy vòng lặp chính
        │
        └── Thất bại nhiều lần
               ├── Khởi tạo BLE
               └── Mở Access Point cấu hình
```

Bluetooth nên được khởi tạo ngay cả khi Wi-Fi không kết nối được để thiết bị vẫn có thể được phát hiện và cấu hình trong các phiên bản sau.

---

## 7. Quản lý Wi-Fi

## 7.1 Chế độ hoạt động

Thiết bị hỗ trợ hai chế độ:

### Station Mode

Thiết bị kết nối vào Wi-Fi hiện có.

Ví dụ:

```text
SSID: Home-IoT
IP: 192.168.1.120
Hostname: touchdeck-01
```

### Access Point Mode

Khi chưa có cấu hình hoặc kết nối thất bại, thiết bị tự tạo Wi-Fi:

```text
SSID: TouchDeck-Setup-XXXX
Password: được cấu hình mặc định
IP: 192.168.4.1
```

Người dùng kết nối vào Access Point này để nhập:

- SSID Wi-Fi.
- Mật khẩu Wi-Fi.
- Tên thiết bị.
- Mật khẩu OTA.
- Tên hiển thị Bluetooth.

---

## 7.2 Cơ chế kết nối lại

Không nên gọi kết nối Wi-Fi liên tục trong vòng lặp chính.

Quy trình đề xuất:

1. Thử kết nối trong khoảng thời gian giới hạn.
2. Nếu thất bại, chờ một khoảng ngắn.
3. Thử lại theo số lần cấu hình.
4. Nếu vượt số lần thử, mở Access Point.
5. Sau một khoảng thời gian, tiếp tục thử Station Mode.
6. Không chặn vòng lặp chính trong thời gian dài.

Các trạng thái nên có:

```text
WIFI_IDLE
WIFI_CONNECTING
WIFI_CONNECTED
WIFI_DISCONNECTED
WIFI_AP_MODE
WIFI_ERROR
```

---

## 7.3 Hostname

Mỗi thiết bị nên có hostname riêng:

```text
touchdeck-01.local
touchdeck-office.local
touchdeck-macmini.local
```

Hostname giúp thực hiện OTA mà không cần nhớ IP.

---

## 7.4 Cấu hình lưu trữ

Thông tin có thể lưu trong NVS:

```text
wifi_ssid
wifi_password
device_name
ble_name
ota_password
hostname
provisioned
```

Không hard-code thông tin Wi-Fi thật trong repository.

---

## 8. Bluetooth Low Energy

## 8.1 Mục tiêu BLE ở giai đoạn đầu

BLE dùng để:

- Mac phát hiện ESP32.
- Kiểm tra khả năng kết nối ổn định.
- Gửi action ID từ màn hình về Mac trong giai đoạn sau.
- Nhận cấu hình hoặc trạng thái từ Mac.
- Là nền tảng cho ứng dụng companion.

Giai đoạn đầu chưa cần giả lập bàn phím BLE HID. Nên triển khai BLE GATT trước vì dễ kiểm tra và phù hợp giao tiếp hai chiều.

---

## 8.2 Mô hình BLE GATT

ESP32 đóng vai trò:

```text
BLE Peripheral / GATT Server
```

Mac đóng vai trò:

```text
BLE Central / GATT Client
```

ESP32 quảng bá một service riêng.

Ví dụ cấu trúc:

```text
TouchDeck Service
├── Command Characteristic
├── Event Characteristic
├── Status Characteristic
└── Device Info Characteristic
```

---

## 8.3 Chức năng từng characteristic

### Command Characteristic

Mac gửi lệnh xuống ESP32.

Ví dụ:

```text
set_profile
set_brightness
restart
get_status
start_ota
```

Thuộc tính:

```text
Write
Write Without Response
```

### Event Characteristic

ESP32 gửi sự kiện lên Mac.

Ví dụ:

```text
button_pressed
button_released
page_changed
device_ready
wifi_connected
```

Thuộc tính:

```text
Notify
```

### Status Characteristic

Mac đọc trạng thái hiện tại.

Ví dụ:

```text
firmware_version
wifi_status
ip_address
uptime
free_heap
active_profile
```

Thuộc tính:

```text
Read
Notify
```

### Device Info Characteristic

Thông tin nhận dạng thiết bị.

Ví dụ:

```text
model
serial
firmware
hardware_revision
device_name
```

Thuộc tính:

```text
Read
```

---

## 8.4 Định dạng dữ liệu BLE

Ở phiên bản đầu, có thể sử dụng JSON ngắn.

Ví dụ sự kiện:

```json
{
  "type": "button",
  "id": 3,
  "event": "press"
}
```

Ví dụ trạng thái:

```json
{
  "type": "status",
  "wifi": true,
  "ip": "192.168.1.120"
}
```

Tuy nhiên BLE có giới hạn kích thước gói. Khi hệ thống phức tạp hơn nên chuyển sang:

- MessagePack.
- CBOR.
- Protocol nhị phân riêng.
- Chia gói dữ liệu thành nhiều chunk.

Ở MVP, chỉ gửi dữ liệu ngắn và tránh truyền ảnh qua BLE.

---

## 8.5 Tên Bluetooth

Tên quảng bá nên dễ nhận biết:

```text
TouchDeck-01
TouchDeck-Office
TouchDeck-A1B2
```

Hậu tố có thể lấy từ một phần địa chỉ MAC để tránh trùng.

---

## 8.6 Trạng thái BLE cần quản lý

```text
BLE_STOPPED
BLE_ADVERTISING
BLE_CONNECTED
BLE_DISCONNECTED
BLE_ERROR
```

Sau khi Mac ngắt kết nối, ESP32 cần tự phát quảng bá trở lại.

---

## 9. OTA qua Wi-Fi

## 9.1 Mục tiêu

Cho phép cập nhật firmware mà không cần cắm cáp USB sau lần nạp đầu tiên.

OTA dùng cho:

- Cập nhật firmware trong mạng nội bộ.
- Sửa lỗi.
- Thêm tính năng.
- Cập nhật giao diện LVGL trong giai đoạn sau.
- Cập nhật nhiều thiết bị nhanh hơn.

---

## 9.2 Phương án OTA giai đoạn đầu

Sử dụng ArduinoOTA tích hợp với PlatformIO.

Luồng:

```text
Mac build firmware
      │
      ▼
PlatformIO tìm thiết bị qua mDNS
      │
      ▼
Xác thực mật khẩu OTA
      │
      ▼
Upload firmware qua Wi-Fi
      │
      ▼
ESP32 ghi firmware vào partition OTA
      │
      ▼
Kiểm tra hoàn tất
      │
      ▼
Khởi động lại
```

---

## 9.3 Điều kiện kích hoạt OTA

OTA chỉ hoạt động khi:

- Wi-Fi đã kết nối.
- Thiết bị có địa chỉ IP.
- ArduinoOTA đã được khởi tạo.
- Partition table hỗ trợ OTA.
- Firmware còn đủ dung lượng.

Có thể để OTA luôn sẵn sàng trong mạng nội bộ ở giai đoạn phát triển.

Khi chuyển sang sản phẩm thực tế, nên giới hạn:

- Chỉ bật OTA trong một khoảng thời gian sau khi khởi động.
- Chỉ bật OTA khi người dùng bấm nút trong giao diện.
- Yêu cầu xác thực.
- Kiểm tra version và chữ ký firmware.

---

## 9.4 Partition table

Cần dùng partition scheme hỗ trợ tối thiểu hai vùng ứng dụng:

```text
ota_0
ota_1
```

Ngoài ra cần vùng:

```text
nvs
otadata
spiffs hoặc littlefs
```

Dung lượng firmware cần được tính trước khi tích hợp LVGL và asset lớn.

Một cấu hình phổ biến cho Flash 16 MB:

```text
NVS
OTA Data
App OTA 0
App OTA 1
LittleFS
```

Không nên dùng partition mặc định nếu không chắc dung lượng hai slot OTA đủ cho firmware có LVGL.

---

## 9.5 Bảo mật OTA

Tối thiểu cần:

- Mật khẩu OTA.
- Không commit mật khẩu thật vào Git.
- Chỉ cho phép OTA trong LAN.
- Không expose OTA trực tiếp ra Internet.
- Tắt OTA khi thiết bị ở mạng không tin cậy.

Bản thương mại nên bổ sung:

- HTTPS OTA.
- Firmware signing.
- Secure Boot.
- Flash Encryption.
- Rollback.
- Version control.
- Device authorization.

---

## 9.6 Trạng thái OTA

```text
OTA_IDLE
OTA_READY
OTA_STARTING
OTA_UPDATING
OTA_SUCCESS
OTA_FAILED
```

Trong quá trình OTA, thiết bị nên:

- Tạm dừng tác vụ không cần thiết.
- Không tự restart vì watchdog.
- Không ghi NVS ngoài ý muốn.
- Không khởi tạo lại Wi-Fi.
- Không thực hiện animation nặng.
- Hiển thị tiến trình trên LCD trong giai đoạn sau.

---

## 10. Cập nhật OTA bằng PlatformIO

Sau lần upload USB đầu tiên, PlatformIO có thể upload OTA qua:

```text
hostname.local
```

hoặc IP:

```text
192.168.1.120
```

Nên tạo hai environment:

```text
usb
ota
```

### Environment USB

Dùng khi:

- Nạp firmware lần đầu.
- Firmware lỗi không vào được Wi-Fi.
- Cần xem log trực tiếp.
- Khôi phục thiết bị.

### Environment OTA

Dùng khi:

- Thiết bị đã kết nối Wi-Fi.
- ArduinoOTA đang chạy.
- Thiết bị và Mac cùng mạng.
- Cần cập nhật nhanh.

---

## 11. Cấu hình PlatformIO dự kiến

Tài liệu này chưa cung cấp code, nhưng file cấu hình cần xác định:

- Platform Espressif 32.
- Board ESP32-S3 phù hợp.
- Arduino framework.
- Flash size 16 MB.
- PSRAM OPI.
- USB CDC khi boot.
- Partition table tùy chỉnh.
- Serial baud rate.
- Thư viện NimBLE.
- Thư viện portal nếu sử dụng.
- Hai môi trường upload USB và OTA.

Các thông số phần cứng cần được kiểm tra thực tế:

```text
board
flash_mode
flash_size
psram_type
memory_type
upload_speed
monitor_speed
```

Không nên sao chép nguyên cấu hình của một bo JC8048W550C khác nếu chưa kiểm tra revision phần cứng.

---

## 12. Quản lý cấu hình thiết bị

## 12.1 NVS / Preferences

NVS phù hợp cho:

- SSID.
- Mật khẩu Wi-Fi.
- Tên thiết bị.
- Mật khẩu OTA.
- Cờ đã cấu hình.
- Profile đang chọn.
- Độ sáng.
- Cấu hình nhỏ.

LittleFS phù hợp cho:

- Danh sách profile.
- Cấu hình giao diện.
- Icon.
- Theme.
- JSON lớn.

Giai đoạn đầu chỉ cần NVS.

---

## 12.2 Reset cấu hình

Cần thiết kế một cơ chế reset:

- Giữ một vùng cảm ứng khi khởi động.
- Giữ nút BOOT trong vài giây.
- Gửi lệnh BLE.
- Gọi endpoint trên portal.
- Chọn reset trong giao diện sau này.

Reset có thể chia thành:

### Reset Network

Chỉ xóa Wi-Fi.

### Factory Reset

Xóa toàn bộ:

```text
Wi-Fi
BLE name
OTA password
Device name
Profiles
Settings
```

---

## 13. Logging và chẩn đoán

Serial log cần thể hiện rõ:

```text
[BOOT] Firmware version
[BOOT] Reset reason
[NVS] Settings loaded
[WIFI] Connecting
[WIFI] Connected
[WIFI] IP address
[BLE] Advertising started
[BLE] Client connected
[OTA] Ready
[OTA] Progress
[OTA] Success
```

Không in ra log:

- Mật khẩu Wi-Fi.
- Mật khẩu OTA.
- Token.
- Secret.
- Dữ liệu nhạy cảm.

Có thể sử dụng các mức:

```text
ERROR
WARN
INFO
DEBUG
VERBOSE
```

Production nên giảm log để tiết kiệm tài nguyên.

---

## 14. Quản lý task

Arduino Framework vẫn chạy trên FreeRTOS. Có thể chia logic thành:

```text
Main Loop
├── Wi-Fi state machine
├── OTA handler
├── BLE event processing
├── System status
└── Future UI event queue
```

Giai đoạn đầu chưa cần tạo quá nhiều task riêng.

Nguyên tắc:

- Không block lâu.
- Không dùng delay dài.
- Dùng timer hoặc millis.
- Callback BLE không thực hiện tác vụ nặng.
- Đưa dữ liệu từ callback vào queue.
- OTA cần được xử lý thường xuyên.
- Tránh dùng chung dữ liệu mà không bảo vệ.

---

## 15. Watchdog và khả năng phục hồi

Thiết bị cần tự phục hồi khi:

- Wi-Fi mất kết nối.
- BLE client bị ngắt.
- Router khởi động lại.
- OTA thất bại.
- Bộ nhớ thấp.
- Callback bị treo.

Cơ chế đề xuất:

1. Theo dõi Wi-Fi.
2. Tự reconnect.
3. Tự restart BLE advertising.
4. Theo dõi free heap.
5. Ghi lại reset reason.
6. Restart thiết bị nếu trạng thái lỗi kéo dài.
7. Không restart trong lúc OTA đang ghi firmware.

---

## 16. Version firmware

Mỗi bản build cần có:

```text
Firmware version
Build number
Build date
Git commit
Hardware revision
Protocol version
```

Ví dụ:

```text
Firmware: 0.1.0
Build: 12
Hardware: JC8048W550C-R1
Protocol: 1
```

Version được hiển thị qua:

- Serial log.
- BLE Device Info.
- Trang cấu hình.
- Màn hình hệ thống trong giai đoạn sau.

---

## 17. Quy trình phát triển

### Giai đoạn 1: Kiểm tra board

- Nạp firmware qua USB.
- Kiểm tra Serial.
- Xác nhận Flash.
- Xác nhận PSRAM.
- Kiểm tra MAC address.
- Kiểm tra reset ổn định.

### Giai đoạn 2: Wi-Fi

- Kết nối Wi-Fi cố định để test.
- Kiểm tra reconnect.
- Kiểm tra hostname.
- Kiểm tra IP.
- Kiểm tra mất mạng và có mạng lại.

### Giai đoạn 3: NVS

- Lưu SSID.
- Lưu mật khẩu.
- Lưu device name.
- Kiểm tra sau reboot.

### Giai đoạn 4: Config Portal

- Mở Access Point.
- Nhập cấu hình.
- Lưu NVS.
- Restart.
- Kết nối Station Mode.

### Giai đoạn 5: BLE

- Quảng bá thiết bị.
- Mac scan thấy.
- Kết nối GATT.
- Đọc Device Info.
- Gửi command.
- Nhận notify.

### Giai đoạn 6: OTA

- Upload USB lần đầu.
- Bật ArduinoOTA.
- Upload lại qua IP.
- Upload qua hostname.
- Kiểm tra version mới.
- Test OTA thất bại.

### Giai đoạn 7: Tích hợp LCD

- Khởi tạo RGB LCD.
- Khởi tạo GT911.
- Tích hợp LVGL.
- Hiển thị trạng thái Wi-Fi, BLE và OTA.

---

## 18. Checklist kiểm thử

## 18.1 Wi-Fi

- [ ] Kết nối đúng Wi-Fi.
- [ ] Nhận IP.
- [ ] Có hostname.
- [ ] Tự reconnect khi router restart.
- [ ] Chuyển AP Mode khi không kết nối được.
- [ ] Lưu cấu hình sau reboot.
- [ ] Không block hệ thống.

## 18.2 Bluetooth

- [ ] Mac phát hiện thiết bị.
- [ ] Kết nối ổn định.
- [ ] Đọc được thông tin thiết bị.
- [ ] Gửi được command.
- [ ] Nhận được notify.
- [ ] Tự advertising lại sau disconnect.
- [ ] Không làm mất Wi-Fi.

## 18.3 OTA

- [ ] Upload OTA qua IP.
- [ ] Upload OTA qua hostname.
- [ ] Xác thực mật khẩu.
- [ ] Firmware mới khởi động.
- [ ] Cấu hình NVS không bị mất.
- [ ] Thiết bị không brick khi upload lỗi.
- [ ] Có thể khôi phục qua USB.

## 18.4 Ổn định

- [ ] Chạy liên tục 24 giờ.
- [ ] Không giảm heap bất thường.
- [ ] Không tự restart.
- [ ] Wi-Fi và BLE hoạt động đồng thời.
- [ ] OTA vẫn hoạt động khi BLE đang kết nối.

---

## 19. Các rủi ro kỹ thuật

### Wi-Fi và BLE dùng chung radio

ESP32-S3 chia sẻ tài nguyên RF giữa Wi-Fi và BLE. Có thể xuất hiện:

- BLE chậm khi Wi-Fi truyền nhiều.
- Mất notify.
- Độ trễ tăng.
- OTA ảnh hưởng BLE.

Cần giữ dữ liệu BLE ngắn và không gửi liên tục.

### Firmware quá lớn

Khi thêm LVGL, font và icon, kích thước firmware tăng nhanh. Nếu hai slot OTA không đủ, OTA sẽ thất bại.

Cần thiết kế partition table từ đầu.

### Nguồn không ổn định

Màn hình 5 inch và đèn nền tiêu thụ đáng kể. Nguồn yếu có thể gây:

- Reset.
- Brownout.
- Wi-Fi mất kết nối.
- OTA lỗi.
- LCD nhiễu.

Nên dùng nguồn USB-C ổn định và cáp tốt.

### Sai cấu hình PSRAM

Sai `memory_type` hoặc PSRAM mode có thể khiến:

- Boot loop.
- Crash.
- LCD không chạy.
- Heap không đủ.

Cần xác nhận đúng cấu hình board thực tế.

### OTA không an toàn

ArduinoOTA phù hợp phát triển trong LAN nhưng chưa đủ an toàn cho sản phẩm bán ra Internet.

---

## 20. Định hướng giai đoạn tiếp theo

Sau khi Wi-Fi, BLE và OTA ổn định, tiếp tục triển khai:

1. Driver LCD RGB.
2. Driver cảm ứng GT911.
3. LVGL.
4. Layout grid nút cảm ứng.
5. BLE event khi chạm nút.
6. macOS companion app.
7. Profile theo ứng dụng.
8. Đồng bộ icon.
9. Web config.
10. HTTPS OTA tập trung.
11. Device provisioning.
12. Secure Boot và firmware signing.

---

## 21. Kiến trúc sản phẩm hoàn chỉnh dự kiến

```text
JC8048W550C
├── Wi-Fi Manager
├── BLE GATT
├── BLE HID
├── OTA
├── LVGL UI
├── Touch Input
├── Profile Cache
├── Icon Cache
└── Device Settings
          │
          ├── BLE
          │
          ▼
     macOS Companion
     ├── Action Runner
     ├── Profile Manager
     ├── App Detection
     ├── Shortcut Engine
     ├── Shell / AppleScript
     └── Device Updater
```

---

## 22. Kết luận

Với JC8048W550C, hướng triển khai hợp lý là xây firmware theo từng lớp:

```text
Board ổn định
→ Wi-Fi
→ NVS
→ BLE GATT
→ OTA
→ LCD
→ Touch
→ LVGL
→ macOS Companion
```

Không nên tích hợp LCD, cảm ứng, BLE, Wi-Fi và OTA cùng lúc ngay từ đầu. Việc tách từng giai đoạn giúp dễ kiểm tra lỗi, ổn định firmware và tạo nền tảng tốt cho sản phẩm TouchDeck hoàn chỉnh.
